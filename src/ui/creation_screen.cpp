#include "ui/creation_screen.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
#include "components/prayer.h"
#include "components/tenet.h"
#include "components/background.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

static const char* RANDOM_NAMES[] = {
    "Aldric", "Brenna", "Caius", "Dagna", "Eryn", "Falk", "Greta", "Hakon",
    "Isolde", "Jareth", "Kael", "Lyra", "Maren", "Nym", "Osric", "Petra",
    "Riven", "Sable", "Thane", "Ulric", "Voss", "Wren", "Xara", "Yorick", "Zara",
    "Asher", "Briar", "Corwin", "Dove", "Esk", "Flint", "Gale", "Holt",
};
static constexpr int NAME_COUNT = sizeof(RANDOM_NAMES) / sizeof(RANDOM_NAMES[0]);

void CreationScreen::randomize_name() {
    unsigned h = static_cast<unsigned>(selected_ * 7 + static_cast<int>(build_.class_id) * 13 +
                                        SDL_GetTicks());
    build_.name = RANDOM_NAMES[h % NAME_COUNT];
}

void CreationScreen::reset() {
    phase_ = CreationPhase::CLASS_SELECT;
    selected_ = 0;
    cancelled_ = false;
    build_ = {};
    bg_screen_.reset();
    trait_screen_.reset();
    randomize_name();
    for (int i = 0; i < CLASS_COUNT; i++)
        class_unlocked_[i] = (i < BASE_CLASS_COUNT);
}

void CreationScreen::randomize(RNG& rng) {
    reset();
    std::vector<int> available;
    for (int i = 0; i < CLASS_COUNT; i++) {
        if (class_unlocked_[i]) available.push_back(i);
    }
    if (!available.empty())
        build_.class_id = static_cast<ClassId>(available[rng.range(0, static_cast<int>(available.size()) - 1)]);
    randomize_name();
    if (rng.chance(10))
        build_.god = GodId::NONE;
    else
        build_.god = static_cast<GodId>(rng.range(0, GOD_COUNT - 1));
    build_.background = static_cast<BackgroundId>(rng.range(0, BACKGROUND_COUNT - 1));
    build_.traits.clear();
    int num_traits = rng.range(1, 3);
    for (int t = 0; t < num_traits; t++) {
        TraitId tid = static_cast<TraitId>(rng.range(0, TRAIT_COUNT - 1));
        bool dup = false;
        for (auto existing : build_.traits) { if (existing == tid) { dup = true; break; } }
        if (!dup) build_.traits.push_back(tid);
    }
    build_.hardcore = rng.chance(10);
    phase_ = CreationPhase::DONE;
}

void CreationScreen::set_unlocked(const bool* unlocks, int count) {
    for (int i = 0; i < CLASS_COUNT && i < count; i++)
        class_unlocked_[i] = unlocks[i];
}

void CreationScreen::set_unlock_progress(int class_idx, const char* progress) {
    if (class_idx >= 0 && class_idx < CLASS_COUNT)
        unlock_progress_[class_idx] = progress ? progress : "";
}

bool CreationScreen::handle_input(SDL_Event& event) {
    if (phase_ == CreationPhase::DONE) return false;

    if (phase_ == CreationPhase::BACKGROUND_SELECT) {
        bool consumed = bg_screen_.handle_input(event);
        if (bg_screen_.is_confirmed()) {
            build_.background = bg_screen_.get_selected();
            bg_screen_.reset();
            trait_screen_.reset();
            phase_ = CreationPhase::TRAIT_SELECT;
            return true;
        }
        if (!consumed && event.type == SDL_KEYDOWN &&
            (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_BACKSPACE)) {
            phase_ = CreationPhase::GOD_SELECT;
            selected_ = static_cast<int>(build_.god);
            return true;
        }
        return consumed;
    }

    // === BUILD_SCREEN: combined god + traits + background + name ===
    if (phase_ == CreationPhase::BUILD_SCREEN) {
        if (event.type != SDL_KEYDOWN) return false;
        auto sym = event.key.keysym.sym;

        // Left/Right: switch columns
        if (sym == SDLK_LEFT || sym == SDLK_a) { if (build_column_ > 0) build_column_--; return true; }
        if (sym == SDLK_RIGHT || sym == SDLK_d) { if (build_column_ < 2) build_column_++; return true; }

        // Up/Down: navigate within column
        if (sym == SDLK_UP || sym == SDLK_w) {
            if (build_column_ == 0 && build_god_cursor_ > 0) build_god_cursor_--;
            else if (build_column_ == 1 && build_trait_cursor_ > 0) build_trait_cursor_--;
            else if (build_column_ == 2 && build_bg_cursor_ > 0) build_bg_cursor_--;
            return true;
        }
        if (sym == SDLK_DOWN || sym == SDLK_s) {
            if (build_column_ == 0 && build_god_cursor_ < GOD_COUNT - 1) build_god_cursor_++;
            else if (build_column_ == 1 && build_trait_cursor_ < TRAIT_COUNT - 1) build_trait_cursor_++;
            else if (build_column_ == 2 && build_bg_cursor_ < BACKGROUND_COUNT - 1) build_bg_cursor_++;
            return true;
        }

        // Enter: toggle selection in current column
        if (sym == SDLK_RETURN || sym == SDLK_e) {
            if (build_column_ == 0) {
                // Select god
                build_.god = static_cast<GodId>(build_god_cursor_);
            } else if (build_column_ == 1) {
                // Toggle trait
                TraitId tid = static_cast<TraitId>(build_trait_cursor_);
                auto it = std::find(build_traits_selected_.begin(), build_traits_selected_.end(), tid);
                if (it != build_traits_selected_.end()) {
                    build_traits_selected_.erase(it);
                } else if (static_cast<int>(build_traits_selected_.size()) < 3) {
                    build_traits_selected_.push_back(tid);
                }
            } else if (build_column_ == 2) {
                build_.background = static_cast<BackgroundId>(build_bg_cursor_);
            }
            return true;
        }

        // Space: confirm all and start game
        if (sym == SDLK_SPACE) {
            build_.god = static_cast<GodId>(build_god_cursor_);
            build_.traits = build_traits_selected_;
            build_.background = static_cast<BackgroundId>(build_bg_cursor_);
            build_.hardcore = true; // always hardcore
            if (build_.name.empty()) randomize_name();
            phase_ = CreationPhase::DONE;
            return true;
        }

        // Escape: back to class select
        if (sym == SDLK_ESCAPE || sym == SDLK_BACKSPACE) {
            phase_ = CreationPhase::CLASS_SELECT;
            return true;
        }
        return false;
    }

    if (phase_ == CreationPhase::TRAIT_SELECT) {
        bool consumed = trait_screen_.handle_input(event);
        if (trait_screen_.is_confirmed()) {
            build_.traits = trait_screen_.get_selected_traits();
            build_.hardcore = true; // always hardcore
            phase_ = CreationPhase::DONE;
            return true;
        }
        if (!consumed && event.type == SDL_KEYDOWN &&
            (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_BACKSPACE)) {
            trait_screen_.reset();
            bg_screen_.reset();
            phase_ = CreationPhase::BACKGROUND_SELECT;
            return true;
        }
        return consumed;
    }

    if (phase_ == CreationPhase::HARDCORE_SELECT) {
        if (event.type != SDL_KEYDOWN) return false;
        auto sym = event.key.keysym.sym;
        if (sym == SDLK_LEFT || sym == SDLK_RIGHT || sym == SDLK_h || sym == SDLK_l
            || sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_k || sym == SDLK_j) {
            build_.hardcore = !build_.hardcore;
            return true;
        }
        if (sym == SDLK_RETURN || sym == SDLK_e) {
            phase_ = CreationPhase::DONE;
            return true;
        }
        if (sym == SDLK_ESCAPE || sym == SDLK_BACKSPACE) {
            trait_screen_.reset();
            phase_ = CreationPhase::TRAIT_SELECT;
            return true;
        }
        return true;
    }

    if (event.type != SDL_KEYDOWN) return false;

    if (phase_ == CreationPhase::NAME_ENTRY) {
        auto sym = event.key.keysym.sym;
        if (sym == SDLK_RETURN) {
            if (!build_.name.empty()) {
                phase_ = CreationPhase::GOD_SELECT;
                selected_ = 0;
            }
            return true;
        }
        if (sym == SDLK_ESCAPE || sym == SDLK_BACKSPACE) {
            if (sym == SDLK_BACKSPACE && !build_.name.empty()) {
                build_.name.pop_back();
                return true;
            }
            if (sym == SDLK_ESCAPE) {
                phase_ = CreationPhase::CLASS_SELECT;
                selected_ = static_cast<int>(build_.class_id);
                return true;
            }
        }
        if (sym == SDLK_TAB) {
            randomize_name();
            return true;
        }
        if (build_.name.size() < 20) {
            char c = 0;
            if (sym >= SDLK_a && sym <= SDLK_z) {
                c = 'a' + (sym - SDLK_a);
                if (event.key.keysym.mod & KMOD_SHIFT) c -= 32;
                if (build_.name.empty()) c = static_cast<char>(toupper(c));
            } else if (sym == SDLK_SPACE) {
                c = ' ';
            } else if (sym == SDLK_MINUS) {
                c = '-';
            }
            if (c) {
                build_.name += c;
                return true;
            }
        }
        return true;
    }

    int max_sel = 0;
    if (phase_ == CreationPhase::CLASS_SELECT) max_sel = CLASS_COUNT;
    else if (phase_ == CreationPhase::GOD_SELECT) max_sel = GOD_COUNT;

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        if (phase_ == CreationPhase::CLASS_SELECT && grid_cell_w_ > 0 && grid_cell_h_ > 0) {
            int col = (event.button.x - grid_x_) / grid_cell_w_;
            int row = (event.button.y - grid_y_) / grid_cell_h_;
            int cols = grid_cols_;
            int idx = row * cols + col;
            if (idx >= 0 && idx < CLASS_COUNT && col >= 0 && col < cols) {
                selected_ = idx;
                if (class_unlocked_[selected_]) {
                    build_.class_id = static_cast<ClassId>(selected_);
                    randomize_name();
                    phase_ = CreationPhase::BUILD_SCREEN;
                }
                return true;
            }
        }
        if (phase_ == CreationPhase::GOD_SELECT) {
            int mx = event.button.x, my = event.button.y;
            for (int i = 0; i < static_cast<int>(god_rects_.size()); i++) {
                auto& r = god_rects_[i];
                if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
                    if (i == selected_) {
                        // Double-click: confirm
                        build_.god = static_cast<GodId>(selected_);
                        phase_ = CreationPhase::BACKGROUND_SELECT;
                        return true;
                    }
                    selected_ = i;
                    return true;
                }
            }
        }
        return false;
    }
    // Mouse hover for god select
    if (event.type == SDL_MOUSEMOTION && phase_ == CreationPhase::GOD_SELECT) {
        int mx = event.motion.x, my = event.motion.y;
        for (int i = 0; i < static_cast<int>(god_rects_.size()); i++) {
            auto& r = god_rects_[i];
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
                selected_ = i; break;
            }
        }
        return false;
    }

    int grid_cols = (phase_ == CreationPhase::CLASS_SELECT) ? grid_cols_ : 1;

    switch (event.key.keysym.sym) {
        case SDLK_UP: case SDLK_w: case SDLK_k:
            if (selected_ - grid_cols >= 0) selected_ -= grid_cols;
            else if (phase_ != CreationPhase::CLASS_SELECT && selected_ > 0) selected_--;
            return true;
        case SDLK_DOWN: case SDLK_s: case SDLK_j:
            if (selected_ + grid_cols < max_sel) selected_ += grid_cols;
            else if (phase_ != CreationPhase::CLASS_SELECT && selected_ < max_sel - 1) selected_++;
            return true;
        case SDLK_LEFT: case SDLK_a: case SDLK_h:
            if (selected_ > 0) selected_--;
            return true;
        case SDLK_RIGHT: case SDLK_d: case SDLK_l:
            if (selected_ < max_sel - 1) selected_++;
            return true;
        case SDLK_RETURN: case SDLK_e:
            if (phase_ == CreationPhase::CLASS_SELECT) {
                if (!class_unlocked_[selected_]) return true;
                build_.class_id = static_cast<ClassId>(selected_);
                randomize_name();
                phase_ = CreationPhase::BUILD_SCREEN;
            } else if (phase_ == CreationPhase::GOD_SELECT) {
                build_.god = static_cast<GodId>(selected_);
                bg_screen_.reset();
                phase_ = CreationPhase::BACKGROUND_SELECT;
            }
            return true;
        case SDLK_r:
            if (phase_ == CreationPhase::CLASS_SELECT) {
                RNG temp_rng(static_cast<uint64_t>(SDL_GetTicks()));
                randomize(temp_rng);
                return true;
            }
            return false;
        case SDLK_ESCAPE: case SDLK_BACKSPACE:
            if (phase_ == CreationPhase::CLASS_SELECT) {
                cancelled_ = true;
            } else if (phase_ == CreationPhase::GOD_SELECT) {
                phase_ = CreationPhase::BUILD_SCREEN;
            }
            return true;
        default:
            return false;
    }
}

