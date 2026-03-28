#include "ui/main_menu.h"
#include "ui/ui_draw.h"

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
                    case 2: return MenuChoice::SETTINGS;
                    case 3: return MenuChoice::QUIT;
                }
            } else {
                switch (selected_) {
                    case 0: return MenuChoice::NEW_GAME;
                    case 1: return MenuChoice::SETTINGS;
                    case 2: return MenuChoice::QUIT;
                }
            }
            return MenuChoice::NONE;
        case SDLK_ESCAPE:
            return MenuChoice::QUIT;
        default:
            return MenuChoice::NONE;
    }
}

void MainMenu::render(SDL_Renderer* renderer, TTF_Font* body, [[maybe_unused]] TTF_Font* title,
                       TTF_Font* title_large, const SpriteManager& sprites, int w, int h) const {
    // Dark background
    SDL_SetRenderDrawColor(renderer, 18, 20, 28, 255);
    SDL_RenderClear(renderer);

    int cx = w / 2;

    // Layout: spread title, fire, and menu evenly across the screen height
    // Title at ~15% from top, fire centered at ~42%, menu at ~68%
    SDL_Color title_col = {200, 180, 160, 255};
    int title_y = h * 12 / 100;
    ui::draw_text_centered(renderer, title_large, "Reliquary", title_col, cx, title_y);

    // Animated campfire — fire pit (lit) = animated-tiles.png row 3, 6 frames
    int fire_frame = static_cast<int>((SDL_GetTicks() / 150) % 6);
    int fire_size = 128;
    int fire_cx = cx;
    int fire_cy = h * 42 / 100; // vertical center of fire
    int fire_x = fire_cx - fire_size / 2;
    int fire_y = fire_cy - fire_size / 2;

    sprites.draw_sprite_sized(renderer, SHEET_ANIMATED, fire_frame, 3,
                               fire_x, fire_y, fire_size);

    // Warm glow centered on the fire
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

    // Menu options
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};

    int count = option_count();
    const char* opts_with_continue[] = {"New Game", "Continue", "Settings", "Quit"};
    const char* opts_no_continue[]   = {"New Game", "Settings", "Quit"};
    const char** options = can_continue_ ? opts_with_continue : opts_no_continue;

    int line_h = body ? TTF_FontLineSkip(body) : 20;
    int menu_y = h * 68 / 100;

    for (int i = 0; i < count; i++) {
        bool is_sel = (i == selected_);

        if (is_sel) {
            int tw = 0, th = 0;
            if (body) TTF_SizeText(body, options[i], &tw, &th);
            SDL_Rect hl = {cx - tw / 2 - 10, menu_y - 2, tw + 20, line_h + 4};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        ui::draw_text_centered(renderer, body, options[i], is_sel ? sel_col : normal_col, cx, menu_y);
        menu_y += line_h + 12;
    }

    // Controls hint at bottom
    SDL_Color hint_col = {70, 65, 60, 255};
    ui::draw_text_centered(renderer, body, "[Up/Down] select   [Enter] confirm", hint_col, cx, h - 40);
}
