#include <algorithm>
#include "ui/settings_screen.h"
#include "ui/ui_draw.h"
#include "core/audio.h"
#include <cstdio>

bool SettingsScreen::handle_input(SDL_Event& event, SDL_Window* window) {
    if (event.type != SDL_KEYDOWN) return false;

    // Keybinds sub-screen — any key closes it
    if (keybinds_open_) {
        keybinds_open_ = false;
        return true;
    }

    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return true;
        case SDLK_DOWN:
        case SDLK_j:
            if (selected_ < OPTION_COUNT - 1) selected_++;
            return true;

        case SDLK_LEFT:
        case SDLK_h:
            if (selected_ == 0) {
                resolution_index_ = (resolution_index_ - 1 + RES_COUNT) % RES_COUNT;
            } else if (selected_ == 1) {
                if (sfx_volume_ > 0) sfx_volume_ -= 5;
                if (audio_) audio_->set_volume(sfx_volume_);
            } else if (selected_ == 2) {
                if (music_volume_ > 0) music_volume_ -= 5;
                if (audio_) audio_->set_music_volume(music_volume_);
            } else if (selected_ == 3) {
                scale_index_ = (scale_index_ - 1 + SCALE_COUNT) % SCALE_COUNT;
                scale_changed_ = true;
            }
            return true;

        case SDLK_RIGHT:
        case SDLK_l:
            if (selected_ == 0) {
                resolution_index_ = (resolution_index_ + 1) % RES_COUNT;
            } else if (selected_ == 1) {
                if (sfx_volume_ < 100) sfx_volume_ += 5;
                if (audio_) audio_->set_volume(sfx_volume_);
            } else if (selected_ == 2) {
                if (music_volume_ < 100) music_volume_ += 5;
                if (audio_) audio_->set_music_volume(music_volume_);
            } else if (selected_ == 3) {
                scale_index_ = (scale_index_ + 1) % SCALE_COUNT;
                scale_changed_ = true;
            }
            return true;

        case SDLK_RETURN:
        case SDLK_e:
            if (selected_ == 0) {
                auto& res = RESOLUTIONS[resolution_index_];
                if (res.w == 0) {
                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else {
                    SDL_SetWindowFullscreen(window, 0);
                    SDL_SetWindowSize(window, res.w, res.h);
                    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                }
                return true;
            } else if (selected_ == 4) {
                keybinds_open_ = true;
                return true;
            } else if (selected_ == 5) {
                should_close_ = true;
                return true;
            }
            return true;

        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            should_close_ = true;
            return true;

        default:
            return false;
    }
}

void SettingsScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                             int w, int h) const {
    SDL_SetRenderDrawColor(renderer, 18, 20, 28, 255);
    SDL_RenderClear(renderer);

    if (!font) return;

    if (keybinds_open_) {
        render_keybinds(renderer, font, w, h);
        return;
    }

    int cx = w / 2;
    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color value_col = {180, 175, 170, 255};

    ui::draw_text_centered(renderer, font, "Settings", title_col, cx, 40);

    int panel_w = 500;
    int panel_x = cx - panel_w / 2;
    int panel_y = 80;
    int panel_h = h - 140;

    ui::draw_panel(renderer, panel_x, panel_y, panel_w, panel_h);

    int y = panel_y + 20;
    int label_x = panel_x + 20;
    int val_x = panel_x + panel_w - 20;

    auto draw_option_bg = [&](int idx) {
        if (selected_ == idx) {
            SDL_Rect hl = {panel_x + 4, y - 2, panel_w - 8, line_h + 4};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }
    };

    auto sel_or = [&](int idx, SDL_Color def) -> SDL_Color {
        return selected_ == idx ? sel_col : def;
    };

    // Resolution
    draw_option_bg(0);
    ui::draw_text(renderer, font, "Resolution", sel_or(0, normal_col), label_x, y);
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "< %s >", RESOLUTIONS[resolution_index_].label);
        int tw = 0, th = 0;
        TTF_SizeText(font, buf, &tw, &th);
        ui::draw_text(renderer, font, buf, sel_or(0, value_col), val_x - tw, y);
    }
    y += line_h + 16;

    // SFX Volume
    draw_option_bg(1);
    ui::draw_text(renderer, font, "SFX Volume", sel_or(1, normal_col), label_x, y);
    {
        int bar_w = 120, bar_h = 12;
        int bar_x = val_x - bar_w - 50;
        int bar_y_ = y + (line_h - bar_h) / 2;
        SDL_Rect bar_bg = {bar_x, bar_y_, bar_w, bar_h};
        SDL_SetRenderDrawColor(renderer, 40, 35, 50, 255);
        SDL_RenderFillRect(renderer, &bar_bg);
        SDL_Rect bar_fill = {bar_x, bar_y_, (sfx_volume_ * bar_w) / 100, bar_h};
        SDL_SetRenderDrawColor(renderer, 140, 120, 160, 255);
        SDL_RenderFillRect(renderer, &bar_fill);
        SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
        SDL_RenderDrawRect(renderer, &bar_bg);
        char vol_buf[16]; snprintf(vol_buf, sizeof(vol_buf), "%d%%", sfx_volume_);
        int tw = 0, th = 0; TTF_SizeText(font, vol_buf, &tw, &th);
        ui::draw_text(renderer, font, vol_buf, sel_or(1, value_col), val_x - tw, y);
    }
    y += line_h + 16;

    // Music Volume
    draw_option_bg(2);
    ui::draw_text(renderer, font, "Music Volume", sel_or(2, normal_col), label_x, y);
    {
        int bar_w = 120, bar_h = 12;
        int bar_x = val_x - bar_w - 50;
        int bar_y_ = y + (line_h - bar_h) / 2;
        SDL_Rect bar_bg = {bar_x, bar_y_, bar_w, bar_h};
        SDL_SetRenderDrawColor(renderer, 40, 35, 50, 255);
        SDL_RenderFillRect(renderer, &bar_bg);
        SDL_Rect bar_fill = {bar_x, bar_y_, (music_volume_ * bar_w) / 100, bar_h};
        SDL_SetRenderDrawColor(renderer, 100, 140, 160, 255);
        SDL_RenderFillRect(renderer, &bar_fill);
        SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
        SDL_RenderDrawRect(renderer, &bar_bg);
        char vol_buf[16]; snprintf(vol_buf, sizeof(vol_buf), "%d%%", music_volume_);
        int tw = 0, th = 0; TTF_SizeText(font, vol_buf, &tw, &th);
        ui::draw_text(renderer, font, vol_buf, sel_or(2, value_col), val_x - tw, y);
    }
    y += line_h + 16;

    // UI Scale
    draw_option_bg(3);
    ui::draw_text(renderer, font, "UI Scale", sel_or(3, normal_col), label_x, y);
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "< %s >", SCALE_LABELS[scale_index_]);
        int tw = 0, th = 0;
        TTF_SizeText(font, buf, &tw, &th);
        ui::draw_text(renderer, font, buf, sel_or(3, value_col), val_x - tw, y);
    }
    y += line_h + 16;

    // Keybinds
    draw_option_bg(4);
    ui::draw_text(renderer, font, "Keybinds", sel_or(4, normal_col), label_x, y);
    {
        int tw = 0, th = 0;
        TTF_SizeText(font, "View >", &tw, &th);
        ui::draw_text(renderer, font, "View >", sel_or(4, dim_col), val_x - tw, y);
    }
    y += line_h + 16;

    // Back
    draw_option_bg(5);
    ui::draw_text(renderer, font, "Back", sel_or(5, normal_col), label_x, y);

    // Hints
    ui::draw_text_centered(renderer, font,
        "[Up/Down] select  [Left/Right] adjust  [Enter] apply  [Esc] back",
        dim_col, cx, h - 40);
}

