#pragma once
#include <SDL2/SDL.h>
#include <algorithm>

namespace ui {

// ── Rect: the atom of layout ────────────────────────────────────────
// Every position on screen is a Rect produced by subdividing a parent.
// Children never escape parent bounds. Overlap is impossible by construction.

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    // Shrink all edges inward
    Rect inset(int pad) const {
        return {x + pad, y + pad, std::max(0, w - pad * 2), std::max(0, h - pad * 2)};
    }
    Rect inset(int h_pad, int v_pad) const {
        return {x + h_pad, y + v_pad, std::max(0, w - h_pad * 2), std::max(0, h - v_pad * 2)};
    }

    // Cut: remove a strip from one edge. Returns the strip.
    // The caller's rect is NOT mutated (use Layout for cursor-style cuts).
    Rect top(int height) const { return {x, y, w, std::min(height, h)}; }
    Rect bottom(int height) const { int t = std::max(0, h - height); return {x, y + t, w, h - t}; }
    Rect left(int width) const { return {x, y, std::min(width, w), h}; }
    Rect right(int width) const { int l = std::max(0, w - width); return {x + l, y, w - l, h}; }

    // Fractional splits (non-mutating)
    Rect left_frac(int num, int den) const { return {x, y, w * num / den, h}; }
    Rect right_frac(int num, int den) const {
        int lw = w * num / den;
        return {x + w - lw, y, lw, h};
    }
    Rect top_frac(int num, int den) const { return {x, y, w, h * num / den}; }
    Rect bottom_frac(int num, int den) const {
        int th = h * num / den;
        return {x, y + h - th, w, th};
    }

    // Center a rect of given size within this rect
    Rect center(int cw, int ch) const {
        return {x + (w - cw) / 2, y + (h - ch) / 2, cw, ch};
    }

    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
    int x2() const { return x + w; }
    int y2() const { return y + h; }
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    SDL_Rect sdl() const { return {x, y, w, h}; }
};

// ── Layout: a Rect with a cursor ────────────────────────────────────
// Cut rows off the top, columns off the left/right.
// The cursor always represents the remaining unallocated space.

struct Layout {
    Rect bounds;    // total area (immutable after construction)
    Rect cursor;    // remaining space (shrinks as you cut)
    int line_h;     // base text line height (the unit of measurement)
    int pad;        // standard padding (derived from line_h)
    int gap;        // standard gap between elements (derived from line_h)

    // ── Construction ──

    static Layout from_screen(int w, int h, int lh) {
        Layout l;
        l.bounds = l.cursor = {0, 0, w, h};
        l.line_h = lh;
        l.pad = pad_from(lh);
        l.gap = gap_from(lh);
        return l;
    }

    static Layout from_rect(Rect r, int lh) {
        Layout l;
        l.bounds = l.cursor = r;
        l.line_h = lh;
        l.pad = pad_from(lh);
        l.gap = gap_from(lh);
        return l;
    }

    // ── Cut operations (advance the cursor) ──

    // Cut a row from the top of remaining space.
    // height=0 means one line_h.
    Rect row(int height = 0) {
        if (height <= 0) height = line_h;
        height = std::min(height, cursor.h);
        Rect r = {cursor.x, cursor.y, cursor.w, height};
        cursor.y += height;
        cursor.h -= height;
        return r;
    }

    // Cut N lines worth of rows (including gap between them)
    Rect row_n(int n) {
        return row(n * line_h + (n - 1) * gap);
    }

    // Cut from the bottom
    Rect row_bottom(int height = 0) {
        if (height <= 0) height = line_h;
        height = std::min(height, cursor.h);
        Rect r = {cursor.x, cursor.y + cursor.h - height, cursor.w, height};
        cursor.h -= height;
        return r;
    }

    // Cut a column from the left
    Rect col_left(int width) {
        width = std::min(width, cursor.w);
        Rect r = {cursor.x, cursor.y, width, cursor.h};
        cursor.x += width;
        cursor.w -= width;
        return r;
    }

    // Cut a column from the right
    Rect col_right(int width) {
        width = std::min(width, cursor.w);
        Rect r = {cursor.x + cursor.w - width, cursor.y, width, cursor.h};
        cursor.w -= width;
        return r;
    }

    // Add vertical space
    void skip(int pixels) {
        int s = std::min(pixels, cursor.h);
        cursor.y += s;
        cursor.h -= s;
    }

