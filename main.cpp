// Copyright (c) Len Popp
// This source code is licensed under the MIT license - see LICENSE file.

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <print>
#include <ranges>
#include <span>
#include <string_view>
#include <string>
#include <vector>

#include "timer.h"

// Definitions for command line options and help text (see cmdline.h)
#define CMDLINE_PROG_DESCRIPTION \
    "Wordle solver - Given a series of hints, compute which word to guess next" \
    "\n\nExample: wordler raise y.gy. thumb yg..."
#define CMDLINE_OPTIONS(ITEM) \
    /* ITEM(id, nameShort, nameLong, valType, defVal, help) */ \
    ITEM(Default, d, default, bool, true, "Use a default first guess (default true)") \
    ITEM(Solve, s, solve, bool, false, "Solve for the given answers") \
    ITEM(SolveAll, a, all, bool, false, "Solve all possible answers - slow!") \
    ITEM(ShowStats, x, stats, bool, false, "Display stats from a results file") \
    ITEM(Test, t, test, unsigned, 0, "Test mode")
#define CMDLINE_ALLOW_ARGS true
#define CMDLINE_ARGS_DESCRIPTION \
    "Other arguments depend on the options given.\n" \
    "With no options, args are the known hints. Each hint is a pair of args:\n" \
    "    First is the word guessed (5 letters)\n" \
    "    Second is the Wordle hint ('g' for green, 'y' for yellow, '.' for grey)\n" \
    "--solve: args are a list of answer words to solve\n" \
    "--stats: arg is a filename containing output from --all (or stdin if omitted)\n" \
    "--test: args depend on which test is selected."

#include "cmdline.h"

// Words have 5 letters.
static constexpr size_t wordLen = 5;

// word_t is a word stored as a char array
using word_t = char[wordLen];

// wordRef_t is a reference to a word_t, as a std::span
using wordRef_t = std::span<const char, wordLen>;

// wordList_t is a list of words
using wordList_t = std::vector<std::string>;

// solution_t is an answer word and the number of guesses that it took to solve
using solution_t = std::pair<std::string, unsigned>;

// It takes a long time to compute the first guess with no hints, and the
// answer is always "raise". So just use that.
static constexpr wordRef_t firstGuess()
{
    static constexpr word_t firstGuess_ = { 'r', 'a', 'i', 's', 'e' };
    return wordRef_t{ firstGuess_ };
}

// Throw an exception with a message.
[[noreturn]] static void throwError(const char* message)
{
    throw std::runtime_error(message);
}

// Throw an exception for a naughty word.
[[noreturn]] static void throwWordError(std::span<const char> word)
{
    throwError(std::format("Invalid word: {}"sv,
        std::string_view(word)).c_str());
}

// Verify that the given word is OK (5 letters, lower case).
static void checkWord(std::span<const char> word)
{
    if (word.size() != wordLen)
        throwWordError(word);
    if (!std::ranges::all_of(word, [](auto&& ch) {return std::islower(ch); }))
        throwWordError(word);
}

// Throw an exception for an invalid hint.
[[noreturn]] static void throwHintError(std::span<const char> hint)
{
    throwError(std::format("Invalid hint: {}"sv,
        std::string_view(hint)).c_str());
}

// Verify that the given hint is OK (5 special characters).
static void checkHint(std::span<const char> hint)
{
    if (hint.size() != wordLen)
        throwHintError(hint);
    if (!std::ranges::all_of(hint, [](auto&& ch) {
        return ch == 'g' || ch == 'y' || ch == '.';
        }))
    {
        throwHintError(hint);
    }
}

// Convert a numeric string to an unsigned int.
static unsigned numFromStr(std::string_view s)
{
    unsigned num;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), num);
    if (ec != std::errc() || (ptr - s.data()) != s.size())
        throwError(std::format("Bad number: \"{}\"", s).c_str());
    return num;
}

// A guess-hint pair with a match() function
class Hint
{
private:
    word_t guess;   // guess word - 5 letters, lower case
    word_t hint;    // hint chars ('g', 'y', or '.')

public:
    // Construct a Hint from a guess word and a hint pattern.
    explicit Hint(wordRef_t guessIn, wordRef_t hintIn)
    {
        copyWordFrom(guess, guessIn);
        copyWordFrom(hint, hintIn);
    }

