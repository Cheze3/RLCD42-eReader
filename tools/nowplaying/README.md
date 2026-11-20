# Now Playing for the RLCD-4.2 board

Shows what Apple Music is playing (title, artist, album, cover, progress) on the board's
**Now Playing** screen, and lets the board's keys control it.

## Set up (once)

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
- `--demo` send a test card instead of reading Apple Music, to try the screen.

## How it works

The program polls Apple Music once a second through AppleScript (title, artist, album, position, play
state), exports the cover, and sends it over the USB serial port in short text lines. The board sends
its key presses back as `NP:` lines, and the program runs the matching Music command. The line format
is documented in `lib/NowPlaying/src/nowplaying/Protocol.h`.

## Tests

`.venv/bin/python -m unittest test_nowplaying` (the board-side parser is tested by the repo's host
tests, against `test/now_playing/data/session.txt`, which this program generates).
