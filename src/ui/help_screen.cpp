#include "ui/help_screen.h"
#include "ui/ui_draw.h"
#include <algorithm>

bool HelpScreen::handle_input(SDL_Event& event) {
    if (!open_) return false;
    if (event.type != SDL_KEYDOWN) return false;
    // Any key closes
    close();
    return true;
}

void HelpScreen::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                         int w, int h) const {
    if (!open_ || !font) return;

    int line_h = TTF_FontLineSkip(font);

    // Darken
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, w, h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 190);
    SDL_RenderFillRect(renderer, &overlay);

    int panel_w = std::min(w * 3 / 4, 1100);
    int panel_h = h - 50;
    int px = (w - panel_w) / 2;
    int py = 25;

    ui::draw_panel(renderer, px, py, panel_w, panel_h);

    int y = py + 12;
    int cx = w / 2;

    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color key_col = {220, 210, 180, 255};
    SDL_Color desc_col = {150, 145, 140, 255};
    SDL_Color section_col = {170, 150, 120, 255};
    SDL_Color dim_col = {100, 95, 90, 255};

    ui::draw_text_centered(renderer, font_title, "Keybinds", title_col, cx, y);
    y += (font_title ? TTF_FontLineSkip(font_title) : line_h) + 10;

    int col1 = px + 20;
    int col2 = px + 200;
    int col3 = px + panel_w / 2 + 10;
    int col4 = px + panel_w / 2 + 180;

    struct Bind { const char* key; const char* desc; };

    // Left column
    int ly = y;
    ui::draw_text(renderer, font, "-- Movement --", section_col, col1, ly);
    ly += line_h + 4;

    Bind movement[] = {
        {"Arrows/hjkl", "Move cardinal"},
        {"yubn/Numpad", "Move diagonal"},
        {"./Numpad 5",  "Wait"},
    };
    for (auto& b : movement) {
        ui::draw_text(renderer, font, b.key, key_col, col1, ly);
        ui::draw_text(renderer, font, b.desc, desc_col, col2, ly);
        ly += line_h + 2;
    }

    ly += 8;
    ui::draw_text(renderer, font, "-- Actions --", section_col, col1, ly);
    ly += line_h + 4;

    Bind actions[] = {
        {"Enter / >",  "Descend stairs"},
        {"< / Enter",  "Ascend stairs"},
        {"g / ,",      "Pick up item"},
        {"r",          "Rest (heal)"},
    };
    for (auto& b : actions) {
        ui::draw_text(renderer, font, b.key, key_col, col1, ly);
        ui::draw_text(renderer, font, b.desc, desc_col, col2, ly);
        ly += line_h + 2;
    }

    ly += 8;
    ui::draw_text(renderer, font, "-- Screens --", section_col, col1, ly);
    ly += line_h + 4;

    Bind screens[] = {
        {"i",   "Inventory"},
        {"c",   "Character sheet"},
        {"z",   "Spellbook"},
        {"q",   "Quest journal"},
        {"Esc", "Pause menu"},
    };
    for (auto& b : screens) {
        ui::draw_text(renderer, font, b.key, key_col, col1, ly);
        ui::draw_text(renderer, font, b.desc, desc_col, col2, ly);
        ly += line_h + 2;
    }

    // Right column
    int ry = y;
    ui::draw_text(renderer, font, "-- Inventory --", section_col, col3, ry);
    ry += line_h + 4;

    Bind inv[] = {
        {"e / Enter", "Equip/unequip"},
        {"u",         "Use (eat/drink)"},
        {"d",         "Drop item"},
        {"Right-click","Equip"},
        {"a-z",       "Quick select"},
    };
    for (auto& b : inv) {
        ui::draw_text(renderer, font, b.key, key_col, col3, ry);
        ui::draw_text(renderer, font, b.desc, desc_col, col4, ry);
        ry += line_h + 2;
    }

    ry += 8;
    ui::draw_text(renderer, font, "-- Spellbook --", section_col, col3, ry);
    ry += line_h + 4;

    Bind spells[] = {
        {"c / Enter", "Cast spell"},
        {"z / Esc",   "Close"},
    };
    for (auto& b : spells) {
        ui::draw_text(renderer, font, b.key, key_col, col3, ry);
        ui::draw_text(renderer, font, b.desc, desc_col, col4, ry);
        ry += line_h + 2;
    }

    ry += 8;
    ui::draw_text(renderer, font, "-- Other --", section_col, col3, ry);
    ry += line_h + 4;

    Bind other[] = {
        {"F11", "Fullscreen"},
        {"F12", "Screenshot"},
        {"Tab", "Switch tabs"},
        {"?",   "This screen"},
    };
    for (auto& b : other) {
        ui::draw_text(renderer, font, b.key, key_col, col3, ry);
        ui::draw_text(renderer, font, b.desc, desc_col, col4, ry);
        ry += line_h + 2;
    }

    ui::draw_text_centered(renderer, font, "Press any key to close.", dim_col, cx, py + panel_h - line_h - 10);
}
