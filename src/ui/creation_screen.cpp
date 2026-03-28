#include "ui/creation_screen.h"
#include <cstdio>
#include <algorithm>

void CreationScreen::reset() {
    phase_ = CreationPhase::GOD_SELECT;
    selected_ = 0;
    build_ = {};
    bg_screen_.reset();
    trait_screen_.reset();
}

bool CreationScreen::handle_input(SDL_Event& event) {
    if (phase_ == CreationPhase::DONE) return false;
    if (event.type != SDL_KEYDOWN) return false;

    // Delegate to sub-screens
    if (phase_ == CreationPhase::BACKGROUND_SELECT) {
        bool consumed = bg_screen_.handle_input(event);
        if (bg_screen_.is_confirmed()) {
            build_.background = bg_screen_.get_selected();
            bg_screen_.reset();
            trait_screen_.reset();
            phase_ = CreationPhase::TRAIT_SELECT;
            return true;
        }
        // Esc goes back
        if (!consumed && event.type == SDL_KEYDOWN &&
            (event.key.keysym.sym == SDLK_ESCAPE ||
             event.key.keysym.sym == SDLK_BACKSPACE)) {
            phase_ = CreationPhase::CLASS_SELECT;
            selected_ = static_cast<int>(build_.class_id);
            return true;
        }
        return consumed;
    }

    if (phase_ == CreationPhase::TRAIT_SELECT) {
        bool consumed = trait_screen_.handle_input(event);
        if (trait_screen_.is_confirmed()) {
            build_.traits = trait_screen_.get_selected_traits();
            phase_ = CreationPhase::DONE;
            return true;
        }
        // Esc goes back
        if (!consumed && event.type == SDL_KEYDOWN &&
            (event.key.keysym.sym == SDLK_ESCAPE ||
             event.key.keysym.sym == SDLK_BACKSPACE)) {
            trait_screen_.reset();
            bg_screen_.reset();
            phase_ = CreationPhase::BACKGROUND_SELECT;
            return true;
        }
        return consumed;
    }

    int max_sel = 0;
    if (phase_ == CreationPhase::GOD_SELECT) max_sel = GOD_COUNT;
    else if (phase_ == CreationPhase::CLASS_SELECT) max_sel = CLASS_COUNT;

    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return true;
        case SDLK_DOWN:
        case SDLK_j:
            if (selected_ < max_sel - 1) selected_++;
            return true;
        case SDLK_RETURN:
        case SDLK_e:
            if (phase_ == CreationPhase::GOD_SELECT) {
                build_.god = static_cast<GodId>(selected_);
                phase_ = CreationPhase::CLASS_SELECT;
                selected_ = 0;
            } else if (phase_ == CreationPhase::CLASS_SELECT) {
                build_.class_id = static_cast<ClassId>(selected_);
                bg_screen_.reset();
                phase_ = CreationPhase::BACKGROUND_SELECT;
            }
            return true;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            // Go back
            if (phase_ == CreationPhase::CLASS_SELECT) {
                phase_ = CreationPhase::GOD_SELECT;
                selected_ = static_cast<int>(build_.god);
            }
            return true;
        default:
            return false;
    }
}