void CreationScreen::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                             const SpriteManager& sprites, int w, int h) const {
    SDL_SetRenderDrawColor(renderer, 8, 6, 10, 255);
    SDL_RenderClear(renderer);

    int cw = (phase_ != CreationPhase::CLASS_SELECT) ? w * 74 / 100 : w;

    if (phase_ == CreationPhase::CLASS_SELECT) {
        render_class_select(renderer, font, font_title, sprites, w, h);
    } else if (phase_ == CreationPhase::BUILD_SCREEN) {
        render_build_screen(renderer, font, font_title, w, h);
    } else if (phase_ == CreationPhase::NAME_ENTRY) {
        render_name_entry(renderer, font, font_title, sprites, cw, h);
    } else if (phase_ == CreationPhase::GOD_SELECT) {
        render_god_select(renderer, font, font_title, sprites, cw, h);
    } else if (phase_ == CreationPhase::BACKGROUND_SELECT) {
        bg_screen_.render(renderer, font, cw, h);
    } else if (phase_ == CreationPhase::TRAIT_SELECT) {
        trait_screen_.render(renderer, font, cw, h);
    } else if (phase_ == CreationPhase::HARDCORE_SELECT) {
        if (!font) return;
        int line_h = TTF_FontLineSkip(font);
        int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;

        auto screen = ui::Layout::from_screen(cw, h, line_h);
        screen.skip(h / 2 - title_h * 2);

        SDL_Color title_col = {200, 180, 160, 255};
        SDL_Color desc_col = {140, 130, 120, 255};
        SDL_Color sel_col = {255, 220, 140, 255};
        SDL_Color dim_col = {100, 95, 90, 255};

        auto title_row = screen.row(title_h + title_h / 2);
        ui::draw_text_in(renderer, font_title, "Game mode", title_col, title_row, ui::Align::CENTER);

        const char* mode = build_.hardcore ? "Hardcore" : "Normal";
        SDL_Color mode_col = build_.hardcore ? SDL_Color{200, 80, 80, 255} : sel_col;
        auto mode_row = screen.row(title_h + line_h);
        ui::draw_text_in(renderer, font_title, mode, mode_col, mode_row, ui::Align::CENTER);

        auto desc_row = screen.row(line_h + line_h);
        if (build_.hardcore) {
            ui::draw_text_in(renderer, font,
                "Permadeath. Save deleted on death. No second chances.",
                desc_col, desc_row, ui::Align::CENTER);
        } else {
            ui::draw_text_in(renderer, font,
                "Standard save and load. Die and try again.",
                desc_col, desc_row, ui::Align::CENTER);
        }

        auto hint_rect = ui::Rect{0, h - line_h - 4, cw, line_h};
        { auto* ig = InputGlyphs::get();
          char hbuf[256];
          if (ig && ig->using_gamepad())
              snprintf(hbuf, sizeof(hbuf), "D-Pad toggle   %s begin", ig->confirm().c_str());
          else
              snprintf(hbuf, sizeof(hbuf), "[Left/Right] toggle   [Enter] begin");
          ui::draw_text_in(renderer, font, hbuf, dim_col, hint_rect, ui::Align::CENTER); }
    }

    if (phase_ != CreationPhase::CLASS_SELECT) {
        render_character_preview(renderer, font, font_title, sprites, w, h);
    }
}

