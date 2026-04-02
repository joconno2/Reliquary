#include "ui/character_sheet.h"
#include "ui/ui_draw.h"
#include "components/stats.h"
#include "components/renderable.h"
#include "components/player.h"
#include "components/god.h"
#include "components/class_def.h"
#include "components/inventory.h"
#include "components/item.h"
#include "components/spellbook.h"
#include "components/disease.h"
#include <cstdio>
#include <algorithm>

// Equipment bonus totals
static void get_equip_totals(World& world, Entity e,
                              int& dmg, int& armor, int& atk, int& dodge) {
    dmg = armor = atk = dodge = 0;
    if (!world.has<Inventory>(e)) return;
    auto& inv = world.get<Inventory>(e);
    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        Entity eq = inv.equipped[s];
        if (eq == NULL_ENTITY || !world.has<Item>(eq)) continue;
        auto& item = world.get<Item>(eq);
        dmg += item.damage_bonus;
        armor += item.armor_bonus;
        atk += item.attack_bonus;
        dodge += item.dodge_bonus;
    }
}

bool CharacterSheet::handle_input(SDL_Event& event) {
    if (!open_) return false;
    if (event.type != SDL_KEYDOWN) return false;

    switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_c:
            close();
            return true;
        case SDLK_UP:
        case SDLK_w:
        case SDLK_k:
            if (scroll_ > 0) scroll_--;
            return true;
        case SDLK_DOWN:
        case SDLK_s:
        case SDLK_j:
            scroll_++;
            return true;
        default:
            return true; // consume all keys while open
    }
}

