#include "ui/message_log.h"
#include <algorithm>

void MessageLog::add(const std::string& text, SDL_Color color) {
    messages_.push_back({text, color, current_turn_});
    scroll_to_bottom();
}

void MessageLog::scroll_up() {
    if (scroll_offset_ > 0) scroll_offset_--;
}

void MessageLog::scroll_down() {
    scroll_offset_++;
}

void MessageLog::scroll_to_bottom() {
    scroll_offset_ = static_cast<int>(messages_.size());
}

void MessageLog::render(SDL_Renderer* renderer, TTF_Font* font,
                         int x, int y, int w, int h) const {
    if (!font || messages_.empty()) return;

    int line_height = TTF_FontLineSkip(font);
    int max_lines = h / line_height;
    if (max_lines <= 0) return;

    // Draw background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect bg = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer, 10, 8, 12, 220);
    SDL_RenderFillRect(renderer, &bg);

    // Draw border
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderDrawRect(renderer, &bg);

    // Calculate visible range
    int total = static_cast<int>(messages_.size());
    int end = std::min(scroll_offset_, total);
    int start = std::max(0, end - max_lines);

    int draw_y = y + h - line_height; // draw bottom-up
    for (int i = end - 1; i >= start; i--) {
        auto& msg = messages_[i];

        SDL_Surface* surface = TTF_RenderText_Blended_Wrapped(
            font, msg.text.c_str(), msg.color, static_cast<Uint32>(w - 8));
        if (!surface) continue;

        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_Rect dst = {x + 4, draw_y, surface->w, surface->h};
        draw_y -= surface->h;
        SDL_FreeSurface(surface);

        if (dst.y < y) {
            SDL_DestroyTexture(texture);
            break;
        }

        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
}
