#include "ui/help_screen.h"
#include "ui/ui_draw.h"
#include <algorithm>
#include <cstdio>

bool HelpScreen::handle_input(SDL_Event& event) {
    if (!open_) return false;
    if (event.type == SDL_MOUSEWHEEL) {
        scroll_ -= event.wheel.y * 30;
        if (scroll_ < 0) scroll_ = 0;
        if (scroll_ > max_scroll_) scroll_ = max_scroll_;
        return true;
    }
    if (event.type != SDL_KEYDOWN) return false;
    auto sym = event.key.keysym.sym;
    if (sym == SDLK_DOWN || sym == SDLK_s) { scroll_ += 20; if (scroll_ > max_scroll_) scroll_ = max_scroll_; return true; }
    if (sym == SDLK_UP || sym == SDLK_w) { scroll_ -= 20; if (scroll_ < 0) scroll_ = 0; return true; }
    if (sym == SDLK_PAGEDOWN) { scroll_ += 200; if (scroll_ > max_scroll_) scroll_ = max_scroll_; return true; }
    if (sym == SDLK_PAGEUP) { scroll_ -= 200; if (scroll_ < 0) scroll_ = 0; return true; }
    close();
    return true;
}

void HelpScreen::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                         int w, int h) const {
    if (!open_ || !font) return;

    int line_h = TTF_FontLineSkip(font);
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;

    ui::draw_overlay(renderer, w, h, 210);

    auto screen = ui::Layout::from_screen(w, h, line_h);
    auto outer = screen.panel_outer(4, 5, 19, 20);
    auto panel = ui::draw_panel_in(renderer, outer, line_h);

    // Clip rendering to panel
    SDL_Rect clip = panel.cursor.inset(0).sdl();
    SDL_RenderSetClipRect(renderer, &clip);

    // Two-column keybind layout: key column = left 40%, desc column = right 60%
    int key_x = panel.cursor.x;
    int desc_x = panel.cursor.x + panel.cursor.w * 2 / 5;

    SDL_Color title_col = {220, 200, 160, 255};
    SDL_Color section_col = {200, 180, 120, 255};
    SDL_Color key_col = {220, 215, 200, 255};
    SDL_Color desc_col = {160, 155, 145, 255};
    SDL_Color tip_col = {130, 160, 120, 255};
    SDL_Color dim_col = {100, 95, 85, 255};
    SDL_Color skill_col = {140, 180, 160, 255};

    struct Bind { const char* key; const char* desc; };
    auto draw_binds = [&](int x_k, int x_d, Bind* binds, int count, int& y) {
        for (int i = 0; i < count; i++) {
            ui::draw_text(renderer, font, binds[i].key, key_col, x_k, y);
            ui::draw_text(renderer, font, binds[i].desc, desc_col, x_d, y);
            y += line_h + 1;
        }
    };

    int y = panel.cursor.y - scroll_;

    // Title
    ui::draw_text_centered(renderer, font_title ? font_title : font,
                            "Guide to Reliquary", title_col, panel.cursor.cx(), y);
    y += title_h + 12;

    // MOVEMENT
    ui::draw_text(renderer, font, "MOVEMENT", section_col, key_x, y);
    y += line_h + 4;
    Bind movement[] = {
        {"Arrows / WASD / hjkl", "Move (cardinal)"},
        {"yubn / Numpad",        "Move (diagonal)"},
        {". / Numpad 5",         "Wait one turn"},
        {"o",                    "Toggle sneak mode"},
    };
    draw_binds(key_x, desc_x, movement, 4, y);

    // ACTIONS
    y += 8;
    ui::draw_text(renderer, font, "ACTIONS", section_col, key_x, y);
    y += line_h + 4;
    Bind actions[] = {
        {"g / ,",          "Pick up / forage (Nature 25)"},
        {"Enter / > / <",  "Use stairs"},
        {"f",              "Fire ranged weapon"},
        {"r",              "Rest (limited per floor)"},
        {"p",              "Pray (1-2 prayers + mastery at 75 favor)"},
        {"x",              "Examine tile / creature"},
        {"Bump NPC",       "Talk / trade (or pickpocket while sneaking)"},
        {"Bump enemy",     "Melee attack (backstab from sneak)"},
    };
    draw_binds(key_x, desc_x, actions, 8, y);

    // SCREENS
    y += 8;
    ui::draw_text(renderer, font, "SCREENS", section_col, key_x, y);
    y += line_h + 4;
    Bind screens[] = {
        {"i",   "Inventory"},
        {"c",   "Character sheet (stats, skills, spells)"},
        {"z",   "Spellbook (cast spells)"},
        {"t",   "Passive tree (spend points)"},
        {"q",   "Quest journal"},
        {"M",   "World map"},
        {"Tab", "Bestiary"},
        {"?",   "This help screen"},
        {"Esc", "Pause (save/load/settings)"},
    };
    draw_binds(key_x, desc_x, screens, 9, y);

    // COMBAT
    y += 8;
    ui::draw_text(renderer, font, "COMBAT", section_col, key_x, y);
    y += line_h + 4;
    Bind combat[] = {
        {"Bump",     "Melee attack"},
        {"f",        "Ranged attack (bow/crossbow)"},
        {"z",        "Open spellbook, pick a spell"},
        {"v",        "Quick-cast last spell"},
        {"p",        "God prayers (2 + mastery)"},
        {"1-4",      "Use passive tree active abilities"},
    };
    draw_binds(key_x, desc_x, combat, 6, y);

    // STEALTH
    y += 8;
    ui::draw_text(renderer, font, "STEALTH", section_col, key_x, y);
    y += line_h + 4;
    const char* stealth_tips[] = {
        "Press O to toggle sneak. You move at half speed.",
        "Enemies have reduced detection range (shown as red circles).",
        "First attack from sneak is a backstab (2-4x damage by skill).",
        "Bump an NPC while sneaking to attempt pickpocket.",
        "Failed pickpockets summon town guards.",
        "Stealth 25: sleeping monsters don't wake when you pass.",
        "Stealth 50: detection range drops to 1 tile.",
    };
    for (auto& t : stealth_tips) {
        ui::draw_text_clipped(renderer, font, t, tip_col, key_x, y, panel.cursor.w);
        y += line_h + 1;
    }

    // PASSIVE TREE
    y += 8;
    ui::draw_text(renderer, font, "PASSIVE TREE (T key)", section_col, key_x, y);
    y += line_h + 4;
    const char* tree_tips[] = {
        "Earn 1 point per level. Spend on the shared passive tree.",
        "8 sectors: Might, Finesse, Arcane, Faith, Fortitude, Nature, Shadow, Venom.",
        "Your class determines your starting position on the tree.",
        "Click nodes to allocate. Must be connected to existing nodes.",
        "Notables (diamonds): strong named passives.",
        "Keystones (gold diamonds): powerful trade-offs (Blood Magic, Ghost Blade, etc).",
        "Capstones (gold circles): active abilities on cooldown (1-4 keys).",
        "Respec at same-god shrines (costs 10 favor, refunds 3 points).",
        "Flashing +N [T] on HUD means you have unspent points.",
    };
    for (auto& t : tree_tips) {
        ui::draw_text_clipped(renderer, font, t, tip_col, key_x, y, panel.cursor.w);
        y += line_h + 1;
    }

    // SKILLS
    y += 8;
    ui::draw_text(renderer, font, "SKILLS (level through use)", section_col, key_x, y);
    y += line_h + 4;
    const char* skill_info[] = {
        "Blades/Axes/Blunt/Unarmed: level by hitting with that weapon type.",
        "  25: +crit/+dmg/+stun  |  50: intimidate humanoids  |  75: stronger bonus",
        "Archery: level by landing ranged hits.  25/50/75: +crit%.",
        "Spell schools: level by casting.  25/50/75: reduced MP cost.",
        "Stealth: level by sneaking + backstabs.  25: don't wake sleepers.",
        "Dodge: level when enemies miss you.  25/50/75: +dodge%.",
        "Heavy Armor: level when hit in heavy armor.  50: less spell failure.",
        "Prayer: level by praying.  50: god hints at quest direction.",
        "Divination 25: auto-identify potions on pickup.",
        "Nature 25: forage herbs in overworld grass tiles.",
        "Dark Arts 25: examine corpses for dungeon hints.",
    };
    for (auto& s : skill_info) {
        ui::draw_text_clipped(renderer, font, s, skill_col, key_x, y, panel.cursor.w);
        y += line_h + 1;
    }

    // GODS
    y += 8;
    ui::draw_text(renderer, font, "GODS & FAVOR", section_col, key_x, y);
    y += line_h + 4;
    const char* god_tips[] = {
        "Each god has tenets (rules). Breaking them loses favor.",
        "Prayers cost favor. 2 prayers per god, plus a mastery at 75+ favor.",
        "Favor below -50: prayers fail. Below -100: excommunicated (divine wrath).",
        "Same-god shrines: +5 favor, heal, identify items, respec tree.",
        "God factions affect shop prices and priest reactions.",
    };
    for (auto& t : god_tips) {
        ui::draw_text_clipped(renderer, font, t, tip_col, key_x, y, panel.cursor.w);
        y += line_h + 1;
    }

    // TIPS
    y += 8;
    ui::draw_text(renderer, font, "GENERAL TIPS", section_col, key_x, y);
    y += line_h + 4;
    const char* gen_tips[] = {
        "Rest heals less each time per floor (exhaustion). Plan carefully.",
        "Potions are scarce. Nature skill lets you forage replacements.",
        "Summons persist between floors but vanish on rest (max 3).",
        "Troll regeneration is blocked by fire damage.",
        "Wraiths can only be hurt by silver/mithril/adamantine weapons or magic.",
        "Lava deals damage + burn. Deep water slows you; heavy armor = drowning.",
        "Traps are hidden. High PER or Reveal Map spell detects them.",
        "Cursed items have active negative effects (not just can't-unequip).",
        "The overworld has random encounters: merchants, hermits, shrines.",
        "Polymorph doesn't work on bosses or strong creatures.",
        "Follow road signs for directions between towns.",
    };
    for (auto& t : gen_tips) {
        ui::draw_text_clipped(renderer, font, t, tip_col, key_x, y, panel.cursor.w);
        y += line_h + 1;
    }

    // SYSTEM
    y += 8;
    ui::draw_text(renderer, font, "SYSTEM", section_col, key_x, y);
    y += line_h + 4;
    Bind sys[] = {
        {"F5",  "Quicksave"},
        {"F6",  "Quickload"},
        {"F11", "Toggle fullscreen"},
        {"F12", "Screenshot"},
    };
    draw_binds(key_x, desc_x, sys, 4, y);

    y += 12;
    max_scroll_ = std::max(0, y - panel.cursor.y - panel.cursor.h + scroll_ + 20);

    SDL_RenderSetClipRect(renderer, nullptr);

    // Scroll indicators
    if (scroll_ > 0)
        ui::draw_text_centered(renderer, font, "^ scroll up ^", dim_col, outer.cx(), outer.y + 6);
    if (scroll_ < max_scroll_)
        ui::draw_text_centered(renderer, font, "v scroll down v", dim_col, outer.cx(), outer.y2() - line_h - 4);

    ui::draw_text_centered(renderer, font, "Any other key to close  |  Up/Down/Scroll to navigate",
                            dim_col, outer.cx(), outer.y2() - 4);
}
