# CrossFFB

[![Build](https://github.com/teosemi/CrossFFB/actions/workflows/build.yml/badge.svg)](https://github.com/teosemi/CrossFFB/actions/workflows/build.yml)

CrossFFB is a macOS menu bar app that enables Logitech G29 Force Feedback for supported 64-bit Windows games running through CrossOver/Wine.

It uses a local `dinput8.dll` proxy installed next to the game executable and a native macOS bridge that sends Force Feedback commands to the wheel.

Made by **Matteo Seminara & Maurizio Seminara**.

---

## Download

Download the latest DMG from the [Releases](https://github.com/teosemi/CrossFFB/releases/latest)
page. The app is signed and notarized with an Apple Developer ID.

---

## Requirements

- macOS 14 Sonoma or later
- Logitech G29 connected via USB
- CrossOver or Wine
- A supported 64-bit Windows game

Currently tested mainly with:

- Euro Truck Simulator 2
- Assetto Corsa Competizione

---

## How it works

```text
Windows game
    ↓
dinput8.dll proxy
    ↓
CrossFFB bridge
    ↓
Logitech G29 via macOS HID
```

CrossFFB does **not** install a kernel extension or a macOS driver.

The Windows proxy is installed locally in the selected game folder.

---

## Installation

1. Download the DMG and open it.
2. Drag `CrossFFB.app` to Applications.
3. Open CrossFFB. It starts its bridge and opens a checklist of what is left
   to do.
4. Choose the folder that contains the Windows game executable, either from the
   checklist or from **Setup** behind the gear in the panel.
5. Press **Install Proxy**.

For Euro Truck Simulator 2, choose the folder that contains:

```text
eurotrucks2.exe
```

Usually:

```text
Euro Truck Simulator 2/bin/win_x64
```

---

## CrossOver setup

In the CrossOver bottle used by the game, `dinput8` must be set to:

```text
native,builtin
```

Steps:

1. Open CrossOver.
2. Select the bottle where your game is installed.
3. Open **Bottle Settings**.
4. Open **Wine Configuration**.
5. Go to the **Libraries** tab.
6. Add a new override for:

```text
dinput8
```

7. Set it to:

```text
native,builtin
```

8. Apply and close Wine Configuration.

Then install the proxy from CrossFFB Setup.

---

## Usage

1. Start CrossFFB. The bridge starts with it; you do not have to press anything.
2. Connect the Logitech G29 over USB. Unplug it and CrossFFB notices; plug it
   back in and the bridge restarts on its own.
3. Start the Windows game.

Click the steering wheel in the menu bar for the panel.

### The panel

Two lamps at the top say what is live: **WHEEL** when the G29 is open, **GAME**
when a game is talking to the bridge. Both green means force feedback is
flowing.

- **The arc** is the steering range. Drag along it, tap 540 / 720 / 900, or type
  an exact angle.
- **FORCE** is the force feedback gain, on a scale you drag anywhere along.
- **DAMPER** is a thick surface you can grab anywhere. Zero turns it off.

**Every number can be typed**: double click it, enter a value, press Return.
Escape or a click elsewhere cancels.

Recommended starting values:

```text
FORCE:  1.00
RANGE:  900°
DAMPER: 1.00
```

### Damper

Some games ask for a damper alongside the constant force - Assetto Corsa
Competizione updates it constantly while you drive, and it is what gives the
wheel its weight. Euro Truck Simulator 2 never asks for it, so the setting does
nothing there.

### Log

**LOG** opens a translucent window pinned to the top left of the screen, showing
the lamps and the tail of the bridge log. It floats over a game running windowed
or borderless; nothing can float over an exclusive-fullscreen game.

### First run

The first launch opens a checklist of the four things between a fresh install
and force feedback - wheel, game folder, proxy, Wine override - which fills
itself in as you do them. It stays available from **Help** in the menu bar, and
is the first place to look when something stops working.

---

## Removing the proxy

Open CrossFFB Setup and press **Remove Proxy**.

CrossFFB installs `dinput8.dll` only in the selected game folder.

It does **not** replace CrossOver/Wine system DLLs.

Do **not** manually install the proxy in:

```text
drive_c/windows/system32
```

or:

```text
drive_c/windows/syswow64
```

---

## Troubleshooting

### The WHEEL lamp is dark

- Make sure the Logitech G29 is connected via USB. CrossFFB starts the bridge
  again by itself within a couple of seconds of the wheel reappearing.
- Keep your hands away from the wheel while the bridge starts.
- If the panel shows a warning row instead, it names what went wrong.

### The GAME lamp stays dark

Check that:

- `dinput8.dll` is installed in the same folder as the game executable.
- The CrossOver bottle has `dinput8` set to `native,builtin`.
- You selected the correct game folder.
- You are running the 64-bit version of the game.

### Force feedback is too weak or too strong

Adjust **FORCE** in the panel, while driving if you like - it applies straight
away.

### Steering rotation feels wrong

Adjust the arc, or type the angle the game expects.

### The wheel feels heavy or dead in Assetto Corsa Competizione

Adjust **DAMPER**. At zero the damper is off entirely.

---

## Building from source

### Requirements

- macOS 14 or later
- Xcode 16 or later (Command Line Tools included)
- [mingw-w64](https://www.mingw-w64.org) for the Windows proxy: `brew install mingw-w64`

The mingw-w64 toolchain is only needed to build `dinput8.dll`. Without it the app
still builds, but it cannot install the proxy into a game folder.

### Build

```bash
git clone https://github.com/teosemi/CrossFFB.git
cd CrossFFB
open CrossFFB.xcodeproj
```

Then build and run the `CrossFFB` scheme. Xcode compiles both native components
automatically and embeds them into the app bundle, so no prebuilt binary is needed.

The same build works from the command line:

```bash
xcodebuild -project CrossFFB.xcodeproj -scheme CrossFFB -configuration Debug build
```

The Debug configuration signs the app locally, so no Apple Developer account is
required to build and run it.

### Native components

Both are produced by versioned scripts under `scripts/`:

| Component | Source | Script |
|---|---|---|
| `g29_ffb_bridge` | `native_bridge/g29_ffb_bridge.c` | `scripts/build_native_bridge.sh` |
| `dinput8.dll` | `dinput8_proxy/dinput8.cpp` | `scripts/build_dinput8_proxy.sh` |

To build them outside Xcode:

```bash
scripts/prepare_resources.sh
```

The results are written to `build/resources/`, which is not tracked by git. The
bridge is built as a universal binary (arm64 + x86_64); the Xcode build narrows it
to whatever architectures the current build is targeting.

### Release builds

The Release configuration signs with the maintainers' Apple Developer ID and
enables Hardened Runtime. To build Release with your own account, override the
team:

```bash
xcodebuild -project CrossFFB.xcodeproj -scheme CrossFFB -configuration Release DEVELOPMENT_TEAM=YOURTEAMID build
```

Signing, notarization and DMG creation for public releases are handled separately
by the maintainers.

---

## Troubleshooting the proxy

The proxy writes a short `dinput8_proxy.log` next to the game executable,
covering startup, the DirectInput handshake and connection errors.

Detailed per-event tracing is off by default, because it used to write one
line per force update and grew to gigabytes. To turn it back on for
diagnostics, use **Detailed proxy log** in CrossFFB Setup, which writes a
`crossffb_verbose_log.enabled` marker next to the game executable.

The same tracing can be enabled without the app by setting this variable in
the CrossOver/Wine bottle environment:

```text
CROSSFFB_PROXY_LOG=1
```

Either way the setting is read when the game starts, so restart the game to
apply it, and expect the log to grow by tens of megabytes per session while
it is enabled.

---

## Current limitations

- Logitech G29 only for now.
- 64-bit Windows games only.
- Tested mainly with Euro Truck Simulator 2 and Assetto Corsa Competizione.
- Constant force and damper are supported; no game tested so far asks for any
  other effect.
- Tested on Apple Silicon only, although the app and its helper are built as universal binaries.
- CrossOver/Wine setup still requires manually setting `dinput8 = native,builtin`.

---

## License

CrossFFB is released under the MIT License. See [LICENSE](LICENSE) for the full text.

The dinput8 proxy and the native macOS bridge are included in source form and are part of this project.

Made by Matteo Seminara & Maurizio Seminara.
