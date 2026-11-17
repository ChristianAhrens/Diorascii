# Using Diorascii

Start it with

    diorascii

and the first scene appears. By default Diorascii runs in the [cycle mode](#cycle-mode-and-user-mode): it shows all
scenes one after the other by itself. If you would rather choose the scene, the time of day and the events yourself,
press `o` or start with `diorascii --user`. It runs until you press `q`. Resize the terminal at any time: the scene
adapts to the new size. Everything below can also be set on the command line, so a favourite setup is one command.

## Keys

| Key | What it does |
|---|---|
| `m`, Enter or Esc | open and close the [menu](#the-menu) |
| Left / right arrow | previous / next scene |
| `o` | switch between the [cycle mode and the user mode](#cycle-mode-and-user-mode) |
| `f` | the scene's **action**: feed the fish, make it rain, call an ambulance... (see [The scenes](scenes.md)); user mode only |
| `x` | summon the scene's **rare event** right away, instead of waiting for it; user mode only |
| `X` (shift) | summon the scene's *second*, less likely rare event; user mode only |
| `c` | change the [style](#styles): full, terminal, mono |
| `t` | step through the [time of day](#time-of-day): cycle, dawn, day, dusk, night, real time; user mode only |
| `a` | sound: off, stereo, immersive, and round again (a message names the mode) |
| `i` | show or hide the info line on top: version, scene, size, number of entities, seed, frames per second and, in scenes with a day cycle, the time of day |
| `p` | pause |
| `r` | restart the scene with a new random landscape |
| `q` | quit |

## Cycle mode and user mode

Diorascii has two overarching modes.

**Cycle mode** (the default) lets the program run by itself, like a screensaver. It goes through all the scenes in the
order of the menu, and it only leaves a scene once that scene has shown

- one full day and night (on the moon: one lunar day),
- one **rare event**, which has to have come to an end (a late one is waited for, so a scene whose rare event
  is late stays on for more than a day until it has come), and
- one **regular event**, which is what `f` does by hand: it is triggered at random every 10 to 90 seconds.

The time of day runs as a cycle in this mode. As soon as you choose something that the mode governs by hand (the time
of day, the scene's action or a rare event, with `t`, `f`, `x`, `X` or the menu), the cycle mode ends, Diorascii
switches to the user mode and does what you chose. You can still change the scene with the arrow keys, which starts
that scene's round afresh. Switching from the user mode to the cycle mode sets the time of day to *Cycle*.

**User mode** (`--user`) is the way it worked before: you choose the scene, the time of day and the events yourself,
and nothing happens that you did not ask for, apart from the life of the scene. Leaving the cycle mode with `o` or the
*Mode* entry brings back the time of day you had chosen before.

Press `o`, use *Mode* in the menu, or start with `--user` to change the mode.

## The menu

`m`, Enter or Esc open a menu that is drawn right into the scene. Use the up and down arrows to move, left and right
(or Enter) to change a value, and Esc to go back to the scene. It lists:

- **Scene**, **Mode**, **Style**, **Time of day**, **Sound** and **Info line**: the same settings as the keys above
- **the scene's action**, **Rare event** and **Other rare event**: the same as `f`, `x` and `X` (in the cycle mode they end it and switch to the user mode)
- **Resume** and **Quit**

The version of Diorascii and its copyright are shown at the bottom. Everything the menu does can also be done with
the keys; it exists so that nothing has to be remembered.

## Styles

| Style | Look | Use it when |
|---|---|---|
| `full` (default) | gradient backdrops and the scene's own colors | you want the scenes at their best |
| `terminal` | no backdrop: your terminal's own background, sprites in the 16 named ANSI colors | you want the scene to follow your terminal theme, or the terminal has no truecolor |
| `mono` | no color at all: only your terminal's foreground and background | the terminal is monochrome, or you prefer it plain |

Switch with the `c` key, the menu, or `--style full|terminal|mono` at startup. On the Linux text console the default
is `terminal`, because the console cannot show the backdrops.

## Time of day

The aquarium, the forest, the desert and the mountains have a day and night cycle, and the moon has a lunar day with
an earthrise. A full day takes four minutes. In the [user mode](#cycle-mode-and-user-mode) press `t` (or use the menu) to choose (in the cycle mode the clock always runs):

| Setting | Meaning |
|---|---|
| cycle | the clock runs; day turns to dusk, night and dawn |
| dawn, day, dusk, night | the time of day is frozen there |
| real time | the scene follows the clock of your computer: when it is evening outside, it is evening in the scene |

`--hour 22` starts at 22:00 and `--realtime` starts in real time. The choice carries over when you change scenes. At
night most animals sleep, and some, like fireflies and the desert's bats, only come out then.

## Sound

Ambient sound is on by default. It is synthesized while the program runs (there are no recordings), and it follows
what happens on screen: wind, water, traffic, the siren of an ambulance, the roar of a flood. Sounds come from where
they happen: a siren moves with its vehicle, a lightning strike cracks where it struck, bubbles rise round you, and
what is far off or off-screen comes from the sides.

### Off, stereo, immersive

The menu's **Sound** entry has three settings, and the `a` key goes round them:

| Setting | What it does |
|---|---|
| **off** | no sound |
| **stereo** | a plain stereo pair on any output, whatever the output could do. On AirPods the system adds no 3D audio to it |
| **immersive** (the default) | the sound is made for the speakers your output has. On a surround system it plays on all of them, with the height speakers if there are any (up to 9.1.6); on AirPods it is handed to the system as multichannel sound, so that the system's own 3D audio places it around you |

The small print at the bottom of the menu says what is really playing, so you do not have to guess:

| It says | Meaning |
|---|---|
| `Audio output: 7.1 on 8 channels` | the sound is made for the layout and played on the output's channels |
| `Audio output: 7.1.4 on 2 channels (mixed down)` | the output has fewer channels than the layout; the system or the sound library mixes it down |
| `Audio output: 7.1.4 on 2 channels (spatialized)` | the headphones' own spatializer makes the sound from the layout (AirPods) |
| `Audio output: 2.0 on 2 channels` | plain stereo |
| `Audio output: sound off` | no sound |

`--mute` starts with the sound off. Without a sound device Diorascii simply stays silent; with `--audio` it tells you
instead that the device is missing. `--audio-layout` names the layout of the immersive sound (see the
[command line](#command-line)); you normally never need it, because immersive finds the layout itself.

### What it does on each platform

| Platform | Immersive | Stereo | Caveats |
|---|---|---|---|
| **macOS** | Plays through the system's AVSampleBufferAudioRenderer, which places each channel on the output channel of the same position. The layout you set for the device in *Audio MIDI Setup* (stereo, 5.1, 7.1.4...) is what is found, height speakers included; a layout the device has no speakers for steps down to the richest one it has (7.1.4 on a 5.1 device plays as 5.1). **AirPods** (and other Bluetooth headphones) get the system's **3D audio**: immersive sends them 7.1.4 | a stereo pair | Spatial audio for AirPods is the system's own feature and is switched in the system (Control Center, *Sound*); while Diorascii plays immersive, the system's sound menu shows 3D audio as available and active. Sounds that an action starts (feeding the fish, a meteor...) play about a quarter of a second late. On a Bluetooth device that is *not* AirPods, immersive is treated the same way (not tried). Checked with AirPods Pro and a 16-channel virtual device, not yet by ear in a room |
| **Apple TV (tvOS)** | The same renderer. The audio route says how many channels it has, and whether it spatializes. On a receiver it plays on its layout, on AirPods the 3D audio. Swipe down on the Siri Remote goes round off, stereo, immersive | a stereo pair | Built from the source only (no release download). **Not yet tried on a real Apple TV**; the simulator has a stereo route |
| **iOS** | the sound code is ready for it, but there is no iPhone or iPad app | | |
| **Windows** | the layout the system reports, through the bundled audio library: up to 7.1 (over HDMI, plain PCM is limited to eight channels, so 7.1.4 and 9.1.6 reach a receiver only through a multichannel interface or an Atmos-capable path) | a stereo pair | No system 3D audio for headphones. The surround layouts were never run on Windows |
| **Linux** | the layout the sound system (PulseAudio, PipeWire, ALSA) reports; choosing a layout above what a stereo device has is allowed and the sound server mixes it down | a stereo pair | Plain ALSA `default` is often stereo; select a surround device in the system's sound settings. No system 3D audio for headphones. Not tried on a surround system |

Everywhere the layout follows a change of output while Diorascii runs (headphones plugged in, another output chosen),
where the system reports the change. How the surround layouts sound in a room has not been judged yet: they were
checked by measuring what plays on every channel.

## Command line

    diorascii [options]

| Option | Meaning |
|---|---|
| `--scene NAME` | start in this scene: `aquarium`, `forest`, `desert`, `mountains`, `city`, `moon` (`--list-scenes` prints them) |
| `--style NAME` | `full` (default), `terminal` or `mono` |
| `--user` | start in the user mode instead of the cycle mode (see [above](#cycle-mode-and-user-mode)) |
| `--hour H` | start at this time of day, 0 to 24 |
| `--realtime` | let the time of day follow this computer's clock |
| `--mute` | start with the sound off |
| `--audio` | report a missing sound device instead of staying silent |
| `--audio-layout L` | the layout of the immersive [sound](#sound), for a test: `auto` (the default), `2.0` (starts the sound as stereo), `5.1`, `7.1`, `5.1.2`, `5.1.4`, `7.1.2`, `7.1.4`, `9.1.4`, `9.1.6` |
| `--fps N` | frame rate (default 20) |
| `--truecolor`, `--256` | force 24-bit or 256-color output when the detection guesses wrong |
| `--console` | force the Linux text console look (16 colors, dark backdrops) |
| `--seed N` | the same landscape and the same course of events every time |
| `--assets DIR` | add or replace sprites and scenes from a directory (see the README's *Extending* sections) |
| `--version`, `--help` | print the version, or the list of options |

Examples:

    diorascii --user --scene city --hour 22          # the city at night
    diorascii --user --scene forest --style terminal # follow my terminal theme
    diorascii --user --realtime --mute               # a quiet window onto the current time of day

## Looking without a terminal

For a quick look, for scripts, or to copy a scene into a text file or a chat, Diorascii can render a scene without
a terminal and print it as plain text. `--dump N` runs the scene for N steps (20 steps are one second) and prints
the last picture; `--size WxH` sets its size.

    diorascii --dump 300 --seed 3 --scene aquarium --size 78x20

<!-- dump: --dump 300 --seed 3 --scene aquarium --size 78x20 -->
```text
                                      /    \
                                     |      |
                                      \    /
                                       '--'
----~~~~--_______---~~---____--~~~~~~~--____---~~---______--~~~~~----_---~~~~~
    ..     .  ..    .. ..         .         .  _.-._   ..  .               ..
._,                                 <=={o>    (_____)
.'/                                            |/|\|
'               .---.
             ><=(  o )>
                `---'
                         \                 <o>=          |>             |\
                          \     \                       _|_    _   _  <o}==>
           )               )     (                     | _ |__| |_| |__| _\|
       ,  -. , .-.   .     /  ,  ,  ,                  |_|_|  ___ ___  |_|_/
      \|<o|/`-'   `-'     )  \|/\|/ |/                 | _ |  |  |  |  | _)|
       \|/(\|/            (   \|/)\|/                  |   |\/(o_o)\/__|  (|
        | ) |             )    | ( |                   |___|  // \\    |__)|
',`.:, ,`, ,:.:,'. ::'.`.'':. `,` :.:,... ::. .,:``: ', :'. ,,,`: `'`':`...:''
. .:'.`.'`...,..  .. . : ,'::,:.:. :., ..:``,`:.,.',,.``.': .,'  :.`.`,'.  .'`
```
<!-- /dump -->

`--act N`, `--rare N` and `--rare-other N` trigger the action, the rare event or the second one at step N of such a
run, which is how the pictures in this manual were made:

    diorascii --dump 120 --act 100 --seed 3 --scene aquarium --size 78x20

<!-- dump: --dump 120 --act 100 --seed 3 --scene aquarium --size 78x20 -->
```text
                                /    \
                               |      |
                                \    /
                                 '--' _.-._
--____---~~~--_______--~~~~---__---~~(_____)_____---~---______--~~~~~~---_---~
. .     .  .  .  .. .           ..    |\|/|.    .. ..         .         .
                          <=={o>                     *      ++*+++


                                                                   <o>=
                                                 <o}==>
--.                       \                              |>             |>/
  )=><                     (      /                     _|_    _   _   _|(
--'       \                /     (                     | _ |__| |_| |__| ) |
       ,  ,( ,            (   ,  ,  ,                  |-._| .-._ _._  |_\_|
      \|/\|/ |/           )  \|/\|/ |/                <o _`-' | `-' |  | _)|
_o)\/  \|/(\|/            (   \|/)\|/                  |   |__|  |  |__|  (|
 //     | ) |             )    | ( |                   |___|     |     |__)|
',`.:, ,`, ,:.:,'. ::'.`.'':. `,` :.:,... ::. .,:``: ', :'. ,,,`: `'`':`...:''
. .:'.`.'`...,..  .. . : ,'::,:.:. :., ..:``,`:.,.',,.``.': .,'  :.`.`,'.  .'`
```
<!-- /dump -->

(The aquarium a second after the action at step 100: the flakes of fish food, drawn as `*` and `+`, are sinking from the surface.)
`--gallery` prints every sprite the program knows.
