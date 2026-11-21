"""Run with: python3 -m unittest test_nowplaying (needs Pillow; run inside the tool's virtualenv)."""

import base64
import io
import unittest

from PIL import Image

import nowplaying as np
import nowplaying_protocol as proto


class AppleScriptOutput(unittest.TestCase):
    def test_parses_a_paused_track_with_comma_decimals(self):
        out = "paused\x1eForgive Me\x1eimzoo\x1eForgive Me - Single\x1e19,805000305176\x1e197,0"
        t = np.parse_state(out)
        self.assertEqual((t.state, t.title, t.artist, t.album), ("paused", "Forgive Me", "imzoo", "Forgive Me - Single"))
        self.assertAlmostEqual(t.position, 19.805, places=3)
        self.assertEqual(t.duration, 197.0)

    def test_parses_period_decimals_and_playing(self):
        t = np.parse_state("playing\x1eA\x1eB\x1eC\x1e1.5\x1e200.25")
        self.assertEqual(t.state, "playing")
        self.assertEqual((t.position, t.duration), (1.5, 200.25))

    def test_closed_empty_and_stopped(self):
        self.assertIsNone(np.parse_state("off"))
        self.assertIsNone(np.parse_state(""))
        self.assertEqual(np.parse_state("stopped").state, "stopped")

    def test_state_without_track_details(self):
        self.assertEqual(np.parse_state("playing").state, "playing")

    def test_other_states_count_as_playing(self):
        self.assertEqual(np.parse_state("fast forwarding\x1eA\x1eB\x1eC\x1e1\x1e2").state, "playing")

    def test_track_key_ignores_position(self):
        a = np.parse_state("playing\x1eA\x1eB\x1eC\x1e1\x1e200")
        b = np.parse_state("playing\x1eA\x1eB\x1eC\x1e99\x1e200")
        self.assertEqual(a.key, b.key)


class Encoding(unittest.TestCase):
    def test_text_lines_are_base64_and_short(self):
        line = proto.text_line("TITLE", "Café")
        self.assertTrue(line.startswith("CMD:NP TITLE ") and line.endswith("\n"))
        self.assertEqual(base64.b64decode(line.split()[2]).decode(), "Café")

    def test_long_text_is_cut_on_a_character_boundary(self):
        line = proto.text_line("ALBUM", "テ" * 60)  # 180 bytes of katakana
        raw = base64.b64decode(line.split()[2])
        self.assertLessEqual(len(raw), proto.MAX_TEXT_BYTES)
        raw.decode("utf-8")  # must not raise

    def test_a_character_that_exactly_fits_is_kept_and_a_split_one_is_dropped(self):
        fits = proto.text_line("TITLE", "a" * 78 + "\u00e9")     # 78 + 2 bytes = exactly 80
        self.assertEqual(base64.b64decode(fits.split()[2]).decode(), "a" * 78 + "\u00e9")
        split = proto.text_line("TITLE", "a" * 79 + "\u00e9")    # the second byte of the accent is cut off
        self.assertEqual(base64.b64decode(split.split()[2]).decode(), "a" * 79)

    def test_empty_text_clears_the_field(self):
        self.assertEqual(proto.text_line("ARTIST", "").strip(), "CMD:NP ARTIST")

    def test_newlines_are_removed(self):
        self.assertEqual(base64.b64decode(proto.text_line("TITLE", "a\nb").split()[2]), b"a b")

    def test_state_line_never_negative(self):
        self.assertEqual(proto.state_line("paused", -3, 10.9), "CMD:NP STATE paused 0 10\n")

    def test_art_lines_reassemble_to_the_bitmap(self):
        bits = bytes((i * 7) % 256 for i in range(5000))
        lines = proto.art_lines(bits, 200, 200)
        self.assertEqual(lines[0], "CMD:NP ART 200 200 5000\n")
        self.assertEqual(lines[-1], "CMD:NP ARTEND\n")
        rebuilt = bytearray(5000)
        for line in lines[1:-1]:
            _, _, offset, payload = line.split()
            chunk = base64.b64decode(payload)
            self.assertLessEqual(len(line), 230)  # well inside the board's 256-byte serial buffer
            rebuilt[int(offset):int(offset) + len(chunk)] = chunk
        self.assertEqual(bytes(rebuilt), bits)

    def test_art_lines_reject_a_wrong_size(self):
        with self.assertRaises(ValueError):
            proto.art_lines(b"\x00" * 10, 16, 8)


