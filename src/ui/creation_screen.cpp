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

    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return true;
        case SDLK_DOWN:
        case SDLK_j:
            if (selected_ < max_sel - 1) selected_++;
            return true;
        case SDLK_LEFT:
        case SDLK_h:
            if (phase_ == CreationPhase::CLASS_SELECT) {
                selected_ = (selected_ - 1 + max_sel) % max_sel;
            }
            return true;
        case SDLK_RIGHT:
        case SDLK_l:
            if (phase_ == CreationPhase::CLASS_SELECT) {
                selected_ = (selected_ + 1) % max_sel;
            }
            return true;
        case SDLK_RETURN:
        case SDLK_e:
            if (phase_ == CreationPhase::CLASS_SELECT) {
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
    }
}

void CreationScreen::render_class_select(SDL_Renderer* renderer, TTF_Font* font,
                                          TTF_Font* font_title,
                                          const SpriteManager& sprites, int w, int h) const {
    if (!font) return;
    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color desc_col = {140, 130, 120, 255};

    // Title
    ui::draw_text_centered(renderer, font_title, "Reliquary", title_col, w / 2, 15);
    ui::draw_text_centered(renderer, font, "Choose your class.", dim_col, w / 2, 55);

    // Giant sprite of selected class — centerpiece
    auto& cls = get_class_info(static_cast<ClassId>(selected_));
    int sprite_size = 128; // 4x scale
    int sprite_x = w / 2 - sprite_size / 2;
    int sprite_y = 85;
    sprites.draw_sprite_sized(renderer, SHEET_ROGUES, cls.sprite_x, cls.sprite_y,
                               sprite_x, sprite_y, sprite_size);

    // Class name below sprite in Jacquard
    int name_y = sprite_y + sprite_size + 8;
    ui::draw_text_centered(renderer, font_title, cls.name, sel_col, w / 2, name_y);

    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;
    int desc_y = name_y + title_h + 4;
    ui::draw_text_centered(renderer, font, cls.description, desc_col, w / 2, desc_y);

    // Class list at the bottom as horizontal tabs
    int tabs_y = h - 80;
    int tab_w = w / CLASS_COUNT;
    for (int i = 0; i < CLASS_COUNT; i++) {
        auto& c = get_class_info(static_cast<ClassId>(i));
        bool is_sel = (i == selected_);

        int tx = i * tab_w + tab_w / 2;

        if (is_sel) {
            SDL_Rect hl = {i * tab_w + 4, tabs_y - 4, tab_w - 8, line_h + 40};
            SDL_SetRenderDrawColor(renderer, 25, 22, 35, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        // Small sprite
        int small_x = tx - 16;
        sprites.draw_sprite(renderer, SHEET_ROGUES, c.sprite_x, c.sprite_y,
                            small_x, tabs_y, 1);

        ui::draw_text_centered(renderer, font, c.name,
                                is_sel ? sel_col : normal_col, tx, tabs_y + 34);
    }

    // Stats panel on the right side
    int panel_x = w - 280;
    int panel_y = 90;
    int panel_w = 260;
    int panel_h = 220;
    ui::draw_panel(renderer, panel_x, panel_y, panel_w, panel_h);

    int sy = panel_y + 12;
    ui::draw_text(renderer, font, "Attributes", dim_col, panel_x + 10, sy);
    sy += line_h + 4;

    struct StatLine { const char* name; int val; };
    StatLine stats[] = {
        {"STR", cls.str}, {"DEX", cls.dex}, {"CON", cls.con},
        {"INT", cls.intel}, {"WIL", cls.wil}, {"PER", cls.per}, {"CHA", cls.cha},
    };
    for (auto& s : stats) {
        char buf[32];
        snprintf(buf, sizeof(buf), " %s: %d", s.name, s.val);
        SDL_Color col = {180, 175, 170, 255};
        if (s.val >= 14) col = {140, 200, 140, 255};
        if (s.val <= 8) col = {200, 140, 140, 255};
        ui::draw_text(renderer, font, buf, col, panel_x + 10, sy);
        sy += line_h;
    }
    sy += 4;
    char hp_buf[32], mp_buf[32];
    snprintf(hp_buf, sizeof(hp_buf), " HP: %d", cls.hp);
    snprintf(mp_buf, sizeof(mp_buf), " MP: %d", cls.mp);
    ui::draw_text(renderer, font, hp_buf, {200, 100, 100, 255}, panel_x + 10, sy);
    sy += line_h;
    ui::draw_text(renderer, font, mp_buf, {100, 100, 200, 255}, panel_x + 10, sy);

    ui::draw_text_centered(renderer, font, "[Left/Right] browse   [Enter] select",
                            dim_col, w / 2, h - 25);
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

    // Big sprite
    int sprite_size = 128;
    int sprite_x = w / 2 - sprite_size / 2;
    int sprite_y = 40;
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
    int field_w = 340;
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

    y += field_h + 16;

    ui::draw_text_centered(renderer, font, "[Tab] random name   [Enter] confirm   [Esc] back",
                            dim_col, w / 2, y);
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

    // Header
    char header[64];
    snprintf(header, sizeof(header), "%s the %s", build_.name.c_str(),
             get_class_info(build_.class_id).name);
    ui::draw_text_centered(renderer, font, header, dim_col, w / 2, 12);
    ui::draw_text_centered(renderer, font, "Choose your god.", title_col, w / 2, 12 + line_h + 4);

    // God list on the left
    int list_x = 30;
    int list_y = 60;

    for (int i = 0; i < GOD_COUNT; i++) {
        auto& god = get_god_info(static_cast<GodId>(i));
        bool is_sel = (i == selected_);

        if (is_sel) {
            SDL_Rect hl = {list_x - 4, list_y - 2, 420, line_h * 2 + 6};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "%s, %s", god.name, god.title);
        ui::draw_text(renderer, font, buf, is_sel ? sel_col : normal_col, list_x, list_y);

        snprintf(buf, sizeof(buf), "  %s", god.domain);
        ui::draw_text(renderer, font, buf, dim_col, list_x, list_y + line_h);

        list_y += line_h * 2 + 6;
    }

    // Detail panel on the right
    int detail_x = 480;
    int detail_y = 60;
    int detail_w = w - detail_x - 30;

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

    ui::draw_text(renderer, font, "[Enter] select   [Up/Down] browse   [Esc] back",
                  dim_col, 30, h - 25);
}
