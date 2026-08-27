# CrossFFB

[![Build](https://github.com/teosemi/CrossFFB/actions/workflows/build.yml/badge.svg)](https://github.com/teosemi/CrossFFB/actions/workflows/build.yml)

CrossFFB is a macOS menu bar app that enables Logitech G29 Force Feedback for supported 64-bit Windows games running through CrossOver/Wine.

It uses a local `dinput8.dll` proxy installed next to the game executable and a native macOS bridge that sends Force Feedback commands to the wheel.

Made by **Matteo Seminara & Maurizio Seminara**.

---

## Download

Download the latest DMG from the **Releases** section:

**CrossFFB-1.0.0.dmg**

The app is signed and notarized with Apple Developer ID.

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

1. Download `CrossFFB-1.0.0.dmg`.
2. Open the DMG.
3. Drag `CrossFFB.app` to Applications.
4. Open CrossFFB.
5. Open **Setup** from the menu bar.
6. Choose the folder that contains the Windows game executable.
7. Press **Install Proxy**.

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

1. Start CrossFFB.
2. Make sure the Logitech G29 is connected via USB.
3. Start the Windows game.
4. When the game connects, CrossFFB should show:

```text
Game connected
```

You can adjust:

- **FFB Gain**: force strength
- **Steering Range**: wheel rotation degrees

Recommended starting values:

```text
FFB Gain: 1.00
Steering Range: 900°
```

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

### CrossFFB shows “Wheel not found”

- Make sure the Logitech G29 is connected via USB.
- Quit and reopen CrossFFB.
- Keep your hands away from the wheel while the bridge starts.

### The game does not connect

Check that:

- `dinput8.dll` is installed in the same folder as the game executable.
- The CrossOver bottle has `dinput8` set to `native,builtin`.
- You selected the correct game folder.
- You are running the 64-bit version of the game.

### Force Feedback is too weak or too strong

Adjust **FFB Gain** from the CrossFFB menu bar.

### Steering rotation feels wrong

Adjust **Steering Range** from the CrossFFB menu bar.

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
- Tested on Apple Silicon only, although the app and its helper are built as universal binaries.
- CrossOver/Wine setup still requires manually setting `dinput8 = native,builtin`.

---

## License

CrossFFB is released under the MIT License. See [LICENSE](LICENSE) for the full text.

The dinput8 proxy and the native macOS bridge are included in source form and are part of this project.

Made by Matteo Seminara & Maurizio Seminara.
