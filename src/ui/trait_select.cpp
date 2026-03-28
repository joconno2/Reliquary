#include "ui/trait_select.h"
#include <algorithm>
#include <cstdio>

static void tr_draw_text(SDL_Renderer* renderer, TTF_Font* font,
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

static void tr_draw_text_wrapped(SDL_Renderer* renderer, TTF_Font* font,
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
    if (event.type != SDL_KEYDOWN) return false;

    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_k:
            if (cursor_ > 0) cursor_--;
            return true;
        case SDLK_DOWN:
        case SDLK_j:
            if (cursor_ < TRAIT_COUNT - 1) cursor_++;
            return true;
        case SDLK_RETURN:
        case SDLK_e: {
            // If all selections done and this is the confirm action
            if (can_confirm()) {
                confirmed_ = true;
                return true;
            }
            // Toggle selection on current cursor
            TraitId id = static_cast<TraitId>(cursor_);
            const TraitInfo& info = get_trait_info(id);
            if (is_selected(id)) {
                // Deselect
                selected_traits_.erase(
                    std::remove(selected_traits_.begin(), selected_traits_.end(), id),
                    selected_traits_.end());
            } else {
                // Select only if within limits
                if (info.is_positive && positive_selected_count() < 3) {
                    selected_traits_.push_back(id);
                } else if (!info.is_positive && negative_selected_count() < 2) {
                    selected_traits_.push_back(id);
                }
            }
            return true;
        }
        case SDLK_SPACE: {
            // Confirm if ready
            if (can_confirm()) {
                confirmed_ = true;
                return true;
            }
            return true;
        }
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            return false; // caller handles going back
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

    // Title + counter
    tr_draw_text(renderer, font, "Choose your traits.", title_col, 40, 15);

    char count_buf[64];
    snprintf(count_buf, sizeof(count_buf), "Positive: %d/3   Negative: %d/2",
             positive_selected_count(), negative_selected_count());
    tr_draw_text(renderer, font, count_buf, dim_col, 40, 15 + line_h + 2);

    // List: positives first, then negatives
    int list_x = 40;
    int list_y = 15 + line_h * 2 + 10;
    int row_h  = line_h + 4;

    // Section header: Positive
    tr_draw_text(renderer, font, "-- Positive Traits --", section_col, list_x, list_y);
    list_y += line_h + 4;

    for (int i = 0; i < POSITIVE_TRAIT_COUNT; i++) {
        TraitId id = static_cast<TraitId>(i);
        const TraitInfo& info = get_trait_info(id);
        bool is_cursor = (cursor_ == i);
        bool is_picked = is_selected(id);

        if (is_cursor) {
            SDL_Rect hl = {list_x - 4, list_y, 310, row_h};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        // Checkmark prefix
        char buf[128];
        snprintf(buf, sizeof(buf), "%s %s", is_picked ? "[x]" : "[ ]", info.name);

        SDL_Color text_col;
        if (is_picked) text_col = chosen_col;
        else if (is_cursor) text_col = sel_col;
        else text_col = normal_col;

        tr_draw_text(renderer, font, buf, text_col, list_x, list_y);
        list_y += row_h;
    }

    list_y += 6;

    // Section header: Negative
    tr_draw_text(renderer, font, "-- Negative Traits --", section_col, list_x, list_y);
    list_y += line_h + 4;

    for (int i = POSITIVE_TRAIT_COUNT; i < TRAIT_COUNT; i++) {
        TraitId id = static_cast<TraitId>(i);
        const TraitInfo& info = get_trait_info(id);
        bool is_cursor = (cursor_ == i);
        bool is_picked = is_selected(id);

        if (is_cursor) {
            SDL_Rect hl = {list_x - 4, list_y, 310, row_h};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "%s %s", is_picked ? "[x]" : "[ ]", info.name);

        SDL_Color text_col;
        if (is_picked) text_col = neg_col;
        else if (is_cursor) text_col = sel_col;
        else text_col = normal_col;

        tr_draw_text(renderer, font, buf, text_col, list_x, list_y);
        list_y += row_h;
    }

    // Detail panel on right
    int detail_x = 380;
    int detail_y = 40;
    int detail_w = w - detail_x - 40;

    SDL_Rect panel = {detail_x - 10, detail_y - 10, detail_w + 20, h - detail_y - 20};
    SDL_SetRenderDrawColor(renderer, 20, 16, 26, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderDrawRect(renderer, &panel);

    const TraitInfo& cur = get_trait_info(static_cast<TraitId>(cursor_));

    SDL_Color name_col = cur.is_positive ? chosen_col : neg_col;
    tr_draw_text(renderer, font, cur.name, name_col, detail_x, detail_y);
    detail_y += line_h + 6;

    tr_draw_text_wrapped(renderer, font, cur.description, desc_col,
                         detail_x, detail_y, detail_w);
    detail_y += line_h * 2 + 10;

    // Stat modifiers
    tr_draw_text(renderer, font, "Modifiers:", dim_col, detail_x, detail_y);
    detail_y += line_h + 2;

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
        char buf[32];
        snprintf(buf, sizeof(buf), "  %s %+d", m.label, m.val);
        SDL_Color col = m.val > 0
            ? SDL_Color{120, 200, 120, 255}
            : SDL_Color{200, 120, 120, 255};
        tr_draw_text(renderer, font, buf, col, detail_x, detail_y);
        detail_y += line_h;
    }
    if (!any_mod) {
        tr_draw_text(renderer, font, "  (no stat changes)", dim_col, detail_x, detail_y);
        detail_y += line_h;
    }

    // Special flags
    detail_y += 4;
    if (cur.see_invisible) {
        tr_draw_text(renderer, font, "  * Can see invisible", chosen_col, detail_x, detail_y);
        detail_y += line_h;
    }
    if (cur.immune_poison) {
        tr_draw_text(renderer, font, "  * Immune to poison", chosen_col, detail_x, detail_y);
        detail_y += line_h;
    }
    if (cur.immune_disease) {
        tr_draw_text(renderer, font, "  * Immune to disease", chosen_col, detail_x, detail_y);
        detail_y += line_h;
    }
    if (cur.fear_resist) {
        tr_draw_text(renderer, font, "  * Resists fear", chosen_col, detail_x, detail_y);
        detail_y += line_h;
    }
    if (cur.extra_fov) {
        tr_draw_text(renderer, font, "  * +2 sight radius", chosen_col, detail_x, detail_y);
        detail_y += line_h;
    }

    // Confirm hint
    if (can_confirm()) {
        tr_draw_text(renderer, font, "[Enter/Space] CONFIRM SELECTION",
                     sel_col, 40, h - 30);
    } else {
        tr_draw_text(renderer, font,
                     "[Enter] toggle   [Up/Down] browse   [Esc] back",
                     dim_col, 40, h - 30);
    }
}
