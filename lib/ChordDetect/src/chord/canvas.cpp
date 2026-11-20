#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace chord {

namespace {

int trunc_to_int(double v) { return static_cast<int>(v); }  // toward zero, like Pillow's (int) casts

int round_half_up(double v) { return static_cast<int>(std::floor(v + 0.5)); }
int round_half_down(double v) { return static_cast<int>(std::ceil(v - 0.5)); }

// Pillow's ROUND_UP / ROUND_DOWN used for wide lines.
int pil_round_up(double f) {
    return f >= 0.0 ? static_cast<int>(std::floor(f + 0.5)) : -static_cast<int>(std::floor(std::fabs(f) + 0.5));
}
int pil_round_down(double f) {
    return f >= 0.0 ? static_cast<int>(std::ceil(f - 0.5)) : -static_cast<int>(std::ceil(std::fabs(f) - 0.5));
}

// Is pixel (x, y) inside the ellipse inscribed in the inclusive box x0..x1, y0..y1?
bool in_ellipse(int x, int y, int x0, int y0, int x1, int y1) {
    if (x1 < x0 || y1 < y0) return false;
    const double cx = (x0 + x1) / 2.0, cy = (y0 + y1) / 2.0;
    const double ra = (x1 - x0) / 2.0 + 0.5, rb = (y1 - y0) / 2.0 + 0.5;
    const double dx = (x - cx) / ra, dy = (y - cy) / rb;
    return dx * dx + dy * dy <= 1.0;
}

// Rectangle with circular corners of radius r, inclusive box.
bool in_rrect(int x, int y, int x0, int y0, int x1, int y1, int r) {
    if (x < x0 || x > x1 || y < y0 || y > y1) return false;
    if (r <= 0) return true;
    const int d = 2 * r;
    if (x < x0 + r) {
        if (y < y0 + r) return in_ellipse(x, y, x0, y0, x0 + d, y0 + d);
        if (y > y1 - r) return in_ellipse(x, y, x0, y1 - d, x0 + d, y1);
    }
    if (x > x1 - r) {
        if (y < y0 + r) return in_ellipse(x, y, x1 - d, y0, x1, y0 + d);
        if (y > y1 - r) return in_ellipse(x, y, x1 - d, y1 - d, x1, y1);
    }
    return true;
}

}  // namespace

void Canvas::clear(int color) { std::memset(buf_, color == kInk ? 0x00 : 0xFF, sizeof buf_); }

bool Canvas::ink_at(int x, int y) const {
    if (x < 0 || x >= kScreenW || y < 0 || y >= kScreenH) return false;
    return (buf_[y * kStride + (x >> 3)] & (0x80 >> (x & 7))) == 0;
}

void Canvas::set(int x, int y, int color) {
    if (x < 0 || x >= kScreenW || y < 0 || y >= kScreenH) return;
    uint8_t& b = buf_[y * kStride + (x >> 3)];
    const uint8_t mask = 0x80 >> (x & 7);
    if (color == kInk) {
        b &= static_cast<uint8_t>(~mask);
    } else {
        b |= mask;
    }
}

void Canvas::paint(int x, int y, Fill fill) {
    switch (fill) {
        case Fill::Ink: set(x, y, kInk); break;
        case Fill::Paper: set(x, y, kPaper); break;
        // Dithered fills flicker on the reflective RLCD at rest (see the Lyra highlight fix),
        // so the hatches are left blank here; shapes keep their outlines.
        case Fill::Hatch25:
        case Fill::Hatch50:
            break;
    }
}

void Canvas::hline(int y, int x0, int x1, int color) {
    if (x0 > x1) std::swap(x0, x1);
    for (int x = x0; x <= x1; ++x) set(x, y, color);
}

void Canvas::point(double x, double y, int color) { set(trunc_to_int(x), trunc_to_int(y), color); }

