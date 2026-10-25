// Copyright (c) Len Popp
// This source code is licensed under the MIT license - see LICENSE file.

// TEMP
#define TEST5 true

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <print>
#include <random>
#include <ranges>
#include <span>
#include <string_view>
#include <string>
#include <vector>
#if TEST5
#include <map>
#endif

#include "timer.h"

// Definitions for command line options and help text (see cmdline.h)
#define CMDLINE_PROG_DESCRIPTION \
    "Wordle solver - Given a series of hints, compute which word to guess next" \
    "\n\nExample: wordler raise y.gy. thumb yg..."
#define CMDLINE_OPTIONS(ITEM) \
    /* ITEM(id, nameShort, nameLong, valType, defVal, help) */ \
    ITEM(Init, i, init, std::string, "raise", "Initial guess word (default \"raise\", may be empty)") \
    ITEM(HardMode, d, hard, bool, false, "Hard mode - guesses must match hints") \
    ITEM(Play, p, play, bool, false, "Play a game") \
    ITEM(Solve, s, solve, bool, false, "Solve for the given answers") \
    ITEM(SolveAll, a, all, bool, false, "Solve all possible answers - slow!") \
    ITEM(ShowStats, x, stats, bool, false, "Display stats from a results file") \
    ITEM(Verbose, v, verbose, bool, true, "Display more output (default true)") \
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
using word_t = std::array<char, wordLen>;

// Default value for word_t that is initialized to a non-word.
static constexpr const word_t& nonWord()
{
    static constexpr word_t nonWord_ = { '?', '?', '?', '?', '?' };
    return nonWord_;
}

// wordList_t is a list of words
using wordList_t = std::vector<word_t>;

// Number of guesses allowed
static constexpr unsigned maxGuesses = 6;

// solution_t is an answer word and the number of guesses that it took to solve
using solution_t = std::pair<word_t, unsigned>;

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

