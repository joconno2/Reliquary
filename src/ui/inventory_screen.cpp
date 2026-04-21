#include "ui/inventory_screen.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
#include "components/inventory.h"
#include "components/item.h"
#include "components/renderable.h"
#include "components/stats.h"
#include <cstdio>
#include <algorithm>
#include <vector>

static int item_type_sort_key(ItemType t) {
    switch (t) {
        case ItemType::WEAPON: return 0;
        case ItemType::ARMOR_HEAD: case ItemType::ARMOR_CHEST:
        case ItemType::ARMOR_HANDS: case ItemType::ARMOR_FEET: return 1;
        case ItemType::SHIELD: return 2;
        case ItemType::AMULET: case ItemType::RING: return 3;
        case ItemType::POTION: return 4;
        case ItemType::FOOD: return 5;
        case ItemType::SCROLL: return 6;
        case ItemType::PET: return 7;
        default: return 8;
    }
}

static int rarity_sort_key(Rarity r) {
    switch (r) {
        case Rarity::RELIC:     return 0;
        case Rarity::LEGENDARY: return 1;
        case Rarity::RARE:      return 2;
        case Rarity::MAGIC:     return 3;
        case Rarity::COMMON:    return 4;
        default: return 5;
    }
}

std::vector<int> InventoryScreen::get_sorted_indices(World& world) const {
    if (!world.has<Inventory>(player_)) return {};
    auto& inv = world.get<Inventory>(player_);
    int count = static_cast<int>(inv.items.size());
    std::vector<int> indices(count);
    for (int i = 0; i < count; i++) indices[i] = i;

    if (sort_mode_ == InvSortMode::DEFAULT) return indices;

    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        Entity ea = inv.items[a], eb = inv.items[b];
        if (!world.has<Item>(ea) || !world.has<Item>(eb)) return a < b;
        auto& ia = world.get<Item>(ea);
        auto& ib = world.get<Item>(eb);

        switch (sort_mode_) {
            case InvSortMode::BY_TYPE: {
                int ta = item_type_sort_key(ia.type), tb = item_type_sort_key(ib.type);
                if (ta != tb) return ta < tb;
                return ia.gold_value > ib.gold_value;
            }
            case InvSortMode::BY_RARITY: {
                int ra = rarity_sort_key(ia.rarity), rb = rarity_sort_key(ib.rarity);
                if (ra != rb) return ra < rb;
                return ia.gold_value > ib.gold_value;
            }
            case InvSortMode::BY_VALUE:
                return ia.gold_value > ib.gold_value;
            default: return a < b;
        }
    });
    return indices;
}

static const char* sort_mode_name(InvSortMode m) {
    switch (m) {
        case InvSortMode::DEFAULT:  return "Default";
        case InvSortMode::BY_TYPE:  return "By Type";
        case InvSortMode::BY_RARITY: return "By Rarity";
        case InvSortMode::BY_VALUE: return "By Value";
        default: return "";
    }
}

static const char* slot_label(int slot) {
    static const char* names[] = {
        "Weapon", "Off Hand", "Head", "Chest", "Hands",
        "Feet", "Amulet", "Ring 1", "Ring 2", "Pet"
    };
    if (slot >= 0 && slot < EQUIP_SLOT_COUNT) return names[slot];
    return "?";
}

int InventoryScreen::find_slot_at(int mx, int my) const {
    for (auto& sr : slot_rects_) {
        if (mx >= sr.rect.x && mx < sr.rect.x + sr.rect.w &&
            my >= sr.rect.y && my < sr.rect.y + sr.rect.h) {
            return sr.slot_index;
        }
    }
    return -1;
}

int InventoryScreen::find_item_at(int mx, int my) const {
    for (auto& ir : item_rects_) {
        if (mx >= ir.rect.x && mx < ir.rect.x + ir.rect.w &&
            my >= ir.rect.y && my < ir.rect.y + ir.rect.h) {
            return ir.item_index;
        }
    }
    return -1;
}

