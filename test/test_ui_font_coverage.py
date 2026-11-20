"""Checks the built-in UI font headers for Vietnamese and Korean coverage.

Run with python3 test/test_ui_font_coverage.py. Korean is only in the regular-weight UI fonts (8, 10 and
12 pt); the bold ones have none, so text that may be Korean must be drawn in a regular weight.
"""

import re
import unicodedata
from pathlib import Path

FONTS = Path(__file__).resolve().parents[1] / "lib/EpdFont/builtinFonts"
REGULAR = ["ubuntu_8_regular", "ubuntu_10_regular", "ubuntu_12_regular"]
ALL_UI = REGULAR + ["ubuntu_10_bold", "ubuntu_12_bold"]


def covered(name):
    text = (FONTS / f"{name}.h").read_text()
    block = re.search(r"EpdUnicodeInterval\s+\w+Intervals\[\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    ranges = [(int(a, 16), int(b, 16)) for a, b, _ in re.findall(r"\{\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)\s*\}", block)]
    return lambda ch: any(a <= ord(ch) <= b for a, b in ranges)


def vietnamese_letters():
    tones = ["", "̀", "́", "̃", "̉", "̣"]
    bases = ["a", "ă", "â", "e", "ê", "i", "o", "ô", "ơ", "u", "ư", "y"]
    letters = {unicodedata.normalize("NFC", b.upper() + t) for b in bases for t in tones}
    letters |= {unicodedata.normalize("NFC", b + t) for b in bases for t in tones}
    return {c for c in letters | {"đ", "Đ"} if len(c) == 1}


# Words and names that appear in Korean song titles, plus compatibility jamo and CJK punctuation.
KOREAN_SAMPLES = ["사랑해", "한국어", "노래", "가수", "안녕하세요", "봄날", "소녀시대", "방탄소년단", "ㅋㅋㅋ", "，。「」"]
VIETNAMESE_SAMPLES = ["Việt Nam", "Nguyễn Văn Ơn", "Đừng quên anh", "Trường Sa hát mãi", "Mưa rơi ngoài hiên"]


def check_all():
    failures = []
    for name in ALL_UI:
        has = covered(name)
        for text in VIETNAMESE_SAMPLES:
            missing = [c for c in text if c != " " and not has(c)]
            if missing:
                failures.append(f"{name}: Vietnamese '{text}' missing {missing}")
        for letter in vietnamese_letters():
            if not has(letter):
                failures.append(f"{name}: Vietnamese letter {letter} missing")
    for name in REGULAR:
        has = covered(name)
        for text in KOREAN_SAMPLES:
            missing = [c for c in text if not has(c)]
            if missing:
                failures.append(f"{name}: Korean '{text}' missing {missing}")
    for name in ["ubuntu_10_bold", "ubuntu_12_bold"]:
        if covered(name)("한"):
            failures.append(f"{name} now has Korean: update the notes in convert-builtin-fonts.sh and this test")
    return failures


if __name__ == "__main__":
    problems = check_all()
    for p in problems:
        print("FAIL:", p)
    assert not problems
    print("ok: Vietnamese in all UI fonts, Korean in the regular UI fonts")
