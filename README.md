# ALIEN: Fate of The Nostromo

This is a terminal-based clone of the ["ALIEN: Fate of The Nostromo" board game](https://boardgamegeek.com/boardgame/332321/alien-fate-nostromo), implemented in C.

## Installation
```
git clone https://github.com/CharlesAverill/aftn.git
cd aftn

mkdir build && cd build

cmake ..
make
```

## Usage
```
Usage: aftn [OPTION...]

  -a, --use_ash              Include Ash for a more challenging game
  -c, --n_characters=integer Number of characters to create
  -d, --draw_map             Draw the game map if an ASCII map is provided
  -g, --game=FILE            Read game board from this path rather than the
                             default. Check /var/games/aftn/maps/format.txt to
                             create your own game boards
  -n, --n_players=integer    Number of players to create
  -p, --print_map            Print out a text representation of the game map
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version
```
