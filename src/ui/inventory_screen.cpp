#include "ui/inventory_screen.h"
#include "ui/ui_draw.h"
#include "components/inventory.h"
#include "components/item.h"
#include "components/renderable.h"
#include "components/stats.h"
#include <cstdio>
#include <algorithm>
#include <vector>

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

    // Mouse click
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mx = event.button.x;
        int my = event.button.y;

        // Click on carried item — select it
        int item_idx = find_item_at(mx, my);
        if (item_idx >= 0) {
            selected_ = item_idx;
            return InvAction::NONE;
        }

        // Click on equipment slot — if item equipped, select it
        int slot_idx = find_slot_at(mx, my);
        if (slot_idx >= 0) {
            // Find this equipped item in the carried list
            // For now just return EQUIP to toggle
            return InvAction::EQUIP;
        }

        // Click action buttons
        int btn = find_button_at(mx, my);
        if (btn == 0) return InvAction::EQUIP;
        if (btn == 1) return InvAction::USE;
        if (btn == 2) return InvAction::DROP;

        return InvAction::NONE;
    }

    // Double-click to equip/use
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
        int mx = event.button.x;
        int my = event.button.y;
        int item_idx = find_item_at(mx, my);
        if (item_idx >= 0) {
            selected_ = item_idx;
            return InvAction::EQUIP;
        }
        return InvAction::NONE;
    }

    if (event.type != SDL_KEYDOWN) return InvAction::NONE;

    switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_i:
            return InvAction::CLOSE;
        case SDLK_UP:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return InvAction::NONE;
        case SDLK_DOWN:
        case SDLK_j:
            selected_++;
            return InvAction::NONE;
        case SDLK_e:
        case SDLK_RETURN:
            return InvAction::EQUIP;
        case SDLK_u:
            return InvAction::USE;
        case SDLK_d:
            return InvAction::DROP;
        default:
            if (event.key.keysym.sym >= SDLK_a && event.key.keysym.sym <= SDLK_z) {
                selected_ = event.key.keysym.sym - SDLK_a;
            }
            return InvAction::NONE;
    }
}

