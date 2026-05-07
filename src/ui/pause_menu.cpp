#include <algorithm>
#include "ui/pause_menu.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"

static PauseChoice pause_choice_for(int idx) {
    switch (idx) {
        case 0: return PauseChoice::CONTINUE;
        case 1: return PauseChoice::SAVE;
        case 2: return PauseChoice::LOAD;
        case 3: return PauseChoice::SETTINGS;
        case 4: return PauseChoice::SAVE_AND_QUIT;
        case 5: return PauseChoice::EXIT_TO_MENU;
    }
    return PauseChoice::NONE;
}

PauseChoice PauseMenu::handle_input(SDL_Event& event) {
    if (!open_) return PauseChoice::NONE;

    // Exit confirmation sub-dialog
    if (confirming_exit_) {
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_y:
                case SDLK_RETURN:
                    confirming_exit_ = false;
                    return PauseChoice::EXIT_TO_MENU;
                case SDLK_n:
                case SDLK_ESCAPE:
                    confirming_exit_ = false;
                    return PauseChoice::NONE;
                default:
                    return PauseChoice::NONE;
            }
        }
        return PauseChoice::NONE;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mx = event.button.x, my = event.button.y;
        for (int i = 0; i < static_cast<int>(option_rects_.size()); i++) {
            auto& r = option_rects_[i];
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
                selected_ = i;
                if (i == 5) { confirming_exit_ = true; return PauseChoice::NONE; }
                return pause_choice_for(i);
            }
        }
        return PauseChoice::NONE;
    }
    if (event.type == SDL_MOUSEMOTION) {
        int mx = event.motion.x, my = event.motion.y;
        for (int i = 0; i < static_cast<int>(option_rects_.size()); i++) {
            auto& r = option_rects_[i];
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
                selected_ = i; break;
            }
        }
        return PauseChoice::NONE;
    }

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
            if (selected_ == 5) { confirming_exit_ = true; return PauseChoice::NONE; }
            return pause_choice_for(selected_);
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
        "Continue", "Save", "Load", "Settings", "Save & Quit", "Exit to Menu"
    };

    option_rects_.clear();
    for (int i = 0; i < OPTION_COUNT; i++) {
        bool is_sel = (i == selected_);
        auto row = panel.row(line_h + panel.gap);
        option_rects_.push_back({row.x, row.y, row.w, row.h});

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

    // Exit confirmation overlay
    if (confirming_exit_) {
        SDL_Color warn_col = {220, 180, 100, 255};
        SDL_Color hint_col = {140, 130, 120, 255};
        auto confirm_row = panel.row(line_h + 8);
        ui::draw_text_in(renderer, body, "Exit without saving?", warn_col,
                         confirm_row, ui::Align::CENTER);
        auto yn_row = panel.row(line_h + 4);
        ui::draw_text_in(renderer, body, "[Y] Yes   [N] No", hint_col,
                         yn_row, ui::Align::CENTER);
    }

    // Controls hint at bottom
    SDL_Color hint_col = {70, 65, 60, 255};
    auto hint_row = ui::Rect{outer.x, outer.y2() - line_h - 8, outer.w, line_h};
    auto* ig = InputGlyphs::get();
    char hbuf[128];
    if (ig && ig->using_gamepad())
        snprintf(hbuf, sizeof(hbuf), "%s resume   %s select", ig->cancel().c_str(), ig->confirm().c_str());
    else
        snprintf(hbuf, sizeof(hbuf), "[Esc] resume   [Enter] select");
    ui::draw_text_in(renderer, body, hbuf, hint_col, hint_row, ui::Align::CENTER);
}
