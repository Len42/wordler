# wordler

**C++ program to solve [Wordle](https://www.nytimes.com/games/wordle/index.html)**

_For more detailed info, [see my web site](https://lenp.net/dev/wordler/)._

## How to Build

`wordler.sln` is the project file to build wordler using Microsoft Visual Studio on Windows.

I have not tried to build wordler on other systems, but I think it should work with a recent version of gcc or clang on Linux or MacOS.

The source code is in:
- `main.cpp`
- `cmdline.h`
- `timer.h`
- `words-guess.h`, `words-target.h` – word lists

The code uses some C++23 features.

## How to Use

_There’s a simpler way to run `wordler` – [see here for details](https://lenp.net/dev/wordler/#use)_

After entering a guess into [Wordle](https://www.nytimes.com/games/wordle/index.html), run wordler with your guesses and Wordle’s hints as arguments. It will compute a good word for your next guess. For example, if your first guess is “learn” and Wordle displays this:

![screenshot of a Wordle hint](hint1.png)

you would run `wordler learn .g.y.`
Note that the hint is specified by using ‘g’ for a green letter, ‘y’ for a yellow letter, and ‘.’ for a grey letter.

    $ ./wordler learn .g.y.
    Time: 2.30 seconds
    Best guess is "metes"

Enter wordler’s guess into Wordle to get another hint. Then, run wordler again with _both_ hints to get the next guess. For example:

![screenshot of a Wordle hint](hint2.png)

    $ ./wordler learn .g.y. metes .g...
    Time: 0.09 seconds
    Best guess is "block"

And so on, until the game is over.

### Other Options

wordler can be used in other ways. The `--help` option will display all of the command-line options.

_[See here for more info.](https://lenp.net/dev/wordler/#use)_


## How It Works

_[See my web site for details.](https://lenp.net/dev/wordler/#works)_
