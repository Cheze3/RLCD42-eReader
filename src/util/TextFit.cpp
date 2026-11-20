#include "TextFit.h"

#include <cstdio>
#include <cstring>

namespace textfit {

namespace {
bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
}  // namespace

int fit(const GfxRenderer& renderer, int fontId, EpdFontFamily::Style style, const char* text, int maxWidth, char* out,
        size_t cap) {
  snprintf(out, cap, "%s", text);
  int width = renderer.getTextWidth(fontId, out, style);
  size_t len = strlen(out);
  while (width > maxWidth && len > 0) {
    do {
      --len;
    } while (len > 0 && isContinuation(out[len]));
    snprintf(out, cap, "%.*s...", static_cast<int>(len), text);
    width = renderer.getTextWidth(fontId, out, style);
  }
  return width;
}

}  // namespace textfit