// Copy a word from a string into a fixed-size array.
static void copyWordFrom(word_t& word, std::string_view wordIn)
{
    if (wordIn.size() != wordLen)
        throwWordError(wordIn);
    std::ranges::copy(wordIn, std::begin(word));
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

// Was an initial guess specified on the command line?
static bool hasFirstGuess()
{
    return !CommandLine::GetInit().empty();
}

// It takes a long time to compute the initial guess with no hints, so start
// with a given word. Default="raise" which is what it would pick first.
// If empty, compute the first guess from scratch.
static word_t getFirstGuess()
{
    word_t word;
    checkWord(CommandLine::GetInit());
    copyWordFrom(word, CommandLine::GetInit());
    return word;
}

// List of all possible answer words
static constexpr word_t allTargets[] = {
#include "words-target.h"
};

// List of all permitted guess words
static constexpr word_t allGuesses[] = {
#include "words-target.h"
#include "words-guess.h"
};

// Select an answer word randomly
static word_t getRandomTarget()
{
    static std::random_device randDev;
    static std::mt19937_64 randGen(randDev());
    static std::uniform_int_distribution<uint64_t> randDist =
        std::uniform_int_distribution<uint64_t>(1, std::size(allTargets));
    return allTargets[randDist(randGen)];
}

// Formatted printing that uses a fancier format if the --verbose option is given
template<class... Args>
static void lprint(std::format_string<Args...> fmtVerbose, Args&&... args)
{
    std::print(CommandLine::GetVerbose() ? fmtVerbose : "{}",
        std::forward<Args>(args)...);
}

// Formatted printing that uses a fancier format if the --verbose option is given
template<class... Args>
static void lprintln(std::format_string<Args...> fmtVerbose, Args&&... args)
{
    std::println(CommandLine::GetVerbose() ? fmtVerbose : "{}",
        std::forward<Args>(args)...);
}

// Formatted printing that only prints if the --verbose option is given
template<class... Args>
static void lvprint(std::format_string<Args...> fmt, Args&&... args)
{
    if (CommandLine::GetVerbose()) {
        std::print(fmt, std::forward<Args>(args)...);
    }
}

// Formatted printing that only prints if the --verbose option is given
template<class... Args>
static void lvprintln(std::format_string<Args...> fmt, Args&&... args)
{
    if (CommandLine::GetVerbose()) {
        std::println(fmt, std::forward<Args>(args)...);
    }
}

// A guess-hint pair with a match() function
class Hint
{
private:
    word_t guess;   // guess word - 5 letters, lower case
    word_t hint;    // hint chars ('g', 'y', or '.')

public:
    // Construct a Hint from a guess word and a hint pattern.
    explicit Hint(const word_t& guessIn, const word_t& hintIn)
    {
        guess = guessIn;
        hint = hintIn;
    }

    explicit Hint(std::string_view guessIn, std::string_view hintIn)
    {
        checkWord(guessIn);
        copyWordFrom(guess, guessIn);
        checkHint(hintIn);
        copyWordFrom(hint, hintIn);
    }

    const word_t& getGuess(this auto&& self) { return self.guess; }

    const word_t& getHint(this auto&& self) { return self.hint; }

    // Match a word against this hint.
    // Returns true if word matches this, false if not.
    bool match(const word_t& word) const
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
        std::println("{} {}", std::string_view(guess), std::string_view(hint));
    }

    // Return a Hint made by comparing a guess word to a target word.
    static Hint fromGuess(const word_t& wTargetIn, const word_t& wGuessIn)
    {
        // Make copies of the words so that letters can be marked off as they
        // are matched.
        word_t target = wTargetIn;
        word_t guess = wGuessIn;
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
};

// Make a list of Hints from the given command line arguments.
// Each consecutive pair of args is a guess-hint pair for a Hint.
// Returns an unevaluated view.
static std::ranges::view auto makeHints(const std::ranges::range auto& args)
{
    return args | std::views::chunk(2)
        | std::views::transform([](auto&& pattern) {
            return Hint(pattern[0], pattern[1]);
            });
}

// Filter a list of target words and return the ones matching a list of hints.
static wordList_t filterTargets(const std::ranges::range auto& hints,
    const std::ranges::range auto& targetsIn)
{
    wordList_t targets = targetsIn
        | std::views::filter([&hints](auto&& word) {
            return std::ranges::all_of(hints, [&word](auto&& hint) {
                return hint.match(word);
                });
            })
        | std::ranges::to<std::vector>();
    return targets;
}

// Filter a list of target words, returning only the ones matching a single hint.
static wordList_t filterTargets(const Hint& hint,
    const std::ranges::range auto& targetsIn)
{
    return filterTargets(std::views::single(hint), targetsIn);
}

using score_t = unsigned long long;
using guessScore_t = std::pair<word_t, score_t>;

// Evaluate all guesses against a list of targets and return the best guess.
// Helper routine for getNextGuess().
static guessScore_t getNextGuessSub(const std::ranges::range auto& targets,
    const std::ranges::range auto& guessWords)
{
    // Check a couple of special cases.
    if (targets.empty()) {
        // Oops, no matching words at all!
        throwError("No matching words found.");
    } else if (targets.size() <= 2) {
        // Only two possibilities remain - pick one.
        // This prevents an extra roundabout guess when there are only 2 alternatives.
        const word_t& guess = targets.front();
        return { guess, 0 };
    }

    // Test all guess words, looking for the best one.
    // A good guess is one that is expected to cut down the target list as much
    // as possible.
    static constexpr guessScore_t worstGuess =
        guessScore_t(nonWord(), std::numeric_limits<score_t>::max());
#ifdef LOOP_IMPL
    // Implementation with loops
    guessScore_t best = worstGuess;
    for (auto&& guess : guessWords) {
        // Score this guess based on how few matches it allows, over all possible
        // correct answers (targets).
        score_t score = 0;
        for (auto&& target : targets) {
            Hint hint = Hint::fromGuess(target, guess);
            score += std::ranges::count_if(targets, [&hint](auto&& word) {
                return hint.match(word);
                });
        }
        if (score < best.second) {
            best = guessScore_t(guess, score);
        }
    }
#else
    // Implementation with ranges and algorithms
    // (no faster but certainly uglier)
    // Compute numeric scores for all possible guesses.
    auto guessScores = guessWords
        | std::views::transform([&targets](auto&& guess) {
        auto scores = targets
            | std::views::transform([&targets, &guess](auto&& target) {
                Hint hint = Hint::fromGuess(target, guess);
                return std::ranges::count_if(targets, [&hint](auto&& word) {
                    return hint.match(word);
                    });
                });
        score_t score = std::accumulate(scores.begin(), scores.end(), 0ull);
        return guessScore_t(guess, score);
            });
    // Choose the guess with the best (lowest) score.
    guessScore_t best =
        std::accumulate(guessScores.begin(), guessScores.end(), worstGuess,
            [](auto&& min, auto&& next) {
                return (next.second < min.second) ? next : min;
            });
#endif

    return best;
}

// Choose the best word to guess next, given that the correct answer is in a
// list of target words.
static word_t getNextGuess(const std::ranges::range auto& targets,
    const std::ranges::range auto& guessWords)
{
    // guess1 is the best guess that could possibly be a correct answer,
    // guess2 is the best guess of any valid word.
    guessScore_t guess1 = getNextGuessSub(targets, targets);
    guessScore_t guess2 = getNextGuessSub(targets, guessWords);
    // Compare scores of guess1 and guess2, giving a slight preference to a
    // guess that could fortuitously be the correct answer.
    static constexpr double preference = 1.1;
    const guessScore_t& guess =
        (guess1.second <= score_t(preference * guess2.second + 1))
            ? guess1 : guess2;
    return guess.first;
}

// Solve for a given target word by calling getNextGuess() repeatedly.
static solution_t solveWord(const word_t& target,
    const std::ranges::range auto& targetWords,
    const std::ranges::range auto& guessWords,
    auto guessCallback)
{
    // Keep the lists of currently plausible target and guess words in a std::vector.
    wordList_t targets{ std::from_range, targetWords };
    wordList_t guesses{ std::from_range, guessWords };
    // Make guesses to refine the targets list until the answer is found
    // or all guesses are used up.
    // For "hard mode", allow more guesses because it's not guaranteed to
    // succeed every time.
    unsigned maxGuessesT = CommandLine::GetHardMode() ? 99 : maxGuesses;
    for (unsigned i = 0; i < maxGuessesT; ++i) {
        word_t guess = nonWord();
        if (targets.size() == 1) {
            // Only one possibility left, this should be the answer.
            guess = targets.front();
        } else if (i == 0 && hasFirstGuess()) {
            // Use the default first guess.
            guess = getFirstGuess();
        } else {
            guess = getNextGuess(targets, guesses);
        }
        solution_t solution = { guess, i + 1 };
        // Call a callback function for each guess.
        guessCallback(solution);
        // Is this the correct answer?
        //if (std::string_view(guess) == std::string_view(target)) {
        if (guess == target) {
            // Return the answer and the number of guesses.
            return solution;
        }
        // Filter the targets list according to the latest guess.
        Hint hint = Hint::fromGuess(target, guess);
        targets = filterTargets(hint, targets);
        // In hard mode, the guesses must also be limited by the hints.
        // This is a bit inefficient when _not_ in hard mode because it copies
        // the entire guess list unnecessarily.
        if (CommandLine::GetHardMode()) {
            guesses = filterTargets(hint, guesses);
        }
        if (targets.empty()) {
            // Oops, no matching words at all!
            throwError("No matching words found.");
        }
    }
    throwError(std::format("Answer \"{}\" was not found in {} tries.",
        std::string_view(target), maxGuessesT).c_str());
}

// Display the word to guess next, based on the hints given on thte command line.
static void doNextGuess(auto args)
{
    if (args.empty() && hasFirstGuess() ) {
        // Use the default first guess.
        lprintln("First guess is \"{}\"", std::string_view(getFirstGuess()));
    } else {
        // The command line args are the hints given so far.
        if ((args.size() % 2) != 0) {
            throwError("An even number of arguments is required.");
        }
        word_t guess = nonWord();
        // Find a good next guess. Show how long it takes.
        double t = runTime([&]() {
            auto hints = makeHints(args);
            wordList_t targets = filterTargets(hints, allTargets);
            // In "hard mode" the list of guess words must be filtered by the
            // hints seen so far. This is a bit inefficient when _not_ in hard
            // mode because it copies the entire guess list unnecessarily.
            wordList_t guessList;
            if (CommandLine::GetHardMode()) {
                guessList = filterTargets(hints, allGuesses);
            } else {
                guessList = allGuesses | std::ranges::to<std::vector>();
            }
            guess = getNextGuess(targets, guessList);
            });
        lvprintln("Time: {:.02f} seconds", t);
        lprintln("Best guess is \"{}\"", std::string_view(guess));
    }
}

// Read a guess word from an input stream. Repeat until a valid guess is entered.
static std::optional<word_t> getInputGuess(std::istream& input,
    unsigned i,
    const std::ranges::range auto& guessWords)
{
    for (;;) {
        std::string sWord;
        lvprint("Guess #{}: ", i);
        if (!std::getline(input, sWord)) {
            // input error or EOF
            return std::nullopt;
        }
        if (sWord.length() == wordLen) {
            // Kludgy way to check if guess is a valid guess word
            word_t word;
            copyWordFrom(word, sWord);
            Hint hint = Hint::fromGuess(word, word);
            wordList_t matches = filterTargets(hint, guessWords);
            if (std::size(matches) == 1
                && std::string_view(matches.front()) == sWord)
            {
                return word;
            }
        }
        std::println("Invalid guess - try again");
    }
}

// Play a game
static void doPlayGame(auto args)
{
    word_t answer = getRandomTarget();
    wordList_t guesses{ std::from_range, allGuesses };
    for (unsigned i = 1; i <= maxGuesses; ++i) {
        auto guessOpt = getInputGuess(std::cin, i, guesses);
        if (!guessOpt) {
            // Error, or gave up
            return;
        }
        word_t guess = guessOpt.value();
        if (std::string_view(guess) == std::string_view(answer)) {
            // Done!
            lvprintln("Correct! Answer \"{}\" was found in {} tries.",
                std::string_view(answer), i);
            return;
        }
        Hint hint = Hint::fromGuess(answer, guess);
        lprintln("          {}", std::string_view(hint.getHint()));
        if (CommandLine::GetHardMode()) {
            guesses = filterTargets(hint, guesses);
        }
    }
    lvprintln("Answer \"{}\" was not found in {} tries.",
        std::string_view(answer), maxGuesses);
}

// Show the solution for the target word given on the command line.
static void doSolve(auto args)
{
    // Play games automatically with given target words.
    for (auto&& arg : args) {
        word_t target;
        checkWord(arg);
        copyWordFrom(target, arg);
        lvprintln("Target: \"{}\"", std::string_view(target));
        solution_t s;
        double t = runTime([&]() {
            s = solveWord(target, allTargets, allGuesses,
                [](const solution_t& sol) {
                    lvprint("Guess #{} is ", sol.second);
                    lprintln("\"{}\"", std::string_view(sol.first));
                });
            });
        lvprintln("Time: {:.02f} seconds", t);
        lvprint("Answer: \"{}\" in ", std::string_view(s.first));
        lprintln("{} tries", s.second);
    }
}

// Solve for _all_ target words. (This takes hours to run.) Print the number of
// guesses required for each word.
static void doSolveAll(auto args)
{
    for (auto&& target : allTargets) {
        solution_t s = solveWord(target, allTargets, allGuesses,
            [](const solution_t& sol){});
        std::println("{}, {}", std::string_view(s.first), s.second);
        std::cout.flush();
    }
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
        checkWord(word);
        line.remove_prefix(6);
        while (line.starts_with(' '))
            line.remove_prefix(1);
        unsigned num = numFromStr(line);
        word_t wtemp;
        copyWordFrom(wtemp, word);
        results.push_back(solution_t{ wtemp, num });
    }
    return results;
}

