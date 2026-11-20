#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace chord {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kNoteNamesCount = 12;
const char* const kNames[kNoteNamesCount] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const bool kWhitePc[12] = {true, false, true, false, true, true, false, true, false, true, false, true};

// ---------- fonts and text helpers ----------

// Face for a role at a size; falls back to the nearest smaller size if the exact one is missing.
const FontFace& face(FontRole role, int size) {
    if (const FontFace* f = font_find(role, size)) return *f;
    int sizes[64];
    const int n = font_sizes(role, sizes, 64);
    const FontFace* best = font_find(role, sizes[n - 1]);
    for (int i = 0; i < n; ++i) {
        if (sizes[i] <= size) {
            best = font_find(role, sizes[i]);
            break;
        }
    }
    return *best;
}

// Largest even size (ui font) at which s is at most max_w wide; min_size if none fits.
int fit_size(const char* s, int max_w, int max_size, int min_size = 10) {
    for (int size = max_size; size >= min_size; size -= 2) {
        const TextExtent e = text_extent(face(FontRole::Ui, size), s);
        if (e.x1 - e.x0 <= max_w) return size;
    }
    return min_size;
}

// Largest ladder size of a role at which s fits both limits.
int fit(FontRole role, const char* s, int max_w, int max_h, int max_size, int min_size = 12) {
    int sizes[64];
    const int n = font_sizes(role, sizes, 64);
    for (int i = 0; i < n; ++i) {
        if (sizes[i] > max_size || sizes[i] < min_size) continue;
        const TextExtent e = text_extent(face(role, sizes[i]), s);
        if (e.x1 - e.x0 <= max_w && e.y1 - e.y0 <= max_h) return sizes[i];
    }
    return min_size;
}

void ui_text(Canvas& c, double x, double y, const char* s, int size, char h, char v) {
    c.text(face(FontRole::Ui, size), x, y, s, h, v);
}

// Glyph box starts at (x, y) for 'l', ends at x for 'r', is centered on (x, y) for 'm'.
// (Same placement as text_box in visualizer.py.)
void text_box(Canvas& c, const FontFace& f, double x, double y, const char* s, char anchor, int color = kInk) {
    const TextExtent e = text_extent(f, s);
    const double w = e.x1 - e.x0, h = e.y1 - e.y0;
    if (anchor == 'r') {
        x -= w;
    } else if (anchor == 'm') {
        x -= w / 2;
        y -= h / 2;
    }
    c.text(f, x - e.x0, y - (f.ascent + e.y0), s, 'l', 'a', color);
}

// Chord name in the big font; the silent state shows LISTENING in the mono font.
void draw_label(Canvas& c, double x, double y, const char* label, int max_w, int max_h, int max_size, char anchor) {
    const bool silent = std::strcmp(label, "-") == 0 || label[0] == '\0';
    const FontRole role = silent ? FontRole::Mono : FontRole::Big;
    const char* s = silent ? "LISTENING" : label;
    if (silent) max_size = std::min(max_size, 24);
    text_box(c, face(role, fit(role, s, max_w, max_h, max_size)), x, y, s, anchor);
}

// ---------- shared data helpers ----------

int root_pc_of(const char* label) {
    if (!label || label[0] == '\0' || std::strcmp(label, "-") == 0) return -1;
    const int len = label[1] == '#' ? 2 : 1;
    for (int pc = 0; pc < kNoteNamesCount; ++pc) {
        if (static_cast<int>(std::strlen(kNames[pc])) == len && std::strncmp(label, kNames[pc], len) == 0) return pc;
    }
    return -1;
}

bool is_chord(const char* label, const char* text) {
    return std::strcmp(label, "-") != 0 && label[0] != '\0' && std::strcmp(label, text) != 0;
}

double meter_frac(double rms) {
    const double db = 20.0 * std::log10(std::max(rms, 1e-9));
    return std::min(std::max((db + 60.0) / 60.0, 0.0), 1.0);
}

void fold(const float* levels, float* out12) {
    for (int i = 0; i < 12; ++i) out12[i] = 0.0f;
    for (int i = 0; i < kNumKeys; ++i) out12[i % 12] = std::max(out12[i % 12], levels[i]);
}

// Chord-name text for a note, e.g. "C4".
void note_name(int midi, char* out, size_t n) { midi_name(midi, out, n); }

// ---------- wheel UI ----------

int fifths_pos(int pc, bool fifths) { return fifths ? (pc * 7) % 12 : pc; }

