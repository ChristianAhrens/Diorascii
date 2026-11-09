# Diorascii Changelog
All notable changes to Diorascii will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html).

The latest released section is the single source of the version: CMake reads it, and a
release is only built when its tag matches it. The text of a section becomes the text of
the GitHub release (see "Releasing" in the README).

## [Unreleased]
### Added

### Changed

### Fixed
- Fixed the stars staying bright in a night-time sandstorm (or rain): clouds and dust now hide them
- Fixed the desert sandstorm lighting the scene up at night: the dust now takes on the time of day

## [1.2.0] 2026-11-08
### Added
- Added a first tvOS app: the five scenes on the Apple TV, steered with the Siri Remote (swipe left and right to switch scene, up for the time of day, down for the sound, click for the scene's action, hold the click for the menu, Play/Pause to pause), with the sound synthesized through AVAudioEngine, a hint about the menu on the first launches and a menu without a Quit entry. A keyboard works as well. It is generated entirely by CMake (`-G Xcode -DCMAKE_SYSTEM_NAME=tvOS`, see the README) and is not part of the release archives
- Added `scripts/package-tvos.sh`, which builds, signs, validates and uploads the tvOS app to App Store Connect
- Added the draft app icon art and Top Shelf images for tvOS in `Resources/Icon`, and `tvOS/make_rasters.sh` to render them
- Added `--mute` to start the terminal program without sound
- Added PRIVACY.md: Diorascii collects no data, in the terminal program, in the tvOS app and on the project page

### Changed
- Changed the ambient sound to be on by default, in the terminal program as on tvOS; `--mute` starts without it, and the `a` key and the menu still switch it. A machine without a sound device stays silent without a message unless `--audio` is given
- Changed the interactive logic (scene, menu, style, time of day, pause, sound request) to live in a shared `Session` class, which the terminal program and the tvOS app both drive
- Changed the build to skip the terminal program and the tests on tvOS and iOS (`DIORASCII_TERMINAL`)

### Fixed
- Fixed `scripts/package-macos.sh` leaving the release version in the build directory shared with `scripts/build.sh`, so that later local builds reported it; it now builds in `build/package-macos`

## [1.1.1] 2026-11-08
### Added
- Added the Mountains scene, which uses the whole height of the terminal: a meadow with a stream, a forest belt, bare rock and snow on the peaks, and the sky above. The ground is a mountain range with a different set of peaks every run; the tree line and snow line wobble, the far flanks lie in shadow, and the sun and moon set behind the ridges
- Added mountain wildlife that keeps to its own zone: eagles soar across the sky and swoop down as far as the rock, goats leap along the rocky and snowy ledges, deer and small birds live in the meadow and the forest, and bears stroll there and hunt the deer
- Added the snowstorm (`f`): icy wind sweeps snow across the whole screen and turns the scene into a whiteout
- Added the avalanche (the rare event, `x`): the snow cracks high on a flank, then a powder cloud rips down the mountainside through the rock and the forest into the stream, burying trees, bushes and animals; its swath fades after a while and what was buried comes back
- Added mountains soundscape: alpine wind, a gurgling stream, the occasional cry of an eagle and drumming of a woodpecker by day, a howling icy wind in a snowstorm, and the cracking, rumbling and roar of an avalanche, which crackles and thumps as it breaks loose and as it knocks down trees on its way
- Added scene keys `mountains`, `peak-height`, `treeline`, `snowline`, `forest-colors`, `rock-colors`, `snow-colors` and `forest-glyphs`, `rock-glyphs`, `snow-glyphs`; sprite keys `zone`, `fall` and `density`; behaviors `ledge`, `soar` and `avalanche`
- Added a Mountains button and clips to the project page, and screenshots of every scene by day and by night to the README

### Changed
- Changed the background clips of the project page to show only the terminal, without the window border, and to be smaller (20 frames per second, 1920 pixels wide)

### Fixed

## [1.1.0] 2026-11-08
### Added
- Added CHANGELOG.md, which also provides the version for CMake and the text of the GitHub release
- Added ambient sound, synthesized at run time without audio recordings, played through the vendored miniaudio (playback only, no runtime dependencies); off by default, switched on with `--audio`, the `a` key or the menu, and silent without a sound device
- Added forest soundscape: soft wind in the trees with slow gusts that deepens in a storm, rain, night crickets, distant rumbling and far-off claps as a thundercloud approaches, and a sharp crack with rolling thunder at every lightning strike
- Added aquarium soundscape: a deep underwater hum, rising bubbles, a blip for every flake of food a fish eats, the song of a passing whale and the growl of the kraken
- Added desert soundscape: a barely audible dry wind and a few night crickets, a howling sandstorm with the hiss of blown sand, the turning roar of a tornado and the sandworm's growl in the ground
- Added moon soundscape: a faint deep drone felt through the ground, meteor strikes that thud up out of the ground with debris pattering down, and an eerie warble for the alien
- Added Linux text console color mode (`--console`), picked automatically on `TERM=linux`, also from inside tmux
- Added miniaudio credit to the README

### Changed
- Changed the version to be read by CMake from the latest released section of CHANGELOG.md; builds not made from a release tag report it with a `-dev` suffix
- Changed the release workflow to use the changelog section of the tag as GitHub release text and to refuse tags that do not match the changelog

### Fixed
- Fixed the look on the Linux text console, where 256-color output turned the backdrops into loud solid color bands
- Fixed the aquarium whale swimming tail first

## [1.0.0] 2026-11-07
### Added
- Added Diorascii as one small, self-contained C++20 binary without runtime dependencies for macOS (universal disk image), Linux and Windows
- Added truecolor output with automatic 256-color fallback
- Added animated aquarium scene with shoals of fish, jellyfish, eels, crabs, seaweed, a shark that hunts, passing whales and ships, and a castle and coral on the sea bed
- Added forest scene with a river, pines, oaks, bushes and flowers, deer, rabbits, ducks, birds, butterflies, fireflies and an eagle now and then, and rain
- Added desert scene with dunes, pyramids, an oasis with palms, camel caravans, herds of wild horses, bouncing gazelles, vultures, bats and a lion that hunts
- Added moon scene with a lander, a flag, a rover, rocks and the Earth in the sky
- Added per-scene actions on `f`: feed the fish, make it rain, bring a sandstorm or a tornado, and strike the moon with a meteorite that digs a crater, wrecks what stands there and throws up dust that settles again
- Added day and night cycle with a sun and a moon crossing the sky, sunrise and sunset glow, stars and creatures of the day or the night, with most animals sleeping at night (`t` and `--hour` set the time of day)
- Added herds that move together and hunting chases between predators and prey
- Added scheduled events per scene (an eagle, a lion, a shark, a ship, a whale, a meteor)
- Added rare catastrophes in every scene that wipe out life and let it come back later: the kraken, a thunderstorm with lightning, the sandworm and the alien (`x` summons one)
- Added render styles: full color, terminal colors (16 ANSI colors on the terminal's own background) and monochrome (`c` and `--style`)
- Added ASCII menu for scene, style, time of day, info line, the scene's action, the rare event, resume and quit, driven by abstract actions so any input device can use it
- Added data-driven sprite and scene formats (`.sprite`, `.scene`), embedded at build time and extendable or overridable with `--assets`
- Added command line options `--scene`, `--list-scenes`, `--style`, `--hour`, `--assets`, `--gallery` for art review, and headless `--dump` with `--act` and `--rare` for scripted output
- Added unit test suite run by ctest
- Added Doxygen API documentation and a project page, deployed to GitHub Pages
- Added Linux, macOS and Windows CI workflows
- Added release workflow for `x.y.z` tags that builds, tests and packages all three platforms and publishes the release only when all of them passed
- Added macOS packaging script with Developer ID signing and notarization, falling back to an ad-hoc signature
- Added README with credits, download table and format documentation, under the MIT license with original art

### Changed

### Fixed
