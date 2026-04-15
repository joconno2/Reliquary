#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

namespace ui {

// ── Layout constants ────────────────────────────────────────────────
constexpr int PANEL_PAD = 10;       // inner padding from panel edge to content
constexpr int PANEL_BORDER = 8;     // total border thickness (outer 3 + gap + inner 2)
constexpr int ITEM_ROW_PAD = 4;     // vertical padding between item rows
constexpr int SECTION_GAP = 8;      // gap between sections
constexpr int COLUMN_GAP = 12;      // gap between columns

// ── Text helpers ────────────────────────────────────────────────────

// Draw a pixel-art style panel with beveled border (classic SNES/GBA RPG menu box).
void draw_panel(SDL_Renderer* renderer, int x, int y, int w, int h);

// Draw a single line of text at (x, y).
void draw_text(SDL_Renderer* renderer, TTF_Font* font,
               const char* text, SDL_Color col, int x, int y);

// Draw text centered horizontally around cx at vertical position y.
void draw_text_centered(SDL_Renderer* renderer, TTF_Font* font,
                        const char* text, SDL_Color col, int cx, int y);

// Draw word-wrapped text within max_w pixels.
void draw_text_wrapped(SDL_Renderer* renderer, TTF_Font* font,
                       const char* text, SDL_Color col, int x, int y, int max_w);

// Draw text clipped to max_w pixels. Truncates with "..." if too long.
void draw_text_clipped(SDL_Renderer* renderer, TTF_Font* font,
                       const char* text, SDL_Color col, int x, int y, int max_w);

// Measure text width in pixels without rendering.
int text_width(TTF_Font* font, const char* text);

// Measure wrapped text height in pixels.
int text_wrapped_height(TTF_Font* font, const char* text, int max_w);

// Set a clip rect, draw content, then restore previous clip.
// Usage: auto guard = clip_guard(renderer, rect);
struct ClipGuard {
    SDL_Renderer* renderer;
    SDL_Rect prev;
    bool had_clip;
    ClipGuard(SDL_Renderer* r, const SDL_Rect& rect);
    ~ClipGuard();
};

} // namespace ui
