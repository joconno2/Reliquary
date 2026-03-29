#include "ui/creation_screen.h"
#include "ui/ui_draw.h"
#include <cstdio>
#include <algorithm>

static const char* RANDOM_NAMES[] = {
    "Aldric", "Brenna", "Caius", "Dagna", "Eryn", "Falk", "Greta", "Hakon",
    "Isolde", "Jareth", "Kael", "Lyra", "Maren", "Nym", "Osric", "Petra",
    "Riven", "Sable", "Thane", "Ulric", "Voss", "Wren", "Xara", "Yorick", "Zara",
    "Asher", "Briar", "Corwin", "Dove", "Esk", "Flint", "Gale", "Holt",
};
static constexpr int NAME_COUNT = sizeof(RANDOM_NAMES) / sizeof(RANDOM_NAMES[0]);

void CreationScreen::randomize_name() {
    // Simple hash from current state for pseudo-random
    unsigned h = static_cast<unsigned>(selected_ * 7 + static_cast<int>(build_.class_id) * 13 +
                                        SDL_GetTicks());
    build_.name = RANDOM_NAMES[h % NAME_COUNT];
}

void CreationScreen::reset() {
    phase_ = CreationPhase::CLASS_SELECT;
    selected_ = 0;
    build_ = {};
    bg_screen_.reset();
    trait_screen_.reset();
    randomize_name();
    // Base classes always unlocked
    for (int i = 0; i < CLASS_COUNT; i++)
        class_unlocked_[i] = (i < BASE_CLASS_COUNT);
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

    // Background sub-screen
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

    // Trait sub-screen
    if (phase_ == CreationPhase::TRAIT_SELECT) {
        bool consumed = trait_screen_.handle_input(event);
        if (trait_screen_.is_confirmed()) {
            build_.traits = trait_screen_.get_selected_traits();
            build_.hardcore = false;
            phase_ = CreationPhase::HARDCORE_SELECT;
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

    // Hardcore mode toggle
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

    // Name entry — special text input mode
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
        // Type a character (a-z, space)
        if (build_.name.size() < 20) {
            char c = 0;
            if (sym >= SDLK_a && sym <= SDLK_z) {
                c = 'a' + (sym - SDLK_a);
                if (event.key.keysym.mod & KMOD_SHIFT) c -= 32; // uppercase
                if (build_.name.empty()) c = static_cast<char>(toupper(c)); // auto-capitalize first
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
        return true; // consume all keys in name mode
    }

    // Class select / God select — shared navigation
    int max_sel = 0;
    if (phase_ == CreationPhase::CLASS_SELECT) max_sel = CLASS_COUNT;
    else if (phase_ == CreationPhase::GOD_SELECT) max_sel = GOD_COUNT;

    // Grid navigation for class select (6 columns), linear for god select
    int grid_cols = (phase_ == CreationPhase::CLASS_SELECT) ? 6 : 1;

    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_k:
            if (selected_ - grid_cols >= 0) selected_ -= grid_cols;
            else if (phase_ != CreationPhase::CLASS_SELECT && selected_ > 0) selected_--;
            return true;
        case SDLK_DOWN:
        case SDLK_j:
            if (selected_ + grid_cols < max_sel) selected_ += grid_cols;
            else if (phase_ != CreationPhase::CLASS_SELECT && selected_ < max_sel - 1) selected_++;
            return true;
        case SDLK_LEFT:
        case SDLK_h:
            if (selected_ > 0) selected_--;
            return true;
        case SDLK_RIGHT:
        case SDLK_l:
            if (selected_ < max_sel - 1) selected_++;
            return true;
        case SDLK_RETURN:
        case SDLK_e:
            if (phase_ == CreationPhase::CLASS_SELECT) {
                if (!class_unlocked_[selected_]) return true; // locked — can't select
                build_.class_id = static_cast<ClassId>(selected_);
                randomize_name();
                phase_ = CreationPhase::NAME_ENTRY;
            } else if (phase_ == CreationPhase::GOD_SELECT) {
                build_.god = static_cast<GodId>(selected_);
                bg_screen_.reset();
                phase_ = CreationPhase::BACKGROUND_SELECT;
            }
            return true;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            if (phase_ == CreationPhase::GOD_SELECT) {
                phase_ = CreationPhase::NAME_ENTRY;
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

    if (phase_ == CreationPhase::CLASS_SELECT) {
        render_class_select(renderer, font, font_title, sprites, w, h);
    } else if (phase_ == CreationPhase::NAME_ENTRY) {
        render_name_entry(renderer, font, font_title, sprites, w, h);
    } else if (phase_ == CreationPhase::GOD_SELECT) {
        render_god_select(renderer, font, font_title, sprites, w, h);
    } else if (phase_ == CreationPhase::BACKGROUND_SELECT) {
        bg_screen_.render(renderer, font, w, h);
    } else if (phase_ == CreationPhase::TRAIT_SELECT) {
        trait_screen_.render(renderer, font, w, h);
    } else if (phase_ == CreationPhase::HARDCORE_SELECT) {
        // Simple centered screen
        if (!font) return;
        int line_h = TTF_FontLineSkip(font);
        int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;
        SDL_Color title_col = {200, 180, 160, 255};
        SDL_Color desc_col = {140, 130, 120, 255};
        SDL_Color sel_col = {255, 220, 140, 255};
        SDL_Color dim_col = {100, 95, 90, 255};

        int cy = h / 2 - title_h * 2; // vertically centered
        ui::draw_text_centered(renderer, font_title, "Game mode", title_col, w / 2, cy);
        cy += title_h + title_h / 2;

        const char* mode = build_.hardcore ? "Hardcore" : "Normal";
        SDL_Color mode_col = build_.hardcore ? SDL_Color{200, 80, 80, 255} : sel_col;
        ui::draw_text_centered(renderer, font_title, mode, mode_col, w / 2, cy);
        cy += title_h + line_h;

        if (build_.hardcore) {
            ui::draw_text_centered(renderer, font,
                "Permadeath. Save deleted on death. No second chances.",
                desc_col, w / 2, cy);
        } else {
            ui::draw_text_centered(renderer, font,
                "Standard save and load. Die and try again.",
                desc_col, w / 2, cy);
        }

        ui::draw_text_centered(renderer, font, "[Left/Right] toggle   [Enter] begin",
                                dim_col, w / 2, h - line_h - 4);
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

    int margin = w / 30;

    // === Top: title ===
    ui::draw_text_centered(renderer, font_title, "Reliquary", title_col, w / 2, margin / 2);

    // === Sprite grid — fill most of the screen ===
    // Reserve bottom strip for selected class info
    int info_area_h = title_h + line_h * 5 + 20;
    int grid_top = margin + title_h;
    int grid_bottom = h - info_area_h;
    int grid_h = grid_bottom - grid_top;
    int grid_w = w - margin * 2;

    // Compute cell size: try to fit all CLASS_COUNT in a grid
    // Target: ~6 columns, let rows be flexible
    int cols = 6;
    if (w < 1200) cols = 5;
    if (w < 800) cols = 4;
    int rows = (CLASS_COUNT + cols - 1) / cols;

    int cell_w = grid_w / cols;
    int cell_h = grid_h / rows;
    // Sprite at ~70% of cell, centered with room for name
    int sprite_sz = std::min(cell_w - 12, cell_h - line_h - 12) * 7 / 10;
    if (sprite_sz < 32) sprite_sz = 32;

    for (int i = 0; i < CLASS_COUNT; i++) {
        int col = i % cols;
        int row = i / cols;
        auto& c = get_class_info(static_cast<ClassId>(i));
        bool is_sel = (i == selected_);
        bool unlocked = class_unlocked_[i];

        int cx = margin + col * cell_w + cell_w / 2;
        int cy = grid_top + row * cell_h;

        // Sprite centered in cell
        int pad = 6;
        int box_w = sprite_sz + pad * 2;
        int box_h = sprite_sz + title_h + pad * 3;
        int box_x = cx - box_w / 2;
        int box_y = cy + (cell_h - box_h) / 2;

        // SNES panel on selected cell
        if (is_sel) {
            ui::draw_panel(renderer, box_x, box_y, box_w, box_h);
        }

        int sx = cx - sprite_sz / 2;
        int sy = box_y + pad;
        SDL_Color tint = unlocked ? SDL_Color{255,255,255,255} : SDL_Color{50,45,40,255};
        sprites.draw_sprite_sized(renderer, SHEET_ROGUES, c.sprite_x, c.sprite_y,
                                   sx, sy, sprite_sz, tint);

        // Name below sprite (use title font for readability)
        SDL_Color ncol = unlocked ? (is_sel ? sel_col : normal_col) : lock_col;
        ui::draw_text_centered(renderer, font_title ? font_title : font,
                                unlocked ? c.name : "???", ncol,
                                cx, sy + sprite_sz + 4);
    }

    // === Bottom info area: selected class details ===
    auto& cls = get_class_info(static_cast<ClassId>(selected_));
    bool sel_unlocked = class_unlocked_[selected_];
    int info_y = grid_bottom + 4;

    if (sel_unlocked) {
        // Name + description + stats in a horizontal layout
        ui::draw_text_centered(renderer, font_title, cls.name, sel_col, w / 2, info_y);
        info_y += title_h + 2;
        ui::draw_text_centered(renderer, font, cls.description, desc_col, w / 2, info_y);
        info_y += line_h + 4;

        // Stats in a single row
        char stat_buf[256];
        snprintf(stat_buf, sizeof(stat_buf),
            "STR:%d  DEX:%d  CON:%d  INT:%d  WIL:%d  PER:%d  CHA:%d  |  HP:%d  MP:%d",
            cls.str, cls.dex, cls.con, cls.intel, cls.wil, cls.per, cls.cha, cls.hp, cls.mp);
        ui::draw_text_centered(renderer, font, stat_buf, {180, 175, 170, 255}, w / 2, info_y);
    } else {
        // Locked: show name + unlock hint + progress
        ui::draw_text_centered(renderer, font_title, cls.name, {120, 100, 80, 255}, w / 2, info_y);
        info_y += title_h + 2;
        if (cls.unlock_hint) {
            ui::draw_text_centered(renderer, font, cls.unlock_hint,
                                    SDL_Color{160, 140, 100, 255}, w / 2, info_y);
            info_y += line_h + 2;
        }
        if (!unlock_progress_[selected_].empty()) {
            ui::draw_text_centered(renderer, font, unlock_progress_[selected_].c_str(),
                                    SDL_Color{200, 180, 100, 255}, w / 2, info_y);
        }
    }

    ui::draw_text_centered(renderer, font, "[Arrows] browse   [Enter] select",
                            dim_col, w / 2, h - line_h - 4);
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

    // Big sprite — scale with screen, center vertically
    int sprite_size = std::min(h / 4, w / 6);
    int total_content_h = sprite_size + line_h * 5 + 60;
    int sprite_x = w / 2 - sprite_size / 2;
    int sprite_y = (h - total_content_h) / 2 - line_h; // centered
    sprites.draw_sprite_sized(renderer, SHEET_ROGUES, cls.sprite_x, cls.sprite_y,
                               sprite_x, sprite_y, sprite_size);

    int y = sprite_y + sprite_size + 12;

    // Class name
    ui::draw_text_centered(renderer, font, cls.name, dim_col, w / 2, y);
    y += line_h + 16;

    // "Name your character" prompt
    ui::draw_text_centered(renderer, font, "Name your character.", title_col, w / 2, y);
    y += line_h + 12;

    // Name input field
    int field_w = std::min(w / 2, 500);
    int field_h = line_h + 16;
    int field_x = w / 2 - field_w / 2;
    ui::draw_panel(renderer, field_x, y, field_w, field_h);

    // Name text with blinking cursor
    std::string display = build_.name;
    // Simple cursor blink based on SDL ticks
    if ((SDL_GetTicks() / 500) % 2 == 0) {
        display += "_";
    }
    ui::draw_text(renderer, font, display.c_str(), name_col,
                  field_x + 12, y + 8);

    ui::draw_text_centered(renderer, font, "[Tab] random name   [Enter] confirm   [Esc] back",
                            dim_col, w / 2, h - line_h - 4);
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

    int margin = w / 40;

    // Header
    char header[64];
    snprintf(header, sizeof(header), "%s the %s", build_.name.c_str(),
             get_class_info(build_.class_id).name);
    ui::draw_text_centered(renderer, font, header, dim_col, w / 2, margin);
    ui::draw_text_centered(renderer, font, "Choose your god.", title_col, w / 2, margin + line_h + 4);

    // Centered two-column layout — total 80% of screen width
    int content_w = w * 4 / 5;
    int content_x = (w - content_w) / 2;
    int list_w = content_w * 2 / 5;
    int detail_w = content_w * 3 / 5 - margin;
    int list_x = content_x;
    int detail_x = content_x + list_w + margin;

    int list_top = margin + line_h * 3;
    int list_bottom = h - line_h * 2;
    int god_item_h = (list_bottom - list_top) / GOD_COUNT;
    if (god_item_h > line_h * 4) god_item_h = line_h * 4;

    for (int i = 0; i < GOD_COUNT; i++) {
        auto& god = get_god_info(static_cast<GodId>(i));
        bool is_sel = (i == selected_);
        int gy = list_top + i * god_item_h;

        if (is_sel) {
            ui::draw_panel(renderer, list_x - 4, gy - 4, list_w + 8, god_item_h - 2);
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "%s, %s", god.name, god.title);
        ui::draw_text(renderer, font, buf, is_sel ? sel_col : normal_col, list_x + 6, gy + 4);

        snprintf(buf, sizeof(buf), "  %s", god.domain);
        ui::draw_text(renderer, font, buf, dim_col, list_x + 6, gy + line_h + 6);
    }

    // Detail panel
    int detail_y = list_top;

    auto& god = get_god_info(static_cast<GodId>(selected_));

    ui::draw_panel(renderer, detail_x - 10, detail_y - 10, detail_w + 20, h - detail_y - 40);

    // God name
    char title_buf[128];
    snprintf(title_buf, sizeof(title_buf), "%s, %s", god.name, god.title);
    ui::draw_text(renderer, font_title, title_buf, sel_col, detail_x, detail_y);
    detail_y += title_h + 8;

    ui::draw_text_wrapped(renderer, font, god.description, desc_col, detail_x, detail_y, detail_w);
    detail_y += line_h * 3 + 8;

    // Blessings
    ui::draw_text(renderer, font, "Blessings:", dim_col, detail_x, detail_y);
    detail_y += line_h + 2;

    struct BonusPair { const char* name; int val; };
    BonusPair bonuses[] = {
        {"STR", god.str_bonus}, {"DEX", god.dex_bonus}, {"CON", god.con_bonus},
        {"INT", god.int_bonus}, {"WIL", god.wil_bonus}, {"PER", god.per_bonus},
        {"CHA", god.cha_bonus}, {"HP",  god.bonus_hp},  {"MP",  god.bonus_mp},
        {"FOV", god.fov_bonus},
    };
    for (auto& b : bonuses) {
        if (b.val == 0) continue;
        char buf[32];
        snprintf(buf, sizeof(buf), "  %s %+d", b.name, b.val);
        SDL_Color col = b.val > 0 ? SDL_Color{120, 200, 120, 255} : SDL_Color{200, 120, 120, 255};
        ui::draw_text(renderer, font, buf, col, detail_x, detail_y);
        detail_y += line_h;
    }

    ui::draw_text_centered(renderer, font, "[Enter] select   [Up/Down] browse   [Esc] back",
                            dim_col, w / 2, h - line_h - 4);
}
