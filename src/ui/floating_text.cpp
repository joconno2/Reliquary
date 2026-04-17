#include "ui/floating_text.h"
#include "ui/ui_draw.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>

void FloatingTextSystem::spawn(float wx, float wy, const char* text, SDL_Color color, bool big) {
    if (texts_.size() >= MAX_TEXTS) {
        // Drop oldest
        texts_.erase(texts_.begin());
    }
    FloatingText ft;
    ft.wx = wx;
    ft.wy = wy;
    ft.offset_y = 0.0f;
    ft.life = 1.0f;
    ft.decay = 0.025f; // ~40 frames = ~0.67s at 60fps
    ft.text = text;
    ft.color = color;
    ft.big = big;
    ft.jitter_x = static_cast<float>((std::rand() % 21) - 10); // -10 to +10 px
    texts_.push_back(std::move(ft));
}

void FloatingTextSystem::spawn(float wx, float wy, int amount, SDL_Color color, bool big) {
    char buf[16];
    if (amount > 0)
        snprintf(buf, sizeof(buf), "+%d", amount);
    else
        snprintf(buf, sizeof(buf), "%d", amount);
    spawn(wx, wy, buf, color, big);
}

void FloatingTextSystem::update() {
    for (auto& ft : texts_) {
        ft.offset_y -= 0.6f;   // drift up ~0.6 px/frame
        ft.life -= ft.decay;
    }
    texts_.erase(
        std::remove_if(texts_.begin(), texts_.end(),
                        [](const FloatingText& ft) { return ft.life <= 0.0f; }),
        texts_.end());
}

void FloatingTextSystem::render(SDL_Renderer* renderer, TTF_Font* font_body, TTF_Font* font_big,
                                 int cam_x, int cam_y, int tile_size, int y_offset) {
    for (auto& ft : texts_) {
        // World to screen
        int sx = static_cast<int>((ft.wx - cam_x) * tile_size + tile_size / 2 + ft.jitter_x);
        int sy = static_cast<int>((ft.wy - cam_y) * tile_size + ft.offset_y) + y_offset;

        uint8_t alpha = static_cast<uint8_t>(ft.life * 255);
        SDL_Color col = {ft.color.r, ft.color.g, ft.color.b, alpha};

        TTF_Font* font = ft.big ? font_big : font_body;
        if (!font) font = font_body;

        // Render with alpha by creating a surface and texture
        SDL_Surface* surf = TTF_RenderText_Blended(font, ft.text.c_str(), col);
        if (!surf) continue;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        if (!tex) { SDL_FreeSurface(surf); continue; }

        SDL_SetTextureAlphaMod(tex, alpha);
        SDL_Rect dst = {sx - surf->w / 2, sy - surf->h / 2, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);

        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}
