# Troubleshooting

## The colors look flat, or banded

The terminal probably does not offer 24-bit color, and Diorascii fell back to 256 colors. Use a truecolor terminal
(see [Which terminal?](install.md#which-terminal)), or force it with `--truecolor` if yours supports it and
simply does not say so. Over `ssh` or inside `tmux`, the variable `COLORTERM=truecolor` often has to be passed on.
If you would rather have no gradients at all, use `--style terminal`.

## Strange characters, or empty boxes

Diorascii draws with Unicode block and line characters. Make sure that the terminal's encoding is UTF-8 and that its font
has these glyphs; any common monospaced font (DejaVu Sans Mono, Menlo, Consolas, Cascadia, JetBrains Mono) does. On
the Linux text console, a few glyphs may come out as substitutes; `--console` selects the look made for it.

## The picture is cut off, or too small

Scenes are generated for the size of the terminal. Enlarge the window, or reduce the font size, and press `r` to
build a fresh landscape for the new size. Some things only appear when there is room for them: the city's railway and
side streets need a wide screen and a tall sky.

## The scene changes by itself

Diorascii starts in the [cycle mode](usage.md#cycle-mode-and-user-mode), where it moves through the scenes and
triggers the events itself. Choosing a time of day or an event by hand (`t`, `f`, `x`, `X`) ends it and switches to
the user mode; so do `o` and `--user`.

## There is no sound

Check that *Sound* in the menu (or `a`) is on, stereo or immersive, and that you did not start with `--mute`. The line
`Audio output:` at the bottom of the menu says what is playing. Start with `--audio`: it reports when no sound device
could be opened. On a remote machine (`ssh`) there is usually no sound device, and Diorascii stays silent by design.

## The sound is only in two speakers on my surround system

Check the *Audio output:* line in the menu: it should name your layout and its channel count (`7.1 on 8 channels`). If it
says `2.0 on 2 channels`, the system reports a stereo output: on macOS set the device's format in *Audio MIDI Setup* (for
example 7.1 or 7.1.4), on Linux select the surround device in the sound settings, and on Windows set the speaker
configuration. Over HDMI plain PCM is limited to eight channels. Choose *immersive*, not *stereo*, in the menu.

## There is no 3D audio on my AirPods

3D audio comes from the system, and only for *immersive*: with *stereo* it is off by design. On macOS and Apple TV, choose
*Sound: immersive*, put the AirPods in and select them as the output; the menu line then says
`(spatialized)` and the system's menu shows 3D audio as available. If it says `2.0 on 2 channels`, the output was not
recognized as spatializing headphones (other Bluetooth devices are not tried); on Windows and Linux the system has no
3D audio for this program. Also check the Spatial Audio setting of your system. The sound of an action starts about a
quarter of a second late on these platforms, which is normal.

## `diorascii: command not found`

Open a new terminal window after installing, so that it picks up the PATH. On Windows, make sure the installer's
*add to PATH* option was left on. With the Linux `.tar.gz` or the Windows `.zip`, nothing was added to the PATH: run
the binary by its path, or add its folder to the PATH yourself.

## The installer is blocked

Windows SmartScreen warns about the setup because it is not code-signed: *More info*, then *Run anyway*. On
macOS, if the package is refused, right-click it and choose *Open*. See [Installation](install.md).

## Does it use much CPU?

It is light, but it redraws at 20 frames per second and synthesizes sound. `--fps 10` halves the drawing work, `--mute`
switches the sound off, and `p` pauses. Over a slow `ssh` connection, a lower frame rate is also kinder to the link.

## Can I make my own scenes and sprites?

Yes: scenes and sprites are plain text files that you can load with `--assets DIR`. The formats are described in the
README's [Extending: sprites](https://github.com/ChristianAhrens/Diorascii#extending-sprites) and
[Extending: scenes](https://github.com/ChristianAhrens/Diorascii#extending-scenes) sections.

## Where do I report a problem?

On the [issue tracker](https://github.com/ChristianAhrens/Diorascii/issues). Please include the output of
`diorascii --version`, your operating system and terminal, and, for display problems, the values of `TERM` and `COLORTERM`.
