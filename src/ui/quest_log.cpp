#include "ui/quest_log.h"
#include "ui/ui_draw.h"
#include <cstdio>

bool QuestLog::handle_input(SDL_Event& event) {
    if (!open_) return false;
    if (event.type != SDL_KEYDOWN) return false;

    switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_q:
            close();
            return true;
        case SDLK_UP:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return true;
        case SDLK_DOWN:
        case SDLK_j:
            selected_++;
            return true;
        default:
            return true;
    }
}

void QuestLog::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                       const QuestJournal& journal, int w, int h) const {
    if (!open_ || !font) return;

    int line_h = TTF_FontLineSkip(font);

    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color main_col = {220, 200, 140, 255};
    SDL_Color side_col = {160, 170, 180, 255};
    SDL_Color active_col = {180, 175, 170, 255};
    SDL_Color complete_col = {120, 200, 120, 255};
    SDL_Color finished_col = {100, 95, 90, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color desc_col = {140, 130, 120, 255};

    // Darken background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, w, h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &overlay);

    int panel_w = std::min(w * 2 / 3, 900);
    int panel_h = h - 60;
    int panel_x = (w - panel_w) / 2;
    int panel_y = 30;

    ui::draw_panel(renderer, panel_x, panel_y, panel_w, panel_h);

    int y = panel_y + 12;
    ui::draw_text_centered(renderer, font_title, "Quest Journal", title_col, w / 2, y);
    y += (font_title ? TTF_FontLineSkip(font_title) : line_h) + 8;

    if (journal.entries.empty()) {
        ui::draw_text(renderer, font, "No quests yet.", dim_col, panel_x + 16, y);
        ui::draw_text_centered(renderer, font, "[q / Esc] close", dim_col, w / 2,
                                panel_y + panel_h - line_h - 8);
        return;
    }

    // Clamp selection
    int count = static_cast<int>(journal.entries.size());
    if (selected_ >= count) selected_ = count - 1;

    // Quest list
    for (int i = 0; i < count; i++) {
        auto& entry = journal.entries[i];
        auto& info = get_quest_info(entry.id);
        bool is_sel = (i == selected_);

        if (is_sel) {
            SDL_Rect hl = {panel_x + 6, y - 2, panel_w - 12, line_h + 4};
            SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        // State indicator
        const char* state_str = "";
        SDL_Color state_col = active_col;
        switch (entry.state) {
            case QuestState::ACTIVE:   state_str = ""; state_col = active_col; break;
            case QuestState::COMPLETE: state_str = " [DONE]"; state_col = complete_col; break;
            case QuestState::FINISHED: state_str = " [FINISHED]"; state_col = finished_col; break;
            default: break;
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "%s%s%s",
                 info.is_main ? "[Main] " : "",
                 info.name, state_str);

        SDL_Color col = is_sel ? (info.is_main ? main_col : side_col) : state_col;
        ui::draw_text(renderer, font, buf, col, panel_x + 16, y);

        // Progress for count-based quests
        if (entry.target > 0 && entry.state == QuestState::ACTIVE) {
            char prog[32];
            snprintf(prog, sizeof(prog), "(%d/%d)", entry.progress, entry.target);
            int tw = 0, th = 0;
            TTF_SizeText(font, prog, &tw, &th);
            ui::draw_text(renderer, font, prog, dim_col, panel_x + panel_w - tw - 16, y);
        }

        y += line_h + 4;
        if (y > panel_y + panel_h / 2) break;
    }

    // Detail panel for selected quest
    y += 8;
    SDL_Rect sep = {panel_x + 10, y, panel_w - 20, 1};
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderFillRect(renderer, &sep);
    y += 8;

    if (selected_ >= 0 && selected_ < count) {
        auto& entry = journal.entries[selected_];
        auto& info = get_quest_info(entry.id);

        ui::draw_text_wrapped(renderer, font, info.description, desc_col,
                               panel_x + 16, y, panel_w - 32);
        y += line_h * 3 + 8;

        // Current objective
        if (entry.state == QuestState::ACTIVE) {
            ui::draw_text(renderer, font, "Objective:", dim_col, panel_x + 16, y);
            y += line_h;
            ui::draw_text_wrapped(renderer, font, info.objective, active_col,
                                   panel_x + 24, y, panel_w - 48);
        } else if (entry.state == QuestState::COMPLETE) {
            ui::draw_text(renderer, font, "Return to turn in.", complete_col, panel_x + 16, y);
        }
    }

    ui::draw_text_centered(renderer, font, "[q / Esc] close", dim_col, w / 2,
                            panel_y + panel_h - line_h - 8);
}
