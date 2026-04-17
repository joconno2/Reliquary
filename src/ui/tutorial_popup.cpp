#include "ui/tutorial_popup.h"
#include "ui/ui_draw.h"

void TutorialPopup::show(const char* title, const char* body) {
    title_ = title;
    body_ = body;
    open_ = true;
}

bool TutorialPopup::handle_input(const SDL_Event& event) {
    if (!open_) return false;
    // Any key press or mouse click dismisses
    if (event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN) {
        open_ = false;
        return true;
    }
    // Consume all input while open (block movement etc.)
    return true;
}

void TutorialPopup::render(SDL_Renderer* renderer, TTF_Font* font_body, TTF_Font* font_title,
                            int screen_w, int screen_h) {
    if (!open_ || !font_body) return;

    int line_h = TTF_FontLineSkip(font_body);
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;
    int pad = 16;
    int max_text_w = std::min(screen_w * 2 / 3, 600);
    int body_h = ui::text_wrapped_height(font_body, body_.c_str(), max_text_w);

    int panel_w = max_text_w + pad * 2;
    int panel_h = title_h + pad + body_h + line_h + pad * 2; // title + gap + body + dismiss hint + padding
    int px = (screen_w - panel_w) / 2;
    int py = (screen_h - panel_h) / 2;

    // Dim background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 120);
    SDL_Rect full = {0, 0, screen_w, screen_h};
    SDL_RenderFillRect(renderer, &full);

    // Panel
    ui::draw_panel(renderer, px, py, panel_w, panel_h);

    // Title
    int ty = py + pad;
    TTF_Font* tf = font_title ? font_title : font_body;
    ui::draw_text_centered(renderer, tf, title_.c_str(), {220, 200, 140, 255}, screen_w / 2, ty);

    // Body (word-wrapped)
    int by = ty + title_h + pad / 2;
    ui::draw_text_wrapped(renderer, font_body, body_.c_str(), {200, 195, 180, 255},
                           px + pad, by, max_text_w);

    // Dismiss hint
    int hint_y = py + panel_h - line_h - pad / 2;
    ui::draw_text_centered(renderer, font_body, "[press any key]",
                            {120, 115, 100, 255}, screen_w / 2, hint_y);
}
