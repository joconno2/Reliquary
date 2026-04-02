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

    // Semi-transparent dark overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, w, h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_RenderFillRect(renderer, &overlay);

    // Panel
    int panel_w = std::min(w / 3, 500);
    int panel_h = std::min(h / 2, 400);
    int panel_x = (w - panel_w) / 2;
    int panel_y = (h - panel_h) / 2;

    ui::draw_panel(renderer, panel_x, panel_y, panel_w, panel_h);

    // Title
    SDL_Color title_col = {200, 180, 160, 255};
    int title_y = panel_y + 16;
    ui::draw_text_centered(renderer, title ? title : body, "Paused", title_col, w / 2, title_y);

    // Menu options
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};

    static const char* options[] = {
        "Continue", "Save", "Load", "Settings", "Exit to Menu"
    };

    int line_h = body ? TTF_FontLineSkip(body) : 20;
    int menu_y = panel_y + 70;
    int cx = w / 2;

    for (int i = 0; i < OPTION_COUNT; i++) {
        bool is_sel = (i == selected_);

        if (is_sel && body) {
            int tw = 0, th = 0;
            TTF_SizeText(body, options[i], &tw, &th);
            SDL_Rect hl = {cx - tw / 2 - 10, menu_y - 2, tw + 20, line_h + 4};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        ui::draw_text_centered(renderer, body, options[i],
                               is_sel ? sel_col : normal_col, cx, menu_y);
        menu_y += line_h + 12;
    }

    // Controls hint
    SDL_Color hint_col = {70, 65, 60, 255};
    ui::draw_text_centered(renderer, body, "[Esc] resume   [Enter] select",
                           hint_col, cx, panel_y + panel_h - 28);
}
