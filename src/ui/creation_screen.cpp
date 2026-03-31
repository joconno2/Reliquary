#include "ui/creation_screen.h"
#include "ui/ui_draw.h"
#include "components/prayer.h"
#include "components/tenet.h"
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

    // Usable width for content (leave room for preview panel on right)
    int cw = (phase_ != CreationPhase::CLASS_SELECT) ? w * 74 / 100 : w;

    if (phase_ == CreationPhase::CLASS_SELECT) {
        render_class_select(renderer, font, font_title, sprites, w, h);
    } else if (phase_ == CreationPhase::NAME_ENTRY) {
        render_name_entry(renderer, font, font_title, sprites, cw, h);
    } else if (phase_ == CreationPhase::GOD_SELECT) {
        render_god_select(renderer, font, font_title, sprites, cw, h);
    } else if (phase_ == CreationPhase::BACKGROUND_SELECT) {
        bg_screen_.render(renderer, font, cw, h);
    } else if (phase_ == CreationPhase::TRAIT_SELECT) {
        trait_screen_.render(renderer, font, cw, h);
    } else if (phase_ == CreationPhase::HARDCORE_SELECT) {
        // Simple centered screen
        if (!font) return;
        int line_h = TTF_FontLineSkip(font);
        int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;
        SDL_Color title_col = {200, 180, 160, 255};
        SDL_Color desc_col = {140, 130, 120, 255};
        SDL_Color sel_col = {255, 220, 140, 255};
        SDL_Color dim_col = {100, 95, 90, 255};

        int cy = h / 2 - title_h * 2;
        ui::draw_text_centered(renderer, font_title, "Game mode", title_col, cw / 2, cy);
        cy += title_h + title_h / 2;

        const char* mode = build_.hardcore ? "Hardcore" : "Normal";
        SDL_Color mode_col = build_.hardcore ? SDL_Color{200, 80, 80, 255} : sel_col;
        ui::draw_text_centered(renderer, font_title, mode, mode_col, cw / 2, cy);
        cy += title_h + line_h;

        if (build_.hardcore) {
            ui::draw_text_centered(renderer, font,
                "Permadeath. Save deleted on death. No second chances.",
                desc_col, cw / 2, cy);
        } else {
            ui::draw_text_centered(renderer, font,
                "Standard save and load. Die and try again.",
                desc_col, cw / 2, cy);
        }

        ui::draw_text_centered(renderer, font, "[Left/Right] toggle   [Enter] begin",
                                dim_col, cw / 2, h - line_h - 4);
    }

    // Character preview panel — shown after class selection on all screens
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
    int pad = 10; // inner padding for panel content

    // Header
    char header[64];
    snprintf(header, sizeof(header), "%s the %s", build_.name.c_str(),
             get_class_info(build_.class_id).name);
    int header_y = margin;
    ui::draw_text_centered(renderer, font, header, dim_col, w / 2, header_y);
    ui::draw_text_centered(renderer, font, "Choose your god.", title_col, w / 2, header_y + line_h + 4);

    // Layout: god list on left (scrollable), detail panel on right
    int content_w = w * 4 / 5;
    int content_x = (w - content_w) / 2;
    int list_w = content_w * 2 / 5;
    int detail_w = content_w * 3 / 5 - margin;
    int list_x = content_x;
    int detail_x = content_x + list_w + margin;

    int list_top = header_y + line_h * 2 + line_h;
    int list_bottom = h - line_h * 2;
    // Scale item height to fill available space
    int god_item_h = std::max(line_h * 2 + 4, (list_bottom - list_top) / GOD_COUNT);
    int visible_count = (list_bottom - list_top) / god_item_h;

    // Scroll offset to keep selected visible
    int scroll = 0;
    if (GOD_COUNT > visible_count) {
        scroll = selected_ - visible_count / 2;
        if (scroll < 0) scroll = 0;
        if (scroll > GOD_COUNT - visible_count) scroll = GOD_COUNT - visible_count;
    }

    for (int i = scroll; i < GOD_COUNT && i < scroll + visible_count; i++) {
        auto& god = get_god_info(static_cast<GodId>(i));
        bool is_sel = (i == selected_);
        int gy = list_top + (i - scroll) * god_item_h;

        if (is_sel) {
            ui::draw_panel(renderer, list_x - 4, gy - 4, list_w + 8, god_item_h - 2);
        }

        // God color indicator
        SDL_SetRenderDrawColor(renderer, god.color.r, god.color.g, god.color.b, 200);
        SDL_Rect dot = {list_x - 2, gy + 6, 4, line_h - 4};
        SDL_RenderFillRect(renderer, &dot);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s, %s", god.name, god.title);
        ui::draw_text(renderer, font, buf, is_sel ? sel_col : normal_col, list_x + 6, gy + 4);

        snprintf(buf, sizeof(buf), "  %s", god.domain);
        ui::draw_text(renderer, font, buf, dim_col, list_x + 6, gy + line_h + 6);
    }

    // Detail panel
    int detail_y = list_top;

    auto& god = get_god_info(static_cast<GodId>(selected_));

    int panel_x0 = detail_x - pad - 6;
    int panel_y0 = detail_y - pad - 6;
    int panel_w0 = detail_w + (pad + 6) * 2;
    int panel_h0 = h - detail_y - line_h * 2 + pad;
    ui::draw_panel(renderer, panel_x0, panel_y0, panel_w0, panel_h0);

    // God name — inset by pad from panel interior
    char title_buf[128];
    snprintf(title_buf, sizeof(title_buf), "%s, %s", god.name, god.title);
    ui::draw_text(renderer, font_title, title_buf, sel_col, detail_x, detail_y);
    detail_y += title_h + 8;

    ui::draw_text_wrapped(renderer, font, god.description, desc_col, detail_x, detail_y, detail_w - pad);
    detail_y += line_h * 3 + 8;

    // Passive effect
    SDL_Color passive_col = {180, 170, 130, 255};
    ui::draw_text(renderer, font, "Passive:", dim_col, detail_x, detail_y);
    detail_y += line_h + 2;
    ui::draw_text_wrapped(renderer, font, god.passive_desc, passive_col, detail_x, detail_y, detail_w - pad);
    detail_y += line_h * 2 + 8;

    // Prayers
    const PrayerInfo* prayers = get_prayers(static_cast<GodId>(selected_));
    if (prayers) {
        ui::draw_text(renderer, font, "Prayers:", dim_col, detail_x, detail_y);
        detail_y += line_h + 2;
        for (int p = 0; p < 2; p++) {
            char pbuf[128];
            snprintf(pbuf, sizeof(pbuf), "  %s (%d favor)", prayers[p].name, prayers[p].favor_cost);
            ui::draw_text(renderer, font, pbuf, passive_col, detail_x, detail_y);
            detail_y += line_h;
            char pdbuf[128];
            snprintf(pdbuf, sizeof(pdbuf), "    %s", prayers[p].description);
            ui::draw_text(renderer, font, pdbuf, dim_col, detail_x, detail_y);
            detail_y += line_h + 2;
        }
        detail_y += 4;
    }

    // Tenets
    auto tenets = get_god_tenets(static_cast<GodId>(selected_));
    if (tenets.count > 0) {
        SDL_Color tenet_col = {200, 140, 140, 255};
        ui::draw_text(renderer, font, "Tenets:", dim_col, detail_x, detail_y);
        detail_y += line_h + 2;
        for (int t = 0; t < tenets.count; t++) {
            char tbuf[160];
            snprintf(tbuf, sizeof(tbuf), "  %s", tenets.tenets[t].description);
            ui::draw_text(renderer, font, tbuf, tenet_col, detail_x, detail_y);
            detail_y += line_h + 1;
        }
        detail_y += 4;
    }

    // Blessings (stat bonuses)
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

