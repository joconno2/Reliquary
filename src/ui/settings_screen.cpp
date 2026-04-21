#include <algorithm>
#include "ui/settings_screen.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
#include "core/audio.h"
#include <cstdio>

bool SettingsScreen::handle_input(SDL_Event& event, SDL_Window* window) {
    if (event.type != SDL_KEYDOWN) return false;

    // Keybind rebinding: waiting for a key press
    if (keybinds_open_ && rebind_action_ != Action::COUNT) {
        auto sym = event.key.keysym.sym;
        if (sym == SDLK_ESCAPE) {
            // Cancel rebind
            rebind_action_ = Action::COUNT;
        } else if (sym == SDLK_BACKSPACE || sym == SDLK_DELETE) {
            // Clear all bindings for this action
            if (keybinds_) keybinds_->get(rebind_action_).clear();
            rebind_action_ = Action::COUNT;
        } else {
            // Bind the pressed key
            if (keybinds_) {
                keybinds_->rebind(rebind_action_, sym);
                keybinds_->save("save/keybinds.json");
            }
            rebind_action_ = Action::COUNT;
        }
        return true;
    }

    // Keybinds sub-screen: interactive list
    if (keybinds_open_) {
        switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE:
                keybinds_open_ = false;
                return true;
            case SDLK_UP: case SDLK_w: case SDLK_k:
                // Skip unavailable actions when navigating
                { int prev = kb_selected_ - 1;
                  while (prev >= 0 && !action_available(static_cast<Action>(prev))) prev--;
                  if (prev >= 0) kb_selected_ = prev; }
                return true;
            case SDLK_DOWN: case SDLK_s: case SDLK_j:
                { int next = kb_selected_ + 1;
                  while (next < ACTION_COUNT && !action_available(static_cast<Action>(next))) next++;
                  if (next < ACTION_COUNT) kb_selected_ = next; }
                return true;
            case SDLK_RETURN: case SDLK_e:
                // Start rebinding
                rebind_action_ = static_cast<Action>(kb_selected_);
                return true;
            case SDLK_r:
                // Reset selected action to defaults
                if (keybinds_) {
                    keybinds_->reset_action(static_cast<Action>(kb_selected_));
                    keybinds_->save("save/keybinds.json");
                }
                return true;
            case SDLK_F1:
                // Reset ALL to defaults
                if (keybinds_) {
                    keybinds_->reset_all();
                    keybinds_->save("save/keybinds.json");
                }
                return true;
            default:
                return true;
        }
    }

    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_w:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return true;
        case SDLK_DOWN:
        case SDLK_s:
        case SDLK_j:
            if (selected_ < OPTION_COUNT - 1) selected_++;
            return true;

        case SDLK_LEFT:
        case SDLK_a:
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
        case SDLK_d:
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
                kb_selected_ = 0;
                // Skip to first available action
                while (kb_selected_ < ACTION_COUNT && !action_available(static_cast<Action>(kb_selected_)))
                    kb_selected_++;
                kb_scroll_ = 0;
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

    int line_h = TTF_FontLineSkip(font);
    auto screen = ui::Layout::from_screen(w, h, line_h);

    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color value_col = {180, 175, 170, 255};

    // Title above the panel
    auto title_row = screen.row(line_h + screen.gap * 2);
    ui::draw_text_in(renderer, font, "Settings", title_col,
                     title_row, ui::Align::CENTER);

    // Reserve hint row at bottom
    auto hint_area = screen.row_bottom(line_h + screen.gap * 2);

    // Panel: 2/3 width, remaining height
    auto outer = screen.panel_outer(2, 3, 1, 1);
    outer.y = screen.cursor.y;
    outer.h = screen.cursor.h;
    auto panel = ui::draw_panel_in(renderer, outer, line_h);

    auto sel_or = [&](int idx, SDL_Color def) -> SDL_Color {
        return selected_ == idx ? sel_col : def;
    };

    auto draw_option_row = [&](int idx) -> ui::Rect {
        auto row = panel.row(line_h + panel.gap);
        if (selected_ == idx) {
            auto hl = row.inset(4, -2);
            auto sdl = hl.sdl();
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &sdl);
        }
        panel.skip(panel.gap);
        return row;
    };

    // Resolution
    {
        auto row = draw_option_row(0);
        auto cols = ui::Layout::from_rect(row, line_h).split_cols_ratio(2, 3);
        ui::draw_text_in(renderer, font, "Resolution", sel_or(0, normal_col),
                         cols[0], ui::Align::LEFT);
        char buf[64];
        snprintf(buf, sizeof(buf), "< %s >", RESOLUTIONS[resolution_index_].label);
        ui::draw_text_in(renderer, font, buf, sel_or(0, value_col),
                         cols[1], ui::Align::RIGHT);
    }

    // SFX Volume
    {
        auto row = draw_option_row(1);
        auto cols = ui::Layout::from_rect(row, line_h).split_cols_ratio(2, 3);
        ui::draw_text_in(renderer, font, "SFX Volume", sel_or(1, normal_col),
                         cols[0], ui::Align::LEFT);

        // Volume bar: 1/3 of panel width
        int bar_w = panel.bounds.w / 3;
        int bar_h = std::max(8, line_h * 2 / 3);
        int bar_x = cols[1].x;
        int bar_y = cols[1].y + (cols[1].h - bar_h) / 2;
        SDL_Rect bar_bg = {bar_x, bar_y, bar_w, bar_h};
        SDL_SetRenderDrawColor(renderer, 40, 35, 50, 255);
        SDL_RenderFillRect(renderer, &bar_bg);
        SDL_Rect bar_fill = {bar_x, bar_y, (sfx_volume_ * bar_w) / 100, bar_h};
        SDL_SetRenderDrawColor(renderer, 140, 120, 160, 255);
        SDL_RenderFillRect(renderer, &bar_fill);
        SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
        SDL_RenderDrawRect(renderer, &bar_bg);

        char vol_buf[16]; snprintf(vol_buf, sizeof(vol_buf), "%d%%", sfx_volume_);
        auto pct_area = ui::Rect{bar_x + bar_w + panel.gap, cols[1].y,
                                  cols[1].x2() - (bar_x + bar_w + panel.gap), cols[1].h};
        ui::draw_text_in(renderer, font, vol_buf, sel_or(1, value_col),
                         pct_area, ui::Align::RIGHT);
    }

    // Music Volume
    {
        auto row = draw_option_row(2);
        auto cols = ui::Layout::from_rect(row, line_h).split_cols_ratio(2, 3);
        ui::draw_text_in(renderer, font, "Music Volume", sel_or(2, normal_col),
                         cols[0], ui::Align::LEFT);

        int bar_w = panel.bounds.w / 3;
        int bar_h = std::max(8, line_h * 2 / 3);
        int bar_x = cols[1].x;
        int bar_y = cols[1].y + (cols[1].h - bar_h) / 2;
        SDL_Rect bar_bg = {bar_x, bar_y, bar_w, bar_h};
        SDL_SetRenderDrawColor(renderer, 40, 35, 50, 255);
        SDL_RenderFillRect(renderer, &bar_bg);
        SDL_Rect bar_fill = {bar_x, bar_y, (music_volume_ * bar_w) / 100, bar_h};
        SDL_SetRenderDrawColor(renderer, 100, 140, 160, 255);
        SDL_RenderFillRect(renderer, &bar_fill);
        SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
        SDL_RenderDrawRect(renderer, &bar_bg);

        char vol_buf[16]; snprintf(vol_buf, sizeof(vol_buf), "%d%%", music_volume_);
        auto pct_area = ui::Rect{bar_x + bar_w + panel.gap, cols[1].y,
                                  cols[1].x2() - (bar_x + bar_w + panel.gap), cols[1].h};
        ui::draw_text_in(renderer, font, vol_buf, sel_or(2, value_col),
                         pct_area, ui::Align::RIGHT);
    }

    // UI Scale
    {
        auto row = draw_option_row(3);
        auto cols = ui::Layout::from_rect(row, line_h).split_cols_ratio(2, 3);
        ui::draw_text_in(renderer, font, "UI Scale", sel_or(3, normal_col),
                         cols[0], ui::Align::LEFT);
        char buf[32];
        snprintf(buf, sizeof(buf), "< %s >", SCALE_LABELS[scale_index_]);
        ui::draw_text_in(renderer, font, buf, sel_or(3, value_col),
                         cols[1], ui::Align::RIGHT);
    }

    // Keybinds
    {
        auto row = draw_option_row(4);
        auto cols = ui::Layout::from_rect(row, line_h).split_cols_ratio(2, 3);
        ui::draw_text_in(renderer, font, "Keybinds", sel_or(4, normal_col),
                         cols[0], ui::Align::LEFT);
        ui::draw_text_in(renderer, font, "Edit >", sel_or(4, dim_col),
                         cols[1], ui::Align::RIGHT);
    }

    // Back
    {
        auto row = draw_option_row(5);
        ui::draw_text_in(renderer, font, "Back", sel_or(5, normal_col),
                         row, ui::Align::LEFT);
    }

    // Hints
    { auto* ig = InputGlyphs::get();
      char hbuf[256];
      if (ig && ig->using_gamepad())
          snprintf(hbuf, sizeof(hbuf), "D-Pad select/adjust  %s apply  %s back",
                   ig->confirm().c_str(), ig->cancel().c_str());
      else
          snprintf(hbuf, sizeof(hbuf), "[Up/Down] select  [Left/Right] adjust  [Enter] apply  [Esc] back");
      ui::draw_text_in(renderer, font, hbuf, dim_col, hint_area, ui::Align::CENTER); }
}

