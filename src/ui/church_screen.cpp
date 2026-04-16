#include "ui/church_screen.h"
#include "ui/ui_draw.h"
#include "components/stats.h"
#include "components/god.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

void ChurchScreen::open(Entity player, World* world, GodId god, int favor) {
    open_ = true;
    player_ = player;
    world_ = world;
    god_ = god;
    favor_ = favor;
    rank_ = church_rank_for_favor(favor);
    selected_ = 0;
}

ChurchAction ChurchScreen::handle_input(SDL_Event& event) {
    if (!open_) return ChurchAction::NONE;

    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
                close();
                return ChurchAction::CLOSE;
            case SDLK_UP: case SDLK_w:
                if (selected_ > 0) selected_--;
                return ChurchAction::NONE;
            case SDLK_DOWN: case SDLK_s:
                if (selected_ < max_options_ - 1) selected_++;
                return ChurchAction::NONE;
            case SDLK_RETURN: case SDLK_SPACE: {
                int opt = 0;
                if (rank_ >= ChurchRank::INITIATE) {
                    if (selected_ == opt++) return ChurchAction::REST;
                }
                if (rank_ >= ChurchRank::ACOLYTE) {
                    if (selected_ == opt++) return ChurchAction::IDENTIFY;
                    if (selected_ == opt++) return ChurchAction::ENCHANT;
                    if (selected_ == opt++) return ChurchAction::LEARN_SPELL;
                }
                if (rank_ >= ChurchRank::DEVOTED) {
                    if (selected_ == opt++) return ChurchAction::CLAIM_ITEM;
                }
                if (rank_ >= ChurchRank::CHAMPION) {
                    if (selected_ == opt++) return ChurchAction::CLAIM_BLESSING;
                }
                return ChurchAction::CLOSE;
            }
            default: return ChurchAction::NONE;
        }
    }
    return ChurchAction::NONE;
}

