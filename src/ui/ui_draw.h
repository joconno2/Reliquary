#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

namespace ui {

// Draw a pixel-art style panel with beveled border (classic SNES/GBA RPG menu box).
// outer highlight on top-left, outer shadow on bottom-right,
// inner bevel inset by 2px, corner pixels set to mid-tone.
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

} // namespace ui