void SettingsScreen::render_keybinds(SDL_Renderer* renderer, TTF_Font* font,
                                      int w, int h) const {
    int line_h = TTF_FontLineSkip(font);
    auto screen = ui::Layout::from_screen(w, h, line_h);

    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color key_col = {200, 190, 170, 255};
    SDL_Color desc_col = {140, 135, 130, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color rebind_col = {255, 100, 100, 255};

    // Title above the panel
    auto title_row = screen.row(line_h + screen.gap);
    ui::draw_text_in(renderer, font, "Keybinds", title_col,
                     title_row, ui::Align::CENTER);

    // Reserve hint row at bottom
    auto hint_area = screen.row_bottom(line_h + screen.gap * 2);

    // Panel: 4/5 width, remaining height
    auto outer = screen.panel_outer(4, 5, 1, 1);
    outer.y = screen.cursor.y;
    outer.h = screen.cursor.h;
    auto panel = ui::draw_panel_in(renderer, outer, line_h);

    // Clip to panel
    SDL_Rect clip = panel.cursor.sdl();
    SDL_RenderSetClipRect(renderer, &clip);

    int row_h = line_h + 3;

    // Calculate visible rows and scrolling
    int visible_rows = panel.cursor.h / row_h;
    // Keep selected item visible
    int scroll = kb_scroll_;
    if (kb_selected_ < scroll) scroll = kb_selected_;
    if (kb_selected_ >= scroll + visible_rows) scroll = kb_selected_ - visible_rows + 1;
    // Update mutable scroll through const_cast (scroll is a display hint, not logical state)
    const_cast<SettingsScreen*>(this)->kb_scroll_ = scroll;

    int y = panel.cursor.y;

    for (int i = scroll; i < ACTION_COUNT && y + row_h <= panel.cursor.y2(); i++) {
        Action a = static_cast<Action>(i);
        if (!action_available(a)) continue;
        bool selected = (i == kb_selected_);
        bool rebinding = (rebind_action_ == a);

        // Highlight bar
        if (selected) {
            SDL_Rect hl = {panel.cursor.x, y - 1, panel.cursor.w, row_h};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        // Action name (left side)
        SDL_Color name_col = rebinding ? rebind_col : (selected ? sel_col : desc_col);
        ui::draw_text(renderer, font, action_name(a), name_col,
                      panel.cursor.x + 4, y);

        // Key binding string (right side)
        const char* binding_str;
        char buf[128];
        if (rebinding) {
            binding_str = "[ press a key ]";
        } else if (keybinds_) {
            auto s = keybinds_->binding_string(a);
            snprintf(buf, sizeof(buf), "%s", s.c_str());
            binding_str = buf;
        } else {
            binding_str = "???";
        }

        SDL_Color val_col = rebinding ? rebind_col : (selected ? sel_col : key_col);
        // Right-align the binding string
        int tw = 0;
        TTF_SizeText(font, binding_str, &tw, nullptr);
        ui::draw_text(renderer, font, binding_str, val_col,
                      panel.cursor.x2() - tw - 4, y);

        y += row_h;
    }

    SDL_RenderSetClipRect(renderer, nullptr);

    // Scroll indicators
    if (scroll > 0)
        ui::draw_text_centered(renderer, font, "^ more ^", dim_col, outer.cx(), outer.y + 4);
    if (scroll + visible_rows < ACTION_COUNT)
        ui::draw_text_centered(renderer, font, "v more v", dim_col, outer.cx(), outer.y2() - line_h - 4);

    // Hint at bottom
    { auto* ig = InputGlyphs::get();
      char hbuf[256];
      if (ig && ig->using_gamepad()) {
          if (rebind_action_ != Action::COUNT)
              snprintf(hbuf, sizeof(hbuf), "[Press button to bind]  %s cancel", ig->cancel().c_str());
          else
              snprintf(hbuf, sizeof(hbuf), "%s rebind  %s back",
                       ig->confirm().c_str(), ig->cancel().c_str());
      } else {
          if (rebind_action_ != Action::COUNT)
              snprintf(hbuf, sizeof(hbuf), "[Press key to bind]  [Backspace] clear  [Esc] cancel");
          else
              snprintf(hbuf, sizeof(hbuf), "[Enter] rebind  [R] reset  [F1] reset all  [Esc] back");
      }
      ui::draw_text_in(renderer, font, hbuf, dim_col, hint_area, ui::Align::CENTER); }
}