Pt wheel_point(double cx, double cy, int pc, double r, bool fifths) {
    const double ang = (-90.0 + 30.0 * fifths_pos(pc, fifths)) * kPi / 180.0;
    return {cx + r * std::cos(ang), cy + r * std::sin(ang)};
}

void draw_wheel(Canvas& c, double cx, double cy, double radius, const bool* active, int root_pc, bool fifths) {
    c.ellipse(cx - radius, cy - radius, cx + radius, cy + radius, kNone, kInk, 1);

    int act[12], n_act = 0;                               // active pitch classes in wheel order
    for (int pos = 0; pos < 12; ++pos) {
        for (int pc = 0; pc < 12; ++pc) {
            if (active[pc] && fifths_pos(pc, fifths) == pos) act[n_act++] = pc;
        }
    }
    if (n_act >= 3) {
        Pt pts[13];
        for (int i = 0; i < n_act; ++i) pts[i] = wheel_point(cx, cy, act[i], radius, fifths);
        c.polygon(pts, n_act, Fill::Hatch25);
        pts[n_act] = pts[0];
        c.polyline(pts, n_act + 1, 2);
    } else if (n_act == 2) {
        const Pt a = wheel_point(cx, cy, act[0], radius, fifths), b = wheel_point(cx, cy, act[1], radius, fifths);
        c.line(a.x, a.y, b.x, b.y, 2);
    }

    for (int pc = 0; pc < 12; ++pc) {
        const Pt p = wheel_point(cx, cy, pc, radius, fifths);
        if (active[pc]) {
            const int r = pc == root_pc ? 9 : 7;
            c.ellipse(p.x - r, p.y - r, p.x + r, p.y + r, kInk, kNone);
            if (pc == root_pc) c.ellipse(p.x - r - 4, p.y - r - 4, p.x + r + 4, p.y + r + 4, kNone, kInk, 2);
        } else {
            c.ellipse(p.x - 5, p.y - 5, p.x + 5, p.y + 5, kPaper, kInk, 1);
        }
        const Pt l = wheel_point(cx, cy, pc, radius + 24, fifths);
        ui_text(c, l.x, l.y, kNames[pc], 15, 'm', 'm');
    }
}

void draw_center(Canvas& c, double cx, double cy, const char* label, int inner_w) {
    if (std::strcmp(label, "-") == 0 || label[0] == '\0') {
        ui_text(c, cx, cy, "listening", 16, 'm', 'm');
        return;
    }
    const int size = fit_size(label, inner_w, 44);
    const FontFace& f = face(FontRole::Ui, size);
    const TextExtent e = text_extent(f, label);
    const double w = e.x1 - e.x0, h = e.y1 - e.y0;
    const double pad = 5;
    c.rect(cx - w / 2 - pad, cy - h / 2 - pad - 2, cx + w / 2 + pad, cy + h / 2 + pad, kPaper, kInk, 1);
    c.text(f, cx, cy, label, 'm', 'm');
}

void key_centers(double x0, double width, double* xs /*[kNumKeys]*/) {
    int n_white = 0;
    for (int n = kNoteLo; n <= kNoteHi; ++n) n_white += kWhitePc[n % 12];
    const double kw = width / n_white;
    int i = 0;
    for (int n = kNoteLo; n <= kNoteHi; ++n) {
        if (kWhitePc[n % 12]) {
            xs[n - kNoteLo] = x0 + (i + 0.5) * kw;
            ++i;
        } else {
            xs[n - kNoteLo] = x0 + i * kw;                // boundary to the right of the previous white key
        }
    }
}

void draw_spectrum(Canvas& c, double x0, double y0, double width, double height, const float* levels,
                   const int* notes, int n_notes) {
    const double base = y0 + height;
    c.line(x0, base, x0 + width, base, 1);
    double xs[kNumKeys];
    key_centers(x0, width, xs);
    Pt pts[kNumKeys], poly[kNumKeys + 2];
    for (int i = 0; i < kNumKeys; ++i) pts[i] = {xs[i], base - static_cast<double>(levels[i]) * (height - 2)};
    poly[0] = {pts[0].x, base};
    for (int i = 0; i < kNumKeys; ++i) poly[i + 1] = pts[i];
    poly[kNumKeys + 1] = {pts[kNumKeys - 1].x, base};
    c.polygon(poly, kNumKeys + 2, Fill::Hatch25);
    c.polyline(pts, kNumKeys, 1);
    for (int i = 0; i < n_notes; ++i) {
        if (notes[i] < kNoteLo || notes[i] > kNoteHi) continue;
        const Pt p = pts[notes[i] - kNoteLo];
        c.ellipse(p.x - 3, p.y - 3, p.x + 3, p.y + 3, kInk, kNone);
    }
}