void CreationScreen::render_class_select(SDL_Renderer* renderer, TTF_Font* font,
                                          TTF_Font* font_title,
                                          const SpriteManager& sprites, int w, int h) const {
    if (!font) return;
    int line_h = TTF_FontLineSkip(font);
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;
    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color desc_col = {140, 130, 120, 255};
    SDL_Color lock_col = {80, 70, 65, 255};

    auto screen = ui::Layout::from_screen(w, h, line_h);

    // Title
    auto title_row = screen.row(title_h + screen.gap);
    ui::draw_text_in(renderer, font_title, "Reliquary", title_col, title_row, ui::Align::CENTER);

    // Reserve bottom for class info + hints
    auto hint_row = screen.row_bottom(line_h + 4);
    auto info_area = screen.row_bottom(title_h + line_h * 3 + 16);

    // Grid fills remaining space
    auto grid_rect = screen.cursor;
    int grid_w = grid_rect.w - screen.pad * 2;
    int grid_h = grid_rect.h;

    // Compute columns from available width: minimum cell width ~100px
    int min_cell = std::max(80, line_h * 6);
    int cols = std::max(4, grid_w / min_cell);
    if (cols > 8) cols = 8;
    int rows = (CLASS_COUNT + cols - 1) / cols;

    int cell_w = grid_w / cols;
    int cell_h = grid_h / rows;
    int sprite_sz = std::min(cell_w - 10, cell_h - line_h - 10) * 55 / 100;
    if (sprite_sz < 24) sprite_sz = 24;
    if (sprite_sz > 160) sprite_sz = 160;

    int grid_x = grid_rect.x + screen.pad;
    int grid_y = grid_rect.y;

    // Cache for mouse hit-testing
    grid_x_ = grid_x; grid_y_ = grid_y;
    grid_cell_w_ = cell_w; grid_cell_h_ = cell_h;
    grid_cols_ = cols;

    for (int i = 0; i < CLASS_COUNT; i++) {
        int col = i % cols;
        int row = i / cols;
        auto& c = get_class_info(static_cast<ClassId>(i));
        bool is_sel = (i == selected_);
        bool unlocked = class_unlocked_[i];

        int cx = grid_x + col * cell_w + cell_w / 2;
        int cy = grid_y + row * cell_h;

        int pad = 6;
        int box_w = sprite_sz + pad * 2;
        int box_h = sprite_sz + title_h + pad * 3;
        int box_x = cx - box_w / 2;
        int box_y = cy + (cell_h - box_h) / 2;

        if (is_sel) {
            ui::draw_panel(renderer, box_x, box_y, box_w, box_h);
        }

        int sx = cx - sprite_sz / 2;
        int sy = box_y + pad;
        SDL_Color tint = unlocked ? SDL_Color{255,255,255,255} : SDL_Color{50,45,40,255};
        sprites.draw_sprite_sized(renderer, c.sprite_sheet, c.sprite_x, c.sprite_y,
                                   sx, sy, sprite_sz, tint);

        SDL_Color ncol = unlocked ? (is_sel ? sel_col : normal_col) : lock_col;
        SDL_Rect cell_clip = {grid_x + col * cell_w, sy + sprite_sz, cell_w, cell_h - sprite_sz - pad};
        SDL_RenderSetClipRect(renderer, &cell_clip);
        ui::draw_text_centered(renderer, font,
                                unlocked ? c.name : "???", ncol,
                                cx, sy + sprite_sz + 2);
        SDL_RenderSetClipRect(renderer, nullptr);
    }

    // Bottom info area: selected class details
    auto& cls = get_class_info(static_cast<ClassId>(selected_));
    bool sel_unlocked = class_unlocked_[selected_];
    auto info = ui::Layout::from_rect(info_area, line_h);

    if (sel_unlocked) {
        auto name_row = info.row(title_h + 2);
        ui::draw_text_in(renderer, font_title, cls.name, sel_col, name_row, ui::Align::CENTER);

        // Primary ability
        auto desc_row_area = info.row(line_h + 2);
        ui::draw_text_in(renderer, font, cls.description, desc_col, desc_row_area, ui::Align::CENTER);

        // Gear synergy + Level 5 ability
        auto details = get_class_details(static_cast<ClassId>(selected_));
        SDL_Color gear_col = {180, 200, 140, 255};
        SDL_Color lv5_col = {200, 180, 255, 255};
        SDL_Color tip_col = {160, 155, 140, 255};
        if (details.gear_synergy[0]) {
            auto gear_row = info.row(line_h + 1);
            ui::draw_text_in(renderer, font, details.gear_synergy, gear_col, gear_row, ui::Align::CENTER);
        }
        if (details.level5_ability[0]) {
            auto lv5_row = info.row(line_h + 1);
            ui::draw_text_in(renderer, font, details.level5_ability, lv5_col, lv5_row, ui::Align::CENTER);
        }
        if (details.scaling_tip[0]) {
            auto tip_row = info.row(line_h + 1);
            ui::draw_text_in(renderer, font, details.scaling_tip, tip_col, tip_row, ui::Align::CENTER);
        }

        // Stats
        char stat_buf[256];
        snprintf(stat_buf, sizeof(stat_buf),
            "STR:%d  DEX:%d  CON:%d  INT:%d  WIL:%d  PER:%d  |  HP:%d  MP:%d",
            cls.str, cls.dex, cls.con, cls.intel, cls.wil, cls.per, cls.hp, cls.mp);
        auto stat_row = info.row();
        ui::draw_text_in(renderer, font, stat_buf, {180, 175, 170, 255}, stat_row, ui::Align::CENTER);
    } else {
        auto name_row = info.row(title_h + 2);
        ui::draw_text_in(renderer, font_title, cls.name, {120, 100, 80, 255}, name_row, ui::Align::CENTER);
        if (cls.unlock_hint) {
            auto hint_r = info.row(line_h + 2);
            ui::draw_text_in(renderer, font, cls.unlock_hint, {160, 140, 100, 255}, hint_r, ui::Align::CENTER);
        }
        if (!unlock_progress_[selected_].empty()) {
            auto prog_row = info.row();
            ui::draw_text_in(renderer, font, unlock_progress_[selected_].c_str(),
                             {200, 180, 100, 255}, prog_row, ui::Align::CENTER);
        }
    }

    { auto* ig = InputGlyphs::get();
      char hbuf[256];
      if (ig && ig->using_gamepad())
          snprintf(hbuf, sizeof(hbuf), "D-Pad browse   %s select   %s random",
                   ig->confirm().c_str(), ig->label(Action::REST).c_str());
      else
          snprintf(hbuf, sizeof(hbuf), "[Arrows] browse   [Enter] select   [R] random character");
      ui::draw_text_in(renderer, font, hbuf, dim_col, hint_row, ui::Align::CENTER); }
}

void CreationScreen::render_name_entry(SDL_Renderer* renderer, TTF_Font* font,
                                        [[maybe_unused]] TTF_Font* font_title,
                                        const SpriteManager& sprites,
                                        int w, [[maybe_unused]] int h) const {
    if (!font) return;
    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color name_col = {220, 215, 200, 255};

    auto& cls = get_class_info(build_.class_id);

    auto screen = ui::Layout::from_screen(w, h, line_h);
    auto hint_row = screen.row_bottom(line_h + 4);

    // Center content vertically
    int sprite_size = std::min(h / 4, w / 6);
    int total_h = sprite_size + line_h * 4 + 60;
    screen.skip((screen.remaining_h() - total_h) / 2);

    // Sprite
    auto sprite_row = screen.row(sprite_size + 12);
    sprites.draw_sprite_sized(renderer, cls.sprite_sheet, cls.sprite_x, cls.sprite_y,
                               sprite_row.cx() - sprite_size / 2, sprite_row.y, sprite_size);

    // Class name
    auto cls_row = screen.row(line_h + 16);
    ui::draw_text_in(renderer, font, cls.name, dim_col, cls_row, ui::Align::CENTER);

    // Prompt
    auto prompt_row = screen.row(line_h + 12);
    ui::draw_text_in(renderer, font, "Name your character.", title_col, prompt_row, ui::Align::CENTER);

    // Input field: proportional width
    int field_w = w * 2 / 5;
    int field_h = line_h + 16;
    int field_x = w / 2 - field_w / 2;
    int field_y = screen.cursor.y;
    ui::draw_panel(renderer, field_x, field_y, field_w, field_h);

    std::string display = build_.name;
    if ((SDL_GetTicks() / 500) % 2 == 0) display += "_";
    ui::draw_text(renderer, font, display.c_str(), name_col, field_x + 12, field_y + 8);

    { auto* ig = InputGlyphs::get();
      char hbuf[256];
      if (ig && ig->using_gamepad())
          snprintf(hbuf, sizeof(hbuf), "(Y) random name   %s confirm   %s back",
                   ig->confirm().c_str(), ig->cancel().c_str());
      else
          snprintf(hbuf, sizeof(hbuf), "[Tab] random name   [Enter] confirm   [Esc] back");
      ui::draw_text_in(renderer, font, hbuf, dim_col, hint_row, ui::Align::CENTER); }
}