void ChurchScreen::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                           int sw, int sh) const {
    if (!open_ || !font) return;

    int line_h = TTF_FontLineSkip(font);
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;

    ui::draw_overlay(renderer, sw, sh, 200);

    auto screen = ui::Layout::from_screen(sw, sh, line_h);
    auto outer = screen.panel_outer(1, 2, 9, 10);
    auto panel = ui::draw_panel_in(renderer, outer, line_h);

    auto& ginfo = get_god_info(god_);
    auto& rewards = get_church_rewards(god_);
    SDL_Color god_col = {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255};
    SDL_Color title_col = {220, 200, 160, 255};
    SDL_Color rank_col = {255, 220, 100, 255};
    SDL_Color avail_col = {180, 220, 140, 255};
    SDL_Color locked_col = {90, 85, 80, 255};
    SDL_Color desc_col = {160, 155, 145, 255};
    SDL_Color sel_col = {255, 240, 180, 255};
    SDL_Color dim_col = {100, 95, 85, 255};

    // Title: "Church of [God]"
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "Church of %s", ginfo.name);
    auto title_row = panel.row(title_h + panel.gap);
    ui::draw_text_in(renderer, font_title ? font_title : font, title_buf, god_col,
                     title_row, ui::Align::CENTER);

    // God description
    auto desc_row = panel.row(line_h + panel.gap);
    ui::draw_text_in(renderer, font, ginfo.description, desc_col, desc_row, ui::Align::CENTER);
    panel.skip(panel.gap);

    // Rank display
    char rank_buf[64];
    snprintf(rank_buf, sizeof(rank_buf), "Rank: %s  (Favor: %d)", church_rank_name(rank_), favor_);
    auto rank_row = panel.row(line_h + 4);
    ui::draw_text(renderer, font, rank_buf, rank_col, rank_row.x, rank_row.y);

    // Progress bar to next rank
    ChurchRank next_rank = static_cast<ChurchRank>(std::min(static_cast<int>(rank_) + 1,
                                                             CHURCH_RANK_COUNT - 1));
    if (rank_ < ChurchRank::CHAMPION) {
        int next_threshold = church_rank_favor(next_rank);
        int current_threshold = church_rank_favor(rank_);
        int range = next_threshold - current_threshold;
        int progress = favor_ - current_threshold;
        float pct = (range > 0) ? static_cast<float>(progress) / range : 1.0f;
        if (pct > 1.0f) pct = 1.0f;

        auto bar_row = panel.row(14 + line_h + 6);
        int bar_w = bar_row.w;
        int bar_h = 14;
        SDL_Rect bar_bg = {bar_row.x, bar_row.y, bar_w, bar_h};
        SDL_SetRenderDrawColor(renderer, 30, 28, 25, 255);
        SDL_RenderFillRect(renderer, &bar_bg);
        SDL_Rect bar_fill = {bar_row.x, bar_row.y, static_cast<int>(bar_w * pct), bar_h};
        SDL_SetRenderDrawColor(renderer, ginfo.color.r, ginfo.color.g, ginfo.color.b, 200);
        SDL_RenderFillRect(renderer, &bar_fill);
        SDL_SetRenderDrawColor(renderer, 80, 75, 65, 200);
        SDL_RenderDrawRect(renderer, &bar_bg);

        char prog_buf[48];
        snprintf(prog_buf, sizeof(prog_buf), "Next: %s at %d favor",
                 church_rank_name(next_rank), next_threshold);
        ui::draw_text(renderer, font, prog_buf, dim_col, bar_row.x, bar_row.y + bar_h + 2);
    } else {
        auto max_row = panel.row(line_h + 6);
        ui::draw_text(renderer, font, "Maximum rank achieved.", rank_col, max_row.x, max_row.y);
    }

    panel.skip(6);

    // Rank rewards ladder
    struct RankEntry { ChurchRank rank; const char* label; const char* detail; };
    char enchant_buf[64], spell_buf[64], item_buf[64], blessing_buf[64];
    snprintf(enchant_buf, sizeof(enchant_buf), "Weapon enchant: %s (+%d dmg, 50 turns)",
             rewards.enchant_name, rewards.enchant_bonus);
    auto& spell_info = get_spell_info(rewards.exclusive_spell);
    snprintf(spell_buf, sizeof(spell_buf), "Learn: %s", spell_info.name);
    snprintf(item_buf, sizeof(item_buf), "%s: %s", rewards.exclusive_item_name, rewards.exclusive_item_desc);
    snprintf(blessing_buf, sizeof(blessing_buf), "%s: %s", rewards.blessing_name, rewards.blessing_desc);

    RankEntry ladder[] = {
        {ChurchRank::INITIATE, "Initiate (10 favor)", "Free rest (full heal). Discount shop."},
        {ChurchRank::ACOLYTE,  "Acolyte (25 favor)",  enchant_buf},
        {ChurchRank::ACOLYTE,  "",                     spell_buf},
        {ChurchRank::ACOLYTE,  "",                     "Free identify all items."},
        {ChurchRank::DEVOTED,  "Devoted (50 favor)",   item_buf},
        {ChurchRank::CHAMPION, "Champion (75 favor)",  blessing_buf},
    };

    auto rewards_hdr = panel.row(line_h + 2);
    ui::draw_text(renderer, font, "-- Rewards --", title_col, rewards_hdr.x, rewards_hdr.y);

    for (auto& entry : ladder) {
        bool unlocked = rank_ >= entry.rank;
        SDL_Color col = unlocked ? avail_col : locked_col;
        const char* prefix = unlocked ? "[x]" : "[ ]";

        if (entry.label[0]) {
            auto row = panel.row();
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s", prefix, entry.label);
            ui::draw_text_clipped(renderer, font, buf, col, row.x, row.y, row.w);
        }
        if (entry.detail[0]) {
            auto row = panel.row();
            char buf[128];
            snprintf(buf, sizeof(buf), "    %s", entry.detail);
            ui::draw_text_clipped(renderer, font, buf, unlocked ? desc_col : locked_col, row.x, row.y, row.w);
        }
    }

    panel.skip(panel.gap);

    // Action options
    auto svc_hdr = panel.row(line_h + 4);
    ui::draw_text(renderer, font, "-- Services --", title_col, svc_hdr.x, svc_hdr.y);

    int opt = 0;
    auto draw_option = [&](const char* text, bool available) {
        if (!panel.fits_row()) return;
        bool is_sel = (opt == selected_);
        auto row = panel.row(line_h + 2);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s %s", is_sel ? ">" : " ", text);
        SDL_Color col = !available ? locked_col : is_sel ? sel_col : avail_col;
        ui::draw_text_clipped(renderer, font, buf, col, row.x, row.y, row.w);
        opt++;
    };

    auto* self = const_cast<ChurchScreen*>(this);

    if (rank_ >= ChurchRank::INITIATE)
        draw_option("Rest and heal (full restore)", true);
    if (rank_ >= ChurchRank::ACOLYTE) {
        draw_option("Identify all items", true);
        draw_option(enchant_buf, true);
        draw_option(spell_buf, true);
    }
    if (rank_ >= ChurchRank::DEVOTED)
        draw_option(item_buf, true);
    if (rank_ >= ChurchRank::CHAMPION) {
        char bless_opt[128];
        snprintf(bless_opt, sizeof(bless_opt), "Receive blessing: %s", rewards.blessing_name);
        draw_option(bless_opt, true);
    }
    draw_option("Leave", true);

    self->max_options_ = opt;

    // Bottom hint
    auto hint_rect = ui::Rect{outer.x, outer.y2() - line_h - 8, outer.w, line_h};
    ui::draw_text_in(renderer, font, "[Up/Down] select  |  [Enter] choose  |  [Esc] leave",
                     dim_col, hint_rect, ui::Align::CENTER);
}
