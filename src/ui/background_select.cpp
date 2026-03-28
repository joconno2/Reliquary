#include "ui/background_select.h"
#include <cstdio>

static void bg_draw_text(SDL_Renderer* renderer, TTF_Font* font,
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

static void bg_draw_text_wrapped(SDL_Renderer* renderer, TTF_Font* font,
                                  const char* text, SDL_Color col, int x, int y, int wrap_w) {
    if (!font || !text || !text[0]) return;
    SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(font, text, col, static_cast<Uint32>(wrap_w));
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

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

    bg_draw_text(renderer, font, "Choose your background.", title_col, 40, 30);

    int list_x = 40;
    int list_y = 60;
    int row_h  = line_h + 6;

    for (int i = 0; i < BACKGROUND_COUNT; i++) {
        const BackgroundInfo& bg = get_background_info(static_cast<BackgroundId>(i));
        bool is_sel = (i == selected_);

        if (is_sel) {
            SDL_Rect hl = {list_x - 4, list_y, 300, row_h};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        bg_draw_text(renderer, font, bg.name,
                     is_sel ? sel_col : normal_col, list_x, list_y);
        list_y += row_h;
    }

    // Detail panel on the right
    int detail_x = 380;
    int detail_y = 40;
    int detail_w = w - detail_x - 40;

    const BackgroundInfo& sel = get_background_info(static_cast<BackgroundId>(selected_));

    SDL_Rect panel = {detail_x - 10, detail_y - 10, detail_w + 20, h - detail_y - 20};
    SDL_SetRenderDrawColor(renderer, 20, 16, 26, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderDrawRect(renderer, &panel);

    bg_draw_text(renderer, font, sel.name, sel_col, detail_x, detail_y);
    detail_y += line_h + 6;

    bg_draw_text_wrapped(renderer, font, sel.description, desc_col,
                         detail_x, detail_y, detail_w);
    detail_y += line_h * 2 + 10;

    // Passive
    bg_draw_text(renderer, font, "Passive:", dim_col, detail_x, detail_y);
    detail_y += line_h + 2;
    bg_draw_text(renderer, font, sel.passive_name, sel_col, detail_x + 8, detail_y);
    detail_y += line_h + 2;
    bg_draw_text_wrapped(renderer, font, sel.passive_desc, desc_col,
                         detail_x + 8, detail_y, detail_w - 8);
    detail_y += line_h * 2 + 10;

    // Stat bonuses
    bg_draw_text(renderer, font, "Bonuses:", dim_col, detail_x, detail_y);
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
        bg_draw_text(renderer, font, buf, col, detail_x, detail_y);
        detail_y += line_h;
    }
    if (!any_bonus) {
        bg_draw_text(renderer, font, "  (none)", dim_col, detail_x, detail_y);
        detail_y += line_h;
    }

    // Controls hint
    bg_draw_text(renderer, font,
                 "[Enter] select   [Up/Down] browse   [Esc] back",
                 dim_col, 40, h - 30);

    (void)green_col; // used if needed for extension
}
