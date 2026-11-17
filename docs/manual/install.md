# Installation

Every release contains an installer for each platform that puts `diorascii` into your PATH, so that you can run it
from any terminal. Nothing else needs to be installed. The links below always point to the files of the
[latest release](https://github.com/ChristianAhrens/Diorascii/releases/latest).

| Platform | Download |
|---|---|
| macOS (Apple silicon and Intel) | [`Diorascii-x.y.z-macOS.pkg`](https://github.com/ChristianAhrens/Diorascii/releases/latest) |
| Windows 10 / 11 (x64), installer | [`Diorascii-x.y.z-Windows-Setup.exe`](https://github.com/ChristianAhrens/Diorascii/releases/latest) |
| Windows, portable | [`Diorascii-x.y.z-Windows.zip`](https://github.com/ChristianAhrens/Diorascii/releases/latest) |
| Debian, Ubuntu and derivatives, x64 | [`Diorascii-x.y.z-Linux-x64.deb`](https://github.com/ChristianAhrens/Diorascii/releases/latest) |
| Debian, Ubuntu and derivatives, arm64 | [`Diorascii-x.y.z-Linux-arm64.deb`](https://github.com/ChristianAhrens/Diorascii/releases/latest) |
| Fedora, openSUSE and derivatives, x64 | [`Diorascii-x.y.z-Linux-x64.rpm`](https://github.com/ChristianAhrens/Diorascii/releases/latest) |
| Fedora, openSUSE and derivatives, arm64 | [`Diorascii-x.y.z-Linux-arm64.rpm`](https://github.com/ChristianAhrens/Diorascii/releases/latest) |
| Any other Linux, x64 | [`Diorascii-x.y.z-Linux-x64.tar.gz`](https://github.com/ChristianAhrens/Diorascii/releases/latest) |
| Any other Linux, arm64 | [`Diorascii-x.y.z-Linux-arm64.tar.gz`](https://github.com/ChristianAhrens/Diorascii/releases/latest) |

(On this page's website version the links go straight to the file; on GitHub they open the release page.
Older releases are on the [releases page](https://github.com/ChristianAhrens/Diorascii/releases).)

Not sure which Linux you have? Run `uname -m`: `x86_64` is x64, `aarch64` is arm64 (for example a Raspberry Pi 4 or 5
with a 64-bit system, or an ARM server). Up to release 1.3.0 the Linux files were called `Diorascii-x.y.z-Linux.*` and were x64 only.

After installing, open a *new* terminal window (so that it sees the new PATH entry) and run:

    diorascii --version
    diorascii

`q` quits. If the version prints but the scene looks wrong, see [Troubleshooting](faq.md).

## macOS

1. Download `Diorascii-x.y.z-macOS.pkg` (a universal binary: Apple silicon and Intel).
2. Double-click it and follow the installer. It places `diorascii` in `/usr/local/bin`, which every shell has in its
   PATH, and the license and readme in `/usr/local/share/doc/diorascii`.
3. Open Terminal (or iTerm2, Ghostty, Kitty, WezTerm...) and run `diorascii`.

The package attached to a release is signed and notarized, so macOS should open it without a warning. If macOS
still refuses ("cannot be opened because the developer cannot be verified"), right-click the package, choose *Open*,
and confirm once.

To upgrade, install the newer package over the old one. macOS has no uninstaller for packages, so to uninstall:

    sudo rm /usr/local/bin/diorascii && sudo rm -r /usr/local/share/doc/diorascii
    sudo pkgutil --forget com.ChristianAhrens.Diorascii

## Windows

1. Download `Diorascii-x.y.z-Windows-Setup.exe` and run it. It installs to *Program Files*, adds `diorascii` to the
   PATH for both cmd and PowerShell, and registers an uninstaller.
2. Windows SmartScreen may say that it protected your PC, because the installer is not code-signed. Click *More info*,
   then *Run anyway*.
3. Open **Windows Terminal** (recommended) or PowerShell, and run `diorascii`.

Installing a newer setup over an older install upgrades it in place. To uninstall, use *Settings > Apps > Installed
apps* (or the uninstaller in the install folder).

**Portable:** unpack `Diorascii-x.y.z-Windows.zip` anywhere and run `bin\diorascii.exe`. Nothing is installed and
nothing is changed in the system.

Windows 10 or newer is required (the console must understand ANSI escape sequences); Windows Terminal is the recommended
terminal.

## Linux

Pick the package that matches your distribution and CPU (see the table above). The `.deb` and `.rpm` packages install
`/usr/bin/diorascii` and the license and readme under `/usr/share/doc/diorascii`, and your package manager keeps
them upgradeable and removable.

### Debian, Ubuntu, Linux Mint, Raspberry Pi OS (.deb)

    sudo apt install ./Diorascii-x.y.z-Linux-x64.deb       # or ...-Linux-arm64.deb
    diorascii

Upgrade by installing the newer file the same way; remove with `sudo apt remove diorascii`.

### Fedora, RHEL and derivatives, openSUSE (.rpm)

    sudo dnf install ./Diorascii-x.y.z-Linux-x64.rpm       # or ...-Linux-arm64.rpm
    sudo zypper install ./Diorascii-x.y.z-Linux-x64.rpm    # openSUSE
    diorascii

Upgrade by installing the newer file; remove with `sudo dnf remove diorascii` (or `sudo zypper remove diorascii`).

### Any other distribution (.tar.gz)

    tar xzf Diorascii-x.y.z-Linux-x64.tar.gz               # or ...-Linux-arm64.tar.gz
    Diorascii-x.y.z-Linux-x64/bin/diorascii

The archive has no libraries to install; the unpacked folder can live anywhere. Add its `bin` directory to your PATH, or
copy `bin/diorascii` to a directory that is, for example `~/.local/bin`.

### The Linux text console

Diorascii also runs on a plain text console (Ctrl+Alt+F3 and similar, or a Raspberry Pi without a desktop). It
detects `TERM=linux` (also inside tmux) and then starts in the 16-color *terminal* style, because the console cannot
show the gradient backdrops of the default style. `--console` forces that look in any terminal.

## Apple TV (tvOS) and iOS

The Apple TV app is not part of the release downloads. It is generated and built from the source with CMake and Xcode
(macOS only), see [Build](https://github.com/ChristianAhrens/Diorascii#build) in the README; a TestFlight build is
possible for the owner. It shows the same scenes with the Siri Remote (swipe left and right: scene, up: time of day, down:
the sound, click: the scene's action, hold the click: the menu) and plays the sound through the audio route of the Apple
TV, with 3D audio on AirPods. It has not yet been tried on a real Apple TV. There is no iPhone or iPad app.

## Sound on your system

Nothing to install: the sound is made by the program and played through the system's audio. On macOS and Apple TV the
system places the channels and gives AirPods their 3D audio; on Windows and Linux it plays on the layout the system
reports. What each platform does, and its limits, is on the [Using Diorascii](usage.md#what-it-does-on-each-platform) page.

## Which terminal?

Any modern terminal that handles UTF-8 and ANSI colors works. For the full-color look use one with **truecolor**
(24-bit color): Windows Terminal, iTerm2, Terminal.app (256 colors), GNOME Terminal, Konsole, Kitty, WezTerm,
Alacritty, Ghostty and most others. Diorascii detects what the terminal supports and falls back to 256 colors by
itself; `--truecolor` and `--256` override the detection. A larger window shows more: the mountains and the city use
the whole height of the terminal.

## Build from source

If there is no package for you, Diorascii builds with CMake and any C++20 compiler and has no dependencies; see
[Build](https://github.com/ChristianAhrens/Diorascii#build) in the README.
