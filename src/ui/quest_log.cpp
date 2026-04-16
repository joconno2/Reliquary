#include <algorithm>
#include "ui/quest_log.h"
#include "ui/ui_draw.h"
#include "ui/ui_layout.h"
#include "components/dynamic_quest.h"
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
        case SDLK_w:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return true;
        case SDLK_DOWN:
        case SDLK_s:
        case SDLK_j:
            selected_++;
            return true;
        default:
            return true;
    }
}

void QuestLog::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                       const QuestJournal& journal, int w, int h,
                       World* world) const {
    if (!open_ || !font) return;

    int line_h = TTF_FontLineSkip(font);
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;

    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color main_col = {220, 200, 140, 255};
    SDL_Color side_col = {160, 170, 180, 255};
    SDL_Color active_col = {180, 175, 170, 255};
    SDL_Color complete_col = {120, 200, 120, 255};
    SDL_Color finished_col = {100, 95, 90, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color desc_col = {140, 130, 120, 255};

    // Darken background
    ui::draw_overlay(renderer, w, h);

    // Panel: 2/3 screen width, full height minus margins
    auto screen = ui::Layout::from_screen(w, h, line_h);
    ui::Rect outer = screen.panel_outer(2, 3, 1, 1);
    // Shrink vertically to leave 30px margins top and bottom
    outer.y = 30;
    outer.h = h - 60;
    auto panel = ui::draw_panel_in(renderer, outer, line_h);

    // Title row
    ui::Rect title_row = panel.row(title_h);
    ui::draw_text_in(renderer, font_title ? font_title : font,
                     "Quest Journal", title_col, title_row, ui::Align::CENTER);
    panel.skip(8);

    // Footer row (cut from bottom before filling content)
    ui::Rect footer = panel.row_bottom(line_h);
    panel.skip(8); // gap above footer -- no, row_bottom already took from bottom
    // Actually need a gap between content and footer. Cut a small spacer from bottom.
    panel.row_bottom(8);

    ui::draw_text_in(renderer, font, "[q / Esc] close", dim_col, footer, ui::Align::CENTER);

    if (journal.entries.empty()) {
        ui::Rect empty_row = panel.row();
        ui::draw_text_in(renderer, font, "No quests yet.", dim_col, empty_row, ui::Align::LEFT);
        return;
    }

    // Clamp selection
    int count = static_cast<int>(journal.entries.size());
    if (selected_ >= count) selected_ = count - 1;

    // Calculate how much vertical space the quest list can use (half the remaining panel)
    int list_budget = panel.remaining_h() / 2;

    // Quest list
    for (int i = 0; i < count; i++) {
        auto& entry = journal.entries[i];
        auto& info = get_quest_info(entry.id);
        bool is_sel = (i == selected_);

        int row_h = line_h + 4;
        if (!panel.fits(row_h)) break;
        // Stop if we'd exceed the list half of the panel
        if (panel.bounds.y + panel.bounds.h - panel.remaining_h() + row_h >
            outer.y + outer.h / 2) break;

        ui::Rect quest_row = panel.row(row_h);

        if (is_sel) {
            SDL_Rect hl = quest_row.inset(-2, -2).sdl();
            // Inset horizontally to match old panel_x + 6 style
            hl.x = quest_row.x - 10;
            hl.w = quest_row.w + 20;
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
        ui::draw_text_in(renderer, font, buf, col, quest_row, ui::Align::LEFT);

        // Progress for count-based quests
        if (entry.target > 0 && entry.state == QuestState::ACTIVE) {
            char prog[32];
            snprintf(prog, sizeof(prog), "(%d/%d)", entry.progress, entry.target);
            ui::draw_text_in(renderer, font, prog, dim_col, quest_row, ui::Align::RIGHT);
        }
    }

    // Separator
    panel.skip(8);
    ui::Rect sep_row = panel.row(1);
    SDL_Rect sep = sep_row.inset(4, 0).sdl();
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderFillRect(renderer, &sep);
    panel.skip(8);

    // Detail panel for selected quest
    if (selected_ >= 0 && selected_ < count) {
        auto& entry = journal.entries[selected_];
        auto& info = get_quest_info(entry.id);

        // Description (wrapped, estimate 3 lines)
        int desc_h = line_h * 3;
        if (panel.fits(desc_h)) {
            ui::Rect desc_area = panel.row(desc_h);
            ui::draw_text_wrapped(renderer, font, info.description, desc_col,
                                   desc_area.x, desc_area.y, desc_area.w);
        }
        panel.skip(8);

        // Current objective
        if (entry.state == QuestState::ACTIVE) {
            if (panel.fits(line_h)) {
                ui::Rect obj_label = panel.row();
                ui::draw_text_in(renderer, font, "Objective:", dim_col, obj_label, ui::Align::LEFT);
            }
            if (panel.fits(line_h)) {
                ui::Rect obj_text = panel.row();
                // Indent the objective text
                ui::Rect indented = {obj_text.x + 8, obj_text.y, obj_text.w - 16, obj_text.h};
                ui::draw_text_wrapped(renderer, font, info.objective, active_col,
                                       indented.x, indented.y, indented.w);
            }
        } else if (entry.state == QuestState::COMPLETE) {
            if (panel.fits(line_h)) {
                ui::Rect turn_in = panel.row();
                ui::draw_text_in(renderer, font, "Return to turn in.", complete_col, turn_in, ui::Align::LEFT);
            }
        }
    }

    // Dynamic quests (from NPC entities)
    if (world) {
        auto& dq_pool = world->pool<DynamicQuest>();
        bool header_shown = false;
        for (size_t di = 0; di < dq_pool.size(); di++) {
            auto& dq = dq_pool.at_index(di);
            if (!dq.accepted || dq.completed) continue;
            if (!header_shown) {
                if (!panel.fits(line_h)) break;
                panel.skip(line_h);
                if (!panel.fits(line_h)) break;
                ui::Rect hdr = panel.row();
                ui::draw_text_in(renderer, font, "--- Side Tasks ---", dim_col, hdr, ui::Align::LEFT);
                panel.skip(4);
                header_shown = true;
            }
            if (!panel.fits(line_h)) break;
            ui::Rect name_row = panel.row();
            ui::draw_text_in(renderer, font, dq.name.c_str(), active_col, name_row, ui::Align::LEFT);

            int obj_h = line_h * 2;
            if (!panel.fits(obj_h)) break;
            ui::Rect obj_area = panel.row(obj_h);
            ui::Rect indented = {obj_area.x + 8, obj_area.y, obj_area.w - 16, obj_area.h};
            ui::draw_text_wrapped(renderer, font, dq.objective.c_str(), dim_col,
                                   indented.x, indented.y, indented.w);
        }
    }
}
