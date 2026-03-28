#include "ui/inventory_screen.h"
#include "ui/ui_draw.h"
#include "components/inventory.h"
#include "components/item.h"
#include "components/renderable.h"
#include "components/stats.h"

static const char* slot_name(EquipSlot slot) {
    switch (slot) {
        case EquipSlot::MAIN_HAND: return "Main Hand";
        case EquipSlot::OFF_HAND:  return "Off Hand";
        case EquipSlot::HEAD:      return "Head";
        case EquipSlot::CHEST:     return "Chest";
        case EquipSlot::HANDS:     return "Hands";
        case EquipSlot::FEET:      return "Feet";
        case EquipSlot::AMULET:    return "Amulet";
        case EquipSlot::RING_1:    return "Ring 1";
        case EquipSlot::RING_2:    return "Ring 2";
        default:                   return "";
    }
}

InvAction InventoryScreen::handle_input(SDL_Event& event) {
    if (!open_) return InvAction::NONE;
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
            // a-z selects item directly
            if (event.key.keysym.sym >= SDLK_a && event.key.keysym.sym <= SDLK_z) {
                selected_ = event.key.keysym.sym - SDLK_a;
                return InvAction::NONE;
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

    // Panel background
    int panel_w = 500;
    int panel_h = screen_h - 40;
    int panel_x = (screen_w - panel_w) / 2;
    int panel_y = 20;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    ui::draw_panel(renderer, panel_x, panel_y, panel_w, panel_h);

    int y = panel_y + 8;
    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col = {200, 180, 160, 255};
    SDL_Color item_col = {180, 175, 170, 255};
    SDL_Color equip_col = {140, 180, 140, 255};
    SDL_Color sel_col = {255, 220, 140, 255};
    SDL_Color hint_col = {120, 110, 100, 255};

    // Title
    ui::draw_text(renderer, font, "INVENTORY", title_col, panel_x + 10, y);
    y += line_h + 4;

    // Equipment summary
    ui::draw_text(renderer, font, "--- Equipped ---", hint_col, panel_x + 10, y);
    y += line_h;

    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        Entity eq = inv.equipped[s];
        char buf[128];
        if (eq != NULL_ENTITY && world.has<Item>(eq)) {
            auto& item = world.get<Item>(eq);
            snprintf(buf, sizeof(buf), "  %s: %s", slot_name(static_cast<EquipSlot>(s)),
                     item.display_name().c_str());
            ui::draw_text(renderer, font, buf, equip_col, panel_x + 10, y);
        } else {
            snprintf(buf, sizeof(buf), "  %s: (empty)", slot_name(static_cast<EquipSlot>(s)));
            ui::draw_text(renderer, font, buf, hint_col, panel_x + 10, y);
        }
        y += line_h;
    }

    y += 4;
    ui::draw_text(renderer, font, "--- Carried ---", hint_col, panel_x + 10, y);
    y += line_h;

    if (inv.items.empty()) {
        ui::draw_text(renderer, font, "  (nothing)", hint_col, panel_x + 10, y);
        y += line_h;
    }

    // Clamp selection
    int sel = selected_;
    if (sel >= static_cast<int>(inv.items.size()) && !inv.items.empty()) {
        sel = static_cast<int>(inv.items.size()) - 1;
    }

    for (int i = 0; i < static_cast<int>(inv.items.size()); i++) {
        Entity item_e = inv.items[i];
        if (!world.has<Item>(item_e)) continue;
        auto& item = world.get<Item>(item_e);

        char letter = 'a' + static_cast<char>(i);
        bool is_equipped = inv.is_equipped(item_e);
        bool is_selected = (i == sel);

        char buf[128];
        snprintf(buf, sizeof(buf), "%c) %s%s",
                 letter, item.display_name().c_str(),
                 is_equipped ? " [E]" : "");

        SDL_Color col = is_selected ? sel_col : (is_equipped ? equip_col : item_col);

        if (is_selected) {
            // Highlight bar
            SDL_Rect hl = {panel_x + 6, y, panel_w - 12, line_h};
            SDL_SetRenderDrawColor(renderer, 40, 35, 50, 255);
            SDL_RenderFillRect(renderer, &hl);
        }

        // Draw item sprite
        if (world.has<Renderable>(item_e)) {
            auto& rend = world.get<Renderable>(item_e);
            sprites.draw_sprite(renderer, rend.sprite_sheet, rend.sprite_x, rend.sprite_y,
                                panel_x + 10, y, 1);
        }

        ui::draw_text(renderer, font, buf, col, panel_x + 44, y + 8);
        y += std::max(line_h, 32);

        if (y > panel_y + panel_h - line_h * 3) break;
    }

    // Hints at bottom
    y = panel_y + panel_h - line_h * 2 - 4;
    SDL_Rect sep = {panel_x + 8, y - 2, panel_w - 16, 1};
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderFillRect(renderer, &sep);
    ui::draw_text(renderer, font, "[e]quip  [u]se  [d]rop  [i/esc]close", hint_col, panel_x + 10, y);

    // Item description
    if (sel >= 0 && sel < static_cast<int>(inv.items.size())) {
        Entity item_e = inv.items[sel];
        if (world.has<Item>(item_e)) {
            auto& item = world.get<Item>(item_e);
            if (!item.description.empty()) {
                ui::draw_text(renderer, font, item.description.c_str(), hint_col, panel_x + 10, y + line_h);
            }
        }
    }
}