void draw_keyboard(Canvas& c, double x0, double y0, double width, double height, const int* notes, int n_notes,
                   int bass_note) {
    int n_white = 0;
    int white_idx[kNumKeys] = {};
    for (int n = kNoteLo; n <= kNoteHi; ++n) {
        if (kWhitePc[n % 12]) white_idx[n - kNoteLo] = n_white++;
    }
    const double kw = width / n_white;
    const int black_h = static_cast<int>(height * 0.62);
    auto active = [&](int n) {
        for (int i = 0; i < n_notes; ++i) {
            if (notes[i] == n) return true;
        }
        return false;
    };

    for (int n = kNoteLo; n <= kNoteHi; ++n) {
        if (!kWhitePc[n % 12]) continue;
        const int i = white_idx[n - kNoteLo];
        const double xa = x0 + i * kw, xb = x0 + (i + 1) * kw;
        c.rect(xa, y0, xb, y0 + height, kPaper, kInk, 1);
        if (active(n)) {
            const int r = n == bass_note ? 5 : 4;
            c.ellipse((xa + xb) / 2 - r, y0 + height - 4 - 2 * r, (xa + xb) / 2 + r, y0 + height - 4, kInk, kNone);
        }
    }
    for (int n = kNoteLo; n <= kNoteHi; ++n) {
        if (kWhitePc[n % 12]) continue;
        const int left = white_idx[n - 1 - kNoteLo];
        const double xc = x0 + (left + 1) * kw;
        const double bw = kw * 0.6;
        c.rect(xc - bw / 2, y0, xc + bw / 2, y0 + black_h, kInk, kNone);
        if (active(n)) {
            const double cyk = y0 + black_h - 8;
            c.ellipse(xc - 4, cyk - 4, xc + 4, cyk + 4, kPaper, kNone);
        }
    }
    for (int n = kNoteLo; n <= kNoteHi; n += 12) {
        char label[8];
        std::snprintf(label, sizeof label, "C%d", n / 12 - 1);
        ui_text(c, x0 + white_idx[n - kNoteLo] * kw + kw / 2, y0 + height + 3, label, 11, 'm', 't');
    }
}

void draw_meter(Canvas& c, double x0, double y0, double w, double h, double rms, double silence_rms) {
    ui_text(c, x0, y0 - 14, "LEVEL", 11, 'l', 't');
    c.rect(x0, y0, x0 + w, y0 + h, kPaper, kInk, 1);
    const double fill_x = x0 + 1 + meter_frac(rms) * (w - 2);
    c.rect(x0 + 1, y0 + 1, fill_x, y0 + h - 1, kInk, kNone);
    const double xt = x0 + meter_frac(silence_rms) * w;
    c.line(xt, y0 - 4, xt, y0 + h + 4, 2);
    ui_text(c, xt, y0 + h + 5, "min", 10, 'm', 't');
}

void render_wheel(Canvas& c, const Scene& sc, bool fifths) {
    const int W = kScreenW, H = kScreenH;
    const int key_h = 44, spec_h = 36, margin = 8;
    const int kb_y = H - key_h - 18;
    const int spec_y = kb_y - spec_h - 2;
    const double avail_h = spec_y - 6 - margin;
    const int radius = static_cast<int>(std::min(avail_h / 2 - 26, (W - 2 * 88) / 2.0 - 32));
    const double cx = W / 2.0, cy = margin + avail_h / 2;

    bool active[12] = {};
    for (int i = 0; i < sc.n_notes; ++i) active[sc.notes[i] % 12] = true;
    const bool chord = is_chord(sc.label, sc.text);
    const int root = chord ? root_pc_of(sc.label) : -1;

    draw_wheel(c, cx, cy, radius, active, root, fifths);
    const char* center = chord ? sc.label : (sc.text[0] ? sc.text : "-");
    draw_center(c, cx, cy, center, static_cast<int>(radius * 1.15));
    draw_spectrum(c, margin, spec_y, W - 2 * margin, spec_h, sc.levels, sc.notes, sc.n_notes);
    int bass = -1;
    for (int i = 0; i < sc.n_notes; ++i) bass = bass < 0 ? sc.notes[i] : std::min(bass, sc.notes[i]);
    draw_keyboard(c, margin, kb_y, W - 2 * margin, key_h, sc.notes, sc.n_notes, bass);

    draw_meter(c, margin, 170, 84, 10, sc.rms, sc.silence_rms);

    ui_text(c, margin, margin, "NOTES", 12, 'l', 't');
    for (int i = 0; i < std::min(sc.n_notes, 6); ++i) {
        char name[8];
        note_name(sc.notes[i], name, sizeof name);
        ui_text(c, margin, margin + 18 + i * 22, name, 18, 'l', 't');
    }
    ui_text(c, W - margin, margin, "RECENT", 12, 'r', 't');
    for (int i = 0; i < std::min(sc.n_history, 5); ++i) {
        const char* h = sc.history[sc.n_history - 1 - i];     // newest first
        const int sz = fit_size(h, 70, i == 0 ? 22 : 16);
        ui_text(c, W - margin, margin + 18 + i * 26, h, sz, 'r', 't');
    }
}