// Stats helper struct for std::accumulate
struct Stats
{
    unsigned long count = 0;
    unsigned long totalGuesses = 0;
    solution_t min = { nonWord(), 99};
    solution_t max = { nonWord(), 0};
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
        stats.min.second, std::string_view(stats.min.first));
    std::println("Max guesses: {} for e.g. \"{}\"",
        stats.max.second, std::string_view(stats.max.first));
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
    if (args.size() < 3) {
        throwError("Requires 3+ args");
    }
    lvprintln("hint: {} {}", args[0], args[1]);
    checkWord(args[0]);
    checkHint(args[1]);
    Hint hint(args[0], args[1]);
    if (CommandLine::GetVerbose()) {
        hint.print();
    }
    for (auto&& arg : args | std::views::drop(2)) {
        word_t word;
        checkWord(arg);
        copyWordFrom(word, arg);
        std::println("{} {}", std::string_view(word), hint.match(word));
    }
}

// Test 2: Output all target words that match the given hints.
// example args: raise .y..g grill y..y.
static void test2(auto args)
{
    if ((args.size() % 2) == 1) {
        throwError("Requires an even number of args");
    }
    auto hints = makeHints(args);
    auto matches =
        allTargets
        | std::views::filter([&hints](auto&& word) {
            return std::ranges::all_of(hints, [&word](auto&& hint) {
                return hint.match(word);
                });
            })
        | std::views::transform([](auto&& word) { return std::string_view(word); })
        | std::ranges::to<std::vector>(); // convert to vector to get size()
    lvprintln("args: {}", args);
    lprintln("{} matches", matches.size());
    lprintln("{}", matches);
}