void SettingsScreen::render_keybinds(SDL_Renderer* renderer, TTF_Font* font,
                                      int w, int h) const {
    int cx = w / 2;
    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color key_col = {200, 190, 170, 255};
    SDL_Color desc_col = {140, 135, 130, 255};
    SDL_Color dim_col = {100, 95, 90, 255};

    ui::draw_text_centered(renderer, font, "Keybinds", title_col, cx, 30);

    int panel_w = std::min(w * 2 / 3, 900);
    int panel_x = cx - panel_w / 2;
    int panel_y = h / 12;
    int panel_h = h - 110;

    ui::draw_panel(renderer, panel_x, panel_y, panel_w, panel_h);

    int y = panel_y + 16;
    int col1 = panel_x + 20;            // key column
    int col2 = panel_x + panel_w / 2;   // description column

    struct Bind { const char* key; const char* desc; };

    // Movement
    ui::draw_text(renderer, font, "-- Movement --", dim_col, col1, y);
    y += line_h + 4;

    Bind movement[] = {
        {"Arrow keys / hjkl",   "Move (cardinal)"},
        {"yubn / Numpad",       "Move (diagonal)"},
        {". / Numpad 5",        "Wait one turn"},
    };
    for (auto& b : movement) {
        ui::draw_text(renderer, font, b.key, key_col, col1, y);
        ui::draw_text(renderer, font, b.desc, desc_col, col2, y);
        y += line_h + 2;
    }

    y += 8;
    ui::draw_text(renderer, font, "-- Actions --", dim_col, col1, y);
    y += line_h + 4;

    Bind actions[] = {
        {"g / ,",        "Pick up item"},
        {"Enter / >",    "Descend stairs"},
        {"i",            "Inventory"},
        {"z",            "Spellbook"},
        {"c",            "Character sheet"},
        {"F11",          "Toggle fullscreen"},
        {"F12",          "Screenshot"},
    };
    for (auto& b : actions) {
        ui::draw_text(renderer, font, b.key, key_col, col1, y);
        ui::draw_text(renderer, font, b.desc, desc_col, col2, y);
        y += line_h + 2;
    }

    y += 8;
    ui::draw_text(renderer, font, "-- Inventory --", dim_col, col1, y);
    y += line_h + 4;

    Bind inv[] = {
        {"e / Enter",    "Equip / unequip"},
        {"u",            "Use (eat/drink)"},
        {"d",            "Drop item"},
        {"a-z",          "Quick select"},
    };
    for (auto& b : inv) {
        ui::draw_text(renderer, font, b.key, key_col, col1, y);
        ui::draw_text(renderer, font, b.desc, desc_col, col2, y);
        y += line_h + 2;
    }

    y += 8;
    ui::draw_text(renderer, font, "-- Menus --", dim_col, col1, y);
    y += line_h + 4;

    Bind menus[] = {
        {"Esc",          "Pause / back"},
        {"Enter",        "Confirm"},
        {"PageUp/Down",  "Scroll message log"},
    };
    for (auto& b : menus) {
        ui::draw_text(renderer, font, b.key, key_col, col1, y);
        ui::draw_text(renderer, font, b.desc, desc_col, col2, y);
        y += line_h + 2;
    }

    ui::draw_text_centered(renderer, font, "Press any key to return.",
                            dim_col, cx, h - 40);
}
