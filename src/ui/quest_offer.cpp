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

    ui::draw_overlay(renderer, w, h);

    auto screen = ui::Layout::from_screen(w, h, line_h);
    auto outer = screen.panel_outer(2, 5, 2, 3);
    auto panel = ui::draw_panel_in(renderer, outer, line_h);

    // NPC name header
    char header[128];
    snprintf(header, sizeof(header), "%s offers a quest:", npc_name_.c_str());
    auto hdr_row = panel.row(line_h + panel.gap);
    ui::draw_text_in(renderer, font, header, npc_col, hdr_row, ui::Align::CENTER);

    // Quest name
    auto name_row = panel.row(title_h + panel.gap);
    ui::draw_text_in(renderer, font_title, info.name, title_col, name_row, ui::Align::CENTER);

    // Main quest tag
    if (info.is_main) {
        auto tag_row = panel.row(line_h + panel.gap);
        ui::draw_text_in(renderer, font, "[Main Quest]", main_tag, tag_row, ui::Align::CENTER);
    }

    // Description (wrapped)
    int desc_h = ui::text_wrapped_height(font, info.description, panel.cursor.w);
    auto desc_row = panel.row(desc_h + panel.gap);
    ui::draw_text_wrapped(renderer, font, info.description, desc_col,
                           desc_row.x, desc_row.y, desc_row.w);

    // Objective
    auto obj_label = panel.row();
    ui::draw_text(renderer, font, "Objective:", dim_col, obj_label.x, obj_label.y);
    int obj_h = ui::text_wrapped_height(font, info.objective, panel.cursor.w - panel.pad);
    auto obj_row = panel.row(obj_h + panel.gap);
    ui::draw_text_wrapped(renderer, font, info.objective, obj_col,
                           obj_row.x + panel.pad, obj_row.y, obj_row.w - panel.pad);

    // Rewards
    if (info.xp_reward > 0 || info.gold_reward > 0) {
        char reward[64];
        if (info.gold_reward > 0)
            snprintf(reward, sizeof(reward), "Reward: %d XP, %d Gold", info.xp_reward, info.gold_reward);
        else
            snprintf(reward, sizeof(reward), "Reward: %d XP", info.xp_reward);
        auto rew_row = panel.row(line_h + panel.gap);
        ui::draw_text_in(renderer, font, reward, dim_col, rew_row, ui::Align::CENTER);
    }

    // Accept / Decline buttons at bottom
    auto btn_area = panel.cursor.bottom(line_h + panel.pad * 2);
    int btn_w = btn_area.w * 2 / 7;
    int btn_h = line_h + 12;
    int btn_gap = panel.gap * 2;
    int cx = btn_area.cx();

    accept_btn_ = {cx - btn_w - btn_gap / 2, btn_area.y + (btn_area.h - btn_h) / 2, btn_w, btn_h};
    decline_btn_ = {cx + btn_gap / 2, btn_area.y + (btn_area.h - btn_h) / 2, btn_w, btn_h};

    auto draw_btn = [&](const SDL_Rect& r, const char* label, bool is_sel) {
        SDL_SetRenderDrawColor(renderer, is_sel ? 40 : 25, is_sel ? 35 : 22, is_sel ? 55 : 35, 255);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, is_sel ? 100 : 60, is_sel ? 90 : 50, is_sel ? 120 : 70, 255);
        SDL_RenderDrawRect(renderer, &r);
        ui::Rect br = {r.x, r.y, r.w, r.h};
        ui::draw_text_in(renderer, font, label, is_sel ? sel_col : normal_col,
                         br, ui::Align::CENTER);
    };

    draw_btn(accept_btn_, "Accept", selected_ == 0);
    draw_btn(decline_btn_, "Decline", selected_ == 1);

    // Hint below panel
    ui::draw_text_centered(renderer, font, "[Y] accept  [Esc] decline  [Left/Right] select  [Enter] confirm",
                            dim_col, w / 2, outer.y2() + 8);
}