void CreationScreen::render_character_preview(SDL_Renderer* renderer, TTF_Font* font,
                                               [[maybe_unused]] TTF_Font* font_title,
                                               const SpriteManager& sprites,
                                               int w, int h) const {
    if (!font) return;
    int line_h = TTF_FontLineSkip(font);

    // Preview panel on the right 25% of screen
    int panel_w = w * 25 / 100;
    int panel_x = w - panel_w;
    int panel_y = 0;
    int panel_h = h;

    // Solid dark background (no blending — fully covers anything behind)
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 12, 10, 16, 255);
    SDL_Rect bg_rect = {panel_x, panel_y, panel_w, panel_h};
    SDL_RenderFillRect(renderer, &bg_rect);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    ui::draw_panel(renderer, panel_x + 2, panel_y + 4, panel_w - 4, panel_h - 8);

    int pcx = panel_x + panel_w / 2; // panel center x
    int y = panel_y + 16; // clear panel border (3px frame + 4px bevel + padding)
    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color val_col = {180, 175, 160, 255};
    SDL_Color dim_col = {110, 105, 100, 255};
    SDL_Color green_col = {120, 200, 120, 255};
    SDL_Color red_col = {200, 120, 120, 255};

    auto& cls = get_class_info(build_.class_id);

    // Determine "preview" god — use highlighted selection if browsing, else confirmed
    GodId preview_god = build_.god;
    if (phase_ == CreationPhase::GOD_SELECT)
        preview_god = static_cast<GodId>(selected_);

    // Determine "preview" background — use highlighted if browsing
    BackgroundId preview_bg = build_.background;
    if (phase_ == CreationPhase::BACKGROUND_SELECT)
        preview_bg = bg_screen_.get_selected();

    // Character name + class
    ui::draw_text_centered(renderer, font, build_.name.c_str(), title_col, pcx, y);
    y += line_h + 2;
    ui::draw_text_centered(renderer, font, cls.name, val_col, pcx, y);
    y += line_h + 6;

    // --- Character sprite (10x = 320px) with god effects ---
    int SS = std::min(panel_w - 40, h * 35 / 100); // ~35% of screen height, capped by panel width
    int sx = pcx - SS / 2;
    int sy = y;
    int scx = sx + SS / 2; // sprite center x
    int scy = sy + SS / 2; // sprite center y

    auto& ginfo = get_god_info(preview_god);
    Uint32 ticks = SDL_GetTicks();
    float t = ticks / 1000.0f;
    uint8_t gr = ginfo.color.r, gg = ginfo.color.g, gb = ginfo.color.b;

    // God-specific visual effects (matching in-game render_god_visuals — no square auras)
    if (preview_god != GodId::NONE) {
        int ds = std::max(4, SS / 30); // dot/drip size scales with sprite
        switch (preview_god) {
        case GodId::VETHRIK:
            // Rising bone motes — 4 small rects that float upward in a cycle
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
            // 4 orbiting glyphs
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
            // Iron sparks at feet — 3 rects that flash and scatter
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
            // Blood drips falling — 5 drips at staggered phases
            for (int i = 0; i < 5; i++) {
                float phase = std::fmod(t * 0.6f + i * 0.2f, 1.0f);
                int dx2 = scx + static_cast<int>(std::sin(i * 2.3f) * SS * 0.35f);
                int dy2 = sy + static_cast<int>(phase * SS * 1.1f);
                int da = static_cast<int>((1.0f - phase) * 180);
                SDL_SetRenderDrawColor(renderer, 200, 40, 40, static_cast<Uint8>(da));
                SDL_Rect drip = {dx2 - ds / 2, dy2, ds, ds * 3};
                SDL_RenderFillRect(renderer, &drip);
            }
            // Heartbeat expansion
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
            // Floating leaf particles — drift outward and fade
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
            // Flame halo + rising embers
            int fl = static_cast<int>(45 + 25 * std::sin(t * 6.0f));
            SDL_SetRenderDrawColor(renderer, 255, 220, 100, static_cast<Uint8>(fl));
            SDL_Rect halo = {scx - SS / 2, sy - SS / 6, SS, SS / 5};
            SDL_RenderFillRect(renderer, &halo);
            // Rising embers
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
            // Glitch tears — thicker, more aggressive
            if ((ticks / 50) % 4 == 0) {
                int off = (ticks / 25) % 9 - 4;
                SDL_SetRenderDrawColor(renderer, gr, gg, gb, 90);
                SDL_Rect tear = {sx + off * 4, sy + SS / 3, SS + 6, ds * 2};
                SDL_RenderFillRect(renderer, &tear);
                SDL_Rect tear2 = {sx - off * 5, sy + SS * 2 / 3, SS + 8, ds + 2};
                SDL_RenderFillRect(renderer, &tear2);
                SDL_Rect tear3 = {sx + off * 2, sy + SS / 6, SS / 2, ds};
                SDL_RenderFillRect(renderer, &tear3);
            }
            break;
        }
        case GodId::ZHAVEK: {
            // Deep shadow + trailing afterimages
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
            // Water ripples — 3 expanding rings
            for (int i = 0; i < 3; i++) {
                float rp = std::fmod(t * 1.2f + i * 0.33f, 1.0f);
                int rs = static_cast<int>(rp * SS * 0.8f);
                int ra = static_cast<int>((1.0f - rp) * 60);
                SDL_SetRenderDrawColor(renderer, gr, gg, gb, static_cast<Uint8>(ra));
                SDL_Rect ring = {scx - rs / 2, sy + SS - rs / 3, rs, rs * 2 / 3};
                SDL_RenderDrawRect(renderer, &ring);
                // Second line for thickness
                SDL_Rect ring2 = {scx - rs / 2 + 1, sy + SS - rs / 3 + 1, rs - 2, rs * 2 / 3 - 2};
                SDL_RenderDrawRect(renderer, &ring2);
            }
            break;
        }
        case GodId::OSSREN:
            // Forge sparks — scattered upward
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
            // 4 orbiting stone pebbles at feet
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
            // 2 blinking eyes that orbit in mist
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
            // Spore drift — expanding outward and fading
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

    // Draw the sprite with god tint
    SDL_Color sprite_tint = {255, 255, 255, 255};
    if (preview_god != GodId::NONE) {
        sprite_tint.r = static_cast<Uint8>(255 - (255 - gr) / 4);
        sprite_tint.g = static_cast<Uint8>(255 - (255 - gg) / 4);
        sprite_tint.b = static_cast<Uint8>(255 - (255 - gb) / 4);
    }
    sprites.draw_sprite_sized(renderer, SHEET_ROGUES, cls.sprite_x, cls.sprite_y,
                               sx, sy, SS, sprite_tint, false);

    y += SS + line_h;

    // God name (centered under sprite)
    if (phase_ >= CreationPhase::GOD_SELECT) {
        SDL_Color god_col = (preview_god != GodId::NONE)
            ? SDL_Color{gr, gg, gb, 255}
            : SDL_Color{dim_col};
        const char* gname = (preview_god != GodId::NONE) ? ginfo.name : "Godless";
        ui::draw_text_centered(renderer, font, gname, god_col, pcx, y);
        y += line_h + 4;
    }

    // Background name
    if (phase_ >= CreationPhase::BACKGROUND_SELECT) {
        auto& bgi = get_background_info(preview_bg);
        ui::draw_text_centered(renderer, font, bgi.name, dim_col, pcx, y);
        y += line_h + 4;
    }

    // --- Divider ---
    SDL_SetRenderDrawColor(renderer, 60, 55, 50, 255);
    SDL_RenderDrawLine(renderer, panel_x + 16, y, panel_x + panel_w - 16, y);
    y += line_h / 2;

    // --- Running stat totals ---
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

    // Spread stats vertically — each stat gets its own line with generous spacing
    int col1 = panel_x + 16;
    int col2 = panel_x + panel_w / 2 + 8;
    int stat_spacing = line_h + line_h / 3; // ~1.33x line height

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
        int lx = (i % 2 == 0) ? col1 : col2;
        int ly = y + (i / 2) * stat_spacing;
        ui::draw_text(renderer, font, buf, col, lx, ly);
    }
    y += ((7 + 1) / 2) * stat_spacing + line_h / 2;

    // HP / MP
    char hpbuf[24], mpbuf[24];
    snprintf(hpbuf, sizeof(hpbuf), "HP  %d", hp);
    snprintf(mpbuf, sizeof(mpbuf), "MP  %d", mp);
    ui::draw_text(renderer, font, hpbuf, {200, 100, 100, 255}, col1, y);
    ui::draw_text(renderer, font, mpbuf, {100, 120, 200, 255}, col2, y);
    y += stat_spacing;

    // FOV
    if (phase_ >= CreationPhase::GOD_SELECT) {
        int fov = 8 + per / 3 + fov_bonus;
        char fov_buf[24];
        snprintf(fov_buf, sizeof(fov_buf), "FOV  %d", fov);
        ui::draw_text(renderer, font, fov_buf, dim_col, col1, y);
        y += stat_spacing;
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
            if (rr.v != 0) {
                char rb[24];
                snprintf(rb, sizeof(rb), "%s %+d%%", rr.n, rr.v);
                ui::draw_text(renderer, font, rb, rr.v > 0 ? green_col : red_col, col1, y);
                y += stat_spacing;
            }
        }
    }

    // Traits (with divider)
    if (phase_ >= CreationPhase::HARDCORE_SELECT && !build_.traits.empty()) {
        SDL_SetRenderDrawColor(renderer, 60, 55, 50, 255);
        SDL_RenderDrawLine(renderer, panel_x + 16, y, panel_x + panel_w - 16, y);
        y += line_h / 2;
        for (TraitId tid : build_.traits) {
            auto& tr = get_trait_info(tid);
            ui::draw_text(renderer, font, tr.name, tr.is_positive ? green_col : red_col, col1 + 4, y);
            y += line_h + 2;
        }
    }
}