class FakeBoard(np.Board):
    """A Board with no serial port: records what is sent and answers the acknowledgements."""

    def __init__(self, acknowledge=True):
        self._pending = []
        self.sent = []
        self.acknowledge = acknowledge
        self.acks_requested = []

    def send(self, text):
        self.sent.append(text)

    def wait_ack(self, offset, timeout=0.6):
        self.acks_requested.append(offset)
        return self.acknowledge


class ScriptedBoard(np.Board):
    """A Board whose serial lines come from a script: each call to lines() returns the next batch."""

    def __init__(self, batches):
        self._pending = []
        self.batches = list(batches)

    def lines(self):
        return self.batches.pop(0) if self.batches else []


class ReadingEvents(unittest.TestCase):
    def test_events_are_found_even_inside_a_log_line(self):
        board = ScriptedBoard([["[12] [DBG] something NP:PLAYPAUSE", "plain log line", "NP:NEXT"]])
        self.assertEqual(board.events(), ["PLAYPAUSE", "NEXT"])

    def test_unknown_np_text_in_a_log_line_is_ignored(self):
        board = ScriptedBoard([["[1] NP:NEXTVALUE bad", "log NP:FOO", "NP:HELLO 200"]])
        self.assertEqual(board.events(), ["HELLO 200"])

    def test_a_key_press_during_an_upload_is_kept(self):
        board = ScriptedBoard([["NP:NEXT", "NP:ACK 144"]])
        self.assertTrue(board.wait_ack(144))
        self.assertEqual(board.events(), ["NEXT"])  # not discarded while waiting for the acknowledgement

    def test_wait_ack_times_out_without_it(self):
        board = ScriptedBoard([["NP:ACK 0"]])
        self.assertFalse(board.wait_ack(144, timeout=0.05))


class SendingArt(unittest.TestCase):
    def test_sends_begin_every_chunk_in_order_then_end(self):
        board = FakeBoard()
        bits = bytes(range(256)) * 20  # 5120 bytes
        self.assertTrue(board.send_art(bits, 64, 640))
        self.assertTrue(board.sent[0].startswith("CMD:NP ART 64 640 5120"))
        self.assertEqual(board.sent[-1], "CMD:NP ARTEND\n")
        chunk_lines = board.sent[1:-1]
        offsets = [int(l.split()[2]) for l in chunk_lines]
        self.assertEqual(offsets, list(range(0, 5120, proto.CHUNK_BYTES)))
        # one acknowledgement wanted per line before the end marker: the ART line (0), then each chunk
        self.assertEqual(board.acks_requested, [0] + offsets)

    def test_gives_up_when_the_board_never_acknowledges(self):
        board = FakeBoard(acknowledge=False)
        self.assertFalse(board.send_art(b"\x00" * 200, 40, 40))
        self.assertNotIn("CMD:NP ARTEND\n", board.sent)
        self.assertEqual(len(board.sent), 2)  # the first line, tried twice

    def test_a_full_cover_is_sent_without_error(self):
        board = FakeBoard()
        bits = np.make_art(np.demo_art(200), 200)
        self.assertTrue(board.send_art(bits, 200, 200))
        self.assertTrue(all(len(l) <= 230 for l in board.sent))


class CoverArt(unittest.TestCase):
    def _png(self, color, size=(300, 200)):
        buffer = io.BytesIO()
        Image.new("RGB", size, color).save(buffer, "PNG")
        return buffer.getvalue()

    def test_output_has_the_board_size(self):
        for dither in ("fs", "atkinson", "threshold"):
            self.assertEqual(len(np.make_art(self._png((120, 40, 200)), dither=dither)), 5000, dither)

    def test_black_is_one_and_white_is_zero(self):
        black = np.pack_bits(Image.new("1", (16, 2), 0))
        white = np.pack_bits(Image.new("1", (16, 2), 1))
        self.assertEqual(black, b"\xff" * 4)
        self.assertEqual(white, b"\x00" * 4)

    def test_a_dark_cover_is_mostly_black_and_a_light_one_mostly_white(self):
        def black_fraction(color):
            data = np.make_art(self._png(color, (100, 100)) , dither="threshold")
            return sum(bin(b).count("1") for b in data) / (len(data) * 8)
        # A flat image has no contrast to stretch, so these are the extremes the screen will show.
        self.assertGreater(black_fraction((10, 10, 10)), 0.9)
        self.assertLess(black_fraction((250, 250, 250)), 0.1)

    def test_transparent_png_is_composited_on_white(self):
        buffer = io.BytesIO()
        Image.new("RGBA", (50, 50), (0, 0, 0, 0)).save(buffer, "PNG")
        data = np.make_art(buffer.getvalue(), dither="threshold")
        self.assertEqual(sum(bin(b).count("1") for b in data), 0)  # all paper

    def test_jpeg_input_works(self):
        buffer = io.BytesIO()
        Image.new("RGB", (64, 64), (200, 30, 30)).save(buffer, "JPEG")
        self.assertEqual(len(np.make_art(buffer.getvalue())), 5000)

    def test_demo_art_is_a_real_image(self):
        self.assertEqual(len(np.make_art(np.demo_art())), 5000)




