#!/usr/bin/env python3
"""Prints the --additional-intervals arguments for the Korean characters in the built-in UI fonts.

The set is the 2,350 Hangul syllables of the KS X 1001 standard (what Korean text uses in practice;
all 11,172 possible syllables would be about five times the flash), the Hangul compatibility jamo
(for "ㅋㅋ" style text), CJK punctuation and the fullwidth forms. Used by convert-builtin-fonts.sh.
"""


def codepoints():
    chars = set()
    for lead in range(0xB0, 0xC9):  # the Hangul rows of KS X 1001 (EUC-KR)
        for trail in range(0xA1, 0xFF):
            try:
                ch = bytes([lead, trail]).decode("euc-kr")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1 and 0xAC00 <= ord(ch) <= 0xD7A3:
                chars.add(ord(ch))
    chars.update(range(0x3131, 0x318F))  # compatibility jamo
    chars.update(range(0x3000, 0x3040))  # CJK symbols and punctuation
    chars.update(range(0xFF01, 0xFF5F))  # fullwidth ASCII variants
    return sorted(chars)


def intervals(points):
    ranges = []
    for cp in points:
        if ranges and cp == ranges[-1][1] + 1:
            ranges[-1][1] = cp
        else:
            ranges.append([cp, cp])
    return ranges


if __name__ == "__main__":
    for lo, hi in intervals(codepoints()):
        print(f"--additional-intervals 0x{lo:04X},0x{hi:04X}")
