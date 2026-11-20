#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSANS_FONT_SIZES=(10 12 14 16 18)

# ONLY_UI_FONTS=1 skips the reader (Noto Sans) fonts and regenerates just the UI fonts. It also skips
# verify_compression.py, which only checks the compressed reader fonts; the UI fonts are not compressed.
if [ -z "$ONLY_UI_FONTS" ]; then
for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum --zopfli > $output_path
    echo "Generated $output_path"
  done
done

fi

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

# Korean for the UI fonts: Noto Sans KR (SIL OFL), appended last in each font stack so every Latin,
# Hebrew, Arabic and Vietnamese glyph still comes from the same font as before. Only the Regular
# weights get Korean (8, 10 and 12 pt): the Hangul bitmaps are about 190 KB per font, and a bold font
# does not fall back to the regular one, so Korean text must be drawn in a regular weight. Set
# KOREAN_BOLD=1 to add it to the bold UI fonts as well (about 410 KB more flash). The source files are
# large and git-ignored; they are downloaded on first use. The character set (KS X 1001 syllables,
# jamo, punctuation) comes from korean_intervals.py.
KOREAN_DIR="../builtinFonts/source/NotoSansKR"
# Pinned to one noto-cjk commit and checked against a SHA-256, so the generated headers are reproducible.
KOREAN_COMMIT="f8d157532fbfaeda587e826d4cd5b21a49186f7c"
KOREAN_URL="https://raw.githubusercontent.com/notofonts/noto-cjk/$KOREAN_COMMIT/Sans/SubsetOTF/KR"
KOREAN_SHA256_Regular="69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68"
KOREAN_SHA256_Bold="5a6ceb287ed2fc6cfc6213144ebea68cbd94b20fc9eb873d8486493bf02d9bda"
mkdir -p "$KOREAN_DIR"
sha256_of() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
KOREAN_STYLES=(Regular)
if [ -n "$KOREAN_BOLD" ]; then KOREAN_STYLES=(Regular Bold); fi
for style in "${KOREAN_STYLES[@]}"; do
  file="$KOREAN_DIR/NotoSansKR-${style}.otf"
  expected_var="KOREAN_SHA256_${style}"
  if [ ! -f "$file" ]; then
    curl -fsSL -o "$file" "$KOREAN_URL/NotoSansKR-${style}.otf"
  fi
  if [ "$(sha256_of "$file")" != "${!expected_var}" ]; then
    echo "Error: $file does not match the pinned Noto Sans KR ${style} (expected ${!expected_var})." >&2
    exit 1
  fi
done
KOREAN_INTERVALS=($(python3 korean_intervals.py))
# Extra fontconvert flags for the UI fonts, e.g. UI_FONT_FLAGS=--compress.
UI_FONT_FLAGS=${UI_FONT_FLAGS:-}

# Arabic glyphs for UI text (menus, file browser titles). The built-in fonts
# must cover the *output* of MiniBidi's do_shape() — contextual presentation
# forms — not base letters, or shaped UI text silently drops glyphs.
# Curated for firmware-size budget: core Arabic (Presentation Forms-B,
# incl. the Lam-Alef ligature forms) plus the Farsi/Urdu extra letters'
# Presentation Forms-A blocks, the few characters shaping leaves at their
# base codepoint, Arabic punctuation, and both digit sets. No harakat and
# no Sindhi/Pashto/Kurdish forms — book text gets those from SD-card fonts.
ARABIC_INTERVALS=(
  --additional-intervals 0x060C,0x060C  # Arabic comma
  --additional-intervals 0x061B,0x061B  # Arabic semicolon
  --additional-intervals 0x061F,0x061F  # Arabic question mark
  --additional-intervals 0x0621,0x0621  # hamza (non-joining, never shaped)
  --additional-intervals 0x0640,0x0640  # tatweel
  --additional-intervals 0x0654,0x0654  # Persian/Urdu ezafe hamza, Arabic hamza carriers
  --additional-intervals 0x0660,0x0669  # Arabic-Indic digits
  --additional-intervals 0x06BA,0x06BA  # noon ghunna base (initial/medial keep base cp)
  --additional-intervals 0x06D4,0x06D4  # Urdu full stop
  --additional-intervals 0x06D5,0x06D5  # ae (isolated; Kurdish/Uyghur/Ottoman) — has no presentation form
  --additional-intervals 0x06F0,0x06F9  # extended Arabic-Indic digits (Farsi/Urdu)
  --additional-intervals 0xFB56,0xFB59  # peh (Farsi)
  --additional-intervals 0xFB66,0xFB69  # tteh (Urdu)
  --additional-intervals 0xFB7A,0xFB7D  # tcheh (Farsi)
  --additional-intervals 0xFB88,0xFB95  # ddal, jeh, rreh (Urdu), keheh, gaf (Farsi/Urdu)
  --additional-intervals 0xFB9E,0xFB9F  # noon ghunna isolated/final (Urdu)
  --additional-intervals 0xFBA6,0xFBB1  # heh goal, heh doachashmee, yeh barree(+hamza) (Urdu)
  --additional-intervals 0xFBFC,0xFBFF  # farsi yeh (Farsi/Urdu)
  --additional-intervals 0xFE80,0xFEFC  # Presentation Forms-B: core Arabic + Lam-Alef
)

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    latin_fallback_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    hebrew_path="../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-${style}.ttf"
    arabic_path="../builtinFonts/source/NotoSansArabic/NotoSansArabic-${style}.ttf"
    # Ubuntu lacks the Latin Extended Additional block (U+1EA0-U+1EF9) used for
    # Vietnamese tone marks. Append a Vietnamese-only Ubuntu cut so those glyphs
    # are filled from it while every glyph Ubuntu already has stays unchanged
    # (fontstack is ordered by descending priority).
    viet_path="../builtinFonts/source/Ubuntu/Ubuntu-Vietnamese-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    korean_path=()
    korean_intervals=()
    if [ "$style" = "Regular" ] || [ -n "$KOREAN_BOLD" ]; then
      korean_path=("$KOREAN_DIR/NotoSansKR-${style}.otf")
      korean_intervals=("${KOREAN_INTERVALS[@]}")
    fi
    python fontconvert.py $font_name $size $font_path $latin_fallback_path $hebrew_path $arabic_path $viet_path "${korean_path[@]}" $UI_FONT_FLAGS \
      --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" "${korean_intervals[@]}" > $output_path
    echo "Generated $output_path"
  done
done

# The small UI face is Ubuntu too, so UI text stays in one family at every size.
# At this size the stem of "1" sits across a pixel boundary and comes out 3 px
# wide next to the 2 px strokes of the other digits; a quarter-pixel nudge
# lands it on two pixels.
python fontconvert.py ubuntu_8_regular 8 $UI_FONT_FLAGS \
  ../builtinFonts/source/Ubuntu/Ubuntu-Regular.ttf \
  ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf \
  ../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Regular.ttf \
  ../builtinFonts/source/NotoSansArabic/NotoSansArabic-Regular.ttf \
  ../builtinFonts/source/Ubuntu/Ubuntu-Vietnamese-Regular.ttf \
  "$KOREAN_DIR/NotoSansKR-Regular.otf" \
  --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" "${KOREAN_INTERVALS[@]}" \
  --glyph-x-nudge 0x31,16 > ../builtinFonts/ubuntu_8_regular.h

if [ -z "$ONLY_UI_FONTS" ]; then
echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
fi
