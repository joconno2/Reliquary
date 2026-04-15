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
#include "components/skills.h"
#include "components/passive_tree.h"
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
    if (event.type == SDL_MOUSEWHEEL) {
        scroll_ -= event.wheel.y * 25;
        if (scroll_ < 0) scroll_ = 0;
        return true;
    }
    if (event.type != SDL_KEYDOWN) return true; // consume all events while open

    switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_c:
            close();
            return true;
        case SDLK_UP:
        case SDLK_w:
            if (scroll_ > 0) scroll_ -= 20;
            if (scroll_ < 0) scroll_ = 0;
            return true;
        case SDLK_DOWN:
        case SDLK_s:
            scroll_ += 20;
            return true;
        case SDLK_PAGEUP:
            scroll_ -= 150;
            if (scroll_ < 0) scroll_ = 0;
            return true;
        case SDLK_PAGEDOWN:
            scroll_ += 150;
            return true;
        default:
            return true;
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
        if (ga.god != GodId::NONE)
            snprintf(god_buf, sizeof(god_buf), "Branded by %s", god.name);
        else
            snprintf(god_buf, sizeof(god_buf), "Branded (godless)");
        SDL_Color brand_display_col = {god.color.r, god.color.g, god.color.b, 255};
        ui::draw_text(renderer, font, god_buf, brand_display_col, info_x, ly + title_h + line_h + 6);

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

    // Clip right column for scrolling
    SDL_Rect right_clip = {static_cast<int>(panel_x + panel_w * 2.0f / 3), panel_y, panel_w / 3, panel_h};
    SDL_RenderSetClipRect(renderer, &right_clip);

    // Right column: mental + utility + equipment (scrollable)
    int rx = panel_x + panel_w * 2 / 3 + 8;
    int ry = panel_y + 12 - scroll_;

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
        int right_col_w = panel_w / 3 - 16;
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            Entity eq = inv.equipped[s];
            char buf[96];
            if (eq != NULL_ENTITY && world.has<Item>(eq)) {
                auto& item = world.get<Item>(eq);
                snprintf(buf, sizeof(buf), "  %s: %s", slot_names[s],
                         item.display_name().c_str());
                ui::draw_text_clipped(renderer, font, buf, val_col, rx, ry, right_col_w);
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

    // ══ SKILLS (expanded, all shown) ══
    if (world.has<Skills>(player_)) {
        auto& skills = world.get<Skills>(player_);
        ry += 8;
        SDL_Color skill_hdr = {180, 220, 160, 255};
        SDL_Color skill_active = {140, 200, 160, 255};
        SDL_Color skill_zero = {80, 75, 70, 255};
        SDL_Color skill_bar_bg = {30, 28, 25, 255};
        SDL_Color skill_bar_fill = {80, 140, 80, 255};
        SDL_Color unlock_col = {200, 200, 100, 255};
        SDL_Color unlock_done = {100, 180, 100, 255};
        SDL_Color unlock_future = {100, 95, 85, 255};

        ui::draw_text(renderer, font, "-- Skills --", skill_hdr, rx, ry);
        ry += line_h + 4;

        for (int i = 0; i < SKILL_COUNT; i++) {
            SkillId sid = static_cast<SkillId>(i);
            int lv = skills.level[i];
            int xp = skills.xp[i];
            int needed = Skills::xp_for_level(lv);

            // Name + level
            char buf[64];
            snprintf(buf, sizeof(buf), "%-14s %d", skill_name(sid), lv);
            ui::draw_text(renderer, font, buf, lv > 0 ? skill_active : skill_zero, rx, ry);

            // XP progress bar (small, inline)
            if (lv > 0 && lv < 100) {
                int bar_x = rx + 180;
                int bar_w = 60;
                int bar_h = line_h - 4;
                int bar_y = ry + 2;
                SDL_Rect bg = {bar_x, bar_y, bar_w, bar_h};
                SDL_SetRenderDrawColor(renderer, skill_bar_bg.r, skill_bar_bg.g, skill_bar_bg.b, 255);
                SDL_RenderFillRect(renderer, &bg);
                int fill = (xp * bar_w) / std::max(1, needed);
                SDL_Rect fg = {bar_x, bar_y, fill, bar_h};
                SDL_SetRenderDrawColor(renderer, skill_bar_fill.r, skill_bar_fill.g, skill_bar_fill.b, 255);
                SDL_RenderFillRect(renderer, &fg);
            }

            ry += line_h;

            // Next unlock description
            const char* next_unlock = nullptr;
            int next_lv = 0;
            bool already_unlocked = false;

            // Define unlocks per skill
            struct Unlock { int level; const char* desc; };
            Unlock unlocks[3] = {{0, nullptr}, {0, nullptr}, {0, nullptr}};

            switch (sid) {
                case SkillId::BLADES:
                    unlocks[0] = {25, "+3% crit chance"};
                    unlocks[1] = {50, "+5% crit, intimidate humanoids"};
                    unlocks[2] = {75, "+8% crit, Vorpal Strike"};
                    break;
                case SkillId::AXES:
                    unlocks[0] = {25, "+1 damage"};
                    unlocks[1] = {50, "+2 damage, intimidate"};
                    unlocks[2] = {75, "+3 damage"};
                    break;
                case SkillId::BLUNT:
                    unlocks[0] = {25, "5% stun on hit"};
                    unlocks[1] = {50, "10% stun, intimidate"};
                    unlocks[2] = {75, "15% stun"};
                    break;
                case SkillId::UNARMED:
                    unlocks[0] = {25, "+1 unarmed damage"};
                    unlocks[1] = {50, "+3 damage"};
                    unlocks[2] = {75, "+4 damage"};
                    break;
                case SkillId::ARCHERY:
                    unlocks[0] = {25, "+3% crit"};
                    unlocks[1] = {50, "+5% crit"};
                    unlocks[2] = {75, "+8% crit"};
                    break;
                case SkillId::CONJURATION: case SkillId::TRANSMUTATION:
                case SkillId::DIVINATION: case SkillId::HEALING:
                case SkillId::NATURE_MAGIC: case SkillId::DARK_ARTS:
                    unlocks[0] = {25, "-5% spell cost"};
                    unlocks[1] = {50, "-12% spell cost"};
                    unlocks[2] = {75, "-20% spell cost"};
                    // Special overrides
                    if (sid == SkillId::DIVINATION) unlocks[0] = {25, "Auto-ID potions on pickup"};
                    if (sid == SkillId::NATURE_MAGIC) unlocks[0] = {25, "Forage herbs in overworld"};
                    if (sid == SkillId::DARK_ARTS) unlocks[0] = {25, "Speak with dead (corpse hints)"};
                    break;
                case SkillId::STEALTH:
                    unlocks[0] = {25, "Sleeping foes don't wake"};
                    unlocks[1] = {50, "Detection range = 1 tile, 3x backstab"};
                    unlocks[2] = {75, "4x backstab damage"};
                    break;
                case SkillId::DODGE:
                    unlocks[0] = {25, "+2% dodge"};
                    unlocks[1] = {50, "+5% dodge"};
                    unlocks[2] = {75, "+8% dodge"};
                    break;
                case SkillId::HEAVY_ARMOR:
                    unlocks[0] = {25, "-5% spell failure"};
                    unlocks[1] = {50, "-10% spell failure"};
                    unlocks[2] = {75, "-15% spell failure"};
                    break;
                case SkillId::PRAYER:
                    unlocks[0] = {25, "-5% favor cost"};
                    unlocks[1] = {50, "God hints at quests, -12% cost"};
                    unlocks[2] = {75, "-20% favor cost"};
                    break;
                default: break;
            }

            // Show unlocks as a compact line
            for (int u = 0; u < 3; u++) {
                if (unlocks[u].level == 0) continue;
                char ubuf[80];
                bool done = (lv >= unlocks[u].level);
                snprintf(ubuf, sizeof(ubuf), "    %s %d: %s",
                         done ? "[x]" : "[ ]", unlocks[u].level, unlocks[u].desc);
                ui::draw_text(renderer, font, ubuf,
                              done ? unlock_done : (lv >= unlocks[u].level - 10 ? unlock_col : unlock_future),
                              rx, ry);
                ry += line_h;
            }
            ry += 2;
        }
    }

    // ══ GOD FAVOR DETAILS ══
    if (world.has<GodAlignment>(player_)) {
        auto& ga = world.get<GodAlignment>(player_);
        if (ga.god != GodId::NONE) {
            ry += 8;
            SDL_Color god_hdr = {220, 200, 120, 255};
            auto& ginfo = get_god_info(ga.god);
            char gbuf[64];
            snprintf(gbuf, sizeof(gbuf), "-- %s (Favor: %d) --", ginfo.name, ga.favor);
            ui::draw_text(renderer, font, gbuf, god_hdr, rx, ry);
            ry += line_h + 2;

            SDL_Color milestone_done = {100, 200, 100, 255};
            SDL_Color milestone_next = {200, 200, 100, 255};
            SDL_Color milestone_far = {100, 95, 85, 255};

            struct Milestone { int favor; const char* desc; };
            Milestone milestones[] = {
                {25, "Passive bonus tier 2"},
                {50, "Passive bonus tier 3"},
                {75, "Mastery prayer unlocked (press 3 when praying)"},
            };
            for (auto& m : milestones) {
                char mbuf[80];
                bool done = ga.favor >= m.favor;
                snprintf(mbuf, sizeof(mbuf), "    %s Favor %d: %s",
                         done ? "[x]" : "[ ]", m.favor, m.desc);
                ui::draw_text(renderer, font, mbuf,
                              done ? milestone_done : (ga.favor >= m.favor - 15 ? milestone_next : milestone_far),
                              rx, ry);
                ry += line_h;
            }

            // Tenet summary
            ry += 4;
            ui::draw_text(renderer, font, "  Tenets: follow your god's rules to maintain favor.", dim_col, rx, ry);
            ry += line_h;
        }
    }

    // ══ PASSIVE TREE SUMMARY ══
    if (world.has<PassiveTreeState>(player_)) {
        auto& tree = world.get<PassiveTreeState>(player_);
        ry += 8;
        SDL_Color tree_col = {180, 200, 220, 255};
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "-- Passive Tree (%d spent, %d available) --",
                 tree.points_spent, tree.points_available);
        ui::draw_text(renderer, font, tbuf, tree_col, rx, ry);
        ry += line_h + 2;
        if (tree.points_available > 0) {
            ui::draw_text(renderer, font, "  Press T to open and spend points.", {220, 200, 80, 255}, rx, ry);
            ry += line_h;
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

    SDL_RenderSetClipRect(renderer, nullptr);

    // Scroll hint
    if (scroll_ > 0 || ry > panel_y + panel_h) {
        ui::draw_text_centered(renderer, font, "[Up/Down/Scroll] navigate  |  [c / Esc] close",
                                dim_col, screen_w / 2, panel_y + panel_h - line_h - 8);
    } else {
        ui::draw_text_centered(renderer, font, "[c / Esc] close",
                                dim_col, screen_w / 2, panel_y + panel_h - line_h - 8);
    }
}