    // Add horizontal space from left
    void skip_h(int pixels) {
        int s = std::min(pixels, cursor.w);
        cursor.x += s;
        cursor.w -= s;
    }

    // ── Queries ──

    int remaining_h() const { return cursor.h; }
    int remaining_w() const { return cursor.w; }
    bool fits(int height) const { return cursor.h >= height; }
    bool fits_row() const { return cursor.h >= line_h; }

    // ── Panel: centered within this layout ──
    // Returns a new Layout for the panel interior (inset by border + pad).

    Layout panel(int w_frac_num, int w_frac_den,
                 int h_frac_num, int h_frac_den) const {
        int pw = bounds.w * w_frac_num / w_frac_den;
        int ph = bounds.h * h_frac_num / h_frac_den;
        Rect outer = bounds.center(pw, ph);
        Rect inner = outer.inset(PANEL_INSET);
        return from_rect(inner, line_h);
    }

    // Panel with explicit max dimensions (still centered, still fractional)
    Layout panel_max(int w_frac_num, int w_frac_den,
                     int h_frac_num, int h_frac_den,
                     int max_w, int max_h) const {
        int pw = std::min(bounds.w * w_frac_num / w_frac_den, max_w);
        int ph = std::min(bounds.h * h_frac_num / h_frac_den, max_h);
        Rect outer = bounds.center(pw, ph);
        Rect inner = outer.inset(PANEL_INSET);
        return from_rect(inner, line_h);
    }

    // Panel with pixel width, fractional height
    Layout panel_w(int pw, int h_frac_num, int h_frac_den) const {
        pw = std::min(pw, bounds.w - pad * 2);
        int ph = bounds.h * h_frac_num / h_frac_den;
        Rect outer = bounds.center(pw, ph);
        Rect inner = outer.inset(PANEL_INSET);
        return from_rect(inner, line_h);
    }

    // Get the outer rect for a panel (for drawing the border)
    Rect panel_outer(int w_frac_num, int w_frac_den,
                     int h_frac_num, int h_frac_den) const {
        int pw = bounds.w * w_frac_num / w_frac_den;
        int ph = bounds.h * h_frac_num / h_frac_den;
        return bounds.center(pw, ph);
    }

    // ── Column splitting (returns Rect arrays, not Layouts) ──

    struct Columns {
        Rect rects[8];
        int count;
        Rect& operator[](int i) { return rects[i]; }
        const Rect& operator[](int i) const { return rects[i]; }
    };

    Columns split_cols(int n, int col_gap = 0) const {
        if (col_gap <= 0) col_gap = gap;
        Columns result;
        result.count = n;
        int total_gap = col_gap * (n - 1);
        int col_w = (cursor.w - total_gap) / n;
        for (int i = 0; i < n; i++) {
            result.rects[i] = {cursor.x + i * (col_w + col_gap), cursor.y, col_w, cursor.h};
        }
        return result;
    }

    // Split into two columns with a specific left/right ratio
    Columns split_cols_ratio(int left_num, int right_num, int col_gap = 0) const {
        if (col_gap <= 0) col_gap = gap;
        Columns result;
        result.count = 2;
        int total = left_num + right_num;
        int lw = (cursor.w - col_gap) * left_num / total;
        int rw = cursor.w - col_gap - lw;
        result.rects[0] = {cursor.x, cursor.y, lw, cursor.h};
        result.rects[1] = {cursor.x + lw + col_gap, cursor.y, rw, cursor.h};
        return result;
    }

    // Helper: make a Layout from one of the column rects
    static Layout col(const Rect& r, int lh) { return from_rect(r, lh); }

    // ── Constants ──
    // PANEL_INSET = border thickness + inner padding.
    // The border is 8px (SNES panel), inner pad is derived from line height.
    static constexpr int PANEL_BORDER = 8;
    static inline int PANEL_INSET_FOR(int lh) { return PANEL_BORDER + pad_from(lh); }
    int PANEL_INSET_VAL() const { return PANEL_BORDER + pad; }

    // Use a fixed inset that works well for the pixel art border
    static constexpr int PANEL_INSET = 14; // 8px border + 6px inner pad

    // Derived spacing from font metrics
    static int pad_from(int lh) { return std::max(4, lh * 2 / 3); }
    static int gap_from(int lh) { return std::max(2, lh / 2); }
    static int section_gap(int lh) { return lh; }
};

} // namespace ui