// Test 3: Test Hint::fromGuess()
// example args: grade guess guest
static void test3(auto args)
{
    if (args.size() < 2) {
        throwError("Requires 2+ args");
    }
    lvprintln("Target: {}", args[0]);
    word_t target;
    checkWord(args[0]);
    copyWordFrom(target, args[0]);
    for (auto&& guessS : args | std::views::drop(1)) {
        word_t guess;
        checkWord(guessS);
        copyWordFrom(guess, guessS);
        Hint hint = Hint::fromGuess(target, guess);
        hint.print();
    }
}

// Test 4: Count letter frequencies in all answers
static void test4(auto args)
{
    using count_t = std::pair<char, unsigned>;
    std::array<count_t, 'z'-'a'+1> counts;
    for (auto&& [count, ch] : std::views::zip(counts, std::views::iota('a', 'z'+1))) {
        count = { ch, 0 };
    }
    for (auto&& word : allTargets) {
        for (char ch : word) {
            if (ch >= 'a' && ch <= 'z') {
                ++counts[ch - 'a'].second;
            } else {
                throwError("Weird character found");
            }
        }
    }
    std::ranges::sort(counts, std::greater(), &count_t::second);
    for (auto&& [count, rank] : std::views::zip(counts, std::views::iota(1))) {
        std::println("{}\t{}\t{} ", rank, count.first, count.second);
    }
}

