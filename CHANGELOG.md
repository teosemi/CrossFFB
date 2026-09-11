# Changelog

All notable changes to CrossFFB are documented here.

## Unreleased

### Added

- On macOS 27 CrossOver no longer detects the G29, so games saw neither the
  wheel nor its force feedback. When the game's list of controllers comes back
  without it, the proxy now adds the G29 itself: the bridge reads steering,
  pedals, hat and buttons and streams them back over the same connection, and
  the proxy serves them the way DirectInput does - standard and custom data
  formats, buffered input, ranges, dead zones - next to the force feedback it
  already handled. The wheel keeps the identity Wine gave it; the axes follow
  Windows, so pedal bindings made under Wine may need redoing once. If CrossOver
  detects the wheel again, the proxy steps aside. A lost bridge leaves the
  pedals released rather than stuck.
- `GET_INPUT` on the control port prints what the bridge reads from the wheel,
  and `scripts/build_wheel_probe.sh` builds a console tool that reads the G29
  through DirectInput inside a bottle.

### Fixed

- Unplugging the wheel no longer trips a guard fault in the bridge: the
  watchdog closed the listening socket and the loop went on to use it.

## 1.1.0

### Changed

- The menu bar panel is redrawn as an instrument: the steering range is an arc
  you drag, force a stepped scale, the damper a thick surface you drag anywhere.
  Every number can be typed by double clicking it. Black on a dark Mac, white on
  a light one, blue on both, following the system appearance.
- Wheel and game are separate lamps, so a wheel that has gone away can no longer
  hide behind a connected game.
- The log moved out of the panel, which used to widen from 310 to 470 points,
  into a translucent window pinned to the top left of the screen. It floats over
  a game running windowed or borderless.
- Setup is two columns: what you point at on the left, what it did on the right,
  and only Install Proxy is a filled button.
- Onboarding is no longer a five page slideshow but a checklist of the four
  things between a fresh install and force feedback, each row reading real
  state. The Wine override row hands over the line to paste and turns green once
  a game has reached the bridge, which proves the override took.
- The Windows proxy no longer writes a trace line per force feedback event. It
  had grown to gigabytes in a game folder and wrote to disk from the thread
  delivering force feedback. Startup and errors are still logged.
- The app and the native helper are built as universal binaries.

### Added

- Damper support. Assetto Corsa Competizione asks for a condition damper
  alongside the constant force and updates it while driving; the bridge used to
  drop those messages. It now drives the wheel's damper, and the panel has a
  control for its strength. Euro Truck Simulator 2 does not use this effect, so
  nothing changes there.
- Setup reports the architecture of every executable it finds and warns when a
  folder holds no 64-bit executable.
- Setup recognises the Unreal Engine layout and offers to switch to
  `<Project>/Binaries/Win64`, which is the folder Assetto Corsa Competizione
  actually loads the proxy from.
- A **Detailed proxy log** toggle in Setup, and the `CROSSFFB_PROXY_LOG`
  environment variable for the same purpose.

### Fixed

- The project no longer expects prebuilt binaries from outside the repository,
  so a clean clone builds. Both native components are compiled from source by
  versioned scripts, and continuous integration checks this on every push.
- Debug builds sign locally and no longer need an Apple developer account.
- The README no longer claims both MIT licensing and that redistribution is not
  permitted.

## 1.0.0

First public release: menu bar app, native bridge, DirectInput 8 proxy,
Setup, onboarding, and a signed and notarized DMG.
