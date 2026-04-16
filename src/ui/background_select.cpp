#include <algorithm>
#include "ui/background_select.h"
#include "ui/ui_draw.h"
#include <cstdio>

bool BackgroundSelectScreen::handle_input(SDL_Event& event) {
    // Mouse support
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        if (row_h_ > 0) {
            int rel_y = event.button.y - list_y_;
            if (rel_y >= 0) {
                int idx = rel_y / row_h_;
                if (idx >= 0 && idx < BACKGROUND_COUNT) {
                    selected_ = idx;
                    confirmed_ = true;
                    return true;
                }
            }
        }
        return false;
    }

    if (event.type != SDL_KEYDOWN) return false;

    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_w:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return true;
        case SDLK_DOWN:
        case SDLK_s:
        case SDLK_j:
            if (selected_ < BACKGROUND_COUNT - 1) selected_++;
            return true;
        case SDLK_RETURN:
        case SDLK_e:
            confirmed_ = true;
            return true;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            return false;
        default:
            return false;
    }
}

void BackgroundSelectScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                                     int w, int h) const {
    if (!font) return;

    SDL_SetRenderDrawColor(renderer, 8, 6, 10, 255);
    SDL_RenderClear(renderer);

    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col  = {200, 180, 160, 255};
    SDL_Color sel_col    = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color dim_col    = {100,  95,  90, 255};
    SDL_Color desc_col   = {140, 130, 120, 255};
    SDL_Color green_col  = {120, 200, 120, 255};

    auto screen = ui::Layout::from_screen(w, h, line_h);

    // Header
    auto header = screen.row(line_h + screen.gap);
    ui::draw_text_in(renderer, font, "Choose your background.", title_col, header, ui::Align::CENTER);

    // Content: 80% width, split 2:3 list vs detail
    screen.skip(screen.gap);
    auto hint_row = screen.row_bottom(line_h + screen.pad);
    auto content_rect = screen.cursor.inset(w / 10, 0);
    auto cols = ui::Layout::from_rect(content_rect, line_h).split_cols_ratio(2, 3);
    auto list_area = ui::Layout::col(cols[0], line_h);
    auto detail_rect = cols[1];

    // Scale row height to fill list
    int row_h = std::max(line_h + 6, list_area.cursor.h / BACKGROUND_COUNT);
    row_h_ = row_h;
    list_y_ = list_area.cursor.y;

    for (int i = 0; i < BACKGROUND_COUNT; i++) {
        const BackgroundInfo& bg = get_background_info(static_cast<BackgroundId>(i));
        bool is_sel = (i == selected_);
        auto row = list_area.row(row_h);

        if (is_sel) {
            ui::draw_panel(renderer, row.x - 4, row.y - 2, row.w + 8, row.h - 2);
        }

        ui::draw_text(renderer, font, bg.name,
                     is_sel ? sel_col : normal_col, row.x + 6, row.y + 4);
    }

    // Detail panel
    auto detail = ui::draw_panel_in(renderer, detail_rect, line_h);

    const BackgroundInfo& sel = get_background_info(static_cast<BackgroundId>(selected_));

    auto name_row = detail.row(line_h + 8);
    ui::draw_text(renderer, font, sel.name, sel_col, name_row.x, name_row.y);

    int desc_h = ui::text_wrapped_height(font, sel.description, detail.cursor.w - detail.pad);
    auto desc_area = detail.row(desc_h + 12);
    ui::draw_text_wrapped(renderer, font, sel.description, desc_col,
                         desc_area.x, desc_area.y, detail.cursor.w - detail.pad);

    // Passive
    auto passive_label = detail.row();
    ui::draw_text(renderer, font, "Passive:", dim_col, passive_label.x, passive_label.y);
    detail.skip(4);
    auto passive_name = detail.row(line_h + 4);
    ui::draw_text(renderer, font, sel.passive_name, sel_col, passive_name.x + 8, passive_name.y);
    int pdesc_h = ui::text_wrapped_height(font, sel.passive_desc, detail.cursor.w - detail.pad - 8);
    auto pdesc_area = detail.row(pdesc_h + 12);
    ui::draw_text_wrapped(renderer, font, sel.passive_desc, desc_col,
                         pdesc_area.x + 8, pdesc_area.y, detail.cursor.w - detail.pad - 8);

    // Stat bonuses
    auto bonus_label = detail.row(line_h + 2);
    ui::draw_text(renderer, font, "Bonuses:", dim_col, bonus_label.x, bonus_label.y);

    struct BonusPair { const char* label; int val; };
    BonusPair bonuses[] = {
        {"STR", sel.str_bonus}, {"DEX", sel.dex_bonus}, {"CON", sel.con_bonus},
        {"INT", sel.int_bonus}, {"WIL", sel.wil_bonus}, {"PER", sel.per_bonus},
        {"CHA", sel.cha_bonus}, {"HP",  sel.bonus_hp},  {"DMG", sel.bonus_damage},
    };

    bool any_bonus = false;
    for (auto& b : bonuses) {
        if (b.val == 0) continue;
        any_bonus = true;
        auto brow = detail.row();
        char buf[32];
        snprintf(buf, sizeof(buf), "  %s %+d", b.label, b.val);
        SDL_Color col = b.val > 0
            ? SDL_Color{120, 200, 120, 255}
            : SDL_Color{200, 120, 120, 255};
        ui::draw_text(renderer, font, buf, col, brow.x, brow.y);
    }
    if (!any_bonus) {
        auto brow = detail.row();
        ui::draw_text(renderer, font, "  (none)", dim_col, brow.x, brow.y);
    }

    // Controls hint
    ui::draw_text_in(renderer, font,
                     "[Enter] select   [Up/Down] browse   [Esc] back",
                     dim_col, hint_row, ui::Align::LEFT);

    (void)green_col;
}
