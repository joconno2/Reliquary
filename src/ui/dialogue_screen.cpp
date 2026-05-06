#include "ui/dialogue_screen.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
#include <cstdio>
#include <algorithm>

void DialogueScreen::open(const std::string& npc_name, const std::string& text,
                           const std::vector<DialogueOption>& options) {
    open_ = true;
    npc_name_ = npc_name;
    text_ = text;
    options_ = options;
    // Always add Leave at the end
    bool has_leave = false;
    for (auto& o : options_) if (o.action_id == -2) has_leave = true;
    if (!has_leave) options_.push_back({"Leave", -2, true});
    selected_ = 0;
}

int DialogueScreen::handle_input(SDL_Event& event) {
    if (!open_) return -1;

    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
                close();
                return -2;
            case SDLK_UP: case SDLK_w:
                if (selected_ > 0) selected_--;
                return -1;
            case SDLK_DOWN: case SDLK_s:
                if (selected_ < static_cast<int>(options_.size()) - 1) selected_++;
                return -1;
            case SDLK_RETURN: case SDLK_SPACE: {
                if (selected_ >= 0 && selected_ < static_cast<int>(options_.size())) {
                    auto& opt = options_[selected_];
                    if (!opt.enabled) return -1;
                    int id = opt.action_id;
                    if (id == -2) close();
                    return id;
                }
                return -1;
            }
            default: return -1;
        }
    }
    return -1;
}

void DialogueScreen::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                              int sw, int sh) const {
    if (!open_ || !font) return;

    int line_h = TTF_FontLineSkip(font);
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;

    // Dark overlay
    ui::draw_overlay(renderer, sw, sh, 180);

    // Panel: centered, 40% width, dynamic height
    int panel_w = sw * 2 / 5;
    if (panel_w < 400) panel_w = std::min(sw - 40, 600);
    int panel_x = (sw - panel_w) / 2;

    // Calculate height needed
    // Measure text wrapping
    int text_w = panel_w - 40;
    int char_w = 8;
    { int tw = 0; TTF_SizeText(font, "ABCDEFGHIJ", &tw, nullptr); if (tw > 0) char_w = tw / 10; }
    int chars_per_line = std::max(20, text_w / char_w);
    int text_lines = 1;
    { int col = 0;
      for (char c : text_) {
          if (c == '\n') { text_lines++; col = 0; continue; }
          col++;
          if (col >= chars_per_line) { text_lines++; col = 0; }
      }
    }

    int content_h = title_h + 12 + // NPC name
                    text_lines * line_h + 16 + // dialogue text
                    static_cast<int>(options_.size()) * (line_h + 4) + 20; // options
    int panel_h = content_h + 40;
    int panel_y = (sh - panel_h) / 2;

    // Draw panel background
    SDL_Rect bg = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(renderer, 18, 16, 14, 240);
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 80, 70, 55, 200);
    SDL_RenderDrawRect(renderer, &bg);
    // Inner border
    SDL_Rect inner = {panel_x + 2, panel_y + 2, panel_w - 4, panel_h - 4};
    SDL_SetRenderDrawColor(renderer, 50, 45, 38, 180);
    SDL_RenderDrawRect(renderer, &inner);

    int cx = panel_x + 20;
    int cy = panel_y + 16;

    // NPC Name
    SDL_Color name_col = {220, 200, 140, 255};
    ui::draw_text(renderer, font_title ? font_title : font, npc_name_.c_str(), name_col, cx, cy);
    cy += title_h + 8;

    // Separator line
    SDL_SetRenderDrawColor(renderer, 60, 55, 45, 200);
    SDL_RenderDrawLine(renderer, cx, cy, panel_x + panel_w - 20, cy);
    cy += 8;

    // Dialogue text (word wrapped)
    SDL_Color text_col = {180, 175, 165, 255};
    {
        std::string line;
        int col = 0;
        for (size_t i = 0; i <= text_.size(); i++) {
            char c = (i < text_.size()) ? text_[i] : '\n';
            if (c == '\n' || col >= chars_per_line) {
                if (!line.empty()) {
                    ui::draw_text(renderer, font, line.c_str(), text_col, cx, cy);
                    cy += line_h;
                }
                line.clear();
                col = 0;
                if (c == '\n') continue;
            }
            line += c;
            col++;
        }
    }
    cy += 12;

    // Separator
    SDL_SetRenderDrawColor(renderer, 60, 55, 45, 200);
    SDL_RenderDrawLine(renderer, cx, cy, panel_x + panel_w - 20, cy);
    cy += 8;

    // Options
    for (int i = 0; i < static_cast<int>(options_.size()); i++) {
        auto& opt = options_[i];
        bool is_sel = (i == selected_);
        SDL_Color col;
        if (!opt.enabled) col = {80, 75, 70, 255};
        else if (is_sel)  col = {255, 240, 180, 255};
        else              col = {160, 155, 140, 255};

        char buf[128];
        snprintf(buf, sizeof(buf), "%s %s", is_sel ? ">" : " ", opt.label.c_str());
        ui::draw_text(renderer, font, buf, col, cx, cy);
        cy += line_h + 4;
    }
}
