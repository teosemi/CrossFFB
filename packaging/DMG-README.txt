CrossFFB
Force Feedback bridge for Logitech G29 on macOS

Made by Matteo Seminara & Maurizio Seminara


REQUIREMENTS

- macOS 14 Sonoma or later
- Logitech G29 connected via USB
- CrossOver or Wine
- A 64-bit Windows game


INSTALLATION

1. Drag CrossFFB.app to Applications.
2. Open CrossFFB. It starts its bridge on its own and opens a checklist
   of the four things left to do.


CROSSOVER SETUP

The game bottle must load dinput8 as native,builtin. CrossFFB cannot set
this for you yet, so it is the one step you do by hand:

1. Open CrossOver.
2. Select the bottle where your game is installed.
3. Open Bottle Settings, then Wine Configuration.
4. Go to the Libraries tab.
5. Add an override for dinput8 and set it to native,builtin.
6. Apply and close Wine Configuration.

The checklist has a button that copies that line to the clipboard, and
the row turns green once a game has reached the bridge - which proves the
override took.


INSTALL THE PROXY

1. Choose the folder that holds the game's 64-bit executable.
2. Press Install Proxy.

For Euro Truck Simulator 2 that is the folder with eurotrucks2.exe,
usually Euro Truck Simulator 2/bin/win_x64.

Assetto Corsa Competizione keeps its executable deeper, in
AC2/Binaries/Win64. CrossFFB recognises that layout and offers to move
there.

The proxy is installed only in the folder you chose. CrossFFB never
replaces the CrossOver or Wine system DLLs.


USING IT

Click the steering wheel in the menu bar.

Two lamps say what is live: WHEEL when the G29 is open, GAME when a game
is talking to the bridge. Both green means force feedback is flowing.

- The arc is the steering range. Drag it, tap 540 / 720 / 900, or type an
  exact angle.
- FORCE is the force feedback gain.
- DAMPER is used by games that ask for one, such as Assetto Corsa
  Competizione. Euro Truck Simulator 2 never does. Zero turns it off.

Every number can be typed: double click it, enter a value, press Return.

LOG opens a translucent window in the top left corner of the screen. It
floats over a game running windowed or borderless.

Suggested starting values: FORCE 1.00, RANGE 900, DAMPER 1.00.


REMOVING THE PROXY

Open Setup and press Remove Proxy. If CrossFFB replaced an existing
dinput8.dll it restores the backup; if it installed its own, it removes
it.


TROUBLESHOOTING

The WHEEL lamp is dark:
- Check that the G29 is connected over USB. CrossFFB starts the bridge
  again by itself within a couple of seconds of the wheel reappearing.
- Keep your hands off the wheel while the bridge starts.

The GAME lamp stays dark:
- Check that dinput8.dll sits next to the game executable.
- Check the bottle override is dinput8 = native,builtin.
- Check you picked the folder with the 64-bit executable.


NOTES

- 64-bit Windows games only; the proxy is dinput8.dll x64.
- Never install the proxy in the bottle's system32 or syswow64 folders.
- Tested with Euro Truck Simulator 2 and Assetto Corsa Competizione, on
  Apple Silicon.


https://github.com/teosemi/CrossFFB
