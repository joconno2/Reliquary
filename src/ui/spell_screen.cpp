#include "ui/spell_screen.h"
#include "ui/ui_draw.h"
#include "components/stats.h"
#include <cstdio>

SpellAction SpellScreen::handle_input(SDL_Event& event) {
    if (!open_) return SpellAction::NONE;
    if (event.type != SDL_KEYDOWN) return SpellAction::NONE;

    switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_z:
            return SpellAction::CLOSE;
        case SDLK_UP:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return SpellAction::NONE;
        case SDLK_DOWN:
        case SDLK_j:
            selected_++;
            return SpellAction::NONE;
        case SDLK_RETURN:
        case SDLK_c:
            return SpellAction::CAST;
        default:
            // a-z quick select
            if (event.key.keysym.sym >= SDLK_a && event.key.keysym.sym <= SDLK_z) {
                int idx = event.key.keysym.sym - SDLK_a;
                selected_ = idx;
            }
            return SpellAction::NONE;
    }
}

SpellId SpellScreen::get_selected_spell(World& world) const {
    if (!world.has<Spellbook>(player_)) return SpellId::COUNT;
    auto& book = world.get<Spellbook>(player_);
    if (selected_ < 0 || selected_ >= static_cast<int>(book.known_spells.size()))
        return SpellId::COUNT;
    return book.known_spells[selected_];
}

void SpellScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                          World& world, int screen_w, int screen_h) const {
    if (!open_ || !font) return;
    if (!world.has<Spellbook>(player_)) return;

    auto& book = world.get<Spellbook>(player_);
    bool has_stats = world.has<Stats>(player_);

    int panel_w = 460;
    int panel_h = screen_h - 40;
    int panel_x = (screen_w - panel_w) / 2;
    int panel_y = 20;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    ui::draw_panel(renderer, panel_x, panel_y, panel_w, panel_h);

    int y = panel_y + 8;
    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col = {180, 160, 200, 255};
    SDL_Color sel_col = {220, 200, 255, 255};
    SDL_Color normal_col = {160, 155, 170, 255};
    SDL_Color dim_col = {100, 95, 110, 255};
    SDL_Color cost_col = {120, 120, 180, 255};

    // Title + MP
    if (has_stats) {
        auto& stats = world.get<Stats>(player_);
        char title[64];
        snprintf(title, sizeof(title), "Spells  (MP: %d/%d)", stats.mp, stats.mp_max);
        ui::draw_text(renderer, font, title, title_col, panel_x + 10, y);
    } else {
        ui::draw_text(renderer, font, "Spells", title_col, panel_x + 10, y);
    }
    y += line_h + 6;

    if (book.known_spells.empty()) {
        ui::draw_text(renderer, font, "You know no spells.", dim_col, panel_x + 10, y);
    }

    int sel = selected_;
    int count = static_cast<int>(book.known_spells.size());
    if (sel >= count && count > 0) sel = count - 1;

    for (int i = 0; i < count; i++) {
        auto& info = get_spell_info(book.known_spells[i]);
        bool is_sel = (i == sel);

        if (is_sel) {
            SDL_Rect hl = {panel_x + 4, y, panel_w - 8, line_h + 2};
            SDL_SetRenderDrawColor(renderer, 30, 25, 45, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        char letter = 'a' + static_cast<char>(i);
        char buf[128];
        snprintf(buf, sizeof(buf), "%c) %s", letter, info.name);
        ui::draw_text(renderer, font, buf, is_sel ? sel_col : normal_col, panel_x + 10, y);

        char cost[16];
        snprintf(cost, sizeof(cost), "%d mp", info.mp_cost);
        ui::draw_text(renderer, font, cost, cost_col, panel_x + panel_w - 60, y);

        y += line_h + 2;
    }

    // Description of selected spell
    if (sel >= 0 && sel < count) {
        auto& info = get_spell_info(book.known_spells[sel]);
        y += 8;
        SDL_Rect sep = {panel_x + 8, y, panel_w - 16, 1};
        SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
        SDL_RenderFillRect(renderer, &sep);
        y += 6;

        // Flavor text
        ui::draw_text_wrapped(renderer, font, info.description, dim_col,
                              panel_x + 10, y, panel_w - 20);
    }

    // Hints
    ui::draw_text(renderer, font, "[c/enter]cast  [z/esc]close", dim_col, panel_x + 10, panel_y + panel_h - line_h - 8);
}