#if TEST5

static constexpr char hintChars[3] = {'.', 'y', 'g'};

static std::ranges::view auto getAllHints()
{
    return std::views::cartesian_product(hintChars, hintChars, hintChars, hintChars, hintChars)
        | std::views::transform([](auto&& tup) {
                return word_t{ std::get<0>(tup), std::get<1>(tup), std::get<2>(tup), std::get<3>(tup), std::get<4>(tup) };
            });
}

static unsigned indexFromHint(const word_t& w)
{
    unsigned n = 0;
    for (auto&& ch : w) {
        unsigned nCh = (ch == 'g' ? 2 : (ch == 'y' ? 1 : 0));
        n = 3 * n + nCh;
    }
    return n;
}

static word_t hintFromIndex(unsigned n)
{
    word_t hint;
    for (unsigned i = 0; i < wordLen; ++i) {
        unsigned nCh = n % 3;
        n /= 3;
        char ch = (nCh == 2 ? 'g' : (nCh == 1 ? 'y' : '.'));
        hint[wordLen - 1 - i] = ch;
    }
    return hint;
}

class GuessNode
{
public: // private:
    word_t guess = nonWord();
    // map hint -> next GuessNode
    std::map<word_t, GuessNode> mpHintGuess;
    static size_t numNodes;

public:
    explicit GuessNode(const word_t& guessIn) : guess(guessIn) { }