void CreationScreen::render_god_select(SDL_Renderer* renderer, TTF_Font* font,
                                        TTF_Font* font_title,
                                        [[maybe_unused]] const SpriteManager& sprites,
                                        int w, int h) const {
    if (!font) return;
    int line_h = TTF_FontLineSkip(font);
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;
    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color desc_col = {140, 130, 120, 255};

    auto screen = ui::Layout::from_screen(w, h, line_h);

    // Header
    char header[64];
    snprintf(header, sizeof(header), "%s the %s", build_.name.c_str(),
             get_class_info(build_.class_id).name);
    auto hdr1 = screen.row(line_h + 4);
    ui::draw_text_in(renderer, font, header, dim_col, hdr1, ui::Align::CENTER);
    auto hdr2 = screen.row(line_h + screen.gap);
    ui::draw_text_in(renderer, font, "Choose your god.", title_col, hdr2, ui::Align::CENTER);

    // Reserve hint
    auto hint_row = screen.row_bottom(line_h + 4);

    // Content: 80% width, 2:3 split
    auto content_rect = screen.cursor.inset(w / 10, 0);
    auto cols = ui::Layout::from_rect(content_rect, line_h).split_cols_ratio(2, 3);
    auto list_rect = cols[0];
    auto detail_rect = cols[1];

    // God list
    int list_h = list_rect.h;
    int god_item_h = std::max(line_h * 2 + 4, list_h / GOD_COUNT);
    int visible_count = list_h / god_item_h;

    int scroll = 0;
    if (GOD_COUNT > visible_count) {
        scroll = selected_ - visible_count / 2;
        if (scroll < 0) scroll = 0;
        if (scroll > GOD_COUNT - visible_count) scroll = GOD_COUNT - visible_count;
    }

    god_rects_.clear();
    for (int i = scroll; i < GOD_COUNT && i < scroll + visible_count; i++) {
        auto& god = get_god_info(static_cast<GodId>(i));
        bool is_sel = (i == selected_);
        int gy = list_rect.y + (i - scroll) * god_item_h;
        god_rects_.push_back({list_rect.x, gy, list_rect.w, god_item_h});

        if (is_sel) {
            ui::draw_panel(renderer, list_rect.x - 4, gy - 4, list_rect.w + 8, god_item_h - 2);
        }

        SDL_SetRenderDrawColor(renderer, god.color.r, god.color.g, god.color.b, 200);
        SDL_Rect dot = {list_rect.x - 2, gy + 6, 4, line_h - 4};
        SDL_RenderFillRect(renderer, &dot);

        SDL_Rect god_clip = {list_rect.x, gy, list_rect.w, god_item_h};
        SDL_RenderSetClipRect(renderer, &god_clip);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s, %s", god.name, god.title);
        ui::draw_text(renderer, font, buf, is_sel ? sel_col : normal_col, list_rect.x + 6, gy + 4);
        snprintf(buf, sizeof(buf), "  %s", god.domain);
        ui::draw_text(renderer, font, buf, dim_col, list_rect.x + 6, gy + line_h + 6);
        SDL_RenderSetClipRect(renderer, nullptr);
    }

    // Detail panel
    auto detail = ui::draw_panel_in(renderer, detail_rect, line_h);
    auto& god = get_god_info(static_cast<GodId>(selected_));

    char title_buf[128];
    snprintf(title_buf, sizeof(title_buf), "%s, %s", god.name, god.title);
    auto tn = detail.row(title_h + 8);
    ui::draw_text_clipped(renderer, font_title, title_buf, sel_col, tn.x, tn.y, tn.w);

    int dh = ui::text_wrapped_height(font, god.description, detail.cursor.w - detail.pad);
    auto dr = detail.row(dh + 8);
    ui::draw_text_wrapped(renderer, font, god.description, desc_col, dr.x, dr.y, dr.w - detail.pad);

    // Passive
    SDL_Color passive_col = {180, 170, 130, 255};
    auto pl = detail.row(line_h + 2);
    ui::draw_text(renderer, font, "Passive:", dim_col, pl.x, pl.y);
    int pdh = ui::text_wrapped_height(font, god.passive_desc, detail.cursor.w - detail.pad);
    auto pd = detail.row(pdh + 8);
    ui::draw_text_wrapped(renderer, font, god.passive_desc, passive_col, pd.x, pd.y, pd.w - detail.pad);

    // Prayers
    const PrayerInfo* prayers = get_prayers(static_cast<GodId>(selected_));
    if (prayers) {
        auto prh = detail.row(line_h + 2);
        ui::draw_text(renderer, font, "Prayers:", dim_col, prh.x, prh.y);
        for (int p = 0; p < 2; p++) {
            char pbuf[128];
            snprintf(pbuf, sizeof(pbuf), "  %s (%d favor)", prayers[p].name, prayers[p].favor_cost);
            auto pr = detail.row();
            ui::draw_text_clipped(renderer, font, pbuf, passive_col, pr.x, pr.y, pr.w);
            char pdbuf[128];
            snprintf(pdbuf, sizeof(pdbuf), "    %s", prayers[p].description);
            auto pdr = detail.row(line_h + 2);
            ui::draw_text_clipped(renderer, font, pdbuf, dim_col, pdr.x, pdr.y, pdr.w);
        }
        detail.skip(4);
    }

    // Tenets
    auto tenets = get_god_tenets(static_cast<GodId>(selected_));
    if (tenets.count > 0) {
        SDL_Color tenet_col = {200, 140, 140, 255};
        auto th = detail.row(line_h + 2);
        ui::draw_text(renderer, font, "Tenets:", dim_col, th.x, th.y);
        for (int t = 0; t < tenets.count; t++) {
            if (!detail.fits_row()) break;
            char tbuf[160];
            snprintf(tbuf, sizeof(tbuf), "  %s", tenets.tenets[t].description);
            auto tr = detail.row(line_h + 1);
            ui::draw_text_clipped(renderer, font, tbuf, tenet_col, tr.x, tr.y, tr.w);
        }
        detail.skip(4);
    }

    // Blessings
    if (detail.fits_row()) {
        auto bl = detail.row(line_h + 2);
        ui::draw_text(renderer, font, "Blessings:", dim_col, bl.x, bl.y);

        struct BonusPair { const char* name; int val; };
        BonusPair bonuses[] = {
            {"STR", god.str_bonus}, {"DEX", god.dex_bonus}, {"CON", god.con_bonus},
            {"INT", god.int_bonus}, {"WIL", god.wil_bonus}, {"PER", god.per_bonus},
            {"CHA", god.cha_bonus}, {"HP", god.bonus_hp}, {"MP", god.bonus_mp},
            {"FOV", god.fov_bonus},
        };
        for (auto& b : bonuses) {
            if (b.val == 0 || !detail.fits_row()) continue;
            char buf2[32];
            snprintf(buf2, sizeof(buf2), "  %s %+d", b.name, b.val);
            SDL_Color col = b.val > 0 ? SDL_Color{120, 200, 120, 255} : SDL_Color{200, 120, 120, 255};
            auto br = detail.row();
            ui::draw_text(renderer, font, buf2, col, br.x, br.y);
        }
    }

    { auto* ig = InputGlyphs::get();
      char hbuf[256];
      if (ig && ig->using_gamepad())
          snprintf(hbuf, sizeof(hbuf), "%s select   D-Pad browse   %s back",
                   ig->confirm().c_str(), ig->cancel().c_str());
      else
          snprintf(hbuf, sizeof(hbuf), "[Enter] select   [Up/Down] browse   [Esc] back");
      ui::draw_text_in(renderer, font, hbuf, dim_col, hint_row, ui::Align::CENTER); }
}