void CharacterSheet::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                             const SpriteManager& sprites, World& world,
                             int screen_w, int screen_h) const {
    if (!open_ || !font) return;
    if (!world.has<Stats>(player_)) return;

    auto& stats = world.get<Stats>(player_);
    int line_h = TTF_FontLineSkip(font);
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;

    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color label_col = {140, 135, 130, 255};
    SDL_Color val_col = {200, 195, 185, 255};
    SDL_Color good_col = {120, 200, 120, 255};
    SDL_Color bad_col = {200, 120, 120, 255};
    SDL_Color dim_col = {100, 95, 90, 255};
    SDL_Color section_col = {160, 140, 120, 255};
    SDL_Color hp_col = {200, 100, 100, 255};
    SDL_Color mp_col = {100, 100, 200, 255};

    // Full screen panel
    int margin = 20;
    int panel_x = margin;
    int panel_y = margin;
    int panel_w = screen_w - margin * 2;
    int panel_h = screen_h - margin * 2;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    // Darken background
    SDL_Rect overlay = {0, 0, screen_w, screen_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &overlay);

    ui::draw_panel(renderer, panel_x, panel_y, panel_w, panel_h);

    // Equipment totals
    int eq_dmg, eq_armor, eq_atk, eq_dodge;
    get_equip_totals(world, player_, eq_dmg, eq_armor, eq_atk, eq_dodge);

    // Left column: portrait + identity + attributes
    int lx = panel_x + 16;
    int ly = panel_y + 12;

    // Character sprite (big)
    if (world.has<Renderable>(player_)) {
        auto& rend = world.get<Renderable>(player_);
        sprites.draw_sprite_sized(renderer, rend.sprite_sheet, rend.sprite_x, rend.sprite_y,
                                   lx, ly, 96);
    }

    // Name + level next to sprite
    int info_x = lx + 104;
    ui::draw_text(renderer, font_title, stats.name.c_str(), title_col, info_x, ly);

    char lvl_buf[32];
    snprintf(lvl_buf, sizeof(lvl_buf), "Level %d", stats.level);
    ui::draw_text(renderer, font, lvl_buf, label_col, info_x, ly + title_h + 2);

    // God
    if (world.has<GodAlignment>(player_)) {
        auto& ga = world.get<GodAlignment>(player_);
        auto& god = get_god_info(ga.god);
        char god_buf[64];
        snprintf(god_buf, sizeof(god_buf), "Servant of %s", god.name);
        ui::draw_text(renderer, font, god_buf, dim_col, info_x, ly + title_h + line_h + 6);

        char favor_buf[32];
        snprintf(favor_buf, sizeof(favor_buf), "Favor: %d", ga.favor);
        ui::draw_text(renderer, font, favor_buf, label_col, info_x, ly + title_h + line_h * 2 + 10);
    }

    ly += 108;

    // XP bar
    char xp_buf[48];
    snprintf(xp_buf, sizeof(xp_buf), "XP: %d / %d", stats.xp, stats.xp_next);
    ui::draw_text(renderer, font, xp_buf, label_col, lx, ly);
    ly += line_h + 4;

    // Vitals
    char hp_buf[32], mp_buf[32];
    snprintf(hp_buf, sizeof(hp_buf), "HP: %d / %d", stats.hp, stats.hp_max);
    snprintf(mp_buf, sizeof(mp_buf), "MP: %d / %d", stats.mp, stats.mp_max);
    ui::draw_text(renderer, font, hp_buf, hp_col, lx, ly);
    ly += line_h;
    ui::draw_text(renderer, font, mp_buf, mp_col, lx, ly);
    ly += line_h + 8;

    // === PRIMARY ATTRIBUTES ===
    ui::draw_text(renderer, font, "-- Attributes --", section_col, lx, ly);
    ly += line_h + 2;

    const char* attr_names[] = {"STR", "DEX", "CON", "INT", "WIL", "PER", "CHA"};
    for (int i = 0; i < ATTR_COUNT; i++) {
        int val = stats.attr(static_cast<Attr>(i));
        char buf[32];
        snprintf(buf, sizeof(buf), "  %s: %d", attr_names[i], val);
        SDL_Color col = val >= 14 ? good_col : val <= 8 ? bad_col : val_col;
        ui::draw_text(renderer, font, buf, col, lx, ly);
        ly += line_h;
    }

    // Middle column: offensive + defensive stats
    int mx = panel_x + panel_w / 3 + 8;
    int my = panel_y + 12;

    ui::draw_text(renderer, font, "-- Offensive --", section_col, mx, my);
    my += line_h + 2;

    struct StatEntry { const char* label; int value; const char* fmt; };

    int total_atk = stats.melee_attack() + eq_atk;
    int total_dmg = stats.melee_damage() + eq_dmg;
    int crit_chance = stats.attr(Attr::PER);
    int spell_power = stats.attr(Attr::INT) + stats.attr(Attr::INT) / 3;
    int spell_fail = std::max(0, 100 - stats.attr(Attr::INT) * 2);

    StatEntry offense[] = {
        {"Melee Attack",    total_atk,    nullptr},
        {"Melee Damage",    total_dmg,    nullptr},
        {"Crit Chance",     crit_chance,  "%d%%"},
        {"Attack Speed",    stats.base_speed, nullptr},
        {"Spell Power",     spell_power,  nullptr},
        {"Spell Fail",      spell_fail,   "%d%%"},
    };

    for (auto& s : offense) {
        char buf[48];
        if (s.fmt) {
            char val_str[16];
            snprintf(val_str, sizeof(val_str), s.fmt, s.value);
            snprintf(buf, sizeof(buf), "  %-16s %s", s.label, val_str);
        } else {
            snprintf(buf, sizeof(buf), "  %-16s %d", s.label, s.value);
        }
        ui::draw_text(renderer, font, buf, val_col, mx, my);
        my += line_h;
    }

    my += 8;
    ui::draw_text(renderer, font, "-- Defensive --", section_col, mx, my);
    my += line_h + 2;

    int total_dodge = stats.dodge_value() + eq_dodge;
    int total_prot = stats.protection() + eq_armor;
    int hp_regen = stats.attr(Attr::CON) / 10;
    int mp_regen = stats.attr(Attr::WIL) / 8;

    StatEntry defense[] = {
        {"Dodge",           total_dodge,  nullptr},
        {"Protection",      total_prot,   nullptr},
        {"HP Regen/turn",   hp_regen,     nullptr},
        {"MP Regen/turn",   mp_regen,     nullptr},
    };

    for (auto& s : defense) {
        char buf[48];
        snprintf(buf, sizeof(buf), "  %-16s %d", s.label, s.value);
        ui::draw_text(renderer, font, buf, val_col, mx, my);
        my += line_h;
    }

    my += 8;
    ui::draw_text(renderer, font, "-- Resistances --", section_col, mx, my);
    my += line_h + 2;

    // Resistances (derived from stats — placeholder values)
    struct ResEntry { const char* name; int val; };
    ResEntry resists[] = {
        {"Fire",      0},
        {"Cold",      0},
        {"Lightning", 0},
        {"Poison",    stats.attr(Attr::CON) / 2},
        {"Disease",   stats.attr(Attr::CON) / 3},
        {"Magic",     stats.attr(Attr::WIL) / 3},
        {"Holy",      0},
        {"Dark",      0},
    };

    for (auto& r : resists) {
        char buf[48];
        snprintf(buf, sizeof(buf), "  %-12s %d%%", r.name, r.val);
        SDL_Color col = r.val > 0 ? good_col : r.val < 0 ? bad_col : dim_col;
        ui::draw_text(renderer, font, buf, col, mx, my);
        my += line_h;
    }

    // Right column: mental + utility + equipment
    int rx = panel_x + panel_w * 2 / 3 + 8;
    int ry = panel_y + 12;

    ui::draw_text(renderer, font, "-- Mental --", section_col, rx, ry);
    ry += line_h + 2;

    StatEntry mental[] = {
        {"Fear Resist",     stats.attr(Attr::WIL) + stats.level, nullptr},
        {"Charm Resist",    stats.attr(Attr::WIL) + stats.attr(Attr::CHA) / 2, nullptr},
        {"Confuse Resist",  stats.attr(Attr::INT) + stats.attr(Attr::WIL) / 2, nullptr},
        {"Stun Resist",     stats.attr(Attr::CON), nullptr},
    };

    for (auto& s : mental) {
        char buf[48];
        snprintf(buf, sizeof(buf), "  %-16s %d", s.label, s.value);
        ui::draw_text(renderer, font, buf, val_col, rx, ry);
        ry += line_h;
    }

    ry += 8;
    ui::draw_text(renderer, font, "-- Utility --", section_col, rx, ry);
    ry += line_h + 2;

    int fov = stats.fov_radius();
    int trap_detect = stats.attr(Attr::PER) / 2;
    int secret_detect = stats.attr(Attr::PER) / 3;
    int carry_cap = stats.attr(Attr::STR) * 10 + stats.attr(Attr::CON) * 2;
    int move_speed = 100 + stats.attr(Attr::DEX) * 2;
    int stealth = stats.attr(Attr::DEX) / 2;
    int shop_mod = stats.attr(Attr::CHA) * 2;

    StatEntry utility[] = {
        {"FOV Radius",      fov,          nullptr},
        {"Trap Detection",  trap_detect,  nullptr},
        {"Secret Detection",secret_detect,nullptr},
        {"Carry Capacity",  carry_cap,    nullptr},
        {"Move Speed",      move_speed,   nullptr},
        {"Stealth",         stealth,      nullptr},
        {"Shop Modifier",   shop_mod,     "%+d%%"},
    };

    for (auto& s : utility) {
        char buf[48];
        if (s.fmt) {
            char val_str[16];
            snprintf(val_str, sizeof(val_str), s.fmt, s.value);
            snprintf(buf, sizeof(buf), "  %-16s %s", s.label, val_str);
        } else {
            snprintf(buf, sizeof(buf), "  %-16s %d", s.label, s.value);
        }
        ui::draw_text(renderer, font, buf, val_col, rx, ry);
        ry += line_h;
    }

    // Equipment summary at bottom right
    ry += 8;
    ui::draw_text(renderer, font, "-- Equipment --", section_col, rx, ry);
    ry += line_h + 2;

    if (world.has<Inventory>(player_)) {
        auto& inv = world.get<Inventory>(player_);
        const char* slot_names[] = {
            "Weapon", "Off Hand", "Head", "Chest", "Hands", "Feet",
            "Amulet", "Ring 1", "Ring 2", "Pet"
        };
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            Entity eq = inv.equipped[s];
            char buf[64];
            if (eq != NULL_ENTITY && world.has<Item>(eq)) {
                auto& item = world.get<Item>(eq);
                snprintf(buf, sizeof(buf), "  %s: %s", slot_names[s],
                         item.display_name().c_str());
                ui::draw_text(renderer, font, buf, val_col, rx, ry);
            } else {
                snprintf(buf, sizeof(buf), "  %s: -", slot_names[s]);
                ui::draw_text(renderer, font, buf, dim_col, rx, ry);
            }
            ry += line_h;
        }
    }

    // Spells known
    if (world.has<Spellbook>(player_)) {
        auto& book = world.get<Spellbook>(player_);
        if (!book.known_spells.empty()) {
            ry += 8;
            ui::draw_text(renderer, font, "-- Spells --", section_col, rx, ry);
            ry += line_h + 2;
            for (auto sid : book.known_spells) {
                auto& info = get_spell_info(sid);
                char buf[48];
                snprintf(buf, sizeof(buf), "  %s (%dmp)", info.name, info.mp_cost);
                ui::draw_text(renderer, font, buf, mp_col, rx, ry);
                ry += line_h;
                if (ry > panel_y + panel_h - line_h * 2) break;
            }
        }
    }

    // Diseases (permanent conditions)
    if (world.has<Diseases>(player_)) {
        auto& diseases = world.get<Diseases>(player_);
        if (!diseases.empty()) {
            ry += 8;
            SDL_Color disease_col = {180, 120, 200, 255};
            ui::draw_text(renderer, font, "-- Afflictions --", disease_col, rx, ry);
            ry += line_h + 2;
            for (auto did : diseases.active) {
                auto& dinfo = get_disease_info(did);
                ui::draw_text(renderer, font, dinfo.name, disease_col, rx + 8, ry);
                ry += line_h;
                if (ry > panel_y + panel_h - line_h * 3) break;
            }
        }
    }

    // Bottom hint
    ui::draw_text_centered(renderer, font, "[c / Esc] close",
                            dim_col, screen_w / 2, panel_y + panel_h - line_h - 8);
}
