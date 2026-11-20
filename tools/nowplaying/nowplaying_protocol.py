"""Encoding for the board's "now playing" serial protocol.

The board reads lines that start with `CMD:`; the verbs are documented in
lib/NowPlaying/src/nowplaying/Protocol.h, which is the other end of this file.
"""

import base64

MAX_TEXT_BYTES = 80      # board keeps at most 95 bytes; leave room, and keep each line short
CHUNK_BYTES = 144        # raw bytes per ARTDATA line (192 base64 characters, ~215 in the line)
ART_SIZE = 200           # default cover size; the board announces the size it wants in NP:HELLO


def _b64(text: str, limit: int = MAX_TEXT_BYTES) -> str:
    data = text.replace("\n", " ").replace("\r", " ").encode("utf-8")
    if len(data) > limit:
        # Cut at the byte limit, then drop only a character that was cut in half.
        data = data[:limit].decode("utf-8", "ignore").encode("utf-8")
    return base64.b64encode(data).decode("ascii")


def text_line(verb: str, text: str) -> str:
    """verb is TITLE, ARTIST or ALBUM."""
    return f"CMD:NP {verb} {_b64(text)}\n"


def state_line(state: str, position_sec: float, duration_sec: float) -> str:
    """state is playing, paused or stopped."""
    return f"CMD:NP STATE {state} {max(0, int(position_sec))} {max(0, int(duration_sec))}\n"


def art_lines(bits: bytes, width: int, height: int) -> list:
    """bits: 1-bit rows of ceil(width/8) bytes, most significant bit first, 1 = black."""
    row_bytes = (width + 7) // 8
    if len(bits) != row_bytes * height:
        raise ValueError("bitmap size does not match its dimensions")
    lines = [f"CMD:NP ART {width} {height} {len(bits)}\n"]
    for offset in range(0, len(bits), CHUNK_BYTES):
        chunk = base64.b64encode(bits[offset:offset + CHUNK_BYTES]).decode("ascii")
        lines.append(f"CMD:NP ARTDATA {offset} {chunk}\n")
    lines.append("CMD:NP ARTEND\n")
    return lines


NO_ART_LINE = "CMD:NP NOART\n"
PING_LINE = "CMD:NP PING\n"
