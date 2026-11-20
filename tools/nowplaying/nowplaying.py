#!/usr/bin/env python3
"""Shows what Apple Music is playing on the board's "Now Playing" screen, and lets the board's keys
control it. Run this on the Mac, connect the board with its USB cable, then open "Now Playing" on
the board. See README.md.

macOS only for now (Apple Music through AppleScript). The line protocol it speaks is documented in
lib/NowPlaying/src/nowplaying/Protocol.h.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from typing import List, Optional

import nowplaying_protocol as proto

CONTROLS = {"PLAYPAUSE": "playpause", "NEXT": "next track", "PREV": "previous track"}
RECORD_SEP = "\x1e"

# Sample tracks for --demo; they change every 12 seconds so the screen can be checked with each script.
DEMO_TRACKS = [
    ("Demo Song With a Long Title", "Demo Artist", "Demo Album"),
    ("사랑해 - 한국어 노래 제목", "방탄소년단 ㅋㅋ", "봄날 (Spring Day)"),
    ("Mưa rơi ngoài hiên - Đừng quên anh", "Nguyễn Văn Ơn", "Việt Nam trường ca"),
]

# ----- Apple Music ---------------------------------------------------------------------------

STATE_SCRIPT = '''
if application "Music" is not running then return "off"
tell application "Music"
  set playerStateText to (player state as text)
  if playerStateText is "stopped" then return "stopped"
  try
    set theTrack to current track
    set thePosition to player position
    set sep to (ASCII character 30)
    return playerStateText & sep & (name of theTrack) & sep & (artist of theTrack) & sep & (album of theTrack) & sep & (thePosition as text) & sep & ((duration of theTrack) as text)
  on error errMsg
    return playerStateText
  end try
end tell
'''

ARTWORK_SCRIPT = '''
on run argv
  set outPath to item 1 of argv
  tell application "Music"
    if player state is stopped then return "stopped"
    set theTrack to current track
    if (count of artworks of theTrack) is 0 then return "noart"
    set artData to raw data of artwork 1 of theTrack
  end tell
  set fileRef to open for access (POSIX file outPath) with write permission
  try
    set eof fileRef to 0
    write artData to fileRef
    close access fileRef
  on error errMsg
    try
      close access fileRef
    end try
    return "error: " & errMsg
  end try
  return "ok"
end run
'''


@dataclass
class Track:
    state: str            # playing, paused or stopped
    title: str = ""
    artist: str = ""
    album: str = ""
    position: float = 0.0
    duration: float = 0.0

    @property
    def key(self):
        return (self.title, self.artist, self.album, round(self.duration))


def to_seconds(text: str) -> float:
    """AppleScript formats numbers in the user's locale, so the decimal point may be a comma."""
    try:
        return float(text.strip().replace(",", "."))
    except ValueError:
        return 0.0


def parse_state(output: str) -> Optional[Track]:
    """Turns the state script's output into a Track; None when Music is closed or has nothing."""
    output = output.strip()
    if not output or output == "off":
        return None
    fields = output.split(RECORD_SEP)
    state = fields[0].strip().lower()
    if state == "stopped":
        return Track("stopped")
    mapped = "paused" if state == "paused" else "playing"
    if len(fields) < 6:
        return Track(mapped)
    return Track(mapped, fields[1], fields[2], fields[3], to_seconds(fields[4]), to_seconds(fields[5]))


class AppleMusic:
    def _osascript(self, script: str, *args: str, timeout: float = 8.0) -> str:
        result = subprocess.run(["osascript", "-e", script, *args], capture_output=True, text=True, timeout=timeout)
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or "osascript failed")
        return result.stdout

    def poll(self) -> Optional[Track]:
        return parse_state(self._osascript(STATE_SCRIPT, timeout=5.0))

    def artwork(self) -> Optional[bytes]:
        """The current track's cover as the file bytes (PNG or JPEG), or None."""
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "art.bin")
            if self._osascript(ARTWORK_SCRIPT, path).strip() != "ok" or not os.path.exists(path):
                return None
            with open(path, "rb") as f:
                return f.read()

    def control(self, event: str) -> None:
        self._osascript(f'tell application "Music" to {CONTROLS[event]}', timeout=5.0)


# ----- Cover art -----------------------------------------------------------------------------

def pack_bits(image) -> bytes:
    """Packs a Pillow '1' image: rows of ceil(width/8) bytes, most significant bit first, 1 = black."""
    return bytes(b ^ 0xFF for b in image.convert("1", dither=0).tobytes())