    explicit GuessNode(std::string_view guessIn)
    {
        checkWord(guessIn);
        copyWordFrom(guess, guessIn);
    }

    GuessNode& addChild(const word_t& hint, const word_t& guess)
    {
        GuessNode node(guess);
        auto [it, f] = mpHintGuess.insert({ hint, node });
        if (f) {
            ++numNodes;
        } else {
            // Check if trying to set a different guess value - that's an error!
            if (node.guess != it->second.guess) {
                throwError("GuessNode::addChild: different guesses for the same hint");
            }
        }
        return it->second;
    }

    GuessNode& addChild(std::string_view hintS, std::string_view guessS)
    {
        word_t hint;
        copyWordFrom(hint, hintS);
        checkHint(hint);
        word_t guess;
        copyWordFrom(guess, guessS);
        checkWord(guess);
        return addChild(hint, guess);
    }

    // TODO: is it OK to return a const?
    const GuessNode* findChild(const word_t& hint) const
    {
        auto it = mpHintGuess.find(hint);
        if (it == mpHintGuess.end()) {
            return nullptr;
        }
        return &it->second;
    }

    const GuessNode* findChild(std::string_view hintS) const
    {
        word_t hint;
        copyWordFrom(hint, hintS);
        checkHint(hint);
        return findChild(hint);
    }
};

size_t GuessNode::numNodes = 1; // start at 1 to count the tree root node

// Root node maps empty hint to the default first guess.
static GuessNode guessRoot = GuessNode(nonWord());

static void addSolutionToTree(const word_t& target)
{
    GuessNode* guessCur = &guessRoot;
    Hint hintCur = Hint("xxxxx"sv, "....."sv);
    solution_t s = solveWord(target, allTargets, allGuesses,
        [&target, &guessCur, &hintCur](const solution_t& sol) {
            lprintln("Hint: {}", std::string_view(hintCur.getHint()));
            guessCur = &guessCur->addChild(hintCur.getHint(), sol.first);
            Hint hintNext = Hint::fromGuess(target, sol.first);
            lprintln("Guess: {}", std::string_view(sol.first));
            lprintln("Next hint: {}", std::string_view(hintNext.getHint()));
            hintCur = hintNext;
        });
}

static solution_t solveWordFast(const word_t& target, auto guessCallback)
{
    const GuessNode* guessCur = &guessRoot;
    Hint hintCur = Hint("xxxxx"sv, "....."sv);
    unsigned numGuesses = 0;
    for (;;) {
        //lprintln("Hint: {}", std::string_view(hintCur.getHint()));
        guessCur = guessCur->findChild(hintCur.getHint());
        if (!guessCur) {
            throwError("No matching words found.");
        }
        solution_t solution = { guessCur->guess, ++numGuesses };
        // Call a callback function for each guess.
        guessCallback(solution);
        if (guessCur->guess == target) {
            // Return the answer and the number of guesses.
            return solution;
        }
        hintCur = Hint::fromGuess(target, guessCur->guess);
    }
}

// Show the solution for the target word given on the command line.
static void doSolveFast(auto args)
{
    // Play games automatically with given target words.
    for (auto&& arg : args) {
        word_t target;
        checkWord(arg);
        copyWordFrom(target, arg);
        lvprintln("Target: \"{}\"", std::string_view(target));
        solution_t s = solveWordFast(target,
                [](const solution_t& sol) {
                    lvprint("Guess #{} is ", sol.second);
                    lprintln("\"{}\"", std::string_view(sol.first));
                });
        lvprint("Answer: \"{}\" in ", std::string_view(s.first));
        lprintln("{} tries", s.second);
    }
}

