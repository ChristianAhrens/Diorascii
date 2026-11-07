# Diorascii

Diorascii is a terminal scene animator in C++20: an underwater aquarium today, with moon,
forest, mountain and desert scenes planned. One self-contained binary, no
runtime dependencies, truecolor with automatic 256-color fallback.

All code and art in this repository are original and MIT licensed.

## Build

    scripts/build.sh            # macOS / Linux  -> build/<preset>/src/diorascii
    scripts/build.ps1           # Windows        -> build\release\src\Release\diorascii.exe

Needs only CMake >= 3.21 and a C++20 compiler. `scripts/build.sh dev` makes a
debug build and runs the tests. Assets are embedded into the binary at build time.

## Run

    diorascii                     # q quit, p pause, r restart, i info
    diorascii --dump 100          # headless: print the scene after 100 steps
    diorascii --assets ./my-art   # add or override sprites from a directory

## Add your own creature

Drop a `*.sprite` file into a directory and pass it with `--assets` (or into
`assets/` to ship it). Example:

    name: darter
    facing: right            # direction the art points
    speed: 9..18             # cells per second
    depth: 3..16             # draw order, larger = further away
    spawn-per-cells: 230     # one per N cells of open water
    behaviors: swim emit_bubbles
    pool: warm               # color pool for mask digits 1-9
    --- art
    <=={o>
    --- mask
    2111W1

Mask rules: `1`-`9` use the creature's randomized palette, letters are fixed
colors (`W`hite `R`ed `G`reen `B`lue `C`yan `Y`ellow `M`agenta `O`range blac`K`;
lower case = darker), a space is the default color. A space in the art with
mask `_` is opaque, with a color letter it is a solid block of that color. Add
more `--- art` / `--- mask` pairs for animation frames; set `fps:`.

## Layout

    src/core   headless simulation + frame buffer (no I/O, deterministic per seed)
    src/term   VT/ANSI encoder and the terminal backends (posix, windows)
    src/app    command line + main loop
    assets/    sprites, embedded at build time (cmake/embed.cmake)
    tests/     dependency-free tests (ctest)

The core only produces a grid of styled cells and consumes abstract actions, so
other frontends (SDL window, tvOS) can be added without touching it.

## Status

Aquarium slice only: waterline, stars, seabed, seaweed, four creatures, bubbles.
The Windows backend is written but has not been compiled yet.
