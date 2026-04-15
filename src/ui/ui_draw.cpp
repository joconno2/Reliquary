#include "ui/ui_draw.h"
#include <string>

namespace ui {

// Helper: filled rect shortcut
static void fill_rect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

void draw_panel(SDL_Renderer* renderer, int x, int y, int w, int h) {
    // Chunky pixel border — 3px outer frame + 2px inner bevel
    // Reads clearly at any resolution

    // Colors
    SDL_Color bg       = {15, 13, 20, 240};
    SDL_Color hi       = {90, 80, 110, 255};   // bright edge (top/left)
    SDL_Color mid      = {55, 48, 68, 255};     // mid tone (secondary edge)
    SDL_Color sh       = {28, 24, 36, 255};     // dark edge (bottom/right)
    SDL_Color corner   = {45, 40, 58, 255};      // corners

    // 1. Fill interior
    fill_rect(renderer, x, y, w, h, bg);

    // 2. Outer border — 3px thick bright edges on top/left, dark on bottom/right
    // Top edge (3px tall)
    fill_rect(renderer, x + 3, y, w - 6, 3, hi);
    // Left edge (3px wide)
    fill_rect(renderer, x, y + 3, 3, h - 6, hi);
    // Bottom edge (3px tall)
    fill_rect(renderer, x + 3, y + h - 3, w - 6, 3, sh);
    // Right edge (3px wide)
    fill_rect(renderer, x + w - 3, y + 3, 3, h - 6, sh);

    // Corners — 3x3 blocks of mid-tone
    fill_rect(renderer, x, y, 3, 3, corner);
    fill_rect(renderer, x + w - 3, y, 3, 3, corner);
    fill_rect(renderer, x, y + h - 3, 3, 3, corner);
    fill_rect(renderer, x + w - 3, y + h - 3, 3, 3, corner);

    // 3. Inner bevel — 2px, inset by 4px from edge
    int ix = x + 4;
    int iy = y + 4;
    int iw = w - 8;
    int ih = h - 8;

    // Top inner (2px)
    fill_rect(renderer, ix + 2, iy, iw - 4, 2, mid);
    // Left inner (2px)
    fill_rect(renderer, ix, iy + 2, 2, ih - 4, mid);
    // Bottom inner (2px)
    fill_rect(renderer, ix + 2, iy + ih - 2, iw - 4, 2, sh);
    // Right inner (2px)
    fill_rect(renderer, ix + iw - 2, iy + 2, 2, ih - 4, sh);

    // Inner corners
    fill_rect(renderer, ix, iy, 2, 2, corner);
    fill_rect(renderer, ix + iw - 2, iy, 2, 2, corner);
    fill_rect(renderer, ix, iy + ih - 2, 2, 2, corner);
    fill_rect(renderer, ix + iw - 2, iy + ih - 2, 2, 2, corner);
}

void draw_text(SDL_Renderer* renderer, TTF_Font* font,
               const char* text, SDL_Color col, int x, int y) {
    if (!font || !text || !text[0]) return;
    SDL_Surface* surf = TTF_RenderText_Blended(font, text, col);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

void draw_text_centered(SDL_Renderer* renderer, TTF_Font* font,
                        const char* text, SDL_Color col, int cx, int y) {
    if (!font || !text || !text[0]) return;
    int tw = 0, th = 0;
    TTF_SizeText(font, text, &tw, &th);
    draw_text(renderer, font, text, col, cx - tw / 2, y);
}

void draw_text_wrapped(SDL_Renderer* renderer, TTF_Font* font,
                       const char* text, SDL_Color col, int x, int y, int max_w) {
    if (!font || !text || !text[0]) return;
    SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(font, text, col, static_cast<Uint32>(max_w));
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

void draw_text_clipped(SDL_Renderer* renderer, TTF_Font* font,
                       const char* text, SDL_Color col, int x, int y, int max_w) {
    if (!font || !text || !text[0] || max_w <= 0) return;

    int tw = 0, th = 0;
    TTF_SizeText(font, text, &tw, &th);

    if (tw <= max_w) {
        draw_text(renderer, font, text, col, x, y);
        return;
    }

    // Truncate: binary search for how many chars fit with "..."
    int ellipsis_w = 0;
    TTF_SizeText(font, "...", &ellipsis_w, nullptr);
    int target_w = max_w - ellipsis_w;
    if (target_w <= 0) { draw_text(renderer, font, "...", col, x, y); return; }

    // Find truncation point
    std::string truncated(text);
    while (!truncated.empty()) {
        truncated.pop_back();
        int cw = 0;
        TTF_SizeText(font, truncated.c_str(), &cw, nullptr);
        if (cw <= target_w) break;
    }
    truncated += "...";
    draw_text(renderer, font, truncated.c_str(), col, x, y);
}

int text_width(TTF_Font* font, const char* text) {
    if (!font || !text || !text[0]) return 0;
    int w = 0, h = 0;
    TTF_SizeText(font, text, &w, &h);
    return w;
}

int text_wrapped_height(TTF_Font* font, const char* text, int max_w) {
    if (!font || !text || !text[0]) return 0;
    SDL_Color dummy = {255, 255, 255, 255};
    SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(font, text, dummy, static_cast<Uint32>(max_w));
    if (!surf) return TTF_FontLineSkip(font);
    int h = surf->h;
    SDL_FreeSurface(surf);
    return h;
}

ClipGuard::ClipGuard(SDL_Renderer* r, const SDL_Rect& rect) : renderer(r) {
    SDL_Rect current;
    had_clip = (SDL_RenderGetClipRect(renderer, &current), current.w > 0 && current.h > 0);
    if (had_clip) prev = current;
    SDL_RenderSetClipRect(renderer, &rect);
}

ClipGuard::~ClipGuard() {
    if (had_clip)
        SDL_RenderSetClipRect(renderer, &prev);
    else
        SDL_RenderSetClipRect(renderer, nullptr);
}

} // namespace ui