static void draw_text(SDL_Renderer* renderer, TTF_Font* font,
                       const char* text, SDL_Color col, int x, int y) {
    if (!font || !text || !text[0]) return;
    SDL_Surface* surf = TTF_RenderText_Blended(font, text, col);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static void draw_text_wrapped(SDL_Renderer* renderer, TTF_Font* font,
                               const char* text, SDL_Color col, int x, int y, int w) {
    if (!font || !text || !text[0]) return;
    SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(font, text, col, static_cast<Uint32>(w));
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

void CreationScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                             const SpriteManager& sprites, int w, int h) const {
    // Dark background
    SDL_SetRenderDrawColor(renderer, 8, 6, 10, 255);
    SDL_RenderClear(renderer);

    if (phase_ == CreationPhase::GOD_SELECT) {
        render_god_select(renderer, font, sprites, w, h);
    } else if (phase_ == CreationPhase::CLASS_SELECT) {
        render_class_select(renderer, font, sprites, w, h);
    } else if (phase_ == CreationPhase::BACKGROUND_SELECT) {
        bg_screen_.render(renderer, font, w, h);
    } else if (phase_ == CreationPhase::TRAIT_SELECT) {
        trait_screen_.render(renderer, font, w, h);
    }
}

void CreationScreen::render_god_select(SDL_Renderer* renderer, TTF_Font* font,
                                        [[maybe_unused]] const SpriteManager& sprites,
                                        int w, int h) const {
    if (!font) return;
    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color desc_col = {140, 130, 120, 255};

    // Title
    draw_text(renderer, font, "RELIQUARY", title_col, w / 2 - 50, 30);
    draw_text(renderer, font, "Choose your god.", dim_col, w / 2 - 70, 30 + line_h + 4);

    // God list on the left
    int list_x = 40;
    int list_y = 80;

    for (int i = 0; i < GOD_COUNT; i++) {
        auto& god = get_god_info(static_cast<GodId>(i));
        bool is_sel = (i == selected_);

        if (is_sel) {
            SDL_Rect hl = {list_x - 4, list_y, 300, line_h * 2 + 4};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "%s, %s", god.name, god.title);
        draw_text(renderer, font, buf, is_sel ? sel_col : normal_col, list_x, list_y);

        snprintf(buf, sizeof(buf), "  %s", god.domain);
        draw_text(renderer, font, buf, dim_col, list_x, list_y + line_h);

        list_y += line_h * 2 + 8;
    }

    // Detail panel on the right for selected god
    int detail_x = 400;
    int detail_y = 80;
    int detail_w = w - detail_x - 40;

    auto& god = get_god_info(static_cast<GodId>(selected_));

    // Border
    SDL_Rect panel = {detail_x - 10, detail_y - 10, detail_w + 20, h - detail_y - 20};
    SDL_SetRenderDrawColor(renderer, 20, 16, 26, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderDrawRect(renderer, &panel);

    char title_buf[128];
    snprintf(title_buf, sizeof(title_buf), "%s, %s", god.name, god.title);
    draw_text(renderer, font, title_buf, sel_col, detail_x, detail_y);
    detail_y += line_h + 8;

    draw_text_wrapped(renderer, font, god.description, desc_col, detail_x, detail_y, detail_w);
    detail_y += line_h * 3 + 8;

    // Stat bonuses
    draw_text(renderer, font, "Blessings:", dim_col, detail_x, detail_y);
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
        draw_text(renderer, font, buf, col, detail_x, detail_y);
        detail_y += line_h;
    }

    // Controls
    draw_text(renderer, font, "[Enter] select   [Up/Down] browse",
              dim_col, 40, h - 30);
}

void CreationScreen::render_class_select(SDL_Renderer* renderer, TTF_Font* font,
                                          const SpriteManager& sprites, int w, int h) const {
    if (!font) return;
    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color desc_col = {140, 130, 120, 255};

    // Show chosen god
    auto& god = get_god_info(build_.god);
    char god_buf[128];
    snprintf(god_buf, sizeof(god_buf), "Servant of %s", god.name);
    draw_text(renderer, font, god_buf, dim_col, 40, 15);

    draw_text(renderer, font, "Choose your class.", title_col, 40, 35);

    int list_x = 40;
    int list_y = 70;

    for (int i = 0; i < CLASS_COUNT; i++) {
        auto& cls = get_class_info(static_cast<ClassId>(i));
        bool is_sel = (i == selected_);

        if (is_sel) {
            SDL_Rect hl = {list_x - 4, list_y, 300, 40};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        // Draw class sprite
        sprites.draw_sprite(renderer, SHEET_ROGUES, cls.sprite_x, cls.sprite_y,
                            list_x, list_y, 1);

        draw_text(renderer, font, cls.name, is_sel ? sel_col : normal_col,
                  list_x + 40, list_y + 4);
        draw_text(renderer, font, cls.description, dim_col,
                  list_x + 40, list_y + 4 + line_h);

        list_y += 48;
    }

    // Detail panel
    auto& cls = get_class_info(static_cast<ClassId>(selected_));
    int detail_x = 400;
    int detail_y = 70;
    int detail_w = w - detail_x - 40;

    SDL_Rect panel = {detail_x - 10, detail_y - 10, detail_w + 20, h - detail_y - 20};
    SDL_SetRenderDrawColor(renderer, 20, 16, 26, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderDrawRect(renderer, &panel);

    // Big sprite
    sprites.draw_sprite(renderer, SHEET_ROGUES, cls.sprite_x, cls.sprite_y,
                        detail_x + detail_w / 2 - 32, detail_y, 2);
    detail_y += 72;

    draw_text(renderer, font, cls.name, sel_col, detail_x, detail_y);
    detail_y += line_h + 4;
    draw_text_wrapped(renderer, font, cls.description, desc_col, detail_x, detail_y, detail_w);
    detail_y += line_h * 2 + 8;

    // Stats (class + god combined)
    draw_text(renderer, font, "Starting Attributes:", dim_col, detail_x, detail_y);
    detail_y += line_h + 2;

    struct StatLine { const char* name; int base; int bonus; };
    StatLine stats[] = {
        {"STR", cls.str,   god.str_bonus},
        {"DEX", cls.dex,   god.dex_bonus},
        {"CON", cls.con,   god.con_bonus},
        {"INT", cls.intel, god.int_bonus},
        {"WIL", cls.wil,   god.wil_bonus},
        {"PER", cls.per,   god.per_bonus},
        {"CHA", cls.cha,   god.cha_bonus},
    };

    for (auto& s : stats) {
        int total = s.base + s.bonus;
        char buf[48];
        if (s.bonus != 0) {
            snprintf(buf, sizeof(buf), "  %s: %d (%+d)", s.name, total, s.bonus);
        } else {
            snprintf(buf, sizeof(buf), "  %s: %d", s.name, total);
        }
        SDL_Color col = {180, 175, 170, 255};
        if (total >= 14) col = {140, 200, 140, 255};
        if (total <= 8) col = {200, 140, 140, 255};
        draw_text(renderer, font, buf, col, detail_x, detail_y);
        detail_y += line_h;
    }

    detail_y += 4;
    char hp_buf[48], mp_buf[48];
    snprintf(hp_buf, sizeof(hp_buf), "  HP: %d", cls.hp + god.bonus_hp);
    snprintf(mp_buf, sizeof(mp_buf), "  MP: %d", cls.mp + god.bonus_mp);
    draw_text(renderer, font, hp_buf, {200, 100, 100, 255}, detail_x, detail_y);
    detail_y += line_h;
    draw_text(renderer, font, mp_buf, {100, 100, 200, 255}, detail_x, detail_y);

    draw_text(renderer, font, "[Enter] select   [Up/Down] browse   [Esc] back",
              dim_col, 40, h - 30);
}