void CreationScreen::render_character_preview(SDL_Renderer* renderer, TTF_Font* font,
                                               [[maybe_unused]] TTF_Font* font_title,
                                               const SpriteManager& sprites,
                                               int w, int h) const {
    if (!font) return;
    int line_h = TTF_FontLineSkip(font);

    // Preview panel on the right 25% of screen
    int panel_w = w * 25 / 100;
    int panel_x = w - panel_w;

    // Solid dark background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 12, 10, 16, 255);
    SDL_Rect bg_rect = {panel_x, 0, panel_w, h};
    SDL_RenderFillRect(renderer, &bg_rect);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    ui::draw_panel(renderer, panel_x + 2, 4, panel_w - 4, h - 8);

    auto preview = ui::Layout::from_rect({panel_x + ui::Layout::PANEL_INSET, ui::Layout::PANEL_INSET,
                                           panel_w - ui::Layout::PANEL_INSET * 2, h - ui::Layout::PANEL_INSET * 2}, line_h);

    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color val_col = {180, 175, 160, 255};
    SDL_Color dim_col = {110, 105, 100, 255};
    SDL_Color green_col = {120, 200, 120, 255};
    SDL_Color red_col = {200, 120, 120, 255};

    auto& cls = get_class_info(build_.class_id);

    GodId preview_god = build_.god;
    if (phase_ == CreationPhase::GOD_SELECT)
        preview_god = static_cast<GodId>(selected_);

    BackgroundId preview_bg = build_.background;
    if (phase_ == CreationPhase::BACKGROUND_SELECT)
        preview_bg = bg_screen_.get_selected();

    // Name + class
    auto name_row = preview.row(line_h + 2);
    ui::draw_text_in(renderer, font, build_.name.c_str(), title_col, name_row, ui::Align::CENTER);
    auto cls_row = preview.row(line_h + 6);
    ui::draw_text_in(renderer, font, cls.name, val_col, cls_row, ui::Align::CENTER);

    // Character sprite with god effects
    int SS = std::min(preview.cursor.w - 20, h * 35 / 100);
    int sx = preview.cursor.cx() - SS / 2;
    int sy = preview.cursor.y;
    int scx = sx + SS / 2;
    int scy = sy + SS / 2;

    auto& ginfo = get_god_info(preview_god);
    Uint32 ticks = SDL_GetTicks();
    float t = ticks / 1000.0f;
    uint8_t gr = ginfo.color.r, gg = ginfo.color.g, gb = ginfo.color.b;

    // God visual effects (same as before, using SS for scaling)
    if (preview_god != GodId::NONE) {
        int ds = std::max(4, SS / 30);
        switch (preview_god) {
        case GodId::VETHRIK:
            for (int i = 0; i < 4; i++) {
                float phase = std::fmod(t * 0.8f + i * 0.25f, 1.0f);
                int mx = scx + static_cast<int>(std::sin(t * 0.5f + i * 1.7f) * SS * 0.3f);
                int my = sy + SS - static_cast<int>(phase * SS * 0.8f);
                int ma = static_cast<int>((1.0f - phase) * 160);
                SDL_SetRenderDrawColor(renderer, 200, 200, 220, static_cast<Uint8>(ma));
                SDL_Rect mote = {mx - ds, my - ds, ds * 2, ds * 2};
                SDL_RenderFillRect(renderer, &mote);
            }
            break;
        case GodId::THESSARKA:
            for (int i = 0; i < 4; i++) {
                float a = t * 1.8f + i * 1.5708f;
                int ox = scx + static_cast<int>(std::cos(a) * SS * 0.45f);
                int oy = scy + static_cast<int>(std::sin(a) * SS * 0.35f);
                SDL_SetRenderDrawColor(renderer, gr, gg, gb, 170);
                SDL_Rect dot = {ox - ds * 2, oy - ds * 2, ds * 4, ds * 4};
                SDL_RenderFillRect(renderer, &dot);
            }
            break;
        case GodId::MORRETH:
            for (int i = 0; i < 3; i++) {
                float phase = std::fmod(t * 2.0f + i * 0.33f, 1.0f);
                if (phase < 0.3f) {
                    int spx = scx + static_cast<int>((phase - 0.15f) * SS * 2.0f * (i % 2 ? 1 : -1));
                    int spy = sy + SS - static_cast<int>(phase * SS * 0.4f);
                    int sa = static_cast<int>((0.3f - phase) * 600);
                    SDL_SetRenderDrawColor(renderer, 220, 180, 120, static_cast<Uint8>(std::min(sa, 200)));
                    SDL_Rect spark = {spx - ds, spy - ds, ds * 2, ds * 2};
                    SDL_RenderFillRect(renderer, &spark);
                }
            }
            break;
        case GodId::YASHKHET: {
            for (int i = 0; i < 5; i++) {
                float phase = std::fmod(t * 0.6f + i * 0.2f, 1.0f);
                int dx2 = scx + static_cast<int>(std::sin(i * 2.3f) * SS * 0.35f);
                int dy2 = sy + static_cast<int>(phase * SS * 1.1f);
                int da = static_cast<int>((1.0f - phase) * 180);
                SDL_SetRenderDrawColor(renderer, 200, 40, 40, static_cast<Uint8>(da));
                SDL_Rect drip = {dx2 - ds / 2, dy2, ds, ds * 3};
                SDL_RenderFillRect(renderer, &drip);
            }
            float hb = std::sin(t * 4.0f);
            if (hb > 0.7f) {
                int exp = SS / 3;
                SDL_SetRenderDrawColor(renderer, 200, 40, 40, 50);
                SDL_Rect beat = {sx - exp, sy - exp, SS + exp * 2, SS + exp * 2};
                SDL_RenderFillRect(renderer, &beat);
            }
            break;
        }
        case GodId::KHAEL:
            for (int i = 0; i < 5; i++) {
                float phase = std::fmod(t * 0.4f + i * 0.2f, 1.0f);
                float angle = i * 1.257f + t * 0.3f;
                int lx = scx + static_cast<int>(std::cos(angle) * phase * SS * 0.6f);
                int ly = scy + static_cast<int>(std::sin(angle) * phase * SS * 0.4f);
                int la = static_cast<int>((1.0f - phase) * 140);
                SDL_SetRenderDrawColor(renderer, 80, 180, 60, static_cast<Uint8>(la));
                SDL_Rect leaf = {lx - ds, ly - ds / 2, ds * 2, ds};
                SDL_RenderFillRect(renderer, &leaf);
            }
            break;
        case GodId::SOLETH: {
            int fl = static_cast<int>(45 + 25 * std::sin(t * 6.0f));
            SDL_SetRenderDrawColor(renderer, 255, 220, 100, static_cast<Uint8>(fl));
            SDL_Rect halo = {scx - SS / 2, sy - SS / 6, SS, SS / 5};
            SDL_RenderFillRect(renderer, &halo);
            for (int i = 0; i < 4; i++) {
                float phase = std::fmod(t * 1.0f + i * 0.25f, 1.0f);
                int ex = scx + static_cast<int>(std::sin(t * 0.7f + i * 2.1f) * SS * 0.3f);
                int ey = sy + SS - static_cast<int>(phase * SS * 0.9f);
                int ea = static_cast<int>((1.0f - phase) * 160);
                SDL_SetRenderDrawColor(renderer, 255, 180, 60, static_cast<Uint8>(ea));
                SDL_Rect ember = {ex - ds, ey - ds, ds * 2, ds * 2};
                SDL_RenderFillRect(renderer, &ember);
            }
            break;
        }
        case GodId::IXUUL: {
            if ((ticks / 50) % 4 == 0) {
                int off = (ticks / 25) % 9 - 4;
                SDL_SetRenderDrawColor(renderer, gr, gg, gb, 90);
                SDL_Rect tear = {sx + off * 4, sy + SS / 3, SS + 6, ds * 2};
                SDL_RenderFillRect(renderer, &tear);
                SDL_Rect tear2 = {sx - off * 5, sy + SS * 2 / 3, SS + 8, ds + 2};
                SDL_RenderFillRect(renderer, &tear2);
            }
            break;
        }
        case GodId::ZHAVEK: {
            SDL_SetRenderDrawColor(renderer, 5, 5, 15, 55);
            int sh_pad = SS / 3;
            SDL_Rect sh = {sx - sh_pad, sy - sh_pad, SS + sh_pad * 2, SS + sh_pad * 2};
            SDL_RenderFillRect(renderer, &sh);
            for (int i = 1; i <= 3; i++) {
                int off = i * SS / 10;
                int alpha = 45 - i * 10;
                SDL_SetRenderDrawColor(renderer, 15, 15, 30, static_cast<Uint8>(alpha));
                SDL_Rect trail = {sx + off, sy + off, SS - off / 2, SS - off / 2};
                SDL_RenderFillRect(renderer, &trail);
            }
            break;
        }
        case GodId::THALARA: {
            for (int i = 0; i < 3; i++) {
                float rp = std::fmod(t * 1.2f + i * 0.33f, 1.0f);
                int rs = static_cast<int>(rp * SS * 0.8f);
                int ra = static_cast<int>((1.0f - rp) * 60);
                SDL_SetRenderDrawColor(renderer, gr, gg, gb, static_cast<Uint8>(ra));
                SDL_Rect ring = {scx - rs / 2, sy + SS - rs / 3, rs, rs * 2 / 3};
                SDL_RenderDrawRect(renderer, &ring);
            }
            break;
        }
        case GodId::OSSREN:
            for (int i = 0; i < 3; i++) {
                float phase = std::fmod(t * 1.5f + i * 0.33f, 1.0f);
                if (phase < 0.4f) {
                    int spx = scx + static_cast<int>(std::sin(i * 3.1f) * SS * 0.3f);
                    int spy = sy + SS - static_cast<int>(phase * SS * 0.5f);
                    int sa = static_cast<int>((0.4f - phase) * 500);
                    SDL_SetRenderDrawColor(renderer, 255, 180, 60, static_cast<Uint8>(std::min(sa, 200)));
                    SDL_Rect spark = {spx - ds, spy - ds, ds * 2, ds * 2};
                    SDL_RenderFillRect(renderer, &spark);
                }
            }
            break;
        case GodId::GATHRUUN:
            for (int i = 0; i < 4; i++) {
                float a = t * 1.2f + i * 1.5708f;
                int ox = scx + static_cast<int>(std::cos(a) * SS * 0.45f);
                int oy = sy + SS - SS / 8 + static_cast<int>(std::sin(a) * SS * 0.06f);
                SDL_SetRenderDrawColor(renderer, 160, 130, 90, 190);
                SDL_Rect peb = {ox - ds * 2, oy - ds, ds * 3, ds * 2};
                SDL_RenderFillRect(renderer, &peb);
            }
            break;
        case GodId::LETHIS: {
            for (int ei = 0; ei < 2; ei++) {
                float ep = std::fmod(t * 0.6f + ei * 1.5f, 3.0f);
                if (ep < 0.6f) {
                    int ea = static_cast<int>((0.6f - std::abs(ep - 0.3f)) * 500);
                    SDL_SetRenderDrawColor(renderer, 200, 160, 240, static_cast<Uint8>(std::min(ea, 220)));
                    float eangle = t * 0.3f + ei * 3.14f;
                    int ex = scx + static_cast<int>(std::cos(eangle) * SS * 0.4f);
                    int ey = scy - SS / 8 + static_cast<int>(std::sin(eangle * 0.7f) * SS * 0.06f);
                    SDL_Rect eye = {ex - ds * 2, ey - ds, ds * 4, ds * 2};
                    SDL_RenderFillRect(renderer, &eye);
                }
            }
            break;
        }
        case GodId::SYTHARA:
            for (int i = 0; i < 6; i++) {
                float phase = std::fmod(t * 0.5f + i * 0.167f, 1.0f);
                float angle = i * 1.047f + t * 0.2f;
                int spx = scx + static_cast<int>(std::cos(angle) * phase * SS * 0.5f);
                int spy = scy + static_cast<int>(std::sin(angle) * phase * SS * 0.4f);
                int sa = static_cast<int>((1.0f - phase) * 140);
                SDL_SetRenderDrawColor(renderer, 100, 160, 40, static_cast<Uint8>(sa));
                SDL_Rect spore = {spx - ds, spy - ds, ds * 2, ds * 2};
                SDL_RenderFillRect(renderer, &spore);
            }
            break;
        default: break;
        }
    }

    // Draw sprite with god tint
    SDL_Color sprite_tint = {255, 255, 255, 255};
    if (preview_god != GodId::NONE) {
        sprite_tint.r = static_cast<Uint8>(255 - (255 - gr) / 4);
        sprite_tint.g = static_cast<Uint8>(255 - (255 - gg) / 4);
        sprite_tint.b = static_cast<Uint8>(255 - (255 - gb) / 4);
    }
    sprites.draw_sprite_sized(renderer, cls.sprite_sheet, cls.sprite_x, cls.sprite_y,
                               sx, sy, SS, sprite_tint, false);

    preview.skip(SS + line_h);

    // God name
    if (phase_ >= CreationPhase::GOD_SELECT) {
        SDL_Color god_col = (preview_god != GodId::NONE)
            ? SDL_Color{gr, gg, gb, 255} : dim_col;
        const char* gname = (preview_god != GodId::NONE) ? ginfo.name : "Godless";
        auto gr2 = preview.row(line_h + 4);
        ui::draw_text_in(renderer, font, gname, god_col, gr2, ui::Align::CENTER);
    }

    // Background name
    if (phase_ >= CreationPhase::BACKGROUND_SELECT) {
        auto& bgi = get_background_info(preview_bg);
        auto bgr = preview.row(line_h + 4);
        ui::draw_text_in(renderer, font, bgi.name, dim_col, bgr, ui::Align::CENTER);
    }

    // Divider
    SDL_SetRenderDrawColor(renderer, 60, 55, 50, 255);
    SDL_RenderDrawLine(renderer, preview.cursor.x, preview.cursor.y,
                       preview.cursor.x2(), preview.cursor.y);
    preview.skip(line_h / 2);

    // Running stat totals
    int str = cls.str, dex = cls.dex, con = cls.con;
    int intel = cls.intel, wil = cls.wil, per = cls.per, cha = cls.cha;
    int hp = cls.hp, mp = cls.mp;
    int fov_bonus = 0;

    if (phase_ >= CreationPhase::GOD_SELECT) {
        auto& gi = get_god_info(preview_god);
        str += gi.str_bonus; dex += gi.dex_bonus; con += gi.con_bonus;
        intel += gi.int_bonus; wil += gi.wil_bonus; per += gi.per_bonus; cha += gi.cha_bonus;
        hp += gi.bonus_hp; mp += gi.bonus_mp;
        fov_bonus = gi.fov_bonus;
    }
    if (phase_ >= CreationPhase::BACKGROUND_SELECT) {
        auto& bgi = get_background_info(preview_bg);
        str += bgi.str_bonus; dex += bgi.dex_bonus; con += bgi.con_bonus;
        intel += bgi.int_bonus; wil += bgi.wil_bonus;
        per += bgi.per_bonus; cha += bgi.cha_bonus;
        hp += bgi.bonus_hp;
    }
    if (phase_ >= CreationPhase::HARDCORE_SELECT) {
        for (TraitId tid : build_.traits) {
            auto& tr = get_trait_info(tid);
            str += tr.str_mod; dex += tr.dex_mod; con += tr.con_mod;
            intel += tr.int_mod; wil += tr.wil_mod; per += tr.per_mod; cha += tr.cha_mod;
        }
    }

    // Two-column stat display
    auto stat_cols = preview.split_cols(2);
    auto left_stats = ui::Layout::col(stat_cols[0], line_h);
    auto right_stats = ui::Layout::col(stat_cols[1], line_h);
    int stat_spacing = line_h + line_h / 3;

    struct StatLine { const char* label; int val; int base; };
    StatLine stats[] = {
        {"STR", str, cls.str}, {"DEX", dex, cls.dex}, {"CON", con, cls.con},
        {"INT", intel, cls.intel}, {"WIL", wil, cls.wil}, {"PER", per, cls.per},
        {"CHA", cha, cls.cha},
    };

    for (int i = 0; i < 7; i++) {
        auto& s = stats[i];
        char buf[32];
        snprintf(buf, sizeof(buf), "%s  %2d", s.label, s.val);
        SDL_Color col = val_col;
        if (s.val > s.base) col = green_col;
        else if (s.val < s.base) col = red_col;
        auto& target = (i % 2 == 0) ? left_stats : right_stats;
        auto row = target.row(stat_spacing);
        ui::draw_text(renderer, font, buf, col, row.x, row.y);
    }

    // Advance preview past stats
    preview.skip(((7 + 1) / 2) * stat_spacing + line_h / 2);

    // HP / MP
    auto hp_mp_cols = preview.split_cols(2);
    char hpbuf[24], mpbuf[24];
    snprintf(hpbuf, sizeof(hpbuf), "HP  %d", hp);
    snprintf(mpbuf, sizeof(mpbuf), "MP  %d", mp);
    ui::draw_text(renderer, font, hpbuf, {200, 100, 100, 255}, hp_mp_cols[0].x, hp_mp_cols[0].y);
    ui::draw_text(renderer, font, mpbuf, {100, 120, 200, 255}, hp_mp_cols[1].x, hp_mp_cols[1].y);
    preview.skip(stat_spacing);

    // FOV
    if (phase_ >= CreationPhase::GOD_SELECT) {
        int fov = 8 + per / 3 + fov_bonus;
        char fov_buf[24];
        snprintf(fov_buf, sizeof(fov_buf), "FOV  %d", fov);
        auto fr = preview.row(stat_spacing);
        ui::draw_text(renderer, font, fov_buf, dim_col, fr.x, fr.y);
    }

    // Resistances
    if (phase_ >= CreationPhase::GOD_SELECT && preview_god != GodId::NONE) {
        struct RL { const char* n; int v; };
        RL res[3] = {{"Fire", 0}, {"Poison", 0}, {"Bleed", 0}};
        switch (preview_god) {
            case GodId::SOLETH: res[0].v = 30; break;
            case GodId::KHAEL: res[1].v = 25; break;
            case GodId::THALARA: res[1].v = 100; res[0].v = -20; break;
            case GodId::SYTHARA: res[1].v = 100; break;
            default: break;
        }
        for (auto& rr : res) {
            if (rr.v != 0 && preview.fits_row()) {
                char rb[24];
                snprintf(rb, sizeof(rb), "%s %+d%%", rr.n, rr.v);
                auto rrow = preview.row(stat_spacing);
                ui::draw_text(renderer, font, rb, rr.v > 0 ? green_col : red_col, rrow.x, rrow.y);
            }
        }
    }

    // Traits
    if (phase_ >= CreationPhase::HARDCORE_SELECT && !build_.traits.empty()) {
        SDL_SetRenderDrawColor(renderer, 60, 55, 50, 255);
        SDL_RenderDrawLine(renderer, preview.cursor.x, preview.cursor.y,
                           preview.cursor.x2(), preview.cursor.y);
        preview.skip(line_h / 2);
        for (TraitId tid : build_.traits) {
            if (!preview.fits_row()) break;
            auto& tr = get_trait_info(tid);
            auto trow = preview.row(line_h + 2);
            SDL_Color trait_col = {220, 200, 140, 255}; // gold for trade-off traits
            ui::draw_text_clipped(renderer, font, tr.name, trait_col,
                                  trow.x + 4, trow.y, trow.w - 4);
        }
    }
}

