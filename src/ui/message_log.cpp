#include "ui/message_log.h"
#include "ui/ui_draw.h"
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
    int padding = 8;
    int max_lines = (h - padding * 2) / line_height;
    if (max_lines <= 0) return;

    // Draw background panel
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    ui::draw_panel(renderer, x, y, w, h);

    // Show the most recent messages, top-down (newest at top)
    int total = static_cast<int>(messages_.size());
    int end = std::min(scroll_offset_, total);
    int start = std::max(0, end - max_lines);

    int draw_y = y + padding;
    // Draw from newest (end-1) to oldest (start), top to bottom
    for (int i = end - 1; i >= start && draw_y < y + h - padding; i--) {
        auto& msg = messages_[i];

        SDL_Surface* surface = TTF_RenderText_Blended_Wrapped(
            font, msg.text.c_str(), msg.color, static_cast<Uint32>(w - padding * 2));
        if (!surface) continue;

        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_Rect dst = {x + padding, draw_y, surface->w, surface->h};

        if (dst.y + surface->h > y + h - padding) {
            SDL_DestroyTexture(texture);
            SDL_FreeSurface(surface);
            break;
        }

        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        draw_y += surface->h;
        SDL_DestroyTexture(texture);
        SDL_FreeSurface(surface);
    }
}