class WindowsHelpers(unittest.TestCase):
    def test_playback_status_names(self):
        import nowplaying_windows as w
        self.assertEqual([w.state_name(n) for n in (0, 1, 2, 3, 4, 5)],
                         ["stopped", "stopped", "paused", "stopped", "playing", "paused"])

    def test_position_advances_only_while_playing(self):
        import nowplaying_windows as w
        self.assertEqual(w.live_position(10, 100, 105, True, 200), 15)
        self.assertEqual(w.live_position(10, 100, 105, False, 200), 10)

    def test_position_is_kept_inside_the_track(self):
        import nowplaying_windows as w
        self.assertEqual(w.live_position(198, 100, 110, True, 200), 200)
        self.assertEqual(w.live_position(5, 100, 90, True, 0), 5)  # a clock step back never subtracts


class WifiLink(unittest.TestCase):
    def test_constants_match_the_firmware_header(self):
        import os, re
        header = os.path.join(os.path.dirname(__file__), "..", "..", "lib", "NowPlaying", "src", "nowplaying", "Protocol.h")
        text = open(header).read()
        self.assertEqual(int(re.search(r"kTcpPort = (\d+)", text).group(1)), proto.TCP_PORT)
        self.assertEqual(re.search(r'kHostName = "([^"]+)"', text).group(1) + ".local", proto.HOST_NAME)

    def test_a_board_talks_over_tcp(self):
        import socket as sk, threading
        server = sk.socket()
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        port = server.getsockname()[1]
        received = []

        def board_side():
            conn, _ = server.accept()
            conn.sendall(b"NP:HELLO 200\nNP:NEXT\n")
            received.append(conn.recv(100))
            conn.close()

        thread = threading.Thread(target=board_side)
        thread.start()
        board = np.Board(None, f"127.0.0.1:{port}")
        events = []
        for _ in range(50):
            events += board.events()
            if len(events) >= 2:
                break
        self.assertEqual(events, ["HELLO 200", "NEXT"])
        board.send("CMD:NP PING\n")
        thread.join()
        self.assertEqual(received, [b"CMD:NP PING\n"])
        with self.assertRaises(OSError):  # the board hung up
            for _ in range(50):
                board.events()
        board.ser.close()
        server.close()


class ChoosingTheLink(unittest.TestCase):
    def setUp(self):
        self.calls = []
        self.original = np.Board
        outer = self

        class Recorder:
            def __init__(self, port, host=None):
                outer.calls.append((port, host))
                if host and outer.wifi_down:
                    raise ConnectionRefusedError("not listening")

        np.Board = Recorder

    def tearDown(self):
        np.Board = self.original

    def args(self, **kw):
        import argparse
        return argparse.Namespace(**{"port": None, "host": None, "wifi": False, **kw})

    def test_usb_by_default(self):
        self.wifi_down = False
        np.open_board(self.args())
        self.assertEqual(self.calls, [(None, None)])

    def test_wifi_is_tried_first_with_the_flag(self):
        self.wifi_down = False
        np.open_board(self.args(wifi=True))
        self.assertEqual(self.calls, [(None, proto.HOST_NAME)])

    def test_usb_is_the_fallback_when_wifi_is_not_listening(self):
        self.wifi_down = True
        np.open_board(self.args(wifi=True))
        self.assertEqual(self.calls, [(None, proto.HOST_NAME), (None, None)])


class Latency(unittest.TestCase):
    def test_every_ping_is_timed_and_a_lost_one_is_none(self):
        class Echo(np.Board):
            def __init__(self):
                self._pending, self.pings = [], 0

            def send(self, text):
                self.pings += 1

            def events(self):
                return ["PONG"] if self.pings != 2 else []  # the second ping gets no answer

        original = np.time.sleep
        np.time.sleep = lambda _s: None
        try:
            results = np.measure_latency(Echo(), count=3, timeout=0.05)
        finally:
            np.time.sleep = original
        self.assertIsNotNone(results[0])
        self.assertIsNone(results[1])
        self.assertIsNotNone(results[2])


if __name__ == "__main__":
    unittest.main()