void CreationScreen::render_build_screen(SDL_Renderer* renderer, TTF_Font* font,
                                          TTF_Font* font_title, int w, int h) const {
    if (!font) return;
    int line_h = TTF_FontLineSkip(font);
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;
    int margin = 12;

    // Layout: right 1/4 = character preview, left 3/4 split into 3 pairs (name+detail)
    // Each pair is 1/8 list + 1/8 description = 1/4 total per category
    int preview_w = w / 4;
    int content_w = w - preview_w;
    int pair_w = content_w / 3; // each god/trait/bg pair gets 1/3 of content area
    int list_w = pair_w / 2;    // left half: names
    int desc_w = pair_w / 2;    // right half: description

    int top_y = margin;
    int avail_h = h - top_y - line_h * 2 - margin;

    SDL_Color sel_col = {255, 240, 140, 255};
    SDL_Color dim_col = {140, 135, 130, 255};
    SDL_Color active_col = {220, 210, 200, 255};
    SDL_Color header_col = {200, 180, 100, 255};
    SDL_Color picked_col = {220, 200, 140, 255};
    SDL_Color desc_col = {170, 165, 155, 255};

    // Vertical separator lines
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 50, 45, 60, 150);
    SDL_RenderDrawLine(renderer, pair_w, top_y, pair_w, h - line_h * 2);
    SDL_RenderDrawLine(renderer, pair_w * 2, top_y, pair_w * 2, h - line_h * 2);
    SDL_RenderDrawLine(renderer, content_w, top_y, content_w, h - line_h * 2);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // Row heights (fill vertical space)
    int god_row_h = std::max(line_h + 1, avail_h / GOD_COUNT);
    int trait_row_h = std::max(line_h + 1, avail_h / TRAIT_COUNT);
    int bg_row_h = std::max(line_h + 1, avail_h / BACKGROUND_COUNT);

    // === PAIR 1: Gods (left 1/8 = names, right 1/8 = description) ===
    int gx = margin;
    SDL_Color hdr0 = build_column_ == 0 ? sel_col : header_col;
    ui::draw_text(renderer, font, "GOD", hdr0, gx, top_y);
    int gy = top_y + line_h + 4;
    for (int i = 0; i < GOD_COUNT; i++) {
        auto& gi = get_god_info(static_cast<GodId>(i));
        bool is_cursor = (build_column_ == 0 && i == build_god_cursor_);
        SDL_Color col = is_cursor ? sel_col : (i == build_god_cursor_) ? active_col : dim_col;
        int y = gy + i * god_row_h;
        if (is_cursor) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_Rect hl = {gx - 2, y, list_w - margin, god_row_h - 1};
            SDL_SetRenderDrawColor(renderer, 35, 30, 45, 200);
            SDL_RenderFillRect(renderer, &hl);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
        SDL_SetRenderDrawColor(renderer, gi.color.r, gi.color.g, gi.color.b, 255);
        SDL_Rect dot = {gx, y + 2, 4, line_h - 4};
        SDL_RenderFillRect(renderer, &dot);
        ui::draw_text_clipped(renderer, font, gi.name, col, gx + 7, y, list_w - margin - 10);
    }
    // God description (right half of pair 1) — large icon + text, vertically centered
    int gdx = list_w;
    if (build_god_cursor_ >= 0 && build_god_cursor_ < GOD_COUNT) {
        auto& gi = get_god_info(static_cast<GodId>(build_god_cursor_));
        SDL_Color gc = {gi.color.r, gi.color.g, gi.color.b, 255};

        // Vertically center the icon+text block in available height
        int icon_sz = 32;
        int block_h = icon_sz * 2 + line_h * 4; // icon + name + ~3 lines desc
        int block_y = top_y + (avail_h - block_h) / 2;
        if (block_y < top_y + 10) block_y = top_y + 10;

        // Large god diamond (filled)
        int icon_cx = gdx + desc_w / 2;
        int icon_cy = block_y + icon_sz;
        SDL_SetRenderDrawColor(renderer, gi.color.r, gi.color.g, gi.color.b, 255);
        for (int iy = -icon_sz; iy <= icon_sz; iy++) {
            int hw = icon_sz - std::abs(iy);
            SDL_RenderDrawLine(renderer, icon_cx - hw, icon_cy + iy, icon_cx + hw, icon_cy + iy);
        }
        // Inner highlight
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 60);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (int iy = -icon_sz/2; iy <= 0; iy++) {
            int hw = icon_sz/2 - std::abs(iy);
            SDL_RenderDrawLine(renderer, icon_cx - hw, icon_cy + iy - icon_sz/4, icon_cx + hw, icon_cy + iy - icon_sz/4);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        // God name centered below icon
        int name_y = icon_cy + icon_sz + 12;
        ui::draw_text_clipped(renderer, font, gi.name, gc, gdx + margin, name_y, desc_w - margin * 2);

        // Wrapped description below name
        int dy = name_y + line_h + 6;
        ui::draw_text_wrapped(renderer, font, gi.passive_desc, desc_col, gdx + margin, dy, desc_w - margin * 2);
    }

    // === PAIR 2: Traits (next 1/4) ===
    int tx = pair_w + margin;
    SDL_Color hdr1 = build_column_ == 1 ? sel_col : header_col;
    ui::draw_text(renderer, font, "TRAITS", hdr1, tx, top_y);
    int ty = top_y + line_h + 4;
    for (int i = 0; i < TRAIT_COUNT; i++) {
        auto& tr = get_trait_info(static_cast<TraitId>(i));
        bool is_cursor = (build_column_ == 1 && i == build_trait_cursor_);
        bool is_picked = false;
        for (auto t : build_traits_selected_) if (t == static_cast<TraitId>(i)) { is_picked = true; break; }
        SDL_Color col = is_cursor ? sel_col : is_picked ? picked_col : dim_col;
        int y = ty + i * trait_row_h;
        if (is_cursor) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_Rect hl = {tx - 2, y, list_w - margin, trait_row_h - 1};
            SDL_SetRenderDrawColor(renderer, 35, 30, 45, 200);
            SDL_RenderFillRect(renderer, &hl);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
        char buf[32]; snprintf(buf, sizeof(buf), "%s %s", is_picked ? "[x]" : "[ ]", tr.name);
        ui::draw_text_clipped(renderer, font, buf, col, tx, y, list_w - margin);
    }
    // Trait description (right half of pair 2) — large icon + text, vertically centered
    int tdx = pair_w + list_w;
    if (build_trait_cursor_ >= 0 && build_trait_cursor_ < TRAIT_COUNT) {
        auto& tr = get_trait_info(static_cast<TraitId>(build_trait_cursor_));

        int ticon_sz = 28;
        int tblock_h = ticon_sz * 2 + line_h * 4;
        int tblock_y = top_y + (avail_h - tblock_h) / 2;
        if (tblock_y < top_y + 10) tblock_y = top_y + 10;

        // Large trait symbol (nested squares, gold)
        int ticon_cx = tdx + desc_w / 2;
        int ticon_cy = tblock_y + ticon_sz;
        SDL_SetRenderDrawColor(renderer, 220, 200, 100, 255);
        SDL_Rect tsq1 = {ticon_cx - ticon_sz, ticon_cy - ticon_sz, ticon_sz * 2, ticon_sz * 2};
        SDL_RenderDrawRect(renderer, &tsq1);
        SDL_Rect tsq2 = {ticon_cx - ticon_sz + 4, ticon_cy - ticon_sz + 4, ticon_sz * 2 - 8, ticon_sz * 2 - 8};
        SDL_RenderDrawRect(renderer, &tsq2);
        SDL_Rect tsq3 = {ticon_cx - ticon_sz + 8, ticon_cy - ticon_sz + 8, ticon_sz * 2 - 16, ticon_sz * 2 - 16};
        SDL_RenderFillRect(renderer, &tsq3);

        // Trait name below icon
        int tname_y = ticon_cy + ticon_sz + 12;
        ui::draw_text_clipped(renderer, font, tr.name, picked_col, tdx + margin, tname_y, desc_w - margin * 2);

        // Wrapped description
        int tdy = tname_y + line_h + 6;
        ui::draw_text_wrapped(renderer, font, tr.description, desc_col, tdx + margin, tdy, desc_w - margin * 2);
    }

    // === PAIR 3: Background (next 1/4) ===
    int bx = pair_w * 2 + margin;
    SDL_Color hdr2 = build_column_ == 2 ? sel_col : header_col;
    ui::draw_text(renderer, font, "BACKGROUND", hdr2, bx, top_y);
    int by = top_y + line_h + 4;
    for (int i = 0; i < BACKGROUND_COUNT; i++) {
        auto& bg = get_background_info(static_cast<BackgroundId>(i));
        bool is_cursor = (build_column_ == 2 && i == build_bg_cursor_);
        SDL_Color col = is_cursor ? sel_col : (i == build_bg_cursor_) ? active_col : dim_col;
        int y = by + i * bg_row_h;
        if (is_cursor) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_Rect hl = {bx - 2, y, list_w - margin, bg_row_h - 1};
            SDL_SetRenderDrawColor(renderer, 35, 30, 45, 200);
            SDL_RenderFillRect(renderer, &hl);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
        ui::draw_text_clipped(renderer, font, bg.name, col, bx, y, list_w - margin);
    }
    // Background description (right half of pair 3) — large icon + text, vertically centered
    int bdx = pair_w * 2 + list_w;
    if (build_bg_cursor_ >= 0 && build_bg_cursor_ < BACKGROUND_COUNT) {
        auto& bg = get_background_info(static_cast<BackgroundId>(build_bg_cursor_));

        int bicon_r = 26;
        int bblock_h = bicon_r * 2 + line_h * 4;
        int bblock_y = top_y + (avail_h - bblock_h) / 2;
        if (bblock_y < top_y + 10) bblock_y = top_y + 10;

        // Large background symbol (filled circle)
        int bicon_cx = bdx + desc_w / 2;
        int bicon_cy = bblock_y + bicon_r;
        SDL_SetRenderDrawColor(renderer, 160, 155, 140, 200);
        for (int by2 = -bicon_r; by2 <= bicon_r; by2++) {
            int bw = static_cast<int>(sqrtf(static_cast<float>(bicon_r * bicon_r - by2 * by2)));
            SDL_RenderDrawLine(renderer, bicon_cx - bw, bicon_cy + by2, bicon_cx + bw, bicon_cy + by2);
        }
        // Outline ring
        SDL_SetRenderDrawColor(renderer, 200, 195, 180, 255);
        for (int ba = 0; ba < 48; ba++) {
            float angle = ba * 6.283f / 48.0f;
            float next_a = (ba + 1) * 6.283f / 48.0f;
            int x1 = bicon_cx + static_cast<int>(bicon_r * cosf(angle));
            int y1 = bicon_cy + static_cast<int>(bicon_r * sinf(angle));
            int x2 = bicon_cx + static_cast<int>(bicon_r * cosf(next_a));
            int y2 = bicon_cy + static_cast<int>(bicon_r * sinf(next_a));
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        }

        // Background name below icon
        int bname_y = bicon_cy + bicon_r + 12;
        ui::draw_text_clipped(renderer, font, bg.name, active_col, bdx + margin, bname_y, desc_w - margin * 2);

        // Wrapped description
        int bdy = bname_y + line_h + 6;
        ui::draw_text_wrapped(renderer, font, bg.description, desc_col, bdx + margin, bdy, desc_w - margin * 2);
    }

    // === RIGHT PANEL: Character preview ===
    int px = content_w + margin;
    auto& cls = get_class_info(build_.class_id);
    ui::draw_text(renderer, font, cls.name, sel_col, px, top_y);
    ui::draw_text(renderer, font, build_.name.c_str(), active_col, px, top_y + line_h + 4);

    // Show selected god/traits summary
    int sy = top_y + line_h * 3 + 8;
    if (build_god_cursor_ >= 0 && build_god_cursor_ < GOD_COUNT) {
        auto& gi = get_god_info(static_cast<GodId>(build_god_cursor_));
        SDL_Color gc = {gi.color.r, gi.color.g, gi.color.b, 255};
        ui::draw_text_clipped(renderer, font, gi.name, gc, px, sy, preview_w - margin * 2);
        sy += line_h + 2;
    }
    for (auto t : build_traits_selected_) {
        auto& tr = get_trait_info(t);
        ui::draw_text_clipped(renderer, font, tr.name, picked_col, px, sy, preview_w - margin * 2);
        sy += line_h + 2;
    }

    // Bottom hint
    ui::draw_text(renderer, font, "[</>] column  [v/^] browse  [Enter] select  [Space] GO",
                  {120, 115, 110, 255}, margin, h - line_h - margin);
}
