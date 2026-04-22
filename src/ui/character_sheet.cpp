#include "ui/character_sheet.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
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
    if (event.type != SDL_KEYDOWN) return true;

    switch (event.key.keysym.sym) {
        case SDLK_ESCAPE: case SDLK_c: close(); return true;
        case SDLK_UP: case SDLK_w:
            if (scroll_ > 0) scroll_ -= 20;
            if (scroll_ < 0) scroll_ = 0;
            return true;
        case SDLK_DOWN: case SDLK_s: scroll_ += 20; return true;
        case SDLK_PAGEUP: scroll_ -= 150; if (scroll_ < 0) scroll_ = 0; return true;
        case SDLK_PAGEDOWN: scroll_ += 150; return true;
        default: return true;
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

    ui::draw_overlay(renderer, screen_w, screen_h);

    // Full screen panel with margin proportional to screen
    auto screen = ui::Layout::from_screen(screen_w, screen_h, line_h);
    auto outer = screen.panel_outer(19, 20, 19, 20);
    auto panel = ui::draw_panel_in(renderer, outer, line_h);

    int eq_dmg, eq_armor, eq_atk, eq_dodge;
    get_equip_totals(world, player_, eq_dmg, eq_armor, eq_atk, eq_dodge);

    // Three-column layout
    auto cols = panel.split_cols(3);
    auto left = ui::Layout::col(cols[0], line_h);
    auto mid = ui::Layout::col(cols[1], line_h);
    auto right_rect = cols[2];

    // === LEFT COLUMN: portrait + identity + attributes ===

    // Character sprite + name/level beside it
    if (world.has<Renderable>(player_)) {
        auto& rend = world.get<Renderable>(player_);
        int sprite_sz = std::min(64, left.cursor.w / 3);
        auto sprite_row = left.row(sprite_sz);
        sprites.draw_sprite_sized(renderer, rend.sprite_sheet, rend.sprite_x, rend.sprite_y,
                                   sprite_row.x, sprite_row.y, sprite_sz);

        // Name + level next to sprite (only 2 lines, fits easily)
        int info_x = sprite_row.x + sprite_sz + 8;
        int info_w = sprite_row.w - sprite_sz - 8;
        ui::draw_text_clipped(renderer, font_title, stats.name.c_str(), title_col,
                              info_x, sprite_row.y, info_w);

        char lvl_buf[32];
        snprintf(lvl_buf, sizeof(lvl_buf), "Level %d", stats.level);
        ui::draw_text(renderer, font, lvl_buf, label_col, info_x, sprite_row.y + title_h + 2);
    }
    left.skip(4);

    // God + Favor (own rows, no overlap)
    if (world.has<GodAlignment>(player_)) {
        auto& ga = world.get<GodAlignment>(player_);
        auto& god = get_god_info(ga.god);
        if (ga.god != GodId::NONE) {
            char god_buf[64];
            snprintf(god_buf, sizeof(god_buf), "Branded by %s (Favor: %d)", god.name, ga.favor);
            SDL_Color brand_col = {god.color.r, god.color.g, god.color.b, 255};
            auto god_row = left.row();
            ui::draw_text_clipped(renderer, font, god_buf, brand_col, god_row.x, god_row.y, god_row.w);
        }
    }

    // XP
    char xp_buf[48];
    snprintf(xp_buf, sizeof(xp_buf), "XP: %d / %d", stats.xp, stats.xp_next);
    auto xp_row = left.row(line_h + 4);
    ui::draw_text(renderer, font, xp_buf, label_col, xp_row.x, xp_row.y);

    // Vitals (show equipment bonus)
    char hp_buf[64], mp_buf[64];
    int hp_bonus = stats.hp_max - stats.base_hp_max;
    int mp_bonus = stats.mp_max - stats.base_mp_max;
    if (hp_bonus > 0)
        snprintf(hp_buf, sizeof(hp_buf), "HP: %d / %d (+%d from gear)", stats.hp, stats.hp_max, hp_bonus);
    else
        snprintf(hp_buf, sizeof(hp_buf), "HP: %d / %d", stats.hp, stats.hp_max);
    if (mp_bonus > 0)
        snprintf(mp_buf, sizeof(mp_buf), "MP: %d / %d (+%d from gear)", stats.mp, stats.mp_max, mp_bonus);
    else
        snprintf(mp_buf, sizeof(mp_buf), "MP: %d / %d", stats.mp, stats.mp_max);
    auto hp_row = left.row();
    ui::draw_text(renderer, font, hp_buf, hp_col, hp_row.x, hp_row.y);
    auto mp_row = left.row(line_h + 8);
    ui::draw_text(renderer, font, mp_buf, mp_col, mp_row.x, mp_row.y);

    // Attributes (show base + equipment bonus)
    auto attr_hdr = left.row(line_h + 2);
    ui::draw_text(renderer, font, "-- Attributes --", section_col, attr_hdr.x, attr_hdr.y);

    const char* attr_names[] = {"STR", "DEX", "CON", "INT", "WIL", "PER", "CHA"};
    for (int i = 0; i < ATTR_COUNT; i++) {
        Attr a = static_cast<Attr>(i);
        int base_val = stats.attr(a);
        int eff_val = stats.eff_attr(a);
        int bonus = eff_val - base_val;
        char buf[48];
        if (bonus > 0)
            snprintf(buf, sizeof(buf), "  %s: %d (+%d)", attr_names[i], eff_val, bonus);
        else if (bonus < 0)
            snprintf(buf, sizeof(buf), "  %s: %d (%d)", attr_names[i], eff_val, bonus);
        else
            snprintf(buf, sizeof(buf), "  %s: %d", attr_names[i], eff_val);
        SDL_Color col = eff_val >= 14 ? good_col : eff_val <= 8 ? bad_col : val_col;
        if (bonus > 0) col = good_col;
        auto row = left.row();
        ui::draw_text(renderer, font, buf, col, row.x, row.y);
    }

    // === MIDDLE COLUMN: offensive + defensive + resistances ===

    auto off_hdr = mid.row(line_h + 2);
    ui::draw_text(renderer, font, "-- Offensive --", section_col, off_hdr.x, off_hdr.y);

    struct StatEntry { const char* label; int value; const char* fmt; };

    int total_atk = stats.melee_attack() + eq_atk;
    int total_dmg = stats.melee_damage() + eq_dmg;
    int crit_chance = stats.eff_attr(Attr::PER);
    int spell_power = stats.eff_attr(Attr::INT) + stats.eff_attr(Attr::INT) / 3;
    int spell_fail = std::max(0, 100 - stats.eff_attr(Attr::INT) * 2);

    StatEntry offense[] = {
        {"Melee Attack",    total_atk,    nullptr},
        {"Melee Damage",    total_dmg,    nullptr},
        {"Crit Chance",     crit_chance,  "%d%%"},
        {"Attack Speed",    stats.effective_speed(), nullptr},
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
        auto row = mid.row();
        ui::draw_text_clipped(renderer, font, buf, val_col, row.x, row.y, row.w);
    }

    mid.skip(8);
    auto def_hdr = mid.row(line_h + 2);
    ui::draw_text(renderer, font, "-- Defensive --", section_col, def_hdr.x, def_hdr.y);

    int total_dodge = stats.dodge_value() + eq_dodge;
    int total_prot = stats.protection() + eq_armor;

    StatEntry defense[] = {
        {"Dodge",           total_dodge,  nullptr},
        {"Protection",      total_prot,   nullptr},
        {"HP Regen/turn",   stats.eff_attr(Attr::CON) / 10, nullptr},
        {"MP Regen/turn",   stats.eff_attr(Attr::WIL) / 8,  nullptr},
    };

    for (auto& s : defense) {
        char buf[48];
        snprintf(buf, sizeof(buf), "  %-16s %d", s.label, s.value);
        auto row = mid.row();
        ui::draw_text_clipped(renderer, font, buf, val_col, row.x, row.y, row.w);
    }

    mid.skip(8);
    auto res_hdr = mid.row(line_h + 2);
    ui::draw_text(renderer, font, "-- Resistances --", section_col, res_hdr.x, res_hdr.y);

    struct ResEntry { const char* name; int val; };
    ResEntry resists[] = {
        {"Fire", 0}, {"Cold", 0}, {"Lightning", 0},
        {"Poison", stats.eff_attr(Attr::CON) / 2}, {"Disease", stats.eff_attr(Attr::CON) / 3},
        {"Magic", stats.eff_attr(Attr::WIL) / 3}, {"Holy", 0}, {"Dark", 0},
    };

    for (auto& r : resists) {
        char buf[48];
        snprintf(buf, sizeof(buf), "  %-12s %d%%", r.name, r.val);
        SDL_Color col = r.val > 0 ? good_col : r.val < 0 ? bad_col : dim_col;
        auto row = mid.row();
        ui::draw_text_clipped(renderer, font, buf, col, row.x, row.y, row.w);
    }

    // === RIGHT COLUMN: mental, utility, equipment, skills, god (scrollable) ===
    SDL_Rect right_clip = right_rect.sdl();
    SDL_RenderSetClipRect(renderer, &right_clip);

    auto right = ui::Layout::from_rect(right_rect, line_h);
    // Apply scroll offset by shifting cursor
    right.cursor.y -= scroll_;

    auto mental_hdr = right.row(line_h + 2);
    ui::draw_text(renderer, font, "-- Mental --", section_col, mental_hdr.x, mental_hdr.y);

    StatEntry mental[] = {
        {"Fear Resist",     stats.eff_attr(Attr::WIL) + stats.level, nullptr},
        {"Charm Resist",    stats.eff_attr(Attr::WIL) + stats.eff_attr(Attr::CHA) / 2, nullptr},
        {"Confuse Resist",  stats.eff_attr(Attr::INT) + stats.eff_attr(Attr::WIL) / 2, nullptr},
        {"Stun Resist",     stats.eff_attr(Attr::CON), nullptr},
    };

    for (auto& s : mental) {
        char buf[48];
        snprintf(buf, sizeof(buf), "  %-16s %d", s.label, s.value);
        auto row = right.row();
        ui::draw_text_clipped(renderer, font, buf, val_col, row.x, row.y, row.w);
    }

    right.skip(8);
    auto util_hdr = right.row(line_h + 2);
    ui::draw_text(renderer, font, "-- Utility --", section_col, util_hdr.x, util_hdr.y);

    StatEntry utility[] = {
        {"FOV Radius",      stats.fov_radius(), nullptr},
        {"Trap Detection",  stats.eff_attr(Attr::PER) / 2, nullptr},
        {"Secret Detection",stats.eff_attr(Attr::PER) / 3, nullptr},
        {"Carry Capacity",  stats.eff_attr(Attr::STR) * 10 + stats.eff_attr(Attr::CON) * 2, nullptr},
        {"Move Speed",      stats.effective_speed() + stats.eff_attr(Attr::DEX) * 2, nullptr},
        {"Stealth",         stats.eff_attr(Attr::DEX) / 2, nullptr},
        {"Shop Modifier",   stats.eff_attr(Attr::CHA) * 2, "%+d%%"},
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
        auto row = right.row();
        ui::draw_text_clipped(renderer, font, buf, val_col, row.x, row.y, row.w);
    }

    // Equipment summary
    right.skip(8);
    auto eq_hdr = right.row(line_h + 2);
    ui::draw_text(renderer, font, "-- Equipment --", section_col, eq_hdr.x, eq_hdr.y);

    if (world.has<Inventory>(player_)) {
        auto& inv_ref = world.get<Inventory>(player_);
        const char* slot_names[] = {
            "Weapon", "Off Hand", "Head", "Chest", "Hands", "Feet",
            "Amulet", "Ring 1", "Ring 2", "Pet"
        };
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            Entity eq = inv_ref.equipped[s];
            auto row = right.row();
            char buf[128];
            if (eq != NULL_ENTITY && world.has<Item>(eq)) {
                auto& item = world.get<Item>(eq);
                snprintf(buf, sizeof(buf), "  %s: %s", slot_names[s],
                         item.display_name().c_str());
                ui::draw_text_clipped(renderer, font, buf, val_col, row.x, row.y, row.w);
            } else {
                snprintf(buf, sizeof(buf), "  %s: -", slot_names[s]);
                ui::draw_text(renderer, font, buf, dim_col, row.x, row.y);
            }
        }
    }

    // Spells
    if (world.has<Spellbook>(player_)) {
        auto& book = world.get<Spellbook>(player_);
        if (!book.known_spells.empty()) {
            right.skip(8);
            auto sp_hdr = right.row(line_h + 2);
            ui::draw_text(renderer, font, "-- Spells --", section_col, sp_hdr.x, sp_hdr.y);
            for (auto sid : book.known_spells) {
                auto& info = get_spell_info(sid);
                char buf[48];
                snprintf(buf, sizeof(buf), "  %s (%dmp)", info.name, info.mp_cost);
                auto row = right.row();
                ui::draw_text_clipped(renderer, font, buf, mp_col, row.x, row.y, row.w);
            }
        }
    }

    // Skills
    if (world.has<Skills>(player_)) {
        auto& skills = world.get<Skills>(player_);
        right.skip(8);
        SDL_Color skill_hdr_col = {180, 220, 160, 255};
        SDL_Color skill_active = {140, 200, 160, 255};
        SDL_Color skill_zero = {80, 75, 70, 255};
        SDL_Color skill_bar_bg = {30, 28, 25, 255};
        SDL_Color skill_bar_fill = {80, 140, 80, 255};
        SDL_Color unlock_done = {100, 180, 100, 255};
        SDL_Color unlock_col = {200, 200, 100, 255};
        SDL_Color unlock_future = {100, 95, 85, 255};

        auto sk_hdr = right.row(line_h + 4);
        ui::draw_text(renderer, font, "-- Skills --", skill_hdr_col, sk_hdr.x, sk_hdr.y);

        // XP bar width scales with column
        int bar_w = std::min(60, right.cursor.w / 4);
        int bar_start_x = right.cursor.x + right.cursor.w - bar_w - 4;

        for (int i = 0; i < SKILL_COUNT; i++) {
            SkillId sid = static_cast<SkillId>(i);
            int lv = skills.level[i];
            int xp = skills.xp[i];
            int needed = Skills::xp_for_level(lv);

            auto row = right.row();
            char buf[64];
            snprintf(buf, sizeof(buf), "%-14s %d", skill_name(sid), lv);
            ui::draw_text_clipped(renderer, font, buf, lv > 0 ? skill_active : skill_zero,
                                  row.x, row.y, row.w - bar_w - 8);

            // XP bar (inline, right-aligned in column)
            if (lv > 0 && lv < 100) {
                int bar_h = line_h - 4;
                int bar_y = row.y + 2;
                SDL_Rect bg = {bar_start_x, bar_y, bar_w, bar_h};
                SDL_SetRenderDrawColor(renderer, skill_bar_bg.r, skill_bar_bg.g, skill_bar_bg.b, 255);
                SDL_RenderFillRect(renderer, &bg);
                int fill = (xp * bar_w) / std::max(1, needed);
                SDL_Rect fg = {bar_start_x, bar_y, fill, bar_h};
                SDL_SetRenderDrawColor(renderer, skill_bar_fill.r, skill_bar_fill.g, skill_bar_fill.b, 255);
                SDL_RenderFillRect(renderer, &fg);
            }

            // Unlock checklist
            struct Unlock { int level; const char* desc; };
            Unlock unlocks[3] = {{0, nullptr}, {0, nullptr}, {0, nullptr}};
            switch (sid) {
                case SkillId::BLADES:       unlocks[0]={25,"+3% crit"}; unlocks[1]={50,"+5% crit, intimidate"}; unlocks[2]={75,"+8% crit, Vorpal Strike"}; break;
                case SkillId::AXES:         unlocks[0]={25,"+1 damage"}; unlocks[1]={50,"+2 damage, intimidate"}; unlocks[2]={75,"+3 damage"}; break;
                case SkillId::BLUNT:        unlocks[0]={25,"5% stun"}; unlocks[1]={50,"10% stun, intimidate"}; unlocks[2]={75,"15% stun"}; break;
                case SkillId::UNARMED:      unlocks[0]={25,"+1 dmg"}; unlocks[1]={50,"+3 dmg"}; unlocks[2]={75,"+4 dmg"}; break;
                case SkillId::ARCHERY:      unlocks[0]={25,"+3% crit"}; unlocks[1]={50,"+5% crit"}; unlocks[2]={75,"+8% crit"}; break;
                case SkillId::CONJURATION: case SkillId::TRANSMUTATION:
                case SkillId::DIVINATION: case SkillId::HEALING:
                case SkillId::NATURE_MAGIC: case SkillId::DARK_ARTS:
                    unlocks[0]={25,"-5% cost"}; unlocks[1]={50,"-12% cost"}; unlocks[2]={75,"-20% cost"};
                    if (sid==SkillId::DIVINATION) unlocks[0]={25,"Auto-ID potions"};
                    if (sid==SkillId::NATURE_MAGIC) unlocks[0]={25,"Forage herbs"};
                    if (sid==SkillId::DARK_ARTS) unlocks[0]={25,"Speak with dead"};
                    break;
                case SkillId::STEALTH:      unlocks[0]={25,"Don't wake sleepers"}; unlocks[1]={50,"Range=1, 3x backstab"}; unlocks[2]={75,"4x backstab"}; break;
                case SkillId::DODGE:        unlocks[0]={25,"+2% dodge"}; unlocks[1]={50,"+5% dodge"}; unlocks[2]={75,"+8% dodge"}; break;
                case SkillId::HEAVY_ARMOR:  unlocks[0]={25,"-5% spell fail"}; unlocks[1]={50,"-10% fail"}; unlocks[2]={75,"-15% fail"}; break;
                case SkillId::PRAYER:       unlocks[0]={25,"-5% favor cost"}; unlocks[1]={50,"God hints, -12%"}; unlocks[2]={75,"-20% cost"}; break;
                default: break;
            }

            for (int u = 0; u < 3; u++) {
                if (unlocks[u].level == 0) continue;
                auto urow = right.row();
                char ubuf[80];
                bool done = (lv >= unlocks[u].level);
                snprintf(ubuf, sizeof(ubuf), "    %s %d: %s",
                         done ? "[x]" : "[ ]", unlocks[u].level, unlocks[u].desc);
                ui::draw_text_clipped(renderer, font, ubuf,
                                      done ? unlock_done : (lv >= unlocks[u].level - 10 ? unlock_col : unlock_future),
                                      urow.x, urow.y, urow.w);
            }
            right.skip(2);
        }
    }

    // God favor details
    if (world.has<GodAlignment>(player_)) {
        auto& ga = world.get<GodAlignment>(player_);
        if (ga.god != GodId::NONE) {
            right.skip(8);
            SDL_Color god_hdr_col = {220, 200, 120, 255};
            auto& ginfo = get_god_info(ga.god);
            char gbuf[64];
            snprintf(gbuf, sizeof(gbuf), "-- %s (Favor: %d) --", ginfo.name, ga.favor);
            auto gh = right.row(line_h + 2);
            ui::draw_text_clipped(renderer, font, gbuf, god_hdr_col, gh.x, gh.y, gh.w);

            struct Milestone { int favor; const char* desc; };
            Milestone milestones[] = {
                {25, "Passive bonus tier 2"},
                {50, "Passive bonus tier 3"},
                {75, "Mastery prayer unlocked (press 3 when praying)"},
            };
            for (auto& m : milestones) {
                auto mr = right.row();
                char mbuf[80];
                bool done = ga.favor >= m.favor;
                snprintf(mbuf, sizeof(mbuf), "    %s Favor %d: %s",
                         done ? "[x]" : "[ ]", m.favor, m.desc);
                ui::draw_text_clipped(renderer, font, mbuf,
                                      done ? SDL_Color{100,200,100,255} :
                                      (ga.favor >= m.favor - 15 ? SDL_Color{200,200,100,255} : SDL_Color{100,95,85,255}),
                                      mr.x, mr.y, mr.w);
            }

            right.skip(4);
            auto tr = right.row();
            ui::draw_text_clipped(renderer, font, "  Tenets: follow your god's rules to maintain favor.",
                                  dim_col, tr.x, tr.y, tr.w);
        }
    }

    // Passive tree summary
    if (world.has<PassiveTreeState>(player_)) {
        auto& tree = world.get<PassiveTreeState>(player_);
        right.skip(8);
        SDL_Color tree_col = {180, 200, 220, 255};
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "-- Passive Tree (%d spent, %d available) --",
                 tree.points_spent, tree.points_available);
        auto th = right.row(line_h + 2);
        ui::draw_text_clipped(renderer, font, tbuf, tree_col, th.x, th.y, th.w);
        if (tree.points_available > 0) {
            auto tp = right.row();
            auto* ig = InputGlyphs::get();
            char tbuf[128];
            if (ig && ig->using_gamepad())
                snprintf(tbuf, sizeof(tbuf), "  Press %s to open and spend points.",
                         ig->label(Action::PASSIVE_TREE).c_str());
            else
                snprintf(tbuf, sizeof(tbuf), "  Press T to open and spend points.");
            ui::draw_text(renderer, font, tbuf, {220, 200, 80, 255}, tp.x, tp.y);
        }
    }

    // Diseases
    if (world.has<Diseases>(player_)) {
        auto& diseases = world.get<Diseases>(player_);
        if (!diseases.empty()) {
            right.skip(8);
            SDL_Color disease_col = {180, 120, 200, 255};
            auto dh = right.row(line_h + 2);
            ui::draw_text(renderer, font, "-- Afflictions --", disease_col, dh.x, dh.y);
            for (auto did : diseases.active) {
                auto& dinfo = get_disease_info(did);
                auto dr = right.row();
                ui::draw_text(renderer, font, dinfo.name, disease_col, dr.x + 8, dr.y);
            }
        }
    }

    SDL_RenderSetClipRect(renderer, nullptr);

    // Scroll hint
    auto hint_rect = ui::Rect{outer.x, outer.y2() - line_h - 8, outer.w, line_h};
    { auto* ig = InputGlyphs::get();
      char hbuf[256];
      if (scroll_ > 0 || right.cursor.y > right_rect.y2()) {
          if (ig && ig->using_gamepad())
              snprintf(hbuf, sizeof(hbuf), "D-Pad navigate  |  %s close", ig->cancel().c_str());
          else
              snprintf(hbuf, sizeof(hbuf), "[Up/Down/Scroll] navigate  |  [c / Esc] close");
      } else {
          if (ig && ig->using_gamepad())
              snprintf(hbuf, sizeof(hbuf), "%s close", ig->cancel().c_str());
          else
              snprintf(hbuf, sizeof(hbuf), "[c / Esc] close");
      }
      ui::draw_text_in(renderer, font, hbuf, dim_col, hint_rect, ui::Align::CENTER); }
}
