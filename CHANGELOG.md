# Changelog

All notable changes to CrossFFB are documented here.

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

### Added

- Damper support. Assetto Corsa Competizione asks for a condition damper
  alongside the constant force and updates it while driving; the bridge used to
  drop those messages. It now drives the wheel's damper, and the menu bar has a
  checkbox and a strength slider for it. Euro Truck Simulator 2 does not use
  this effect, so nothing changes there.
- Setup reports the architecture of every executable it finds and warns when a
  folder holds no 64-bit executable.
- Setup recognises the Unreal Engine layout and offers to switch to
  `<Project>/Binaries/Win64`, which is the folder Assetto Corsa Competizione
  actually loads the proxy from.
- A **Detailed proxy log** toggle in Setup, and the `CROSSFFB_PROXY_LOG`
  environment variable for the same purpose.

### Changed

- The Windows proxy no longer writes a trace line per force feedback event. It
  had grown to gigabytes in a game folder and wrote to disk from the thread
  delivering force feedback. Startup and errors are still logged.
- The app and the native helper are built as universal binaries.

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
