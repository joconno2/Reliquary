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
                // Map selected option to action based on rank
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
                return ChurchAction::CLOSE; // last option is always Leave
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

    // Darken
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, sw, sh};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(renderer, &overlay);

    int panel_w = std::min(700, sw - 60);
    int panel_h = sh - 80;
    int px = (sw - panel_w) / 2;
    int py = 40;
    ui::draw_panel(renderer, px, py, panel_w, panel_h);

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

    int y = py + 14;
    int cx = sw / 2;
    int lx = px + 20;

    // Title: "Church of [God]"
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "Church of %s", ginfo.name);
    ui::draw_text_centered(renderer, font_title ? font_title : font, title_buf, god_col, cx, y);
    y += title_h + 4;

    // God description
    ui::draw_text_centered(renderer, font, ginfo.description, desc_col, cx, y);
    y += line_h + 10;

    // Rank display with progress bar
    char rank_buf[64];
    snprintf(rank_buf, sizeof(rank_buf), "Rank: %s  (Favor: %d)", church_rank_name(rank_), favor_);
    ui::draw_text(renderer, font, rank_buf, rank_col, lx, y);
    y += line_h + 4;

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

        int bar_w = panel_w - 40;
        int bar_h = 14;
        SDL_Rect bar_bg = {lx, y, bar_w, bar_h};
        SDL_SetRenderDrawColor(renderer, 30, 28, 25, 255);
        SDL_RenderFillRect(renderer, &bar_bg);
        SDL_Rect bar_fill = {lx, y, static_cast<int>(bar_w * pct), bar_h};
        SDL_SetRenderDrawColor(renderer, ginfo.color.r, ginfo.color.g, ginfo.color.b, 200);
        SDL_RenderFillRect(renderer, &bar_fill);
        SDL_SetRenderDrawColor(renderer, 80, 75, 65, 200);
        SDL_RenderDrawRect(renderer, &bar_bg);

        char prog_buf[48];
        snprintf(prog_buf, sizeof(prog_buf), "Next: %s at %d favor",
                 church_rank_name(next_rank), next_threshold);
        ui::draw_text(renderer, font, prog_buf, dim_col, lx, y + bar_h + 2);
        y += bar_h + line_h + 6;
    } else {
        ui::draw_text(renderer, font, "Maximum rank achieved.", rank_col, lx, y);
        y += line_h + 6;
    }

    y += 6;

    // Rank rewards ladder (always visible, locked ones dimmed)
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

    ui::draw_text(renderer, font, "-- Rewards --", title_col, lx, y);
    y += line_h + 2;

    for (auto& entry : ladder) {
        bool unlocked = rank_ >= entry.rank;
        SDL_Color col = unlocked ? avail_col : locked_col;
        const char* prefix = unlocked ? "[x]" : "[ ]";

        if (entry.label[0]) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s", prefix, entry.label);
            ui::draw_text_clipped(renderer, font, buf, col, lx, y, panel_w - 40);
            y += line_h;
        }
        if (entry.detail[0]) {
            char buf[128];
            snprintf(buf, sizeof(buf), "    %s", entry.detail);
            ui::draw_text_clipped(renderer, font, buf, unlocked ? desc_col : locked_col, lx, y, panel_w - 40);
            y += line_h;
        }
    }

    y += 10;

    // Action options
    ui::draw_text(renderer, font, "-- Services --", title_col, lx, y);
    y += line_h + 4;

    int opt = 0;
    auto draw_option = [&](const char* text, bool available) {
        bool is_sel = (opt == selected_);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s %s", is_sel ? ">" : " ", text);
        SDL_Color col = !available ? locked_col : is_sel ? sel_col : avail_col;
        ui::draw_text_clipped(renderer, font, buf, col, lx, y, panel_w - 40);
        y += line_h + 2;
        opt++;
    };

    // Cast to mutable to update max_options_
    auto* self = const_cast<ChurchScreen*>(this);

    if (rank_ >= ChurchRank::INITIATE) {
        draw_option("Rest and heal (full restore)", true);
    }
    if (rank_ >= ChurchRank::ACOLYTE) {
        draw_option("Identify all items", true);
        draw_option(enchant_buf, true);
        draw_option(spell_buf, true);
    }
    if (rank_ >= ChurchRank::DEVOTED) {
        draw_option(item_buf, true);
    }
    if (rank_ >= ChurchRank::CHAMPION) {
        char bless_opt[128];
        snprintf(bless_opt, sizeof(bless_opt), "Receive blessing: %s", rewards.blessing_name);
        draw_option(bless_opt, true);
    }
    draw_option("Leave", true);

    self->max_options_ = opt;

    // Bottom hint
    ui::draw_text_centered(renderer, font, "[Up/Down] select  |  [Enter] choose  |  [Esc] leave",
                            dim_col, cx, py + panel_h - line_h - 8);
}
