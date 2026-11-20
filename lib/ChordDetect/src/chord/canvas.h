// 1-bit framebuffer with the drawing primitives the UIs need, sized for the 400x300 RLCD.
// Platform-free. Pixel storage matches Pillow's mode "1": rows packed MSB first,
// bit 1 = paper (white), bit 0 = ink (black), so the buffer can be diffed with Python output.
//
// Coordinates are doubles and are truncated to integers like Pillow does, so geometry
// computed in floating point lands on the same pixels as the Python version.
#pragma once

#include <cstdint>

#include "font.h"

namespace chord {

constexpr int kScreenW = 400;
constexpr int kScreenH = 300;

enum Color : int { kNone = -1, kInk = 0, kPaper = 1 };
enum class Fill : int { Ink, Paper, Hatch25, Hatch50 };  // hatches are ordered-dither grays

struct Pt {
    double x, y;
};

class Canvas {
public:
    static constexpr int kStride = kScreenW / 8;

    Canvas() { clear(); }

    void clear(int color = kPaper);
    bool ink_at(int x, int y) const;
    void set(int x, int y, int color);  // ignores points outside the canvas
    const uint8_t* data() const { return buf_; }

    void point(double x, double y, int color = kInk);
    // Pillow semantics: inclusive corners; the outline is skipped when it equals the fill.
    void rect(double x0, double y0, double x1, double y1, int fill, int outline, int width = 1);
    void ellipse(double x0, double y0, double x1, double y1, int fill, int outline, int width = 1);
    void rounded_rect(double x0, double y0, double x1, double y1, int radius, int fill, int outline,
                      int width = 1);
    void line(double x0, double y0, double x1, double y1, int width = 1, int color = kInk);
    void polyline(const Pt* pts, int n, int width = 1, int color = kInk);  // consecutive segments
    void polygon(const Pt* pts, int n, Fill fill);                         // filled, no outline

    // Text. h: 'l' 'm' 'r'. v: 'a' (ascender line), 't' (top of the ink), 'm' (middle), 's' (baseline).
    // The same anchors Pillow's ImageDraw.text uses.
    void text(const FontFace& face, double x, double y, const char* s, char h, char v,
              int color = kInk);

private:
    void hline(int y, int x0, int x1, int color);
    void line_int(int x0, int y0, int x1, int y1, int width, int color);
    void fill_polygon(const int* xs, const int* ys, int n, Fill fill);
    void paint(int x, int y, Fill fill);

    uint8_t buf_[kStride * kScreenH];
};

}  // namespace chord
