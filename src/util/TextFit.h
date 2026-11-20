#pragma once

#include <GfxRenderer.h>

#include <cstddef>

namespace textfit {

// Copies `text` into `out` (capacity `cap`), cutting it with "..." on a UTF-8 character boundary
// until it is no wider than `maxWidth`. Returns the width of the result.
int fit(const GfxRenderer& renderer, int fontId, EpdFontFamily::Style style, const char* text, int maxWidth, char* out,
        size_t cap);

}  // namespace textfit
