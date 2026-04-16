#include "ui/trait_select.h"
#include "ui/ui_draw.h"
#include <algorithm>
#include <cstdio>

void TraitSelectScreen::reset() {
    cursor_ = 0;
    confirmed_ = false;
    selected_traits_.clear();
}

bool TraitSelectScreen::is_selected(TraitId id) const {
    for (auto t : selected_traits_) {
        if (t == id) return true;
    }
    return false;
}

int TraitSelectScreen::positive_selected_count() const {
    int count = 0;
    for (auto t : selected_traits_) {
        if (get_trait_info(t).is_positive) count++;
    }
    return count;
}

int TraitSelectScreen::negative_selected_count() const {
    int count = 0;
    for (auto t : selected_traits_) {
        if (!get_trait_info(t).is_positive) count++;
    }
    return count;
}

bool TraitSelectScreen::can_confirm() const {
    return positive_selected_count() == 3 && negative_selected_count() == 2;
}

bool TraitSelectScreen::handle_input(SDL_Event& event) {
    auto toggle_trait = [&]() {
        if (can_confirm()) {
            confirmed_ = true;
            return;
        }
        TraitId id = static_cast<TraitId>(cursor_);
        const TraitInfo& info = get_trait_info(id);
        if (is_selected(id)) {
            selected_traits_.erase(
                std::remove(selected_traits_.begin(), selected_traits_.end(), id),
                selected_traits_.end());
        } else {
            if (info.is_positive && positive_selected_count() < 3) {
                selected_traits_.push_back(id);
                if (positive_selected_count() >= 3 && cursor_ < POSITIVE_TRAIT_COUNT) {
                    cursor_ = POSITIVE_TRAIT_COUNT;
                }
            } else if (!info.is_positive && negative_selected_count() < 2) {
                selected_traits_.push_back(id);
            }
        }
    };

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        if (row_h_ > 0) {
            int click_y = event.button.y;
            int rel_y = click_y - list_y_;
            if (rel_y >= 0) {
                int idx = rel_y / row_h_;
                if (idx >= 0 && idx < TRAIT_COUNT) {
                    cursor_ = idx;
                    toggle_trait();
                    return true;
                }
            }
        }
        return false;
    }

    if (event.type != SDL_KEYDOWN) return false;

    switch (event.key.keysym.sym) {
        case SDLK_UP: case SDLK_w: case SDLK_k:
            if (cursor_ > 0) cursor_--;
            return true;
        case SDLK_DOWN: case SDLK_s: case SDLK_j:
            if (cursor_ < TRAIT_COUNT - 1) cursor_++;
            return true;
        case SDLK_RETURN: case SDLK_e:
            toggle_trait();
            return true;
        case SDLK_SPACE:
            if (can_confirm()) { confirmed_ = true; }
            return true;
        case SDLK_ESCAPE: case SDLK_BACKSPACE:
            return false;
        default:
            return false;
    }
}

void TraitSelectScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                                int w, int h) const {
    if (!font) return;

    SDL_SetRenderDrawColor(renderer, 8, 6, 10, 255);
    SDL_RenderClear(renderer);

    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col   = {200, 180, 160, 255};
    SDL_Color sel_col     = {255, 220, 140, 255};
    SDL_Color normal_col  = {160, 155, 150, 255};
    SDL_Color dim_col     = {100,  95,  90, 255};
    SDL_Color desc_col    = {140, 130, 120, 255};
    SDL_Color chosen_col  = {120, 200, 120, 255};
    SDL_Color neg_col     = {200, 120, 120, 255};
    SDL_Color section_col = {180, 160, 100, 255};

    auto screen = ui::Layout::from_screen(w, h, line_h);

    // Title + counter
    auto title_row = screen.row(line_h + 2);
    ui::draw_text_in(renderer, font, "Choose your traits.", title_col, title_row, ui::Align::CENTER);

    char count_buf[64];
    snprintf(count_buf, sizeof(count_buf), "Positive: %d/3   Negative: %d/2",
             positive_selected_count(), negative_selected_count());
    auto counter_row = screen.row(line_h + screen.gap);
    ui::draw_text_in(renderer, font, count_buf, dim_col, counter_row, ui::Align::CENTER);

    // Reserve hint row at bottom
    auto hint_row = screen.row_bottom(line_h + screen.pad);

    // Content: 80% width, split 2:3 list vs detail
    auto content_rect = screen.cursor.inset(w / 10, 0);
    auto cols = ui::Layout::from_rect(content_rect, line_h).split_cols_ratio(2, 3);
    auto list_layout = ui::Layout::col(cols[0], line_h);
    auto detail_rect = cols[1];

    // Scale row height to fill list (account for 2 section headers + gap)
    int avail_for_traits = list_layout.cursor.h - line_h * 2 - 16;
    int row_h = std::max(line_h + 6, avail_for_traits / TRAIT_COUNT);
    row_h_ = row_h;

    // Section header: Positive
    auto pos_hdr = list_layout.row(line_h + 4);
    ui::draw_text(renderer, font, "-- Positive Traits --", section_col, pos_hdr.x, pos_hdr.y);
    list_y_ = list_layout.cursor.y;

    for (int i = 0; i < POSITIVE_TRAIT_COUNT; i++) {
        TraitId id = static_cast<TraitId>(i);
        const TraitInfo& info = get_trait_info(id);
        bool is_cursor = (cursor_ == i);
        bool is_picked = is_selected(id);

        auto row = list_layout.row(row_h);

        if (is_cursor) {
            SDL_Rect hl = {row.x - 4, row.y - 1, row.w + 8, row.h};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "%s %s", is_picked ? "[x]" : "[ ]", info.name);
        SDL_Color text_col = is_picked ? chosen_col : is_cursor ? sel_col : normal_col;
        ui::draw_text(renderer, font, buf, text_col, row.x, row.y);
    }

    list_layout.skip(6);

    // Section header: Negative
    auto neg_hdr = list_layout.row(line_h + 4);
    ui::draw_text(renderer, font, "-- Negative Traits --", section_col, neg_hdr.x, neg_hdr.y);

    for (int i = POSITIVE_TRAIT_COUNT; i < TRAIT_COUNT; i++) {
        TraitId id = static_cast<TraitId>(i);
        const TraitInfo& info = get_trait_info(id);
        bool is_cursor = (cursor_ == i);
        bool is_picked = is_selected(id);

        auto row = list_layout.row(row_h);

        if (is_cursor) {
            SDL_Rect hl = {row.x - 4, row.y - 1, row.w + 8, row.h};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "%s %s", is_picked ? "[x]" : "[ ]", info.name);
        SDL_Color text_col = is_picked ? neg_col : is_cursor ? sel_col : normal_col;
        ui::draw_text(renderer, font, buf, text_col, row.x, row.y);
    }

    // Detail panel
    auto detail = ui::draw_panel_in(renderer, detail_rect, line_h);

    const TraitInfo& cur = get_trait_info(static_cast<TraitId>(cursor_));

    SDL_Color name_col = cur.is_positive ? chosen_col : neg_col;
    auto dname = detail.row(line_h + 8);
    ui::draw_text(renderer, font, cur.name, name_col, dname.x, dname.y);

    int tdesc_h = ui::text_wrapped_height(font, cur.description, detail.cursor.w - detail.pad);
    auto tdesc = detail.row(tdesc_h + 12);
    ui::draw_text_wrapped(renderer, font, cur.description, desc_col,
                         tdesc.x, tdesc.y, detail.cursor.w - detail.pad);

    // Stat modifiers
    auto mod_label = detail.row(line_h + 2);
    ui::draw_text(renderer, font, "Modifiers:", dim_col, mod_label.x, mod_label.y);

    struct ModPair { const char* label; int val; };
    ModPair mods[] = {
        {"STR", cur.str_mod}, {"DEX", cur.dex_mod}, {"CON", cur.con_mod},
        {"INT", cur.int_mod}, {"WIL", cur.wil_mod}, {"PER", cur.per_mod},
        {"CHA", cur.cha_mod},
    };

    bool any_mod = false;
    for (auto& m : mods) {
        if (m.val == 0) continue;
        any_mod = true;
        auto mrow = detail.row();
        char buf[32];
        snprintf(buf, sizeof(buf), "  %s %+d", m.label, m.val);
        SDL_Color col = m.val > 0
            ? SDL_Color{120, 200, 120, 255}
            : SDL_Color{200, 120, 120, 255};
        ui::draw_text(renderer, font, buf, col, mrow.x, mrow.y);
    }
    if (!any_mod) {
        auto mrow = detail.row();
        ui::draw_text(renderer, font, "  (no stat changes)", dim_col, mrow.x, mrow.y);
    }

    // Gameplay modifiers
    detail.skip(4);
    auto draw_gmod = [&](const char* fmt_str, int val, bool positive) {
        if (val == 0) return;
        auto gr = detail.row();
        char buf[48];
        snprintf(buf, sizeof(buf), fmt_str, val);
        ui::draw_text(renderer, font, buf, positive ? chosen_col : neg_col, gr.x, gr.y);
    };

    draw_gmod("  HP %+d", cur.bonus_hp, cur.bonus_hp > 0);
    draw_gmod("  Armor %+d", cur.bonus_natural_armor, cur.bonus_natural_armor > 0);
    draw_gmod("  Speed %+d", cur.bonus_speed, cur.bonus_speed > 0);
    draw_gmod("  FOV %+d", cur.bonus_fov, cur.bonus_fov > 0);
    if (cur.fire_resist != 0) draw_gmod("  Fire resist %+d%%", cur.fire_resist, cur.fire_resist > 0);
    if (cur.poison_resist != 0) draw_gmod("  Poison resist %+d%%", cur.poison_resist, cur.poison_resist > 0);
    if (cur.bleed_resist != 0) draw_gmod("  Bleed resist %+d%%", cur.bleed_resist, cur.bleed_resist > 0);
    if (cur.hp_on_kill > 0) {
        auto gr = detail.row();
        char buf[32]; snprintf(buf, sizeof(buf), "  Heal %d per kill", cur.hp_on_kill);
        ui::draw_text(renderer, font, buf, chosen_col, gr.x, gr.y);
    }
    if (cur.immune_fear) {
        auto gr = detail.row();
        ui::draw_text(renderer, font, "  Immune to fear", chosen_col, gr.x, gr.y);
    }
    if (cur.immune_confuse) {
        auto gr = detail.row();
        ui::draw_text(renderer, font, "  Immune to confusion", chosen_col, gr.x, gr.y);
    }

    // Confirm hint
    if (can_confirm()) {
        ui::draw_text_in(renderer, font, "[Enter/Space] CONFIRM SELECTION",
                         sel_col, hint_row, ui::Align::LEFT);
    } else {
        ui::draw_text_in(renderer, font,
                         "[Enter] toggle   [Up/Down] browse   [Esc] back",
                         dim_col, hint_row, ui::Align::LEFT);
    }
}