def atkinson(gray):
    """Atkinson error-diffusion dithering (crisper than Floyd-Steinberg on a small screen)."""
    from PIL import Image
    w, h = gray.size
    px = [float(v) for v in gray.tobytes()]
    for y in range(h):
        for x in range(w):
            i = y * w + x
            old = px[i]
            new = 255.0 if old >= 128 else 0.0
            px[i] = new
            err = (old - new) / 8.0
            for dx, dy in ((1, 0), (2, 0), (-1, 1), (0, 1), (1, 1), (0, 2)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    px[ny * w + nx] += err
    out = Image.frombytes("L", (w, h), bytes(255 if v >= 128 else 0 for v in px))
    return out.convert("1", dither=0)


def make_art(data: bytes, size: int = proto.ART_SIZE, dither: str = "atkinson") -> bytes:
    """Cover file bytes to a 1-bit size x size bitmap in the board's format."""
    import io
    from PIL import Image, ImageOps
    image = Image.open(io.BytesIO(data))
    if image.mode in ("RGBA", "LA", "P"):
        image = image.convert("RGBA")
        backdrop = Image.new("RGBA", image.size, (255, 255, 255, 255))
        image = Image.alpha_composite(backdrop, image)
    gray = ImageOps.fit(image.convert("L"), (size, size), Image.LANCZOS)  # centered square crop
    gray = ImageOps.autocontrast(gray, cutoff=1)
    if dither == "atkinson":
        return pack_bits(atkinson(gray))
    if dither == "threshold":
        return pack_bits(gray.point(lambda p: 255 if p >= 128 else 0).convert("1", dither=0))
    return pack_bits(gray.convert("1", dither=Image.FLOYDSTEINBERG))


def demo_art(size: int = proto.ART_SIZE) -> bytes:
    """A test pattern (rings and a gradient) for trying the screen without any music."""
    from PIL import Image, ImageDraw
    image = Image.new("L", (size, size), 255)
    draw = ImageDraw.Draw(image)
    for x in range(size):
        draw.line([(x, 0), (x, size // 3)], fill=int(255 * x / size))
    for r in range(size // 2, 8, -22):
        draw.ellipse([size // 2 - r, size // 2 - r + 30, size // 2 + r, size // 2 + r + 30], outline=0, width=4)
    import io
    buffer = io.BytesIO()
    image.save(buffer, "PNG")
    return buffer.getvalue()


# ----- The board -----------------------------------------------------------------------------

_EVENT = re.compile(r"NP:(HELLO \d+|ACK \d+|PONG|PLAYPAUSE|NEXT|PREV)(?![A-Za-z0-9])")


class BoardGone(Exception):
    """The board is not connected (or was unplugged)."""


class Board:
    VID = 0x303A  # Espressif's USB vendor ID

    def __init__(self, port: Optional[str]):
        import serial
        from serial.tools import list_ports
        self._serial_module = serial
        if port is None:
            ports = [p.device for p in list_ports.comports() if p.vid == self.VID]
            if not ports:
                raise BoardGone("No board found. Connect it with the USB cable, or pass --port.")
            port = ports[0]
        self.port_name = port
        self.ser = serial.Serial(port, 115200, timeout=0.02, write_timeout=2)
        self._buffer = b""
        self._pending: List[str] = []  # events read while waiting for an acknowledgement

    def send(self, text: str) -> None:
        self.ser.write(text.encode("ascii"))

    def lines(self) -> List[str]:
        """Complete lines the board has sent since the last call (log output included)."""
        self._buffer += self.ser.read(4096)
        *complete, self._buffer = self._buffer.split(b"\n")
        return [l.decode("utf-8", "replace").strip() for l in complete]

    def events(self) -> List[str]:
        """Events from the board ("NP:..." lines), including ones set aside by wait_ack. A log line can be
        cut by an event, so known events are looked for anywhere in the line (other "NP:" text is ignored)."""
        events, self._pending = self._pending, []
        for line in self.lines():
            events.extend(m.group(1) for m in _EVENT.finditer(line))
        return events

    def wait_ack(self, offset: int, timeout: float = 0.6) -> bool:
        end = time.time() + timeout
        while time.time() < end:
            got = False
            for event in self.events():
                if event == f"ACK {offset}":
                    got = True
                else:
                    self._pending.append(event)  # a key press or HELLO during an upload must not be lost
            if got:
                return True
        return False

    def send_art(self, bits: bytes, width: int, height: int) -> bool:
        """Sends a cover, one acknowledged chunk at a time (the board's serial buffer is small)."""
        art = proto.art_lines(bits, width, height)
        # A chunk line is "CMD:NP ARTDATA <offset> <data>"; the ART line is acknowledged as offset 0.
        offsets = [0] + [int(l.split()[2]) for l in art[1:-1]]
        for line, offset in zip(art[:-1], offsets):
            for attempt in range(2):
                self.send(line)
                if self.wait_ack(offset):
                    break
            else:
                return False
        self.send(art[-1])
        return True


# ----- Main loop -----------------------------------------------------------------------------

def run(args) -> None:
    board = Board(args.port)
    args.connected = True
    try:
        _talk(args, board)
    finally:
        board.ser.close()


def _talk(args, board) -> None:
    print(f"Connected to the board on {board.port_name}. Open 'Now Playing' on it.", flush=True)
    music = None if args.demo else AppleMusic()
    full_refresh = True
    art_size = proto.ART_SIZE
    last_key = object()
    last_state = None
    last_state_time = 0.0
    last_expected = 0.0

    while True:
        for event in board.events():
            if event.split()[0] == "HELLO":
                parts = event.split()
                if len(parts) > 1 and parts[1].isdigit() and 16 <= int(parts[1]) <= 256:
                    art_size = int(parts[1]) // 8 * 8  # the bitmap format wants whole bytes per row
                full_refresh = True
            elif event in CONTROLS and music is not None:
                try:
                    music.control(event)
                    last_state = None  # report the change straight away
                except Exception as error:  # noqa: BLE001 - a failed control must not stop the loop
                    print(f"Could not {CONTROLS[event]}: {error}")

        if args.demo:
            title, artist, album = DEMO_TRACKS[int(time.time() // 12) % len(DEMO_TRACKS)]
            track = Track("playing", title, artist, album, time.time() % 12, 200.0)
        else:
            try:
                track = music.poll()
            except Exception as error:  # noqa: BLE001
                print(f"Could not read Apple Music: {error}")
                track = None

        key = track.key if track else None
        if full_refresh or key != last_key:
            full_refresh = False
            last_key = key
            last_state = None
            title, artist, album = (track.title, track.artist, track.album) if track else ("", "", "")
            board.send(proto.text_line("TITLE", title))
            board.send(proto.text_line("ARTIST", artist))
            board.send(proto.text_line("ALBUM", album))
            if track and track.state != "stopped":
                try:
                    data = demo_art(art_size) if args.demo else music.artwork()
                    if data is None and args.verbose:
                        print("This track has no artwork.")
                    if data is not None:
                        bits = make_art(data, art_size, dither=args.dither)
                        if not board.send_art(bits, art_size, art_size):
                            print("The board did not acknowledge the cover (is 'Now Playing' open?)")
                    else:
                        board.send(proto.NO_ART_LINE)
                except Exception as error:  # noqa: BLE001
                    print(f"Could not prepare the cover: {error}")
                    board.send(proto.NO_ART_LINE)
            else:
                board.send(proto.NO_ART_LINE)

        now = time.time()
        state = (track.state if track else "stopped", int(track.duration) if track else 0)
        position = track.position if track else 0.0
        # Report on a change, on a jump in position (a seek), and every 3 s so the board knows the
        # program is still there.
        expected = last_expected + (now - last_state_time) if track and track.state == "playing" else last_expected
        if state != last_state or abs(position - expected) > 2.0 or now - last_state_time >= 3.0:
            board.send(proto.state_line(state[0], position, track.duration if track else 0))
            last_state, last_state_time, last_expected = state, now, position
        time.sleep(0.4)


def main() -> None:
    parser = argparse.ArgumentParser(description="Show Apple Music on the board's Now Playing screen.")
    parser.add_argument("--port", help="serial port of the board (default: found automatically)")
    parser.add_argument("--dither", choices=["fs", "atkinson", "threshold"], default="atkinson",
                        help="how cover art is turned into black and white (default: atkinson)")
    parser.add_argument("--demo", action="store_true", help="send a test card instead of reading Apple Music")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--wait", action="store_true",
                        help="keep running: wait for the board, and reconnect when it is unplugged or restarted "
                             "(used by the background service, see service.sh)")
    args = parser.parse_args()
    last_problem = None
    try:
        while True:
            try:
                run(args)
            except (BoardGone, OSError) as error:  # a serial error is an OSError
                if not args.wait:
                    sys.exit(str(error))
                if getattr(args, "connected", False):  # it was connected, so this is a new problem
                    args.connected, last_problem = False, None
                if str(error) != last_problem:  # log a repeated problem once, not every few seconds
                    print(f"Waiting for the board: {error}", flush=True)
                    last_problem = str(error)
                time.sleep(3)
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