// ---------- shared by poster and tape ----------

void segments(Canvas& c, double x0, double y0, double w, double h, double rms, double silence_rms, int seg = 7,
              int gap = 3) {
    const int n = static_cast<int>(std::floor(w / (seg + gap)));
    const int fill_n = static_cast<int>(std::nearbyint(meter_frac(rms) * n));
    const int thr_i = static_cast<int>(std::nearbyint(meter_frac(silence_rms) * n));
    for (int i = 0; i < n; ++i) {
        const double x = x0 + i * (seg + gap);
        if (i == thr_i) c.rect(x, y0 - 5, x + seg - 1, y0 + h + 4, kNone, kInk, 1);
        if (i < fill_n) {
            c.rect(x, y0, x + seg - 1, y0 + h, kInk, kNone);
        } else {
            c.rect(x, y0, x + seg - 1, y0 + h, kNone, kInk);
        }
    }
}

void join_recent(const Scene& sc, int count, const char* sep, char* out, size_t n) {
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < std::min(sc.n_history, count); ++i) {
        const int w = std::snprintf(out + used, n - used, "%s%s", i ? sep : "", sc.history[sc.n_history - 1 - i]);
        if (w < 0 || static_cast<size_t>(w) >= n - used) break;
        used += static_cast<size_t>(w);
    }
}

void render_tape(Canvas& c, const Scene& sc) {
    const int W = kScreenW;
    draw_label(c, 14, 10, sc.label, 190, 40, 46, 'l');
    const FontFace& mono12 = face(FontRole::Mono, 12);
    text_box(c, mono12, W - 14, 12, "TAPE", 'r');
    text_box(c, mono12, W - 14, 30, sc.text, 'r');
    c.rect(12, 56, W - 12, 58, kInk, kNone);

    const int mx = 18, y0 = 68;
    const double px = (W - 2.0 * mx) / 12;
    const double py = (262.0 - y0) / kTapeRows;
    const int root = root_pc_of(sc.label);
    for (int r = 0; r < kTapeRows; ++r) {
        const double cy = y0 + (r + 0.5) * py;
        for (int col = 0; col < 12; ++col) {
            const double cx = mx + (col + 0.5) * px;
            const double e = sc.rows[r][col];
            if (e >= 0.55) {
                const double rad = 2.0 + 4.0 * (e - 0.55) / 0.45;
                c.ellipse(cx - rad, cy - rad, cx + rad, cy + rad, kInk, kNone);
            } else {
                c.rect(cx - 1, cy - 1, cx, cy, kInk, kNone);
            }
        }
    }
    const double by = 262 - py;                           // bracket around the newest row
    const double bx[2] = {mx - 6.0, W - mx + 6.0};
    for (double x : bx) c.line(x, by, x, 262, 2);
    for (double y : {by, 262.0}) {
        c.line(mx - 6, y, mx - 2, y, 2);
        c.line(W - mx + 2, y, W - mx + 6, y, 2);
    }
    for (int col = 0; col < 12; ++col) {
        const double cx = mx + (col + 0.5) * px;
        if (col == root) {
            c.rect(cx - 13, 270, cx + 13, 287, kInk, kNone);
            text_box(c, mono12, cx, 279, kNames[col], 'm', kPaper);
        } else {
            text_box(c, mono12, cx, 279, kNames[col], 'm');
        }
    }
}

