# Diorascii Changelog
All notable changes to Diorascii will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html).

The latest released section is the single source of the version: CMake reads it, and a
release is only built when its tag matches it. The text of a section becomes the text of
the GitHub release (see "Releasing" in the README).

## [Unreleased]
### Added
- Added a Windows setup `.exe` to the release (Inno Setup, `packaging/windows/diorascii.iss`) that installs to Program Files, adds `diorascii` to the PATH for cmd and PowerShell, can be uninstalled from *Installed apps* and upgrades an older install in place; the Windows workflow builds it on every push and installs, upgrades and uninstalls it as a test
- Added `.deb` and `.rpm` packages for Linux to the release (CPack, `cpack -G DEB` and `cpack -G RPM`), which install `/usr/bin/diorascii` so that it is in the PATH and are upgraded and removed by the package manager; the Linux workflow builds and inspects them on every push
- Added a second catastrophe to the moon: a big meteor that grows from a speck in the black sky and strikes with an impact across the entire width of the screen (crater, flash, everything on the ground wrecked, a wave of dust), with a rumble that swells as it falls and the behavior `asteroid`
- Added a second catastrophe to the city: a fight between a giant lizard and a winged monster, each clearly outlined, spitting streams of blue and violet fire at each other, from which all people and vehicles flee; the fire spreads from where they fight and leaves dozens of buildings in ruins, which are rebuilt starting from the same spot; with their steps and roars, a roaring whoosh for every stream of fire and no screech once the winged one has fallen (`AmbientParams::flames` and `winged`) and the behavior `clash`
- Added a second catastrophe to the mountains: an abnormally large yeti climbs over the highest ridge from behind (hidden by the mountain, then rising above it), comes down into the valley and has to walk right up to the animals on the ground to kill them, with its roars and thudding steps and the behavior `maul`
- Added a second catastrophe to the desert: poachers in a helicopter shoot down everything alive, one animal after the other, with tracer lines, the chop of the rotor and a crack for every shot (`AmbientParams::shots`), the sprite header `lash-shot` and the behavior `poach`
- Added a second catastrophe to the forest: a flood whose wave front is sparse foam that fills in to a wall of muddy water, tearing off all trees and life and leaving mud and bare ground that is replanted only slowly, with the rush of the water and the behavior `flood`
- Added a second catastrophe to the aquarium, which replaces the kraken one time in five: a grey submarine drives through the sea and leaves a trail of poison that spreads while it passes and kills everything it reaches, and the sea fills with life again afterwards, with the hiss and the bubbling of the gas (`AmbientParams::menace`) and the behavior `leak`
- Added a choice of which rare catastrophe to summon: `x` picks by the scene's odds as before, `X` (or the menu entry *Other rare event*, or `--rare-other N` with `--dump`) the scene's least likely one, so that the second catastrophes can be seen without waiting
- Added a second catastrophe to the city: a fight between a giant lizard and a winged monster, each clearly outlined, spitting streams of blue and violet fire at each other, from which all people and vehicles flee; the fire spreads from where they fight and leaves dozens of buildings in ruins, which are rebuilt starting from the same spot; with their steps and roars, a roaring whoosh for every stream of fire and no screech once the winged one has fallen (`AmbientParams::flames` and `winged`) and the behavior `clash`
- Added a second catastrophe to the mountains: an abnormally large yeti climbs over the highest ridge from behind (hidden by the mountain, then rising above it), comes down into the valley and has to walk right up to the animals on the ground to kill them, with its roars and thudding steps and the behavior `maul`
- Added a second catastrophe to the desert: poachers in a helicopter shoot down everything alive, one animal after the other, with tracer lines, the chop of the rotor and a crack for every shot (`AmbientParams::shots`), the sprite header `lash-shot` and the behavior `poach`
- Added a second catastrophe to the forest: a flood whose wave front is sparse foam that fills in to a wall of muddy water, tearing off all trees and life and leaving mud and bare ground that is replanted only slowly, with the rush of the water and the behavior `flood`
- Added a second catastrophe to the aquarium, which replaces the kraken one time in five: a grey submarine drives through the sea and leaves a trail of poison that spreads while it passes and kills everything it reaches, and the sea fills with life again afterwards, with the hiss and the bubbling of the gas (`AmbientParams::menace`) and the behavior `leak`
- Added a choice of which rare catastrophe to summon: `x` picks by the scene's odds as before, `X` (or the menu entry *Other rare event*, or `--rare-other N` with `--dump`) the scene's least likely one, so that the second catastrophes can be seen without waiting
- Added a second catastrophe to the mountains: an abnormally large yeti climbs over the highest ridge from behind (hidden by the mountain, then rising above it), comes down into the valley and has to walk right up to the animals on the ground to kill them, with its roars and thudding steps and the behavior `maul`
- Added a second catastrophe to the desert: poachers in a helicopter shoot down everything alive, one animal after the other, with tracer lines, the chop of the rotor and a crack for every shot (`AmbientParams::shots`), the sprite header `lash-shot` and the behavior `poach`
- Added a second catastrophe to the forest: a flood whose wave front is sparse foam that fills in to a wall of muddy water, tearing off all trees and life and leaving mud and bare ground that is replanted only slowly, with the rush of the water and the behavior `flood`
- Added a second catastrophe to the aquarium, which replaces the kraken one time in five: a grey submarine drives through the sea and leaves a trail of poison that spreads while it passes and kills everything it reaches, and the sea fills with life again afterwards, with the hiss and the bubbling of the gas (`AmbientParams::menace`) and the behavior `leak`
- Added a choice of which rare catastrophe to summon: `x` picks by the scene's odds as before, `X` (or the menu entry *Other rare event*, or `--rare-other N` with `--dump`) the scene's least likely one, so that the second catastrophes can be seen without waiting
- Added a second catastrophe to the desert: poachers in a helicopter shoot down everything alive, one animal after the other, with tracer lines, the chop of the rotor and a crack for every shot (`AmbientParams::shots`), the sprite header `lash-shot` and the behavior `poach`
- Added a second catastrophe to the forest: a flood whose wave front is sparse foam that fills in to a wall of muddy water, tearing off all trees and life and leaving mud and bare ground that is replanted only slowly, with the rush of the water and the behavior `flood`
- Added a second catastrophe to the aquarium, which replaces the kraken one time in five: a grey submarine drives through the sea and leaves a trail of poison that spreads while it passes and kills everything it reaches, and the sea fills with life again afterwards, with the hiss and the bubbling of the gas (`AmbientParams::menace`) and the behavior `leak`
- Added a choice of which rare catastrophe to summon: `x` picks by the scene's odds as before, `X` (or the menu entry *Other rare event*, or `--rare-other N` with `--dump`) the scene's least likely one, so that the second catastrophes can be seen without waiting
- Added a second catastrophe to the forest: a flood whose wave front is sparse foam that fills in to a wall of muddy water, tearing off all trees and life and leaving mud and bare ground that is replanted only slowly, with the rush of the water and the behavior `flood`
- Added a second catastrophe to the aquarium, which replaces the kraken one time in five: a grey submarine drives through the sea and leaves a trail of poison that spreads while it passes and kills everything it reaches, and the sea fills with life again afterwards, with the hiss and the bubbling of the gas (`AmbientParams::menace`) and the behavior `leak`
- Added a choice of which rare catastrophe to summon: `x` picks by the scene's odds as before, `X` (or the menu entry *Other rare event*, or `--rare-other N` with `--dump`) the scene's least likely one, so that the second catastrophes can be seen without waiting
- Added a second catastrophe to the aquarium, which replaces the kraken one time in five: a grey submarine drives through the sea and leaves a trail of poison that spreads while it passes and kills everything it reaches, and the sea fills with life again afterwards, with the hiss and the bubbling of the gas (`AmbientParams::menace`) and the behavior `leak`
- Added a choice of which rare catastrophe to summon: `x` picks by the scene's odds as before, `X` (or the menu entry *Other rare event*, or `--rare-other N` with `--dump`) the scene's least likely one, so that the second catastrophes can be seen without waiting
- Added a choice of which rare catastrophe to summon: `x` picks by the scene's odds as before, `X` (or the menu entry *Other rare event*, or `--rare-other N` with `--dump`) the scene's least likely one, so that the second catastrophes can be seen without waiting
- Added side streets to the City: on wide enough screens one to three streets run off into the city and meet the main street at a crossing; cars turn off the main road into them and drive away, and others come down them and turn onto the main road. Scene key `avenues`, sprite headers `behaviors: drive` and `turns`
- Added the soundscape of the City: traffic, passing cars, horns, the quick two-tone siren of the ambulance and the slow, wailing siren of the fire engine (which drives off without it), trains with their clatter and horn, a faint jet for the planes, and the crackle of the fire
- Added the fire to the City: the rare event (`x`) sets a skyscraper alight, with flames, smoke and a fire engine; when it has burnt out the building is wrapped in scaffolding and renovated with new looks. Sometimes the fire gets out of hand, spreads to 3 to 5 more buildings and burns them to the ground; they are rebuilt from the rubble over the following minute and a half. The fire engine leaves as soon as the last fire is out
- Added the City scene: skyscrapers (generated to the height of the terminal, lower than the mountains) with lit windows at night, a street with lamps, zebra crossings and traffic (cars, taxis, buses, trucks, cyclists), people, shops, a park, an elevated railway with a station and trains, and planes coming in to land in the background. `f` calls an ambulance through the streets. Scene keys `city` and `skyline`, and the habitat `rail`, are new
- Added the scene action `summon <sprite>` (a visitor comes through, one at a time) and the sprite headers `feet-rows` and `traffic` (ground sprites on chosen rows, vehicles keeping to their side of the road), as the groundwork for a city
- Added the version and the copyright to the bottom of the menu
- Added a muffled gulping sound for every animal the kraken's tentacles catch
- Added a tongue to the sandworm: it lashes out at the animals on the ground ahead of it, and each animal is crushed the moment the tongue reaches it. Catastrophe sprites can set `strike-reach` for this
- Added sound to the sandworm: a very deep, trumpeting bellow every few seconds while it crosses the desert, and a low, grumbling chewing for every animal it crushes
- Added a muffled squeal for every animal an avalanche buries
- Added a day cycle to the moon: a large sun stands high in the sky by lunar day, over a slightly lighter grey ground, and the earth rises by night over a darker one; the sky stays black with its stars, and the rest of the scene is as before. The earth is no longer fixed in the corner
- Added a *real time* mode for the time of day (last in the `t` and menu list, or `--realtime`): the day, dusk, night and dawn of the scenes follow the clock of the computer Diorascii runs on, in the terminal program and on tvOS

