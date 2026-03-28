#include "ui/quest_offer.h"
#include "ui/ui_draw.h"
#include <cstdio>
#include <algorithm>

void QuestOffer::show(QuestId id, const std::string& npc_name) {
    quest_id_ = id;
    npc_name_ = npc_name;
    selected_ = 0;
    open_ = true;
}

QuestOfferChoice QuestOffer::handle_input(SDL_Event& event) {
    if (!open_) return QuestOfferChoice::NONE;

    // Mouse
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mx = event.button.x;
        int my = event.button.y;
        auto in_rect = [](int x, int y, const SDL_Rect& r) {
            return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
        };
        if (in_rect(mx, my, accept_btn_)) return QuestOfferChoice::ACCEPT;
        if (in_rect(mx, my, decline_btn_)) return QuestOfferChoice::DECLINE;
        return QuestOfferChoice::NONE;
    }

    if (event.type != SDL_KEYDOWN) return QuestOfferChoice::NONE;

    switch (event.key.keysym.sym) {
        case SDLK_LEFT:
        case SDLK_h:
            selected_ = 0;
            return QuestOfferChoice::NONE;
        case SDLK_RIGHT:
        case SDLK_l:
            selected_ = 1;
            return QuestOfferChoice::NONE;
        case SDLK_TAB:
            selected_ = 1 - selected_;
            return QuestOfferChoice::NONE;
        case SDLK_RETURN:
        case SDLK_e:
            return selected_ == 0 ? QuestOfferChoice::ACCEPT : QuestOfferChoice::DECLINE;
        case SDLK_y:
            return QuestOfferChoice::ACCEPT;
        case SDLK_ESCAPE:
            return QuestOfferChoice::DECLINE;
        default:
            return QuestOfferChoice::NONE;
    }
}

void QuestOffer::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                         int w, int h) const {
    if (!open_ || !font) return;

    auto& info = get_quest_info(quest_id_);
    int line_h = TTF_FontLineSkip(font);
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;

    SDL_Color title_col = {220, 200, 140, 255};
    SDL_Color npc_col = {180, 180, 150, 255};
    SDL_Color desc_col = {160, 155, 150, 255};
    SDL_Color obj_col = {140, 170, 140, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color normal_col = {160, 155, 150, 255};
    SDL_Color main_tag = {200, 180, 100, 255};

    // Darken background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, w, h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &overlay);

    // Modal panel
    int panel_w = 560;
    int panel_h = std::min(h - 40, 520);
    int panel_x = (w - panel_w) / 2;
    int panel_y = (h - panel_h) / 2;

    ui::draw_panel(renderer, panel_x, panel_y, panel_w, panel_h);

    // Helper: draw wrapped text and return the height it used
    auto draw_wrapped_measured = [&](const char* text, SDL_Color col, int tx, int ty, int tw) -> int {
        if (!text || !text[0]) return 0;
        SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(font, text, col, static_cast<Uint32>(tw));
        if (!surf) return line_h;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_Rect dst = {tx, ty, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        int used_h = surf->h;
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
        return used_h;
    };

    int y = panel_y + 16;
    int cx = w / 2;
    int content_w = panel_w - 40;

    // NPC name header
    char header[128];
    snprintf(header, sizeof(header), "%s offers a quest:", npc_name_.c_str());
    ui::draw_text_centered(renderer, font, header, npc_col, cx, y);
    y += line_h + 12;

    // Quest name
    ui::draw_text_centered(renderer, font_title, info.name, title_col, cx, y);
    y += title_h + 8;

    // Main quest tag
    if (info.is_main) {
        ui::draw_text_centered(renderer, font, "[Main Quest]", main_tag, cx, y);
        y += line_h + 8;
    }

    // Description
    int desc_h = draw_wrapped_measured(info.description, desc_col, panel_x + 20, y, content_w);
    y += desc_h + 12;

    // Objective
    ui::draw_text(renderer, font, "Objective:", dim_col, panel_x + 20, y);
    y += line_h + 4;
    int obj_h = draw_wrapped_measured(info.objective, obj_col, panel_x + 28, y, content_w - 16);
    y += obj_h + 12;

    // Rewards
    if (info.xp_reward > 0 || info.gold_reward > 0) {
        char reward[64];
        if (info.gold_reward > 0) {
            snprintf(reward, sizeof(reward), "Reward: %d XP, %d Gold",
                     info.xp_reward, info.gold_reward);
        } else {
            snprintf(reward, sizeof(reward), "Reward: %d XP", info.xp_reward);
        }
        ui::draw_text_centered(renderer, font, reward, dim_col, cx, y);
    }

    // Accept / Decline buttons
    int btn_w = 120;
    int btn_h = line_h + 12;
    int btn_y = panel_y + panel_h - btn_h - 16;
    int btn_gap = 30;

    accept_btn_ = {cx - btn_w - btn_gap / 2, btn_y, btn_w, btn_h};
    decline_btn_ = {cx + btn_gap / 2, btn_y, btn_w, btn_h};

    // Accept button
    {
        bool is_sel = (selected_ == 0);
        SDL_SetRenderDrawColor(renderer, is_sel ? 40 : 25, is_sel ? 35 : 22, is_sel ? 55 : 35, 255);
        SDL_RenderFillRect(renderer, &accept_btn_);
        SDL_SetRenderDrawColor(renderer, is_sel ? 100 : 60, is_sel ? 90 : 50, is_sel ? 120 : 70, 255);
        SDL_RenderDrawRect(renderer, &accept_btn_);
        ui::draw_text_centered(renderer, font, "Accept",
                                is_sel ? sel_col : normal_col,
                                accept_btn_.x + btn_w / 2, btn_y + 6);
    }

    // Decline button
    {
        bool is_sel = (selected_ == 1);
        SDL_SetRenderDrawColor(renderer, is_sel ? 40 : 25, is_sel ? 35 : 22, is_sel ? 55 : 35, 255);
        SDL_RenderFillRect(renderer, &decline_btn_);
        SDL_SetRenderDrawColor(renderer, is_sel ? 100 : 60, is_sel ? 90 : 50, is_sel ? 120 : 70, 255);
        SDL_RenderDrawRect(renderer, &decline_btn_);
        ui::draw_text_centered(renderer, font, "Decline",
                                is_sel ? sel_col : normal_col,
                                decline_btn_.x + btn_w / 2, btn_y + 6);
    }

    // Hint
    ui::draw_text_centered(renderer, font, "[Y] accept  [Esc] decline  [Left/Right] select  [Enter] confirm",
                            dim_col, cx, panel_y + panel_h + 8);
}
