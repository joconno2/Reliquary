#include "ui/spell_screen.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
#include "components/stats.h"
#include <cstdio>
#include <algorithm>
#include <vector>

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
    int count = static_cast<int>(book.known_spells.size());
    if (selected_ < 0 || selected_ >= count)
        return SpellId::COUNT;
    // Build same sorted index as render
    std::vector<int> sorted_idx(count);
    for (int i = 0; i < count; i++) sorted_idx[i] = i;
    std::sort(sorted_idx.begin(), sorted_idx.end(), [&](int a, int b) {
        return static_cast<int>(get_spell_info(book.known_spells[a]).school)
             < static_cast<int>(get_spell_info(book.known_spells[b]).school);
    });
    return book.known_spells[sorted_idx[selected_]];
}

void SpellScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                          World& world, int screen_w, int screen_h) const {
    if (!open_ || !font) return;
    if (!world.has<Spellbook>(player_)) return;

    auto& book = world.get<Spellbook>(player_);
    bool has_stats = world.has<Stats>(player_);

    int count = static_cast<int>(book.known_spells.size());
    if (selected_ >= count && count > 0) selected_ = count - 1;
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

    // Sort spells by school for display (build sorted index)
    std::vector<int> sorted_idx(count);
    for (int i = 0; i < count; i++) sorted_idx[i] = i;
    std::sort(sorted_idx.begin(), sorted_idx.end(), [&](int a, int b) {
        auto sa = static_cast<int>(get_spell_info(book.known_spells[a]).school);
        auto sb = static_cast<int>(get_spell_info(book.known_spells[b]).school);
        return sa < sb;
    });

    // Map selection to sorted order
    int sel = selected_;
    if (sel >= count && count > 0) sel = count - 1;

    // Reserve bottom space
    auto hint_row = panel.row_bottom(line_h);
    int desc_area_h = line_h * 4 + panel.gap * 2;
    auto desc_area = panel.row_bottom(desc_area_h);

    // School colors
    static const SDL_Color SCHOOL_COLORS[] = {
        {255, 160, 80, 255},  // Conjuration (orange)
        {200, 180, 120, 255}, // Transmutation (gold)
        {140, 170, 255, 255}, // Divination (blue)
        {120, 240, 140, 255}, // Healing (green)
        {100, 200, 80, 255},  // Nature (forest green)
        {200, 100, 220, 255}, // Dark Arts (purple)
    };
    static const char* SCHOOL_HEADERS[] = {
        "-- Conjuration --", "-- Transmutation --", "-- Divination --",
        "-- Healing --", "-- Nature --", "-- Dark Arts --"
    };

    // Clip rect for spell list
    SDL_Rect list_clip = {panel.cursor.x, panel.cursor.y,
                          panel.cursor.w, panel.remaining_h()};
    SDL_RenderSetClipRect(renderer, &list_clip);

    int row_h = line_h + 2;
    int header_h = line_h + 6;

    // Count total visual rows (spells + school headers)
    int total_rows = count;
    { int last_school = -1;
      for (int i = 0; i < count; i++) {
          int si = static_cast<int>(get_spell_info(book.known_spells[sorted_idx[i]]).school);
          if (si != last_school) { total_rows++; last_school = si; }
      }
    }

    int visible = panel.remaining_h() / row_h;
    if (visible < 1) visible = 1;

    // Scroll offset based on selected spell position
    int sel_visual_row = 0;
    { int last_school = -1;
      for (int i = 0; i < count; i++) {
          int si = static_cast<int>(get_spell_info(book.known_spells[sorted_idx[i]]).school);
          if (si != last_school) { sel_visual_row++; last_school = si; }
          if (i == sel) break;
          sel_visual_row++;
      }
    }
    int scroll = std::max(0, sel_visual_row - visible / 2);
    if (scroll > total_rows - visible) scroll = std::max(0, total_rows - visible);

    spell_rects_.clear();
    int visual_row = 0;
    int last_school = -1;
    for (int i = 0; i < count; i++) {
        auto& info = get_spell_info(book.known_spells[sorted_idx[i]]);
        int si = static_cast<int>(info.school);

        // School header
        if (si != last_school) {
            last_school = si;
            if (visual_row >= scroll && visual_row < scroll + visible && panel.fits(header_h)) {
                auto hdr_row = panel.row(header_h);
                SDL_Color hdr_col = (si >= 0 && si < 6) ? SCHOOL_COLORS[si] : dim_col;
                hdr_col.a = 180;
                ui::draw_text_in(renderer, font,
                    (si >= 0 && si < 6) ? SCHOOL_HEADERS[si] : "-- Unknown --",
                    hdr_col, hdr_row, ui::Align::LEFT);
            }
            visual_row++;
        }

        bool is_sel = (i == sel);

        if (visual_row >= scroll && visual_row < scroll + visible && panel.fits(row_h)) {
            auto spell_row = panel.row(row_h);
            spell_rects_.push_back(spell_row.sdl());

            if (is_sel) {
                SDL_Rect hl = spell_row.inset(panel.gap / 2, 0).sdl();
                SDL_SetRenderDrawColor(renderer, 30, 25, 45, 255);
                SDL_RenderFillRect(renderer, &hl);
            }

            // Spell name with school-tinted color
            char buf[128];
            snprintf(buf, sizeof(buf), "%s", info.name);
            int name_max = spell_row.w - 80;
            ui::Rect name_area = {spell_row.x, spell_row.y, name_max, spell_row.h};

            bool can_afford = true;
            if (world.has<Stats>(player_))
                can_afford = world.get<Stats>(player_).mp >= info.mp_cost;

            SDL_Color name_col;
            if (is_sel) name_col = sel_col;
            else if (!can_afford) name_col = dim_col;
            else if (si >= 0 && si < 6) name_col = SCHOOL_COLORS[si];
            else name_col = normal_col;

            ui::draw_text_clipped(renderer, font, buf, name_col,
                                   name_area.x, name_area.y, name_max);

            // MP cost right-aligned
            char cost[16];
            snprintf(cost, sizeof(cost), "%dmp", info.mp_cost);
            SDL_Color mp_col = can_afford ? cost_col : dim_col;
            ui::Rect cost_area = {spell_row.x2() - 55, spell_row.y, 55, spell_row.h};
            ui::draw_text_in(renderer, font, cost, mp_col, cost_area, ui::Align::RIGHT);
        } else if (visual_row >= scroll && visual_row < scroll + visible) {
            // Off-screen but in range: push dummy rect for click tracking
            spell_rects_.push_back({0, 0, 0, 0});
        }
        visual_row++;
    }

    SDL_RenderSetClipRect(renderer, nullptr);

    // Spell description for selected spell
    if (sel >= 0 && sel < count) {
        auto& info = get_spell_info(book.known_spells[sorted_idx[sel]]);
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

        // Power / range line
        char stat_line[64] = {};
        if (info.base_power > 0 && info.range > 0)
            snprintf(stat_line, sizeof(stat_line), "Power: %d   Range: %d", info.base_power, info.range);
        else if (info.base_power > 0)
            snprintf(stat_line, sizeof(stat_line), "Power: %d   Range: self", info.base_power);
        else if (info.range > 0)
            snprintf(stat_line, sizeof(stat_line), "Range: %d", info.range);
        else if (info.range == -1)
            snprintf(stat_line, sizeof(stat_line), "Range: all visible");
        if (stat_line[0]) {
            auto stat_row = desc_layout.row(line_h + 2);
            ui::draw_text(renderer, font, stat_line, {140, 180, 200, 255}, stat_row.x, stat_row.y);
        }

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