void Canvas::rect(double dx0, double dy0, double dx1, double dy1, int fill, int outline, int width) {
    int x0 = trunc_to_int(dx0), y0 = trunc_to_int(dy0), x1 = trunc_to_int(dx1), y1 = trunc_to_int(dy1);
    if (x1 < x0 || y1 < y0) return;
    if (fill != kNone) {
        for (int y = y0; y <= y1; ++y) hline(y, x0, x1, fill);
    }
    if (outline != kNone && outline != fill && width > 0) {
        for (int i = 0; i < width && x0 + i <= x1 - i && y0 + i <= y1 - i; ++i) {
            const int ax = x0 + i, ay = y0 + i, bx = x1 - i, by = y1 - i;
            hline(ay, ax, bx, outline);
            hline(by, ax, bx, outline);
            for (int y = ay; y <= by; ++y) {
                set(ax, y, outline);
                set(bx, y, outline);
            }
        }
    }
}

void Canvas::ellipse(double dx0, double dy0, double dx1, double dy1, int fill, int outline, int width) {
    const int x0 = trunc_to_int(dx0), y0 = trunc_to_int(dy0), x1 = trunc_to_int(dx1), y1 = trunc_to_int(dy1);
    if (x1 < x0 || y1 < y0) return;
    const bool ring = outline != kNone && outline != fill && width > 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!in_ellipse(x, y, x0, y0, x1, y1)) continue;
            if (fill != kNone) set(x, y, fill);
            if (ring && !in_ellipse(x, y, x0 + width, y0 + width, x1 - width, y1 - width)) set(x, y, outline);
        }
    }
}

void Canvas::rounded_rect(double dx0, double dy0, double dx1, double dy1, int radius, int fill, int outline,
                          int width) {
    const int x0 = trunc_to_int(dx0), y0 = trunc_to_int(dy0), x1 = trunc_to_int(dx1), y1 = trunc_to_int(dy1);
    if (x1 < x0 || y1 < y0) return;
    const bool ring = outline != kNone && outline != fill && width > 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!in_rrect(x, y, x0, y0, x1, y1, radius)) continue;
            if (fill != kNone) set(x, y, fill);
            if (ring && !in_rrect(x, y, x0 + width, y0 + width, x1 - width, y1 - width, std::max(radius - width, 0))) {
                set(x, y, outline);
            }
        }
    }
}

void Canvas::fill_polygon(const int* xs, const int* ys, int n, Fill fill) {
    if (n < 3) return;
    int ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < n; ++i) {
        ymin = std::min(ymin, ys[i]);
        ymax = std::max(ymax, ys[i]);
    }
    constexpr int kMaxCross = 64;
    for (int y = std::max(ymin, 0); y <= std::min(ymax, kScreenH - 1); ++y) {
        double cross[kMaxCross];
        int nc = 0;
        for (int i = 0; i < n; ++i) {
            const int xa = xs[i], ya = ys[i];
            const int xb = xs[(i + 1) % n], yb = ys[(i + 1) % n];
            if (ya == yb) {
                if (ya == y) {                       // horizontal edge on this row: draw it whole
                    for (int x = std::min(xa, xb); x <= std::max(xa, xb); ++x) paint(x, y, fill);
                }
                continue;
            }
            const int lo = std::min(ya, yb), hi = std::max(ya, yb);
            if (y >= lo && y < hi && nc < kMaxCross) {
                cross[nc++] = xa + static_cast<double>(y - ya) * (xb - xa) / (yb - ya);
            }
        }
        std::sort(cross, cross + nc);
        for (int i = 0; i + 1 < nc; i += 2) {
            const int a = round_half_up(cross[i]), b = round_half_up(cross[i + 1]);
            for (int x = a; x <= b; ++x) paint(x, y, fill);
        }
    }
}