    // Match a word against this hint.
    // Returns true if word matches this, false if not.
    bool match(wordRef_t word) const
    {
        // Keep track of letters that have been matched and ignore them later.
        bool matched[wordLen] = { false,false,false,false,false };
        // Check green matches
        for (auto&& [chWord, chGuess, chHint, fMatched]
            : std::views::zip(word, guess, hint, matched))
        {
            if (chHint == 'g') {
                if (chWord == chGuess) {
                    // Matched a character in the word. Mark it as matched.
                    fMatched = true;
                } else {
                    // Character mismatch. The word does not match this hint.
                    return false;
                }
            }
        }
        // Check yellow matches
        for (auto&& [chYellow, chHintY] : std::views::zip(guess, hint)) {
            if (chHintY == 'y') {
                // Iterate over the chars in the word, looking for an instance
                // of chYellow that is not disallowed by another yellow char.
                bool found = false;
                for (auto&& [chWord, chGuess, chHint, fMatched]
                    : std::views::zip(word, guess, hint, matched))
                {
                    if (chWord == chYellow
                        && !(chHint == 'y' && chYellow == chGuess)
                        && !fMatched)
                    {
                        // Matched a character in the word.
                        // Mark it as matched and stop looking for chYellow.
                        fMatched = true;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // chYellow not found. The word does not match this hint.
                    return false;
                }
            }
        }
        // Check grey letters
        for (auto&& [chSeek, chHint] : std::views::zip(guess, hint)) {
            if (chHint == '.') {
                for (auto&& [chWord, fMatched]
                    : std::views::zip(word, matched))
                {
                    if (chWord == chSeek && !fMatched) {
                        // Word has a grey letter so it does not match this hint.
                        return false;
                    }
                }
            }
        }
        return true;
    }

    void print() const
    {
        std::print("guess:{}", std::string_view(guess, wordLen));
        std::println(" hint:{}", std::string_view(hint, wordLen));
    }

    // Return a Hint made by comparing a guess word to a target word.
    static Hint fromGuess(wordRef_t wTargetIn, wordRef_t wGuessIn)
    {
        word_t target;
        copyWordFrom(target, wTargetIn);
        word_t guess;
        copyWordFrom(guess, wGuessIn);
        // Default to '.' which means an unmatched (grey) letter
        word_t hintWord{ '.', '.', '.', '.', '.' };
        // Find exact matches (green)
        for (auto&& [chGuess, chTarget, chHint]
            : std::views::zip(guess, target, hintWord))
        {
            if (chGuess == chTarget) {
                chHint = 'g';
                chGuess = '.';
                chTarget = '.';
            }
        }
        // Find yellow matches
        for (auto&& [chGuess, chHint] : std::views::zip(guess, hintWord)) {
            if (chGuess != '.') {
                for (auto&& chTarget : target) {
                    if (chGuess == chTarget) {
                        chHint = 'y';
                        chGuess = '.';
                        chTarget = '.';
                        break;
                    }
                }
            }
        }
        return Hint{ wGuessIn, hintWord };
    }

private:
    static void copyWordFrom(word_t& word, wordRef_t wordIn)
    {
        std::ranges::copy(wordIn, std::begin(word));
    }
};

static wordList_t allTargets;   // List of all possible answer words
static wordList_t allGuesses;   // List of all permitted guess words

// Load a list of words from a file.
static wordList_t loadWordFile(std::string_view filename)
{
    wordList_t words;
    std::ifstream file;
    file.open(filename, std::ios::in);
    if (file.fail()) {
        throwError(std::format("Failed to open file {}"sv, filename).c_str());
    }
    std::string word;
    while (std::getline(file, word)) {
        checkWord(word);
        words.push_back(word);
    }
    file.close();
    return words;
}

// Load the lists of all guess and target words.
static void loadWordLists()
{
    allTargets = loadWordFile("words-target.txt"sv);
    // The list of valid guesses should include the target words too.
    // Put the target words first. That makes it more likely that a target word
    // will be chosen as a guess.
    wordList_t guessWordsTemp = loadWordFile("words-guess.txt"sv);
    allGuesses = allTargets;
    allGuesses.append_range(guessWordsTemp);
}

// Throw an error for invalid data in a results file.
[[noreturn]] static void throwResultsError(std::string_view line)
{
    throwError(std::format("Bad results data: \"{}\"", line).c_str());
}

// Load a list of solution_t from a results file (the output of --all).
// Use stdin if filename is empty.
static std::vector<solution_t> loadResultsFile(std::string_view filename)
{
    // Either open a file or use stdin.
    bool inputFromStdin = false;
    std::ifstream inFile;
    if (filename.empty()) {
        inputFromStdin = true;
    } else {
        inputFromStdin = false;
        inFile.open(filename, std::ios::in);
        if (inFile.fail()) {
            throwError(std::format("Failed to open file {}", filename).c_str());
        }
    }
    std::istream& input = inputFromStdin ? std::cin : inFile;
    // Read and parse the file.
    std::vector<solution_t> results;
    std::string lineStr;
    while (std::getline(input, lineStr)) {
        // Each line has a guess word and a number, e.g. "atlas, 3"
        std::string_view line = lineStr;
        auto pos = line.find(',');
        if (pos == std::string::npos || pos < 5)
            throwResultsError(line);
        std::string_view word = line.substr(0, pos);
        if (word.length() != wordLen)
            throwResultsError(line);
        line.remove_prefix(6);
        while (line.starts_with(' '))
            line.remove_prefix(1);
        unsigned num = numFromStr(line);
        results.push_back(solution_t{ word, num });
    }
    return results;
}

// Make a list of Hints from the given command line arguments.
// Each consecutive pair of args is a guess-hint pair for a Hint.
// Returns an unevaluated view.
static std::ranges::view auto makeHints(const std::ranges::range auto& args)
{
    return args | std::views::chunk(2)
        | std::views::transform([](auto&& pattern) {
            checkWord(pattern[0]);
            checkHint(pattern[1]);
            return Hint(wordRef_t{ pattern[0] }, wordRef_t{ pattern[1] });
            });
}

// Filter a list of target words and return the ones matching a list of hints.
static wordList_t filterTargets(const std::ranges::range auto& hints,
    const wordList_t& targetsIn)
{
    wordList_t targets = targetsIn
        | std::views::filter([&hints](auto&& word) {
            return std::ranges::all_of(hints, [&word](auto&& hint) {
                return hint.match(wordRef_t{ word });
                });
            })
        | std::ranges::to<std::vector>();
    return targets;
}

// Filter a list of target words, returning only the ones matching a single hint.
static wordList_t filterTargets(const Hint& hint, const wordList_t& targetsIn)
{
    return filterTargets(std::views::single(hint), targetsIn);
}

// If the given word is in the given wordList_t, remove it.
static void removeWord(std::string_view word, wordList_t& list)
{
    auto pos = std::ranges::find(list, word);
    if (pos != list.end()) {
        list.erase(pos);
    }
}

// Choose the best word to guess next, given that the correct answer is in a
// list of target words.
static std::string getNextGuess(const wordList_t& targets,
    const wordList_t& guessWords)
{
    // Check a couple of special cases.
    if (targets.empty()) {
        // Oops, no matching words at all!
        throwError("No matching words found.");
    } else if (targets.size() <= 2) {
        // Only two possibilities remain - pick one.
        // This prevents an extra roundabout guess when there are only 2 alternatives.
        std::string guess = targets.front();
        return guess;
    }

    // Test all guess words, looking for the best one.
    // A good guess is one that is expected to cut down the target list as much
    // as possible.
    word_t wTemp{ '.','.', '.', '.', '.' };
    using score_t = unsigned long long;
#if LOOP_IMPL
    // Implementation with loops
    wordRef_t bestGuess{ wTemp };
    score_t bestScore = std::numeric_limits<score_t>::max();
    for (auto&& guess : guessWords) {
        // Score this guess based on how few matches it allows, over all possible
        // correct answers (targets).
        score_t score = 0;
        for (auto&& target : targets) {
            Hint hint = Hint::fromGuess(wordRef_t{ target }, wordRef_t{ guess });
            score += std::ranges::count_if(targets, [&hint](auto&& word) {
                return hint.match(wordRef_t{ word });
                });
        }
        if (score < bestScore) {
            bestScore = score;
            bestGuess = wordRef_t{ guess };
        }
    }
    return std::string(std::from_range, bestGuess);
#else
    // Implementation with ranges and algorithms
    // (no faster but certainly uglier)
    using guessScore_t = std::pair<wordRef_t, score_t>;
    // Compute numeric scores for all possible guesses.
    auto guessScores = guessWords
        | std::views::transform([&targets](auto&& guess) {
        auto scores = targets
            | std::views::transform([&targets, &guess](auto&& target) {
                Hint hint = Hint::fromGuess(wordRef_t{target}, wordRef_t{guess});
                return std::ranges::count_if(targets, [&hint](auto&& word) {
                    return hint.match(wordRef_t{ word });
                    });
                });
        score_t score = std::accumulate(scores.begin(), scores.end(), 0ull);
        return guessScore_t(wordRef_t{ guess }, score);
            });
    // Choose the guess with the best (lowest) score.
    guessScore_t best = std::accumulate(guessScores.begin(), guessScores.end(),
        guessScore_t(wTemp, std::numeric_limits<score_t>::max()),
        [](const guessScore_t& min, const guessScore_t& next) {
            return (next.second < min.second) ? next : min;
        });
    // Return the best guess word.
    return std::string(std::from_range, best.first);
#endif
}

// Solve for a given target word by calling getNextGuess() repeatedly.
static solution_t solveWord(wordRef_t target,
    const wordList_t& guessWords,
    const wordList_t& targetWords,
    bool fPrintGuesses)
{
    // Keep the list of currently plausible target words in a std::vector.
    wordList_t targets = targetWords;
    // Make guesses to refine the targets list until the answer is found
    // or all guesses are used up.
    static constexpr unsigned maxGuesses = 6;
    for (unsigned i = 0; i < maxGuesses; ++i) {
        std::string guess;
        if (targets.size() == 1) {
            // Only one possibility left, this should be the answer.
            guess = targets.front();
        } else if (i == 0 && CommandLine::GetDefault()) {
            // Use the default first guess.
            guess = std::string_view(firstGuess());
        } else {
            guess = getNextGuess(targets, guessWords);
        }
        if (fPrintGuesses)
            std::println("Guess #{} is \"{}\"", i + 1, guess);
        // Is this the correct answer?
        if (guess == std::string_view(target)) {
            // Return the answer and the number of guesses.
            return { guess, i + 1 };
        }
        // Filter the targets list according to the latest guess.
        Hint hint = Hint::fromGuess(target, wordRef_t{ guess });
        targets = filterTargets(hint, targets);
        // Also remove guess from targets, if it's there, to avoid getting stuck
        // in a loop.
        removeWord(guess, targets);
        if (targets.empty()) {
            // Oops, no matching words at all!
            throwError("No matching words found.");
        }
    }
    throwError(std::format("Answer \"{}\" was not found in {} tries.",
        std::string_view(target), maxGuesses).c_str());
}

// Display the word to guess next, based on the hints given on thte command line.
static void doNextGuess(auto args)
{
    if (args.empty() && CommandLine::GetDefault()) {
        // Use the default first guess.
        std::println("First guess is \"{}\"", std::string_view(firstGuess()));
    } else {
        // The command line args are the hints given so far.
        if ((args.size() % 2) != 0) {
            throwError("An even number of arguments is required.");
        }
        std::string guess;
        // Find a good next guess. Show how long it takes.
        showTime([&]() {
            auto hints = makeHints(args);
            wordList_t targets = filterTargets(hints, allTargets);
            guess = getNextGuess(targets, allGuesses);
            });
        std::println("Best guess is \"{}\"", guess);
    }
}

// Show the solution for the target word given on the command line.
static void doSolve(auto args)
{
    // Play games automatically with given target words.
    for (auto&& arg : args) {
        checkWord(arg);
        wordRef_t target{ arg };
        std::println("Target: \"{}\"", std::string_view(target));
        solution_t s;
        showTime([&]() {
            s = solveWord(target, allGuesses, allTargets, true);
            });
        std::println("Answer: \"{}\" in {} tries", s.first, s.second);
    }
}

// Solve for _all_ target words. (This takes hours to run.) Print the number of
// guesses required for each word.
static void doSolveAll(auto args)
{
    for (auto&& target : allTargets) {
        solution_t s = solveWord(wordRef_t{ target }, allGuesses, allTargets, false);
        std::println("{}, {}", s.first, s.second);
        std::cout.flush();
    }
}

// Stats helper struct for std::accumulate
struct Stats
{
    unsigned long count = 0;
    unsigned long totalGuesses = 0;
    solution_t min = { ".....",99 };
    solution_t max = { ".....",0 };
};

// Display statistics for the results produced by --all.
// Filename is given on the command line, defaults to stdin.
static void doShowStats(auto args)
{
    // Load the results file, from either stdin or a given filename.
    std::string_view filename = ""sv;
    if (args.size() >= 1) {
        filename = args[0];
    }
    auto results = loadResultsFile(filename);
    std::println("Number of results: {}", results.size());
    // Use std::accumulate to calculate statistics.
    Stats stats = std::accumulate(results.begin(), results.end(), Stats{},
        [](const Stats& accum, const solution_t next) -> Stats {
            return Stats{
                .count = accum.count + 1,
                .totalGuesses = accum.totalGuesses + next.second,
                .min = (next.second < accum.min.second) ? next : accum.min,
                .max = (next.second > accum.max.second) ? next : accum.max
            };
        });
    std::println("Min guesses: {} for \"{}\"",
        stats.min.second, stats.min.first);
    std::println("Max guesses: {} for e.g. \"{}\"",
        stats.max.second, stats.max.first);
    std::println("Mean guesses: {:.2f}",
        double(stats.totalGuesses) / double(stats.count));
    std::println("Histogram stats:");
    std::vector<unsigned> histo;
    histo.resize(stats.max.second + 1, 0);
    for (auto&& result : results) {
        ++histo[result.second];
    }
    for (auto&& [i, count] : std::views::enumerate(histo)) {
        std::println("{}, {}", i, count);
    }
}

// Test 1: Match words against a Hint.
// example args: raise .y..g geese evade amaze fubar exact blend
static void test1(auto args)
{
    std::println("test1");
    if (args.size() < 3) {
        throwError("Requires 3+ args");
    }
    std::println("hint: {} {}", args[0], args[1]);
    checkWord(args[0]);
    checkHint(args[1]);
    Hint hint(wordRef_t{ args[0] }, wordRef_t{ args[1] });
    hint.print();
    for (auto&& word : args | std::views::drop(2)) {
        std::println("{} {}", word, hint.match(wordRef_t{ word }));
    }
}

// Test 2: Output all target words that match the given hints.
// example args: raise .y..g grill y..y.
static void test2(auto args)
{
    std::println("test2");
    if ((args.size() % 2) == 1) {
        throwError("Requires an even number of args");
    }
    auto hints = makeHints(args);
    auto matches = allTargets | std::views::filter([&hints](auto&& word) {
        return std::ranges::all_of(hints, [&word](auto&& hint) {
            return hint.match(wordRef_t{ word });
            });
        });
    std::println("args: {}", args);
    std::println("matches: {}", matches);
}

// Test 3: Test Hint::fromGuess()
// example args: -t 3 grade guess
static void test3(auto args)
{
    std::println("test3");
    if (args.size() != 2) {
        throwError("Requires 2 args");
    }
    std::println("Target: {} Guess: {}", args[0], args[1]);
    checkWord(args[0]);
    checkWord(args[1]);
    Hint hint = Hint::fromGuess(wordRef_t{ args[0] }, wordRef_t{ args[1] });
    hint.print();
}

// Run the test specified by the --test option.
static void doTest(auto args)
{
    switch (CommandLine::GetTest()) {
    case 1: test1(args); break;
    case 2: test2(args); break;
    case 3: test3(args); break;
    default: throwError("Invalid test number");
    }
}

int main(int argc, char* argv[])
{
    try {
        if (CommandLine::Parse(argc, argv)) {
            return 0;
        }
        loadWordLists();
        // Do whatever was commanded
        auto args = CommandLine::GetOtherArgs();
        if (CommandLine::GetSolve()) {
            doSolve(args);
        } else if (CommandLine::GetSolveAll()) {
            doSolveAll(args);
        } else if (CommandLine::GetShowStats()) {
            doShowStats(args);
        } else if (CommandLine::GetTest()) {
            doTest(args);
        } else {
            // The default function is to process some hints and make a guess.
            doNextGuess(args);
        }
    } catch (const std::exception& e) {
        std::print("{}: Error: {}\n", CommandLine::GetProgName(), e.what());
        return 1;
    } catch (...) {
        std::print("{}: Error\n", CommandLine::GetProgName());
        return 1;
    }
    return 0;
}
