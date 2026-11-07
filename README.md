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

    diorascii                       # q quit, p pause, r restart, i info, f feed, left/right scene
    diorascii --scene moon          # start in a specific scene (--list-scenes shows all)
    diorascii --dump 100            # headless: print the scene after 100 steps
    diorascii --gallery             # print every sprite (art review)
    diorascii --assets ./my-art     # add or override sprites and scenes from a directory

The menu (`m`, Enter or Esc) lists scene, style, info line, feed, resume and quit.
It is drawn into the same cell grid as the scene and driven only by abstract
actions (up, down, left, right, select, back), so any input device can use it.

## Extending: sprites

A `*.sprite` file is a header of `key: value` lines followed by one or more
`--- art` frames, each optionally followed by a `--- mask`. Drop it into a
directory and pass `--assets DIR` (or put it in `assets/` to ship it).

    name: darter
    facing: right            # direction the art points
    habitat: medium          # medium (water) | surface | sky | ground
    speed: 9..18             # cells per second
    depth: 3..16             # draw order, larger = further away
    tag: prey                # lets behaviors find it (prey, food, ...)
    behaviors: seek_food swim emit_bubbles
    pool: warm               # color pool for mask digits 1-9
    --- art
    <=={o>
    --- mask
    2111W1

Other keys: `fps`, `loop: no`, `die-after-animation: yes`, `color: #rrggbb`
(default color), `slope` (diagonal flight), `anchor-row` (row that sits on the
surface line), `mouth-row`, `bubble-rate`, `bob-amplitude`, `bob-frequency`.

Mask rules: `1`-`9` use the creature's randomized palette, letters are fixed
colors (`W`hite `R`ed `G`reen `B`lue `C`yan `Y`ellow `M`agenta `O`range blac`K`;
lower case = darker), a space is the default color. A space in the art with
mask `_` is opaque, with a color letter it is a solid block of that color.

Behaviors: `swim` (horizontal, leaves the world at the edge), `fly` (both
axes), `bob`, `rise` (bubbles), `sink` (food), `emit_bubbles`, `seek_food`
(chase and eat food flakes), `hunt` (eat sprites tagged `prey`).

## Extending: scenes

A `*.scene` file describes a whole world as data; sprites are shared between
scenes. See `assets/scenes/aquarium.scene` and `moon.scene`.

    name: aquarium
    title: Aquarium
    sky: #06081c #223462            # top and bottom colors
    medium-colors: #1e7896 #051642  # surface and deep (omit with "medium: no")
    ground-colors: #967c52 #765e3c
    sky-fraction: 0.2               # share of the height above the surface (1 = all sky)
    ground-rows: 2
    stars: 23                       # one star per N sky cells
    feed: food                      # what the f key drops
    event-interval: 12..35          # seconds between rare visitors

    --- population                  # <sprite> per-cells|per-cols|count <n>
    darter  per-cells  230
    @seaweed per-cols  15           # built-in procedural plant

    --- decor                       # <sprite> left|right|center|random <margin> [y <row>]
    castle  right  2

    --- events                      # <sprite> <weight>; one at a time
    shark   3

Scene files are checked on load: a reference to a sprite that does not exist is
reported as an error instead of being silently ignored.

## Layout

    src/core   headless simulation + frame buffer (no I/O, deterministic per seed)
    src/term   VT/ANSI encoder and the terminal backends (posix, windows)
    src/app    command line + main loop
    assets/    sprites, embedded at build time (cmake/embed.cmake)
    tests/     dependency-free tests (ctest)

The core only produces a grid of styled cells and consumes abstract actions, so
other frontends (SDL window, tvOS) can be added without touching it.

## Status

Two scenes (aquarium, moon) with 21 original sprites; feeding, predation,
scheduled events, three render styles and an ASCII menu work.