void Canvas::polygon(const Pt* pts, int n, Fill fill) {
    constexpr int kMaxPts = 64;
    if (n > kMaxPts) n = kMaxPts;
    int xs[kMaxPts], ys[kMaxPts];
    for (int i = 0; i < n; ++i) {
        xs[i] = trunc_to_int(pts[i].x);
        ys[i] = trunc_to_int(pts[i].y);
    }
    fill_polygon(xs, ys, n, fill);
}

void Canvas::line_int(int x0, int y0, int x1, int y1, int width, int color) {
    if (width <= 1) {                                  // Bresenham
        const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
        const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;
        for (;;) {
            set(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
        return;
    }
    const int dx = x1 - x0, dy = y1 - y0;              // wide line as a polygon, like Pillow
    if (dx == 0 && dy == 0) {
        set(x0, y0, color);
        return;
    }
    const double big = std::hypot(dx, dy), small = (width - 1) / 2.0;
    const double rmax = pil_round_up(small) / big, rmin = pil_round_down(small) / big;
    const int dxmin = pil_round_down(rmin * dy), dxmax = pil_round_down(rmax * dy);
    const int dymin = pil_round_down(rmin * dx), dymax = pil_round_down(rmax * dx);
    const int xs[4] = {x0 - dxmin, x1 - dxmin, x1 + dxmax, x0 + dxmax};
    const int ys[4] = {y0 + dymax, y1 + dymax, y1 - dymin, y0 - dymin};
    fill_polygon(xs, ys, 4, color == kInk ? Fill::Ink : Fill::Paper);
}

void Canvas::line(double x0, double y0, double x1, double y1, int width, int color) {
    line_int(trunc_to_int(x0), trunc_to_int(y0), trunc_to_int(x1), trunc_to_int(y1), width, color);
}

void Canvas::polyline(const Pt* pts, int n, int width, int color) {
    for (int i = 0; i + 1 < n; ++i) line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, width, color);
}

void Canvas::text(const FontFace& face, double x, double y, const char* s, char h, char v, int color) {
    const TextExtent e = text_extent(face, s);
    const double xo = h == 'm' ? e.advance / 2.0 : h == 'r' ? e.advance : 0.0;
    double yo = 0.0;
    switch (v) {
        case 'a': yo = face.ascent; break;
        case 't': yo = -e.y0; break;                     // top of the tallest glyph
        case 'm': yo = (face.ascent - face.descent) / 2.0; break;
        default: yo = 0.0; break;                        // 's': baseline
    }
    // Pillow rounds half-way positions differently for whole and fractional anchors.
    const bool frac_x = x != std::floor(x), frac_y = y != std::floor(y);
    const double vx = x - xo, vy = y + yo;
    const int ox = frac_x ? round_half_up(vx) : round_half_down(vx);
    const int base = frac_y ? round_half_down(vy) : round_half_up(vy);

    // Pillow rounds the baseline by the tallest glyph in the string, so other glyphs sit
    // (shift of the tallest) - (own shift) rows lower than their isolated placement.
    int tallest_shift = 0, tallest_top = -1000000;
    for (const char* q = s; *q; ++q) {
        const Glyph* g = font_glyph(face, *q);
        if (g && g->w && g->h && -g->by0 > tallest_top) {
            tallest_top = -g->by0;
            tallest_shift = g->shift;
        }
    }

    int pen = 0;
    for (; *s; ++s) {
        const Glyph* g = font_glyph(face, *s);
        if (!g) g = font_glyph(face, ' ');
        if (!g) continue;
        if (g->w && g->h) {
            const int dy = tallest_shift - g->shift;
            const uint8_t* bits = fontdata::kBits + g->offset;
            const int row_bytes = (g->w + 7) / 8;
            for (int row = 0; row < g->h; ++row) {
                for (int col = 0; col < g->w; ++col) {
                    if (bits[row * row_bytes + (col >> 3)] & (0x80 >> (col & 7))) {
                        set(ox + pen + g->left + col, base - g->top + row + dy, color);
                    }
                }
            }
        }
        pen += g->adv;
    }
}

}  // namespace chord
