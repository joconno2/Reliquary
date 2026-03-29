#include "ui/background_select.h"
#include "ui/ui_draw.h"
#include <cstdio>

bool BackgroundSelectScreen::handle_input(SDL_Event& event) {
    if (event.type != SDL_KEYDOWN) return false;

    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return true;
        case SDLK_DOWN:
        case SDLK_j:
            if (selected_ < BACKGROUND_COUNT - 1) selected_++;
            return true;
        case SDLK_RETURN:
        case SDLK_e:
            confirmed_ = true;
            return true;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            // Signal to caller to go back — we don't handle this ourselves
            return false;
        default:
            return false;
    }
}

void BackgroundSelectScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                                     int w, int h) const {
    if (!font) return;

    // Dark background
    SDL_SetRenderDrawColor(renderer, 8, 6, 10, 255);
    SDL_RenderClear(renderer);

    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col  = {200, 180, 160, 255};
    SDL_Color sel_col    = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color dim_col    = {100,  95,  90, 255};
    SDL_Color desc_col   = {140, 130, 120, 255};
    SDL_Color green_col  = {120, 200, 120, 255};

    int margin = w / 30;

    ui::draw_text_centered(renderer, font, "Choose your background.", title_col, w / 2, margin);

    // Centered two-column layout — 80% of screen
    int content_w = w * 4 / 5;
    int content_x = (w - content_w) / 2;
    int list_w = content_w * 2 / 5;
    int detail_w = content_w * 3 / 5 - margin;
    int list_x = content_x;
    int detail_x_bg = content_x + list_w + margin;

    int list_top = margin + line_h + 12;
    int list_bottom = h - line_h * 2;
    int row_h = std::min((list_bottom - list_top) / BACKGROUND_COUNT, line_h * 3);

    for (int i = 0; i < BACKGROUND_COUNT; i++) {
        const BackgroundInfo& bg = get_background_info(static_cast<BackgroundId>(i));
        bool is_sel = (i == selected_);
        int iy = list_top + i * row_h;

        if (is_sel) {
            ui::draw_panel(renderer, list_x - 4, iy - 2, list_w + 8, row_h - 2);
        }

        ui::draw_text(renderer, font, bg.name,
                     is_sel ? sel_col : normal_col, list_x + 6, iy + 4);
    }

    // Detail panel
    int detail_x = detail_x_bg;
    int detail_y = list_top;

    const BackgroundInfo& sel = get_background_info(static_cast<BackgroundId>(selected_));

    ui::draw_panel(renderer, detail_x - 10, detail_y - 10, detail_w + 20, h - detail_y - 20);

    ui::draw_text(renderer, font, sel.name, sel_col, detail_x, detail_y);
    detail_y += line_h + 6;

    ui::draw_text_wrapped(renderer, font, sel.description, desc_col,
                         detail_x, detail_y, detail_w);
    detail_y += line_h * 2 + 10;

    // Passive
    ui::draw_text(renderer, font, "Passive:", dim_col, detail_x, detail_y);
    detail_y += line_h + 2;
    ui::draw_text(renderer, font, sel.passive_name, sel_col, detail_x + 8, detail_y);
    detail_y += line_h + 2;
    ui::draw_text_wrapped(renderer, font, sel.passive_desc, desc_col,
                         detail_x + 8, detail_y, detail_w - 8);
    detail_y += line_h * 2 + 10;

    // Stat bonuses
    ui::draw_text(renderer, font, "Bonuses:", dim_col, detail_x, detail_y);
    detail_y += line_h + 2;

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
        char buf[32];
        snprintf(buf, sizeof(buf), "  %s %+d", b.label, b.val);
        SDL_Color col = b.val > 0
            ? SDL_Color{120, 200, 120, 255}
            : SDL_Color{200, 120, 120, 255};
        ui::draw_text(renderer, font, buf, col, detail_x, detail_y);
        detail_y += line_h;
    }
    if (!any_bonus) {
        ui::draw_text(renderer, font, "  (none)", dim_col, detail_x, detail_y);
        detail_y += line_h;
    }

    // Controls hint
    ui::draw_text(renderer, font,
                 "[Enter] select   [Up/Down] browse   [Esc] back",
                 dim_col, 40, h - 30);

    (void)green_col; // used if needed for extension
}
