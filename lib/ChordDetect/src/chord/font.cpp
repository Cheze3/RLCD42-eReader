#include "font.h"

#include <algorithm>

namespace chord {

const FontFace* font_find(FontRole role, int size) {
    for (int i = 0; i < fontdata::kFaceCount; ++i) {
        const FontFace& f = fontdata::kFaces[i];
        if (f.role == role && f.size == size) return &f;
    }
    return nullptr;
}

int font_sizes(FontRole role, int* out, int cap) {
    int n = 0;
    for (int i = 0; i < fontdata::kFaceCount && n < cap; ++i) {
        if (fontdata::kFaces[i].role == role) out[n++] = fontdata::kFaces[i].size;
    }
    std::sort(out, out + n, [](int a, int b) { return a > b; });
    return n;
}

const Glyph* font_glyph(const FontFace& face, char c) {
    for (int i = 0; i < face.count; ++i) {
        if (face.chars[i] == c) return &face.glyphs[i];
    }
    return nullptr;
}

TextExtent text_extent(const FontFace& face, const char* s) {
    int pen = 0, x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    bool any = false;
    for (; *s; ++s) {
        const Glyph* g = font_glyph(face, *s);
        if (!g) g = font_glyph(face, ' ');   // unknown characters count as a space
        if (!g) continue;
        if (g->bx1 > g->bx0 && g->by1 > g->by0) {
            x0 = std::min(x0, pen + g->bx0);
            x1 = std::max(x1, pen + g->bx1);
            y0 = any ? std::min<int>(y0, g->by0) : g->by0;
            y1 = any ? std::max<int>(y1, g->by1) : g->by1;
            any = true;
        }
        pen += g->adv;
    }
    x1 = std::max(x1, pen);
    return {x0, y0, x1, y1, pen};
}

}  // namespace chord
