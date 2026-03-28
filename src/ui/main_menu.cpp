#include "ui/main_menu.h"
#include "ui/ui_draw.h"
#include <cmath>

// Option order: New Game, [Continue], Load, Settings, Quit
// With continue:    0=New, 1=Continue, 2=Load, 3=Settings, 4=Quit
// Without continue: 0=New, 1=Load, 2=Settings, 3=Quit

MenuChoice MainMenu::handle_input(SDL_Event& event) {
    if (event.type != SDL_KEYDOWN) return MenuChoice::NONE;

    int count = option_count();

    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return MenuChoice::NONE;
        case SDLK_DOWN:
        case SDLK_j:
            if (selected_ < count - 1) selected_++;
            return MenuChoice::NONE;
        case SDLK_RETURN:
        case SDLK_e:
            if (can_continue_) {
                switch (selected_) {
                    case 0: return MenuChoice::NEW_GAME;
                    case 1: return MenuChoice::CONTINUE;
                    case 2: return MenuChoice::LOAD;
                    case 3: return MenuChoice::SETTINGS;
                    case 4: return MenuChoice::QUIT;
                }
            } else {
                switch (selected_) {
                    case 0: return MenuChoice::NEW_GAME;
                    case 1: return MenuChoice::LOAD;
                    case 2: return MenuChoice::SETTINGS;
                    case 3: return MenuChoice::QUIT;
                }
            }
            return MenuChoice::NONE;
        case SDLK_ESCAPE:
            return MenuChoice::QUIT;
        default:
            return MenuChoice::NONE;
    }
}

void MainMenu::render(SDL_Renderer* renderer, TTF_Font* body, TTF_Font* title,
                       TTF_Font* title_large, const SpriteManager& sprites,
                       int w, int h) const {
    SDL_SetRenderDrawColor(renderer, 18, 20, 28, 255);
    SDL_RenderClear(renderer);

    int cx = w / 2;

    // Title
    SDL_Color title_col = {200, 180, 160, 255};
    int title_y = h * 10 / 100;
    ui::draw_text_centered(renderer, title_large, "Reliquary", title_col, cx, title_y);

    // Campfire
    int fire_frame = static_cast<int>((SDL_GetTicks() / 150) % 6);
    int fire_size = 128;
    int fire_cx = cx;
    int fire_cy = h * 40 / 100;
    int fire_x = fire_cx - fire_size / 2;
    int fire_y = fire_cy - fire_size / 2;

    sprites.draw_sprite_sized(renderer, SHEET_ANIMATED, fire_frame, 3,
                               fire_x, fire_y, fire_size);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int r = 3; r >= 1; r--) {
        int glow_r = fire_size / 2 + r * 24;
        int alpha = 8 - r * 2;
        SDL_SetRenderDrawColor(renderer, 180, 100, 40, static_cast<Uint8>(alpha));
        for (int dy = -glow_r; dy <= glow_r; dy += 4) {
            int dx = static_cast<int>(std::sqrt(static_cast<float>(glow_r * glow_r - dy * dy)));
            SDL_Rect glow = {fire_cx - dx, fire_cy + dy, dx * 2, 4};
            SDL_RenderFillRect(renderer, &glow);
        }
    }

    // Menu options — use the title font for bigger text
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color dim_col = {100, 95, 90, 255}; // for Load (placeholder)

    int count = option_count();
    const char* opts_with_cont[] = {"New Game", "Continue", "Load Game", "Settings", "Quit"};
    const char* opts_no_cont[]   = {"New Game", "Load Game", "Settings", "Quit"};
    const char** options = can_continue_ ? opts_with_cont : opts_no_cont;

    // Use title font for menu items (bigger, more readable)
    TTF_Font* menu_font = title ? title : body;
    int line_h = menu_font ? TTF_FontLineSkip(menu_font) : 24;
    int menu_y = h * 64 / 100;

    for (int i = 0; i < count; i++) {
        bool is_sel = (i == selected_);

        // Load is dimmed (placeholder)
        bool is_load = can_continue_ ? (i == 2) : (i == 1);

        if (is_sel) {
            int tw = 0, th = 0;
            if (menu_font) TTF_SizeText(menu_font, options[i], &tw, &th);
            SDL_Rect hl = {cx - tw / 2 - 12, menu_y - 4, tw + 24, line_h + 8};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        SDL_Color col = is_sel ? sel_col : (is_load ? dim_col : normal_col);
        ui::draw_text_centered(renderer, menu_font, options[i], col, cx, menu_y);
        menu_y += line_h + 16;
    }

    // Hint
    SDL_Color hint_col = {70, 65, 60, 255};
    ui::draw_text_centered(renderer, body, "[Up/Down] select   [Enter] confirm", hint_col, cx, h - 30);
}