void render_poster(Canvas& c, const Scene& sc) {
    const int W = kScreenW, H = kScreenH;
    for (int y = 6; y < H; y += 12) {                     // graph-paper dots
        for (int x = 6; x < W; x += 12) c.point(x, y);
    }
    draw_label(c, 16, 14, sc.label, W - 40, 112, 130, 'l');

    double x = 16;
    int bass = -1;
    for (int i = 0; i < sc.n_notes; ++i) bass = bass < 0 ? sc.notes[i] : std::min(bass, sc.notes[i]);
    const FontFace& mono14 = face(FontRole::Mono, 14);
    for (int i = 0; i < sc.n_notes; ++i) {
        char name[8];
        note_name(sc.notes[i], name, sizeof name);
        const int pw = text_extent(mono14, name).x1 + 20;
        const bool is_bass = sc.notes[i] == bass;
        c.rounded_rect(x, 144, x + pw, 170, 13, is_bass ? kInk : kPaper, kInk, 2);
        text_box(c, mono14, x + pw / 2.0, 157, name, 'm', is_bass ? kPaper : kInk);
        x += pw + 8;
    }

    bool active[12] = {};
    for (int i = 0; i < sc.n_notes; ++i) active[sc.notes[i] % 12] = true;
    const int root = root_pc_of(sc.label);
    const FontFace& mono11 = face(FontRole::Mono, 11);
    for (int pc = 0; pc < 12; ++pc) {
        const int x0 = 17 + pc * 31;
        if (active[pc]) {
            c.rect(x0, 188, x0 + 24, 212, kInk, kNone);
            if (pc == root) c.rect(x0 + 8, 196, x0 + 16, 204, kPaper, kNone);
        } else {
            c.rect(x0, 188, x0 + 24, 212, kPaper, kInk, 2);
        }
        text_box(c, mono11, x0 + 12, 224, kNames[pc], 'm');
    }

    text_box(c, mono11, 17, 244, "IN", 'l');
    segments(c, 17, 258, W - 34, 14, sc.rms, sc.silence_rms);
    char recent[200];
    char names[160];
    join_recent(sc, 3, "  ", names, sizeof names);
    std::snprintf(recent, sizeof recent, "RECENT  %s", names);
    text_box(c, face(FontRole::Mono, 12), 17, 284, recent, 'l');
}

}  // namespace

const char* mode_name(Mode mode) {
    switch (mode) {
        case Mode::Wheel: return "wheel";
        case Mode::Poster: return "poster";
        case Mode::Tape: return "tape";
    }
    return "?";
}

void render(Canvas& canvas, Mode mode, const Scene& scene, bool fifths) {
    canvas.clear();
    switch (mode) {
        case Mode::Wheel: render_wheel(canvas, scene, fifths); break;
        case Mode::Poster: render_poster(canvas, scene); break;
        case Mode::Tape: render_tape(canvas, scene); break;
    }
}

UiState::UiState() {}

void UiState::tick() {
    for (float& v : scene_.levels) v *= 0.92f;
    scene_.rms *= 0.92f;
}

void UiState::update(const Shown& shown) {
    scene_.n_notes = shown.n_notes;
    for (int i = 0; i < shown.n_notes; ++i) scene_.notes[i] = shown.notes[i];
    std::snprintf(scene_.label, sizeof scene_.label, "%s", shown.label);
    std::snprintf(scene_.text, sizeof scene_.text, "%s", shown.text);
    for (int i = 0; i < kNumKeys; ++i) scene_.levels[i] = std::max(scene_.levels[i], shown.levels[i]);
    scene_.rms = std::max(scene_.rms, shown.rms);

    for (int r = 1; r < kTapeRows; ++r) std::memcpy(scene_.rows[r - 1], scene_.rows[r], sizeof scene_.rows[r]);
    fold(shown.levels, scene_.rows[kTapeRows - 1]);

    if (std::strcmp(shown.label, "-") != 0 && std::strcmp(shown.label, last_label_) != 0) {
        if (scene_.n_history == kHistoryMax) {
            for (int i = 1; i < kHistoryMax; ++i) std::memcpy(scene_.history[i - 1], scene_.history[i], sizeof scene_.history[i]);
            --scene_.n_history;
        }
        std::snprintf(scene_.history[scene_.n_history++], sizeof scene_.history[0], "%s", shown.label);
    }
    std::snprintf(last_label_, sizeof last_label_, "%s", shown.label);
}

}  // namespace chord
