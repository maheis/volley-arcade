# Volley-Arcade

Small SDL2 prototype for a retro 80s style volleyball mini-game.

## Concept

The opponent ball rises behind the net and drops into your court.
You move your player along the net line, time a block, receive the ball,
and return it to the right side.

Current state is a playable scaffold:

- game loop
- fixed timestep simulation (120 Hz)
- player movement
- dynamic block timing with `SPACE` (gets tighter with higher level)
- head-only hit zone: only the orange head can play the ball; body lets it pass through
- max 3 touches per side; 4th touch gives point to the opponent
- after each touch, next touch is only valid after 500ms and at least 30px upward ball travel
- each set goes to 25 points and requires at least 2 points lead to win
- points reset after each set; won sets are counted separately
- serve belongs to the side that won the previous point
- ball is held in the server's hand; left player serves with `SPACE`
- hold `SPACE` to charge serve power and release to shoot
- fully charged serve goes out
- side screen edges are out (no side bounce); out gives point to opponent
- target window resolution: 1024x600
- ball physics with gravity
- net collision
- CPU opponent that returns balls in repeating rhythm patterns
- difficulty increases every minute (faster CPU attacks)
- retro square-wave style sound effects via SDL audio queue
- centered top scoreboard: big rally points, smaller set counter below
- window title shows only Volley-Arcade
- start and pause scenes with pixel-text overlay
- animated title screen with moving ball and blinking PRESS ENTER prompt
- local highscore persistence in `highscore.dat`
- highscore now tracks best player set count

## Requirements

- Linux (32-bit or 64-bit)
- GCC
- SDL2 development package

Install SDL2 on Debian/Ubuntu:

```bash
sudo apt-get update
sudo apt-get install libsdl2-dev build-essential
```

Debian 11 (32-bit / i386):

```bash
sudo dpkg --print-architecture
# should output: i386

sudo apt-get update
sudo apt-get install build-essential libsdl2-dev pkg-config
make
```

Optional cross-build on a 64-bit Debian host:

```bash
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install gcc-multilib libc6-dev-i386 libsdl2-dev:i386 pkg-config
make build32
```

## Build

```bash
make
```

## Run

```bash
make run
```

## Controls

- start menu: `UP` one player, `DOWN` two player, `LEFT/RIGHT` easy/normal/hard, `SPACE` sound on/off, `ENTER` start
- player 1 (one-player mode): `A/D` or `LEFT/RIGHT` move, `W` or `UP` jump/block, `LEFT CTRL` (or `RIGHT CTRL`) hold/release serve
- player 2 (two-player mode): `LEFT` left, `UP` jump/block, `RIGHT` right, `ALT GR` hold/release serve
- one-player only: top 7 highscores are shown in the start menu
- one-player only: on game over or exit during a match, name entry is requested to save score
- `ENTER`: start game / resume from pause
- `P`: pause / resume
- `R`: reset game
- `M`: mute/unmute audio
- `ESC`: quit

## Highscore

The prototype stores your best set result locally in:

- `highscore.dat`

The file is created automatically in the project directory when you beat your previous best set count.

## Files

- `src/main.c`: complete prototype scaffold
- `Makefile`: build and run targets

## Next Steps

- sprite sheet and CRT-like post look
- optional volume slider