Entity InventoryScreen::get_selected_item(World& world) const {
    if (!world.has<Inventory>(player_)) return NULL_ENTITY;
    auto& inv = world.get<Inventory>(player_);
    if (selected_ < 0 || selected_ >= static_cast<int>(inv.items.size()))
        return NULL_ENTITY;
    return inv.items[selected_];
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

    // Darken background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, screen_w, screen_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_RenderFillRect(renderer, &overlay);

    // Layout: left panel = paper doll, right panel = carried items — centered
    int total_w = std::min(screen_w * 3 / 4, 1200);
    int total_h = std::min(screen_h * 3 / 4, screen_h - 100);
    int base_x = (screen_w - total_w) / 2;
    int base_y = (screen_h - total_h) / 2;

    int doll_w = total_w * 2 / 5;
    int list_w = total_w - doll_w - 12;
    int doll_x = base_x;
    int list_x = base_x + doll_w + 12;

    // Paper doll panel
    ui::draw_panel(renderer, doll_x, base_y, doll_w, total_h);

    // Character sprite (big)
    if (world.has<Renderable>(player_)) {
        auto& rend = world.get<Renderable>(player_);
        sprites.draw_sprite_sized(renderer, rend.sprite_sheet, rend.sprite_x, rend.sprite_y,
                                   doll_x + doll_w / 2 - 48, base_y + 12, 96);
    }

    // Equipment slots — 3-column grid layout
    slot_rects_.clear();
    int slot_size = 40;
    int slot_gap = 10;
    int dcx = doll_x + doll_w / 2;
    int slots_y = base_y + 120;

    int col_l = dcx - 80;
    int col_c = dcx - 20;
    int col_r = dcx + 40;

    int row_step = slot_size + slot_gap + line_h + 4;
    int r0 = slots_y;
    int r1 = slots_y + row_step;
    int r2 = r1 + row_step;
    int r3 = r2 + row_step;

    struct SL { int x, y; };
    SL positions[] = {
        {col_l, r1},  // 0: main hand
        {col_r, r1},  // 1: off hand
        {col_c, r0},  // 2: head
        {col_c, r1},  // 3: chest
        {col_l, r2},  // 4: hands
        {col_c, r2},  // 5: feet
        {col_r, r0},  // 6: amulet
        {col_l, r3},  // 7: ring 1
        {col_r, r3},  // 8: ring 2
        {col_c, r3},  // 9: pet
    };

    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        int sx = positions[s].x;
        int sy = positions[s].y;
        SDL_Rect slot_rect = {sx, sy, slot_size, slot_size};
        slot_rects_.push_back({slot_rect, s});

        // Slot background with border
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
            // Label inside slot (centered, small text) when empty
            ui::draw_text_centered(renderer, font, slot_label(s), empty_col,
                                    sx + slot_size / 2, sy + (slot_size - line_h) / 2);
        }
    }

    // Carried items panel
    ui::draw_panel(renderer, list_x, base_y, list_w, total_h);

    int y = base_y + 10;
    ui::draw_text(renderer, font, "Inventory", title_col, list_x + 10, y);
    y += line_h + 6;

    item_rects_.clear();

    int sel = selected_;
    int count = static_cast<int>(inv.items.size());
    if (sel >= count && count > 0) sel = count - 1;

    if (inv.items.empty()) {
        ui::draw_text(renderer, font, "(empty)", hint_col, list_x + 10, y);
    }

    for (int i = 0; i < count; i++) {
        Entity item_e = inv.items[i];
        if (!world.has<Item>(item_e)) continue;
        auto& item = world.get<Item>(item_e);

        bool is_equipped = inv.is_equipped(item_e);
        bool is_sel = (i == sel);

        int row_h = std::max(line_h + 8, 36);
        SDL_Rect row_rect = {list_x + 6, y, list_w - 12, row_h};
        item_rects_.push_back({row_rect, i});

        if (is_sel) {
            SDL_SetRenderDrawColor(renderer, 35, 30, 48, 255);
            SDL_RenderFillRect(renderer, &row_rect);
        }

        // Item sprite
        if (world.has<Renderable>(item_e)) {
            auto& rend = world.get<Renderable>(item_e);
            sprites.draw_sprite(renderer, rend.sprite_sheet, rend.sprite_x, rend.sprite_y,
                                list_x + 10, y + 2, 1);
        }

        char letter = 'a' + static_cast<char>(i);
        char buf[128];
        snprintf(buf, sizeof(buf), "%c) %s%s", letter,
                 item.display_name().c_str(),
                 is_equipped ? " [E]" : "");

        SDL_Color col = is_sel ? sel_col : (is_equipped ? equip_col : item_col);
        ui::draw_text(renderer, font, buf, col, list_x + 44, y + 8);

        y += row_h;
        if (y > base_y + total_h - line_h * 4) break;
    }

    // Action buttons at bottom of item list
    int btn_y = base_y + total_h - line_h - 20;
    int btn_w = 80;
    int btn_h = line_h + 8;
    int btn_gap = 10;
    int btn_start = list_x + 10;

    equip_btn_ = {btn_start, btn_y, btn_w, btn_h};
    use_btn_ = {btn_start + btn_w + btn_gap, btn_y, btn_w, btn_h};
    drop_btn_ = {btn_start + (btn_w + btn_gap) * 2, btn_y, btn_w, btn_h};

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

    // Item description and stats
    if (sel >= 0 && sel < count) {
        Entity item_e = inv.items[sel];
        if (world.has<Item>(item_e)) {
            auto& item = world.get<Item>(item_e);
            SDL_Color stat_col = {140, 160, 180, 255};
            SDL_Color value_col = {200, 180, 80, 255};
            int info_y = btn_y - line_h * 3 - 12;

            if (!item.description.empty()) {
                ui::draw_text_wrapped(renderer, font, item.description.c_str(), hint_col,
                                       list_x + 10, info_y, list_w - 20);
                info_y += line_h + 4;
            }

            // Item stats
            char stats_buf[128];
            if (item.type == ItemType::WEAPON) {
                if (item.range > 0) {
                    // Ranged weapon — effective damage uses DEX
                    int eff_dmg = item.damage_bonus;
                    if (world.has<Stats>(player_))
                        eff_dmg += world.get<Stats>(player_).attr(Attr::DEX) / 3;
                    snprintf(stats_buf, sizeof(stats_buf), "Dmg: +%d  Atk: +%d  Range: %d",
                             item.damage_bonus, item.attack_bonus, item.range);
                    ui::draw_text(renderer, font, stats_buf, stat_col, list_x + 10, info_y);
                    info_y += line_h + 2;
                    snprintf(stats_buf, sizeof(stats_buf), "Effective: %d dmg (DEX)", eff_dmg);
                    ui::draw_text(renderer, font, stats_buf, SDL_Color{120, 200, 180, 255}, list_x + 10, info_y);
                    info_y += line_h + 2;
                } else {
                    // Melee weapon — effective damage uses STR
                    int eff_dmg = item.damage_bonus;
                    if (world.has<Stats>(player_))
                        eff_dmg += world.get<Stats>(player_).melee_damage();
                    snprintf(stats_buf, sizeof(stats_buf), "Dmg: +%d  Atk: +%d",
                             item.damage_bonus, item.attack_bonus);
                    ui::draw_text(renderer, font, stats_buf, stat_col, list_x + 10, info_y);
                    info_y += line_h + 2;
                    snprintf(stats_buf, sizeof(stats_buf), "Effective: %d dmg (STR)", eff_dmg);
                    ui::draw_text(renderer, font, stats_buf, SDL_Color{120, 200, 180, 255}, list_x + 10, info_y);
                    info_y += line_h + 2;
                }
            } else if (item.type == ItemType::ARMOR_HEAD || item.type == ItemType::ARMOR_CHEST ||
                       item.type == ItemType::ARMOR_HANDS || item.type == ItemType::ARMOR_FEET ||
                       item.type == ItemType::SHIELD) {
                snprintf(stats_buf, sizeof(stats_buf), "Armor: +%d  Dodge: +%d",
                         item.armor_bonus, item.dodge_bonus);
                ui::draw_text(renderer, font, stats_buf, stat_col, list_x + 10, info_y);
                info_y += line_h + 2;
            } else if (item.type == ItemType::POTION || item.type == ItemType::FOOD) {
                if (item.heal_amount > 0) {
                    snprintf(stats_buf, sizeof(stats_buf), "Heals: %d HP", item.heal_amount);
                    ui::draw_text(renderer, font, stats_buf, stat_col, list_x + 10, info_y);
                    info_y += line_h + 2;
                }
            }

            // Gold value
            if (item.gold_value > 0) {
                char val_buf[64];
                snprintf(val_buf, sizeof(val_buf), "Value: %d gold", item.gold_value);
                ui::draw_text(renderer, font, val_buf, value_col, list_x + 10, info_y);
            }
        }
    }

    // Close hint
    ui::draw_text(renderer, font, "[i/Esc] close   Right-click to equip",
                  hint_col, doll_x + 10, base_y + total_h - line_h - 8);
}
