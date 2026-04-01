#include "ui/spell_screen.h"
#include "ui/ui_draw.h"
#include "components/stats.h"
#include <cstdio>
#include <algorithm>

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
        case SDLK_q:
            return SpellAction::QUICKCAST;
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

    int count = static_cast<int>(book.known_spells.size());
    int line_h_est = TTF_FontLineSkip(font);
    int panel_w = std::min(screen_w * 2 / 5, 600);
    int panel_h = std::min(screen_h * 3 / 4, screen_h - 120);
    int panel_x = screen_w - panel_w - 20;
    int panel_y = (screen_h - panel_h) / 2;

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
    if (sel >= count && count > 0) sel = count - 1;

    // Scrollable list area — reserve bottom for description + hint
    int list_area_h = panel_h - 100 - line_h * 5; // room for desc at bottom
    int visible_spells = list_area_h / (line_h + 2);
    if (visible_spells < 1) visible_spells = 1;

    // Scroll offset
    int scroll = 0;
    if (count > visible_spells) {
        scroll = sel - visible_spells / 2;
        if (scroll < 0) scroll = 0;
        if (scroll > count - visible_spells) scroll = count - visible_spells;
    }

    for (int i = scroll; i < count && i < scroll + visible_spells; i++) {
        auto& info = get_spell_info(book.known_spells[i]);
        bool is_sel = (i == sel);

        if (is_sel) {
            SDL_Rect hl = {panel_x + 4, y, panel_w - 8, line_h + 2};
            SDL_SetRenderDrawColor(renderer, 30, 25, 45, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        char letter = 'a' + static_cast<char>(i);
        char buf[128];
        // Truncate spell name to fit before school/cost columns
        int name_max = panel_w - 140; // leave room for school + cost
        snprintf(buf, sizeof(buf), "%c) %s", letter, info.name);
        // Clip rendering to name column
        SDL_Rect name_clip = {panel_x + 10, y, name_max, line_h + 2};
        SDL_RenderSetClipRect(renderer, &name_clip);
        ui::draw_text(renderer, font, buf, is_sel ? sel_col : normal_col, panel_x + 10, y);
        SDL_RenderSetClipRect(renderer, nullptr);

        // School tag + MP cost in fixed right columns
        static const char* SCHOOL_NAMES[] = {"CONJ", "TRAN", "DIV", "HEAL", "NAT", "DARK"};
        int si = static_cast<int>(info.school);
        if (si >= 0 && si < 6)
            ui::draw_text(renderer, font, SCHOOL_NAMES[si], dim_col, panel_x + panel_w - 115, y);

        char cost[16];
        snprintf(cost, sizeof(cost), "%dmp", info.mp_cost);
        ui::draw_text(renderer, font, cost, cost_col, panel_x + panel_w - 55, y);

        y += line_h + 2;
    }

    // Spell description for selected spell
    if (sel >= 0 && sel < count) {
        auto& info = get_spell_info(book.known_spells[sel]);
        int desc_y = panel_y + panel_h - line_h * 4 - 20;

        // Divider
        SDL_SetRenderDrawColor(renderer, 60, 55, 70, 255);
        SDL_RenderDrawLine(renderer, panel_x + 10, desc_y - 4, panel_x + panel_w - 10, desc_y - 4);

        // Spell name + school
        static const char* SCHOOL_FULL[] = {"Conjuration", "Transmutation", "Divination", "Healing", "Nature", "Dark Arts"};
        int si = static_cast<int>(info.school);
        char header[128];
        snprintf(header, sizeof(header), "%s  (%s, %d MP)", info.name,
                 (si >= 0 && si < 6) ? SCHOOL_FULL[si] : "?", info.mp_cost);
        ui::draw_text(renderer, font, header, sel_col, panel_x + 10, desc_y);
        desc_y += line_h + 2;

        // Description
        ui::draw_text_wrapped(renderer, font, info.description, normal_col,
                               panel_x + 10, desc_y, panel_w - 20);
    }

    // Hint
    ui::draw_text(renderer, font, "[enter]cast [q]quick-cast [esc]close", dim_col, panel_x + 10, panel_y + panel_h - line_h - 6);
}
