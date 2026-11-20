# Now Playing for the RLCD-4.2 board

Shows what Apple Music (macOS) or the Windows media player is playing (title, artist, album, cover, progress) on the board's
**Now Playing** screen, and lets the board's keys control it.

## Over Wi-Fi (no cable)

1. On the board, open **Now Playing**. On the waiting screen **hold the KEY button**, then pick your Wi-Fi
   network. The screen then shows the board's address.
2. On the computer run: `.venv/bin/python nowplaying.py --host crosspoint-np.local`
   (or use the IP address shown on the board, e.g. `--host 192.168.1.20`).

The program and the board must be on the same network. Wi-Fi draws much more power than USB, so it is
only on while this screen is open; leaving the screen restarts the board to Home. Anyone on the network
can connect to the board's port while it is on, so use it on a network you trust. Over Wi-Fi the board does
not need the USB cable, but if the USB program is also running the board uses that instead.

## Windows

The Windows version shows whatever the Windows media controls show, so it works with Apple Music for
Windows, Spotify, a browser tab (YouTube and others) and most players. It is **not yet tested on a real
Windows machine**: tell me what breaks.

```
py -m venv .venv
.venv\Scripts\pip install -r requirements.txt
.venv\Scripts\python nowplaying.py
```

Use Python 3.9 to 3.13 (the `winrt` packages need a wheel for your Python). No permission prompt is
needed. Cover and track details depend on the player: some (browsers) give no album or a wrong position.
The login service (`service.sh`) is macOS only for now.

## Set up (once, macOS)

```
cd tools/nowplaying
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

## Use

1. Connect the board to the Mac with its USB cable.
2. Start the program: `.venv/bin/python nowplaying.py`
3. Open **Now Playing** from the board's home menu.

The first time, macOS asks whether Terminal (or your editor) may control **Music**. Allow it. If you
missed the prompt: System Settings > Privacy & Security > Automation.

Opening the serial port restarts the board, so start the program **first**, then open Now Playing on
the board. Leave the program running; stopping it and starting it again restarts the board again.

## Run it by hand

Run these from this folder. Stop the program with Ctrl+C.

| Command | What it does |
|---|---|
| `.venv/bin/python nowplaying.py` | USB only. Exits if no board is found. |
| `.venv/bin/python nowplaying.py --host crosspoint-np.local` | Wi-Fi only (or `--host <IP shown on the board>`). |
| `.venv/bin/python nowplaying.py --wifi --wait` | Wi-Fi when the board is listening, otherwise USB. Keeps waiting and reconnects. |

To go back from the login service to running by hand, run `./service.sh uninstall`.

## Start it automatically (optional)

To avoid starting the program by hand, install it as a macOS login service:

```
./service.sh install
```

It then starts at every login, waits for the board, and reconnects when the board is unplugged or
restarted. It uses Wi-Fi when the board is listening for it (hold KEY on the waiting screen, see above),
otherwise the USB cable. A board that is plugged in is taken over USB, so unplug it to use Wi-Fi. Other commands: `status`, `log`, `start`, `stop`, `uninstall`.

The program holds the board's USB port, so **run `./service.sh stop` before flashing the board** and
`./service.sh start` afterwards. If you move this folder, run `./service.sh install` again. The log is
`~/Library/Logs/nowplaying.log`.

## Keys on the board

| Key | Action |
|---|---|
| BOOT (short) | play / pause |
| KEY (short) | next track |
| hold KEY | previous track |
| hold BOOT | leave the screen |

There is no volume control.

## Options

- `--dither atkinson|fs|threshold` how the cover is turned into black and white. The default,
  `atkinson`, is crisp. If the cover looks too dotted or flickers on the screen, try `threshold`
  (no dots at all); `fs` (Floyd-Steinberg) is the smoothest but the dottiest.
- `--port /dev/cu.usbmodemXXXX` choose the serial port (normally found automatically).
- `--wifi` use Wi-Fi when the board is listening, otherwise USB (what the login service uses).
- `--host crosspoint-np.local` connect over Wi-Fi instead of USB (see above).
- `--wait` keep running: wait for the board and reconnect (what the login service uses).
- `--demo` send a test card instead of reading Apple Music, to try the screen.

## How it works

The program polls Apple Music once a second through AppleScript (title, artist, album, position, play
state), exports the cover, and sends it over the USB serial port in short text lines. The board sends
its key presses back as `NP:` lines, and the program runs the matching Music command. The line format
is documented in `lib/NowPlaying/src/nowplaying/Protocol.h`.

## Tests

`.venv/bin/python -m unittest test_nowplaying` (the board-side parser is tested by the repo's host
tests, against `test/now_playing/data/session.txt`, which this program generates).
