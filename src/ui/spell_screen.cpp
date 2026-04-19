#include "ui/spell_screen.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
#include "components/stats.h"
#include <cstdio>
#include <algorithm>

SpellAction SpellScreen::handle_input(SDL_Event& event) {
    if (!open_) return SpellAction::NONE;

    // Mouse click: select or cast
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mx = event.button.x, my = event.button.y;
        for (int i = 0; i < static_cast<int>(spell_rects_.size()); i++) {
            auto& r = spell_rects_[i];
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
                if (i == selected_) return SpellAction::CAST;
                selected_ = i;
                return SpellAction::NONE;
            }
        }
        return SpellAction::NONE;
    }
    // Right-click: quickcast
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
        int mx = event.button.x, my = event.button.y;
        for (int i = 0; i < static_cast<int>(spell_rects_.size()); i++) {
            auto& r = spell_rects_[i];
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
                selected_ = i;
                return SpellAction::QUICKCAST;
            }
        }
        return SpellAction::NONE;
    }
    // Mouse hover
    if (event.type == SDL_MOUSEMOTION) {
        int mx = event.motion.x, my = event.motion.y;
        for (int i = 0; i < static_cast<int>(spell_rects_.size()); i++) {
            auto& r = spell_rects_[i];
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
                selected_ = i; break;
            }
        }
        return SpellAction::NONE;
    }
    // Mouse wheel scroll
    if (event.type == SDL_MOUSEWHEEL) {
        if (event.wheel.y > 0 && selected_ > 0) selected_--;
        else if (event.wheel.y < 0) selected_++;
        return SpellAction::NONE;
    }

    if (event.type != SDL_KEYDOWN) return SpellAction::NONE;

    switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_z:
            return SpellAction::CLOSE;
        case SDLK_UP:
        case SDLK_w:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return SpellAction::NONE;
        case SDLK_DOWN:
        case SDLK_s:
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
    int line_h = TTF_FontLineSkip(font);

    SDL_Color title_col = {180, 160, 200, 255};
    SDL_Color sel_col = {220, 200, 255, 255};
    SDL_Color normal_col = {160, 155, 170, 255};
    SDL_Color dim_col = {100, 95, 110, 255};
    SDL_Color cost_col = {120, 120, 180, 255};

    // Right-aligned panel: 2/5 screen width, 3/4 screen height
    auto screen = ui::Layout::from_screen(screen_w, screen_h, line_h);
    ui::Rect outer_full = screen.bounds.right_frac(2, 5);
    // Vertically center within 3/4 of screen height
    int panel_h = screen_h * 3 / 4;
    ui::Rect outer = {outer_full.x, (screen_h - panel_h) / 2, outer_full.w, panel_h};

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    auto panel = ui::draw_panel_in(renderer, outer, line_h);

    // Title + MP
    auto title_row = panel.row(line_h + panel.gap);
    if (has_stats) {
        auto& stats = world.get<Stats>(player_);
        char title[64];
        snprintf(title, sizeof(title), "Spells  (MP: %d/%d)", stats.mp, stats.mp_max);
        ui::draw_text_in(renderer, font, title, title_col, title_row, ui::Align::LEFT);
    } else {
        ui::draw_text_in(renderer, font, "Spells", title_col, title_row, ui::Align::LEFT);
    }

    if (book.known_spells.empty()) {
        auto empty_row = panel.row();
        ui::draw_text_in(renderer, font, "You know no spells.", dim_col, empty_row, ui::Align::LEFT);
    }

    int sel = selected_;
    if (sel >= count && count > 0) sel = count - 1;

    // Reserve bottom space: description area (4 lines) + hint row
    int desc_reserve = line_h * 4 + panel.gap * 2 + line_h + panel.pad;
    auto hint_row = panel.row_bottom(line_h);
    panel.skip(0); // no-op, just for clarity
    // Reserve description area from bottom
    int desc_area_h = line_h * 4 + panel.gap * 2;
    auto desc_area = panel.row_bottom(desc_area_h);

    // Remaining space is the spell list
    int list_h = panel.remaining_h();
    int row_h = line_h + 2;
    int visible_spells = list_h / row_h;
    if (visible_spells < 1) visible_spells = 1;

    // Scroll offset
    int scroll = 0;
    if (count > visible_spells) {
        scroll = sel - visible_spells / 2;
        if (scroll < 0) scroll = 0;
        if (scroll > count - visible_spells) scroll = count - visible_spells;
    }

    spell_rects_.clear();
    for (int i = scroll; i < count && i < scroll + visible_spells; i++) {
        auto& info = get_spell_info(book.known_spells[i]);
        bool is_sel = (i == sel);

        auto spell_row = panel.row(row_h);
        spell_rects_.push_back(spell_row.sdl());

        if (is_sel) {
            ui::Rect hl = spell_row.inset(panel.gap / 2, 0);
            SDL_Rect hl_sdl = hl.sdl();
            SDL_SetRenderDrawColor(renderer, 30, 25, 45, 255);
            SDL_RenderFillRect(renderer, &hl_sdl);
        }

        char letter = 'a' + static_cast<char>(i);
        char buf[128];
        snprintf(buf, sizeof(buf), "%c) %s", letter, info.name);

        // Name column: clip to leave room for school + cost
        int name_max = spell_row.w - 140;
        ui::Rect name_area = {spell_row.x, spell_row.y, name_max, spell_row.h};
        SDL_Rect name_clip = name_area.sdl();
        SDL_RenderSetClipRect(renderer, &name_clip);
        ui::draw_text_in(renderer, font, buf, is_sel ? sel_col : normal_col, name_area, ui::Align::LEFT);
        SDL_RenderSetClipRect(renderer, nullptr);

        // School tag in fixed right column
        static const char* SCHOOL_NAMES[] = {"CONJ", "TRAN", "DIV", "HEAL", "NAT", "DARK"};
        int si = static_cast<int>(info.school);
        if (si >= 0 && si < 6) {
            ui::Rect school_area = {spell_row.x2() - 115, spell_row.y, 60, spell_row.h};
            ui::draw_text_in(renderer, font, SCHOOL_NAMES[si], dim_col, school_area, ui::Align::LEFT);
        }

        // MP cost in rightmost column
        char cost[16];
        snprintf(cost, sizeof(cost), "%dmp", info.mp_cost);
        ui::Rect cost_area = {spell_row.x2() - 55, spell_row.y, 55, spell_row.h};
        ui::draw_text_in(renderer, font, cost, cost_col, cost_area, ui::Align::LEFT);
    }

    // Spell description for selected spell
    if (sel >= 0 && sel < count) {
        auto& info = get_spell_info(book.known_spells[sel]);
        auto desc_layout = ui::Layout::from_rect(desc_area, line_h);

        // Divider line
        SDL_SetRenderDrawColor(renderer, 60, 55, 70, 255);
        SDL_RenderDrawLine(renderer, desc_area.x, desc_area.y,
                           desc_area.x2(), desc_area.y);
        desc_layout.skip(panel.gap);

        // Spell name + school + cost
        static const char* SCHOOL_FULL[] = {"Conjuration", "Transmutation", "Divination", "Healing", "Nature", "Dark Arts"};
        int si = static_cast<int>(info.school);
        char header[128];
        snprintf(header, sizeof(header), "%s  (%s, %d MP)", info.name,
                 (si >= 0 && si < 6) ? SCHOOL_FULL[si] : "?", info.mp_cost);
        auto header_row = desc_layout.row(line_h + 2);
        ui::draw_text_in(renderer, font, header, sel_col, header_row, ui::Align::LEFT);

        // Description (wrapped into remaining desc space)
        ui::draw_text_wrapped(renderer, font, info.description, normal_col,
                               desc_layout.cursor.x, desc_layout.cursor.y,
                               desc_layout.cursor.w);
    }

    // Hint at bottom
    { auto* ig = InputGlyphs::get();
      char hbuf[256];
      if (ig && ig->using_gamepad())
          snprintf(hbuf, sizeof(hbuf), "%s cast  (X) quick-cast  %s close",
                   ig->confirm().c_str(), ig->cancel().c_str());
      else
          snprintf(hbuf, sizeof(hbuf), "[enter]cast [q]quick-cast [esc]close");
      ui::draw_text_in(renderer, font, hbuf, dim_col, hint_row, ui::Align::LEFT); }
}