int InventoryScreen::find_button_at(int mx, int my) const {
    auto in_rect = [](int x, int y, const SDL_Rect& r) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    };
    if (in_rect(mx, my, equip_btn_)) return 0;
    if (in_rect(mx, my, use_btn_)) return 1;
    if (in_rect(mx, my, drop_btn_)) return 2;
    return -1;
}

InvAction InventoryScreen::handle_input(SDL_Event& event) {
    if (!open_) return InvAction::NONE;

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mx = event.button.x;
        int my = event.button.y;
        int item_idx = find_item_at(mx, my);
        if (item_idx >= 0) { selected_ = item_idx; return InvAction::NONE; }
        int slot_idx = find_slot_at(mx, my);
        if (slot_idx >= 0) return InvAction::EQUIP;
        int btn = find_button_at(mx, my);
        if (btn == 0) return InvAction::EQUIP;
        if (btn == 1) return InvAction::USE;
        if (btn == 2) return InvAction::DROP;
        return InvAction::NONE;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
        int mx = event.button.x;
        int my = event.button.y;
        int item_idx = find_item_at(mx, my);
        if (item_idx >= 0) { selected_ = item_idx; return InvAction::EQUIP; }
        return InvAction::NONE;
    }

    // Mouse wheel scroll
    if (event.type == SDL_MOUSEWHEEL) {
        if (event.wheel.y > 0 && selected_ > 0) selected_--;
        else if (event.wheel.y < 0) selected_++;
        return InvAction::NONE;
    }

    if (event.type != SDL_KEYDOWN) return InvAction::NONE;

    switch (event.key.keysym.sym) {
        case SDLK_ESCAPE: case SDLK_i: return InvAction::CLOSE;
        case SDLK_UP: case SDLK_w: case SDLK_k:
            if (selected_ > 0) selected_--;
            return InvAction::NONE;
        case SDLK_DOWN: case SDLK_s: case SDLK_j:
            selected_++;
            return InvAction::NONE;
        case SDLK_e: case SDLK_RETURN: return InvAction::EQUIP;
        case SDLK_u: return InvAction::USE;
        case SDLK_d: return InvAction::DROP;
        case SDLK_TAB:
            sort_mode_ = static_cast<InvSortMode>(
                (static_cast<int>(sort_mode_) + 1) % static_cast<int>(InvSortMode::COUNT));
            return InvAction::NONE;
        default:
            if (event.key.keysym.sym >= SDLK_a && event.key.keysym.sym <= SDLK_z)
                selected_ = event.key.keysym.sym - SDLK_a;
            return InvAction::NONE;
    }
}

Entity InventoryScreen::get_selected_item(World& world) const {
    if (!world.has<Inventory>(player_)) return NULL_ENTITY;
    auto& inv = world.get<Inventory>(player_);
    auto sorted = get_sorted_indices(world);
    if (selected_ < 0 || selected_ >= static_cast<int>(sorted.size()))
        return NULL_ENTITY;
    int orig_idx = sorted[selected_];
    if (orig_idx < 0 || orig_idx >= static_cast<int>(inv.items.size()))
        return NULL_ENTITY;
    return inv.items[orig_idx];
}

void InventoryScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                              const SpriteManager& sprites, World& world,
                              int screen_w, int screen_h) const {
    if (!open_ || !font) return;
    if (!world.has<Inventory>(player_)) return;

    auto& inv = world.get<Inventory>(player_);
    int line_h = TTF_FontLineSkip(font);

    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color item_col = {180, 175, 170, 255};
    SDL_Color equip_col = {140, 180, 140, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color hint_col = {120, 110, 100, 255};
    SDL_Color empty_col = {60, 55, 50, 255};
    SDL_Color btn_col = {160, 155, 150, 255};

    ui::draw_overlay(renderer, screen_w, screen_h, 160);

    auto screen = ui::Layout::from_screen(screen_w, screen_h, line_h);
    auto outer = screen.panel_outer(3, 4, 3, 4);
    auto cols = ui::Layout::from_rect(outer, line_h).split_cols_ratio(2, 3);

    // Paper doll panel (left)
    auto doll_outer = cols[0];
    ui::draw_panel(renderer, doll_outer.x, doll_outer.y, doll_outer.w, doll_outer.h);
    auto doll = ui::Layout::from_rect(doll_outer.inset(ui::Layout::PANEL_INSET), line_h);

    // Character sprite
    if (world.has<Renderable>(player_)) {
        auto& rend = world.get<Renderable>(player_);
        int sprite_sz = std::min(96, doll.cursor.w / 2);
        sprites.draw_sprite_sized(renderer, rend.sprite_sheet, rend.sprite_x, rend.sprite_y,
                                   doll.cursor.cx() - sprite_sz / 2, doll.cursor.y, sprite_sz);
        doll.skip(sprite_sz + 8);
    }

    // Equipment slots: scale slot size to panel width, ensure no overlap
    slot_rects_.clear();
    int slot_gap = 4;
    // 3 columns need: 3*slot_size + 2*slot_gap <= panel width
    int slot_size = std::min(std::max(28, (doll.cursor.w - 2 * slot_gap) / 3), 48);
    int dcx = doll.cursor.cx();
    int total_w = slot_size * 3 + slot_gap * 2;

    int col_l = dcx - total_w / 2;
    int col_c = col_l + slot_size + slot_gap;
    int col_r = col_c + slot_size + slot_gap;

    int row_step = slot_size + slot_gap + line_h + 4;
    int r0 = doll.cursor.y;
    int r1 = r0 + row_step;
    int r2 = r1 + row_step;
    int r3 = r2 + row_step;

    struct SL { int x, y; };
    SL positions[] = {
        {col_l, r1}, {col_r, r1}, {col_c, r0}, {col_c, r1},
        {col_l, r2}, {col_c, r2}, {col_r, r0}, {col_l, r3},
        {col_r, r3}, {col_c, r3},
    };

    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        int sx = positions[s].x;
        int sy = positions[s].y;
        SDL_Rect slot_rect = {sx, sy, slot_size, slot_size};
        slot_rects_.push_back({slot_rect, s});

        SDL_SetRenderDrawColor(renderer, 22, 20, 30, 255);
        SDL_RenderFillRect(renderer, &slot_rect);
        SDL_SetRenderDrawColor(renderer, 55, 48, 65, 255);
        SDL_RenderDrawRect(renderer, &slot_rect);

        Entity eq = inv.equipped[s];
        if (eq != NULL_ENTITY && world.has<Renderable>(eq)) {
            auto& rend = world.get<Renderable>(eq);
            sprites.draw_sprite_sized(renderer, rend.sprite_sheet, rend.sprite_x, rend.sprite_y,
                                       sx + 4, sy + 4, slot_size - 8);
        } else {
            ui::draw_text_centered(renderer, font, slot_label(s), empty_col,
                                    sx + slot_size / 2, sy + (slot_size - line_h) / 2);
        }
    }

    // Carried items panel (right)
    auto list_outer = cols[1];
    ui::draw_panel(renderer, list_outer.x, list_outer.y, list_outer.w, list_outer.h);
    auto list = ui::Layout::from_rect(list_outer.inset(ui::Layout::PANEL_INSET), line_h);

    // Title row
    auto title_row = list.row(line_h + 6);
    ui::draw_text(renderer, font, "Inventory", title_col, title_row.x, title_row.y);

    // Sort mode indicator (right-aligned in title row)
    char sort_buf[32];
    snprintf(sort_buf, sizeof(sort_buf), "[Tab] %s", sort_mode_name(sort_mode_));
    int sort_tw = ui::text_width(font, sort_buf);
    ui::draw_text(renderer, font, sort_buf, hint_col, title_row.x2() - sort_tw, title_row.y - 8);

    item_rects_.clear();

    int sel = selected_;
    auto sorted = get_sorted_indices(world);
    int count = static_cast<int>(sorted.size());
    if (sel >= count && count > 0) sel = count - 1;

    if (sorted.empty()) {
        auto empty_row = list.row();
        ui::draw_text(renderer, font, "(empty)", hint_col, empty_row.x, empty_row.y);
    }

    // Reserve bottom for buttons + detail
    auto btn_strip = list.row_bottom(line_h + 20);
    auto detail_area = list.row_bottom(list.remaining_h() / 2);
    // Remaining space is item list

    // Calculate visible count and scroll
    int row_h = std::max(line_h + 8, 36);
    int visible_count = std::max(1, list.remaining_h() / row_h);
    // Reserve space for scroll indicators (top and bottom take a row each)
    if (count > visible_count) visible_count = std::max(1, visible_count - 2);
    if (sel >= count && count > 0) sel = count - 1;
    // Auto-scroll to keep selection visible
    if (sel < scroll_) scroll_ = sel;
    if (sel >= scroll_ + visible_count) scroll_ = sel - visible_count + 1;
    if (scroll_ > count - visible_count) scroll_ = std::max(0, count - visible_count);

    // Scroll indicator (top)
    if (scroll_ > 0) {
        auto ind_row = list.row(line_h);
        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "-- %d more above --", scroll_);
        ui::draw_text_centered(renderer, font, sbuf, hint_col,
                               ind_row.cx(), ind_row.y);
        visible_count--;
    }

    for (int vi = 0; vi < visible_count && scroll_ + vi < count; vi++) {
        int di = scroll_ + vi;
        int i = sorted[di];
        Entity item_e = inv.items[i];
        if (!world.has<Item>(item_e)) continue;
        auto& item = world.get<Item>(item_e);

        bool is_equipped = inv.is_equipped(item_e);
        bool is_sel = (di == sel);

        if (!list.fits(row_h)) break;
        auto row = list.row(row_h);
        SDL_Rect row_rect = row.sdl();
        item_rects_.push_back({row_rect, i});

        if (is_sel) {
            SDL_SetRenderDrawColor(renderer, 35, 30, 48, 255);
            SDL_RenderFillRect(renderer, &row_rect);
        }

        // Item sprite
        if (world.has<Renderable>(item_e)) {
            auto& rend = world.get<Renderable>(item_e);
            sprites.draw_sprite(renderer, rend.sprite_sheet, rend.sprite_x, rend.sprite_y,
                                row.x + 2, row.y + 2, 1);
        }

        char letter = 'a' + static_cast<char>(i);
        char buf[128];
        snprintf(buf, sizeof(buf), "%c) %s%s", letter,
                 item.display_name().c_str(),
                 is_equipped ? " [E]" : "");

        SDL_Color col;
        if (is_sel) col = sel_col;
        else if (is_equipped) col = equip_col;
        else if (item.rarity != Rarity::COMMON) col = rarity_color(item.rarity);
        else col = item_col;
        int sprite_offset = 36;
        ui::draw_text_clipped(renderer, font, buf, col,
                              row.x + sprite_offset, row.y + 8, row.w - sprite_offset);
    }

    // Scroll indicator (bottom)
    int below = count - (scroll_ + visible_count + (scroll_ > 0 ? 1 : 0));
    if (below > 0 && list.fits(line_h)) {
        auto ind_row = list.row(line_h);
        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "-- %d more below --", below);
        ui::draw_text_centered(renderer, font, sbuf, hint_col,
                               ind_row.cx(), ind_row.y);
    }

    // Action buttons
    int btn_w = btn_strip.w / 4;
    int btn_h = line_h + 8;
    int btn_gap_px = list.gap;

    equip_btn_ = {btn_strip.x, btn_strip.y + (btn_strip.h - btn_h) / 2, btn_w, btn_h};
    use_btn_ = {btn_strip.x + btn_w + btn_gap_px, btn_strip.y + (btn_strip.h - btn_h) / 2, btn_w, btn_h};
    drop_btn_ = {btn_strip.x + (btn_w + btn_gap_px) * 2, btn_strip.y + (btn_strip.h - btn_h) / 2, btn_w, btn_h};

    auto draw_button = [&](const SDL_Rect& r, const char* label) {
        SDL_SetRenderDrawColor(renderer, 30, 25, 40, 255);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
        SDL_RenderDrawRect(renderer, &r);
        ui::draw_text(renderer, font, label, btn_col, r.x + 8, r.y + 4);
    };

    draw_button(equip_btn_, "[E]quip");
    draw_button(use_btn_, "[U]se");
    draw_button(drop_btn_, "[D]rop");

    // Item description and stats in detail area
    if (sel >= 0 && sel < count) {
        int orig_sel = sorted[sel];
        Entity item_e = inv.items[orig_sel];
        if (world.has<Item>(item_e)) {
            auto& item = world.get<Item>(item_e);
            SDL_Color stat_col = {140, 160, 180, 255};
            SDL_Color value_col = {200, 180, 80, 255};

            // Separator
            SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
            SDL_RenderDrawLine(renderer, detail_area.x + 4, detail_area.y - 2,
                               detail_area.x2() - 4, detail_area.y - 2);

            auto det = ui::Layout::from_rect(detail_area.inset(4, 4), line_h);
            ui::ClipGuard cg(renderer, detail_area.sdl());

            if (!item.description.empty()) {
                int dh = ui::text_wrapped_height(font, item.description.c_str(), det.cursor.w);
                auto dr = det.row(dh + 4);
                ui::draw_text_wrapped(renderer, font, item.description.c_str(), hint_col,
                                       dr.x, dr.y, dr.w);
            }

            // Item stats
            char stats_buf[128];
            if (item.type == ItemType::WEAPON) {
                if (item.range > 0) {
                    int eff_dmg = item.damage_bonus;
                    if (world.has<Stats>(player_))
                        eff_dmg += world.get<Stats>(player_).attr(Attr::DEX) / 3;
                    snprintf(stats_buf, sizeof(stats_buf), "Dmg: +%d  Atk: +%d  Range: %d",
                             item.damage_bonus, item.attack_bonus, item.range);
                    auto sr = det.row(line_h + 2);
                    ui::draw_text(renderer, font, stats_buf, stat_col, sr.x, sr.y);
                    snprintf(stats_buf, sizeof(stats_buf), "Effective: %d dmg (DEX)", eff_dmg);
                    auto er = det.row(line_h + 2);
                    ui::draw_text(renderer, font, stats_buf, SDL_Color{120, 200, 180, 255}, er.x, er.y);
                } else {
                    int eff_dmg = item.damage_bonus;
                    if (world.has<Stats>(player_))
                        eff_dmg += world.get<Stats>(player_).melee_damage();
                    snprintf(stats_buf, sizeof(stats_buf), "Dmg: +%d  Atk: +%d",
                             item.damage_bonus, item.attack_bonus);
                    auto sr = det.row(line_h + 2);
                    ui::draw_text(renderer, font, stats_buf, stat_col, sr.x, sr.y);
                    snprintf(stats_buf, sizeof(stats_buf), "Effective: %d dmg (STR)", eff_dmg);
                    auto er = det.row(line_h + 2);
                    ui::draw_text(renderer, font, stats_buf, SDL_Color{120, 200, 180, 255}, er.x, er.y);
                }
            } else if (item.type == ItemType::ARMOR_HEAD || item.type == ItemType::ARMOR_CHEST ||
                       item.type == ItemType::ARMOR_HANDS || item.type == ItemType::ARMOR_FEET ||
                       item.type == ItemType::SHIELD) {
                snprintf(stats_buf, sizeof(stats_buf), "Armor: +%d  Dodge: +%d",
                         item.armor_bonus, item.dodge_bonus);
                auto sr = det.row(line_h + 2);
                ui::draw_text(renderer, font, stats_buf, stat_col, sr.x, sr.y);
            } else if (item.type == ItemType::POTION || item.type == ItemType::FOOD) {
                if (item.heal_amount > 0) {
                    snprintf(stats_buf, sizeof(stats_buf), "Heals: %d HP", item.heal_amount);
                    auto sr = det.row(line_h + 2);
                    ui::draw_text(renderer, font, stats_buf, stat_col, sr.x, sr.y);
                }
            }

            // Comparison with equipped
            if (item.slot != EquipSlot::NONE && !inv.is_equipped(item_e)) {
                Entity cur_eq = inv.get_equipped(item.slot);
                if (item.slot == EquipSlot::RING_1 && cur_eq == NULL_ENTITY)
                    cur_eq = inv.get_equipped(EquipSlot::RING_2);

                if (cur_eq != NULL_ENTITY && world.has<Item>(cur_eq)) {
                    auto& cur = world.get<Item>(cur_eq);
                    auto draw_cmp = [&](const char* label, int sel_val, int cur_val) {
                        if (sel_val == cur_val || !det.fits_row()) return;
                        int diff = sel_val - cur_val;
                        char cbuf[64];
                        snprintf(cbuf, sizeof(cbuf), "%s: %+d", label, diff);
                        SDL_Color cc = (diff > 0) ? SDL_Color{100, 220, 100, 255}
                                                   : SDL_Color{220, 100, 100, 255};
                        auto cr = det.row(line_h + 1);
                        ui::draw_text(renderer, font, cbuf, cc, cr.x, cr.y);
                    };
                    if (item.type == ItemType::WEAPON) {
                        draw_cmp("Dmg", item.damage_bonus, cur.damage_bonus);
                        draw_cmp("Atk", item.attack_bonus, cur.attack_bonus);
                    } else {
                        draw_cmp("Armor", item.armor_bonus, cur.armor_bonus);
                        draw_cmp("Dodge", item.dodge_bonus, cur.dodge_bonus);
                    }
                    draw_cmp("STR", item.str_bonus, cur.str_bonus);
                    draw_cmp("DEX", item.dex_bonus, cur.dex_bonus);
                    draw_cmp("CON", item.con_bonus, cur.con_bonus);
                } else if (cur_eq == NULL_ENTITY) {
                    auto er = det.row(line_h + 1);
                    ui::draw_text(renderer, font, "(empty slot)", hint_col, er.x, er.y);
                }
            }

            // Attribute bonuses
            if (item.str_bonus != 0 || item.dex_bonus != 0 || item.con_bonus != 0) {
                std::string attr_str;
                if (item.str_bonus != 0) { char ab[16]; snprintf(ab, sizeof(ab), "STR %+d  ", item.str_bonus); attr_str += ab; }
                if (item.dex_bonus != 0) { char ab[16]; snprintf(ab, sizeof(ab), "DEX %+d  ", item.dex_bonus); attr_str += ab; }
                if (item.con_bonus != 0) { char ab[16]; snprintf(ab, sizeof(ab), "CON %+d", item.con_bonus); attr_str += ab; }
                if (det.fits_row()) {
                    auto ar = det.row(line_h + 2);
                    ui::draw_text(renderer, font, attr_str.c_str(), stat_col, ar.x, ar.y);
                }
            }

            // Affix effects
            SDL_Color affix_col = {140, 200, 160, 255};
            for (auto& a : item.affixes) {
                if (!det.fits_row()) break;
                char abuf[128];
                switch (a.effect) {
                    case AffixEffect::ONHIT_POISON:    snprintf(abuf, sizeof(abuf), "%d%% chance to poison on hit", a.magnitude); break;
                    case AffixEffect::ONHIT_BURN:      snprintf(abuf, sizeof(abuf), "%d%% chance to burn on hit", a.magnitude); break;
                    case AffixEffect::ONHIT_FREEZE:    snprintf(abuf, sizeof(abuf), "%d%% chance to freeze on hit", a.magnitude); break;
                    case AffixEffect::ONHIT_BLEED:     snprintf(abuf, sizeof(abuf), "%d%% chance to bleed on hit", a.magnitude); break;
                    case AffixEffect::ONHIT_LIFESTEAL: snprintf(abuf, sizeof(abuf), "+%d HP on hit (lifesteal)", a.magnitude); break;
                    case AffixEffect::ONKILL_HEAL:     snprintf(abuf, sizeof(abuf), "+%d HP on kill", a.magnitude); break;
                    case AffixEffect::ONKILL_MANA:     snprintf(abuf, sizeof(abuf), "+%d MP on kill", a.magnitude); break;
                    case AffixEffect::RESIST_POISON:   snprintf(abuf, sizeof(abuf), "-%d poison damage per tick", a.magnitude); break;
                    case AffixEffect::RESIST_FIRE:     snprintf(abuf, sizeof(abuf), "-%d fire damage per tick", a.magnitude); break;
                    case AffixEffect::BONUS_HP:        snprintf(abuf, sizeof(abuf), "+%d max HP", a.magnitude); break;
                    case AffixEffect::BONUS_MP:        snprintf(abuf, sizeof(abuf), "+%d max MP", a.magnitude); break;
                    case AffixEffect::BONUS_SPEED:     snprintf(abuf, sizeof(abuf), "+%d speed", a.magnitude); break;
                    case AffixEffect::BONUS_FAVOR:     snprintf(abuf, sizeof(abuf), "+%d favor per kill", a.magnitude); break;
                    default: abuf[0] = '\0'; break;
                }
                if (abuf[0] != '\0') {
                    auto ar = det.row(line_h + 2);
                    ui::draw_text_clipped(renderer, font, abuf, affix_col, ar.x, ar.y, ar.w);
                }
            }

            // Unique effect
            if (item.unique_effect != UniqueEffect::NONE && det.fits_row()) {
                const char* ue_desc = unique_effect_description(item.unique_effect);
                if (ue_desc[0] != '\0') {
                    auto ur = det.row(line_h + 2);
                    ui::draw_text_clipped(renderer, font, ue_desc, {255, 200, 100, 255}, ur.x, ur.y, ur.w);
                }
            }

            // Rarity tag
            if (item.rarity != Rarity::COMMON && det.fits_row()) {
                auto rr = det.row(line_h + 2);
                ui::draw_text(renderer, font, rarity_name(item.rarity),
                              rarity_color(item.rarity), rr.x, rr.y);
            }

            // Gold value
            if (item.gold_value > 0 && det.fits_row()) {
                char val_buf[64];
                snprintf(val_buf, sizeof(val_buf), "Value: %d gold", item.gold_value);
                auto vr = det.row(line_h + 2);
                ui::draw_text(renderer, font, val_buf, value_col, vr.x, vr.y);
            }
        }
    }

    // Close hint
    auto hint_rect = ui::Rect{outer.x, outer.y2() - line_h - 8, outer.w, line_h};
    { auto* ig = InputGlyphs::get();
      char hbuf[256];
      if (ig && ig->using_gamepad())
          snprintf(hbuf, sizeof(hbuf), "%s close   (Y) sort   %s equip",
                   ig->cancel().c_str(), ig->confirm().c_str());
      else
          snprintf(hbuf, sizeof(hbuf), "[i/Esc] close   [Tab] sort   Right-click to equip");
      ui::draw_text_in(renderer, font, hbuf, hint_col, hint_rect, ui::Align::LEFT); }
}
