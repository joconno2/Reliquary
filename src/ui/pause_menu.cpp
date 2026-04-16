#include <algorithm>
#include "ui/pause_menu.h"
#include "ui/ui_draw.h"

PauseChoice PauseMenu::handle_input(SDL_Event& event) {
    if (!open_) return PauseChoice::NONE;
    if (event.type != SDL_KEYDOWN) return PauseChoice::NONE;

    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_w:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return PauseChoice::NONE;
        case SDLK_DOWN:
        case SDLK_s:
        case SDLK_j:
            if (selected_ < OPTION_COUNT - 1) selected_++;
            return PauseChoice::NONE;
        case SDLK_RETURN:
        case SDLK_e:
            switch (selected_) {
                case 0: return PauseChoice::CONTINUE;
                case 1: return PauseChoice::SAVE;
                case 2: return PauseChoice::LOAD;
                case 3: return PauseChoice::SETTINGS;
                case 4: return PauseChoice::EXIT_TO_MENU;
            }
            return PauseChoice::NONE;
        case SDLK_ESCAPE:
            return PauseChoice::CONTINUE;
        default:
            return PauseChoice::NONE;
    }
}

void PauseMenu::render(SDL_Renderer* renderer, TTF_Font* body, TTF_Font* title,
                        int w, int h) const {
    if (!open_) return;

    int line_h = body ? TTF_FontLineSkip(body) : 20;
    auto screen = ui::Layout::from_screen(w, h, line_h);

    ui::draw_overlay(renderer, w, h, 160);

    // Panel: 1/3 width, 1/2 height, centered
    auto outer = screen.panel_outer(1, 3, 1, 2);
    auto panel = ui::draw_panel_in(renderer, outer, line_h);

    // Title
    SDL_Color title_col = {200, 180, 160, 255};
    auto title_row = panel.row(line_h + panel.gap);
    ui::draw_text_in(renderer, title ? title : body, "Paused", title_col,
                     title_row, ui::Align::CENTER);
    panel.skip(panel.gap);

    // Menu options
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};

    static const char* options[] = {
        "Continue", "Save", "Load", "Settings", "Exit to Menu"
    };

    for (int i = 0; i < OPTION_COUNT; i++) {
        bool is_sel = (i == selected_);
        auto row = panel.row(line_h + panel.gap);

        if (is_sel && body) {
            int tw = 0, th = 0;
            TTF_SizeText(body, options[i], &tw, &th);
            SDL_Rect hl = {row.cx() - tw / 2 - 10, row.y - 2, tw + 20, row.h + 4};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        ui::draw_text_in(renderer, body, options[i],
                         is_sel ? sel_col : normal_col, row, ui::Align::CENTER);
    }

    // Controls hint at bottom
    SDL_Color hint_col = {70, 65, 60, 255};
    auto hint_row = ui::Rect{outer.x, outer.y2() - line_h - 8, outer.w, line_h};
    ui::draw_text_in(renderer, body, "[Esc] resume   [Enter] select",
                     hint_col, hint_row, ui::Align::CENTER);
}