// Test 5: TODO Explore the gamespace.
static void test5(auto args)
{
#if true

    for (auto&& arg : args) {
        word_t target;
        checkWord(arg);
        copyWordFrom(target, arg);
        addSolutionToTree(target);
    }
    lprintln("Tree has {} nodes", GuessNode::numNodes);

    doSolveFast(args);
#endif

#if false
    GuessNode& firstGuess = guessRoot.addChild("....."sv, "raise"sv);
    firstGuess.addChild("....y"sv, "holey"sv);
    firstGuess.addChild("g...."sv, "boody"sv);
    firstGuess.addChild("g...."sv, "boody"sv); // duplicate insert succeeds
    //firstGuess.addChild("g...."sv, "boddy"sv); // insert with different guess fails
    lprintln("Tree has {} nodes", GuessNode::numNodes);

    const GuessNode* guessCur = &guessRoot;
    guessCur = guessCur->findChild("....."sv);
    if (guessCur == nullptr) {
        throwError("GuessNode::findChild: start node not found");
    }
    std::string_view testHint = "....y"sv; // "g...."sv; // "..g.y"sv; // 
    std::print("{} -> ", testHint);
    guessCur = guessCur->findChild(testHint);
    if (guessCur) {
        std::println("{}", std::string_view(guessCur->guess));
    } else {
        std::println("ERROR: Not found");
    }
#endif

#if false
    word_t guess = getFirstGuess();
    size_t cAnyMatches = 0;
    for (const word_t& wHint : getAllHints()) {
        Hint hint(guess, wHint);
        auto matches = filterTargets(hint, allTargets);
        size_t cMatches = matches.size();
        unsigned i = indexFromHint(wHint);
        auto wHint2 = hintFromIndex(i);
        if (wHint != wHint2) {
            std::println("{} {}", std::string_view(wHint), std::string_view(wHint2));
            throwError("oops");
        }
        /*if (cMatches != 0)*/ {
            ++cAnyMatches;
            std::println("{} {} {}",
                std::string_view(wHint), i, std::string_view(wHint2), cMatches);
        }
    }
    std::println("{} of {} hints have some matching targets", cAnyMatches, getAllHints().size());
#endif

#if false
    static constexpr word_t nonTargetGuesses[] = {
    #include "words-guess.h"
    };
    //word_t guess = getFirstGuess();
    //word_t wHint { '.','.','.','.','g' };
    //Hint hint { guess, wHint};
    //auto hints = std::views::single(hint);
    std::string_view aHints[] = { "raise", "...gg", "pulse", "..ygg" };
    lvprintln("hints: {}", aHints);
    auto hints = makeHints(aHints);
    auto matches1 = filterTargets(hints, allTargets);
    lprintln("Targets: {}", matches1.size());
    auto matches2 = filterTargets(hints, nonTargetGuesses);
    lprintln("Others: {}", matches2.size());
#endif
}

#endif // TEST5

// Run the test specified by the --test option.
static void doTest(auto args)
{
    switch (CommandLine::GetTest()) {
    case 1: test1(args); break;
    case 2: test2(args); break;
    case 3: test3(args); break;
    case 4: test4(args); break;
#if TEST5
    case 5: test5(args); break;
#endif // TEST5
    default: throwError("Invalid test number");
    }
}

int main(int argc, char* argv[])
{
    try {
        if (CommandLine::Parse(argc, argv)) {
            return 0;
        }
        // Do whatever was commanded
        auto args = CommandLine::GetOtherArgs();
        if (CommandLine::GetPlay()) {
            doPlayGame(args);
        } else if (CommandLine::GetSolve()) {
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
        std::println("{}: Error: {}", CommandLine::GetProgName(), e.what());
        return 1;
    } catch (...) {
        std::println("{}: Error", CommandLine::GetProgName());
        return 1;
    }
    return 0;
}
