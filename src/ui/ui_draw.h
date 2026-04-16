#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "ui/ui_layout.h"

namespace ui {

// ── Legacy constants (kept for backward compat during migration) ────
constexpr int PANEL_PAD = 10;
constexpr int PANEL_BORDER = 8;
constexpr int ITEM_ROW_PAD = 4;
constexpr int SECTION_GAP = 8;
constexpr int COLUMN_GAP = 12;

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

// ── Layout-aware helpers ─────────────────────────────────────────────

enum class Align { LEFT, CENTER, RIGHT };

// Draw text positioned within a Rect. Vertically centered, horizontally aligned.
void draw_text_in(SDL_Renderer* renderer, TTF_Font* font,
                  const char* text, SDL_Color col, const Rect& area,
                  Align align = Align::LEFT);

// Draw a panel and return a Layout for its interior.
// Draws the SNES border, returns inset rect as a Layout ready for content.
Layout draw_panel_in(SDL_Renderer* renderer, const Rect& outer, int line_h);

// Draw a dark overlay (semi-transparent background dimming).
void draw_overlay(SDL_Renderer* renderer, int w, int h, Uint8 alpha = 180);

} // namespace ui
