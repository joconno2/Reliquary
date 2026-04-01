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

    ui::draw_text_centered(renderer, font_title, "Help", title_col, cx, y);
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
        {"./Numpad 5",  "Wait one turn"},
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
        {"Enter / > / <", "Use stairs"},
        {"g / ,",         "Pick up item"},
        {"f",             "Fire ranged weapon"},
        {"r",             "Rest (heal HP/MP)"},
        {"p",             "Pray (god ability)"},
        {"x",             "Examine / look"},
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
        {"M",   "World map"},
        {"Tab", "Bestiary"},
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
    };
    for (auto& b : inv) {
        ui::draw_text(renderer, font, b.key, key_col, col3, ry);
        ui::draw_text(renderer, font, b.desc, desc_col, col4, ry);
        ry += line_h + 2;
    }

    ry += 8;
    ui::draw_text(renderer, font, "-- Combat --", section_col, col3, ry);
    ry += line_h + 4;

    Bind combat[] = {
        {"Bump",      "Melee attack"},
        {"f",         "Ranged attack"},
        {"z then c",  "Cast a spell"},
        {"v",         "Quick-cast spell"},
        {"p",         "Use god prayer"},
    };
    for (auto& b : combat) {
        ui::draw_text(renderer, font, b.key, key_col, col3, ry);
        ui::draw_text(renderer, font, b.desc, desc_col, col4, ry);
        ry += line_h + 2;
    }

    ry += 8;
    ui::draw_text(renderer, font, "-- System --", section_col, col3, ry);
    ry += line_h + 4;

    Bind system[] = {
        {"F5",  "Quicksave"},
        {"F6",  "Quickload"},
        {"F11", "Toggle fullscreen"},
        {"F12", "Screenshot"},
        {"?",   "This screen"},
    };
    for (auto& b : system) {
        ui::draw_text(renderer, font, b.key, key_col, col3, ry);
        ui::draw_text(renderer, font, b.desc, desc_col, col4, ry);
        ry += line_h + 2;
    }

    ry += 8;
    ui::draw_text(renderer, font, "-- Tips --", section_col, col3, ry);
    ry += line_h + 4;

    SDL_Color tip_col = {130, 140, 120, 255};
    const char* tips[] = {
        "Bump into NPCs to talk/trade.",
        "Explore towns for quests.",
        "Rest heals but may be interrupted.",
        "Heavy armor causes spell failure.",
        "Follow signs on roads for directions.",
    };
    for (auto& tip : tips) {
        ui::draw_text(renderer, font, tip, tip_col, col3, ry);
        ry += line_h + 2;
    }

    ui::draw_text_centered(renderer, font, "Press any key to close.", dim_col, cx, py + panel_h - line_h - 10);
}