### Changed
- Changed the project page's description and the guide for AI collaborators to name the City among the scenes, and the README's list of behaviors to include `drive`, `engine` and `blaze`
- Changed the City's plane to a larger drawing in the style of a pictogram: a swept wing behind and above the fuselage, one below it, a tail fin with a blue tip, a row of windows and a blue stripe along the belly
- Changed the City's skyscrapers to be up to 80% of the sky and the buildings to stand further apart, gave the planes clearer wings, made the traffic thinner overall with rush hours at about 8:00 and 17:30 (vehicle sprites with `rush: yes`), and made the lettering on mirrored sprites (TAXI, FIRE) stay readable
- Changed the fire engine to stay until the building is renovated, spray solid blue jets of water, and to cordon off a wider site with barriers across the road and the sidewalks
- Changed the City's fire engine to park in front of the burning building and spray it with water, which puts the fire out sooner; until the building has been renovated the site is cordoned off and other vehicles and people wait at its edge
- Changed the City's cars, buses, trucks, ambulance, fire engine, train and planes from solid blocks to fine line art on a darkened backing, and made cars in the side streets pass under the viaduct instead of over it
- Changed the City's side streets to break off at a broad crossing and end quickly behind the buildings instead of running to the horizon, added glimpses of further side streets between the buildings of the front row, spaced the buildings out more, made the cars show side, three-quarter, rear and front views as they turn, made the traffic and the crowds thinner, and made the planes long and slim
- Changed the train sound from clicking to the regular "klack-klack, klack-klack" of a metro: two woody knocks, a pause, and again, over a deep rumble
- Changed the City to be less flat: the main street is half as high, the buildings come in three rows, from low offices to towers of up to 70% of the sky, with four kinds of facade and six kinds of roof
- Changed the City's railway to a clearly visible viaduct (dark deck with bright rails on a concrete beam, pillars, masts and a wire), and redrew the trains, cars, buses, trucks, ambulance and fire engine with solid blocks so that they stand out; the planes are twice the size
- Changed the kraken to lash out with up to three tentacles at the animals around it while it moves through the sea, instead of killing every animal at once when it is in the middle of the screen; animals out of its reach survive. Catastrophe sprites can set `strike-aim`, `strike-lashes` and `lash-color` for this
- Changed the sandworm's bellow and chewing to be about half as loud
- Changed the sandworm to swallow the animals on the ground one by one as its head reaches them (`strike-mode: devour`), instead of all at once when it is in the middle of the screen; flying animals are spared
- Changed the alien on the moon to destroy the flagpole and the rover at the moment it passes each of them, instead of both at once when it is in the middle of the screen. The new `strike-mode` header (`centre`, `sweep`, `devour`) of catastrophe sprites selects this; the forest's thunderstorm keeps striking in the middle
- Changed the clouds of the forest, desert and mountains to come in two sizes: half of them are now a larger variant (about 1.5 times)
- Changed the sun and the moon in the aquarium, forest, desert and mountains from 5x3 to 8x5 cells, so that they read as discs. In a low sky (the aquarium's) they are cut off at the top instead of at the bottom

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
