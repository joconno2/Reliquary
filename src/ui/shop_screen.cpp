#include "ui/shop_screen.h"
#include "ui/ui_draw.h"
#include "components/inventory.h"
#include "components/renderable.h"
#include "core/spritesheet.h"
#include <cstdio>
#include <algorithm>

// Reuse item tables from populate — but we define a local subset for shop stock
struct ShopItemDef {
    const char* name;
    const char* description;
    ItemType type;
    EquipSlot slot;
    int sprite_x, sprite_y;
    int damage_bonus, armor_bonus, attack_bonus, dodge_bonus;
    int heal_amount;
    int gold_value;
};

static const ShopItemDef SHOP_WEAPONS[] = {
    {"dagger",        "A short, sharp blade.",          ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0,  2, 0, 1, 0, 0,  15},
    {"short sword",   "A reliable sidearm.",            ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 0,  3, 0, 0, 0, 0,  30},
    {"long sword",    "A well-balanced blade.",         ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0,  5, 0, 0, 0, 0,  60},
    {"mace",          "Blunt and merciless.",           ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5,  4, 0, 0, 0, 0,  40},
    {"spear",         "Long reach. Keeps them at bay.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 6,  4, 0, 1, 0, 0,  35},
    {"battle axe",    "Heavy. Splits bone.",            ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 3,  6, 0,-1, 0, 0,  55},
};

static const ShopItemDef SHOP_ARMOR[] = {
    {"leather armor", "Supple hide. Quiet.",            ItemType::ARMOR_CHEST, EquipSlot::CHEST,    1, 12, 0, 2, 0, 0, 0, 40},
    {"chain mail",    "Rings of iron. Heavy.",          ItemType::ARMOR_CHEST, EquipSlot::CHEST,    3, 12, 0, 4, 0,-1, 0, 80},
    {"leather helm",  "Better than nothing.",           ItemType::ARMOR_HEAD,  EquipSlot::HEAD,     1, 15, 0, 1, 0, 0, 0, 20},
    {"iron helm",     "Cold iron on your skull.",       ItemType::ARMOR_HEAD,  EquipSlot::HEAD,     4, 15, 0, 2, 0, 0, 0, 45},
    {"buckler",       "A small, round shield.",         ItemType::SHIELD,      EquipSlot::OFF_HAND, 0, 11, 0, 2, 0, 1, 0, 30},
    {"leather boots", "Worn but sturdy.",               ItemType::ARMOR_FEET,  EquipSlot::FEET,     1, 14, 0, 1, 0, 0, 0, 20},
};

static const ShopItemDef SHOP_CONSUMABLES[] = {
    {"healing potion", "Mends flesh.",                  ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 15, 25},
    {"strong healing", "Flesh knits together.",         ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 30, 50},
    {"bread",          "Stale. Nourishing enough.",     ItemType::FOOD,   EquipSlot::NONE, 1, 25, 0, 0, 0, 0,  5,  5},
};

static constexpr int SHOP_WEAPON_COUNT = sizeof(SHOP_WEAPONS) / sizeof(SHOP_WEAPONS[0]);
static constexpr int SHOP_ARMOR_COUNT = sizeof(SHOP_ARMOR) / sizeof(SHOP_ARMOR[0]);
static constexpr int SHOP_CONS_COUNT = sizeof(SHOP_CONSUMABLES) / sizeof(SHOP_CONSUMABLES[0]);

static ShopItem make_shop_item(const ShopItemDef& def) {
    ShopItem si;
    si.item.name = def.name;
    si.item.description = def.description;
    si.item.type = def.type;
    si.item.slot = def.slot;
    si.item.damage_bonus = def.damage_bonus;
    si.item.armor_bonus = def.armor_bonus;
    si.item.attack_bonus = def.attack_bonus;
    si.item.dodge_bonus = def.dodge_bonus;
    si.item.heal_amount = def.heal_amount;
    si.item.gold_value = def.gold_value;
    si.item.identified = true;
    si.sprite_x = def.sprite_x;
    si.sprite_y = def.sprite_y;
    return si;
}

void ShopScreen::generate_stock(RNG& rng) {
    stock_.clear();

    // Pick 3-4 weapons
    int n_weapons = rng.range(3, 4);
    for (int i = 0; i < n_weapons; i++) {
        int idx = rng.range(0, SHOP_WEAPON_COUNT - 1);
        stock_.push_back(make_shop_item(SHOP_WEAPONS[idx]));
    }
    // Pick 3-4 armor pieces
    int n_armor = rng.range(3, 4);
    for (int i = 0; i < n_armor; i++) {
        int idx = rng.range(0, SHOP_ARMOR_COUNT - 1);
        stock_.push_back(make_shop_item(SHOP_ARMOR[idx]));
    }
    // Pick 2-3 consumables
    int n_cons = rng.range(2, 3);
    for (int i = 0; i < n_cons; i++) {
        int idx = rng.range(0, SHOP_CONS_COUNT - 1);
        stock_.push_back(make_shop_item(SHOP_CONSUMABLES[idx]));
    }
}

void ShopScreen::open(Entity player, [[maybe_unused]] World& world, RNG& rng, int* gold) {
    open_ = true;
    selected_ = 0;
    player_ = player;
    gold_ = gold;
    buy_tab_ = true;
    generate_stock(rng);
}

ShopAction ShopScreen::handle_input(SDL_Event& event) {
    if (!open_) return ShopAction::NONE;

    if (event.type != SDL_KEYDOWN) return ShopAction::NONE;

    switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_s:
            return ShopAction::CLOSE;
        case SDLK_TAB:
            buy_tab_ = !buy_tab_;
            selected_ = 0;
            return ShopAction::NONE;
        case SDLK_UP:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return ShopAction::NONE;
        case SDLK_DOWN:
        case SDLK_j:
            selected_++;
            return ShopAction::NONE;
        case SDLK_RETURN:
        case SDLK_SPACE:
            return buy_tab_ ? ShopAction::BUY : ShopAction::SELL;
        default:
            return ShopAction::NONE;
    }
}

bool ShopScreen::execute(World& world, int* gold) {
    if (!world.has<Inventory>(player_)) return false;
    auto& inv = world.get<Inventory>(player_);

    if (buy_tab_) {
        // Buy
        if (selected_ < 0 || selected_ >= static_cast<int>(stock_.size())) return false;
        auto& si = stock_[selected_];
        if (*gold < si.item.gold_value) return false;
        if (inv.is_full()) return false;

        *gold -= si.item.gold_value;

        // Create item entity
        Entity e = world.create();
        Item item = si.item; // copy
        world.add<Item>(e, std::move(item));
        inv.add(e);

        // Remove from stock
        stock_.erase(stock_.begin() + selected_);
        if (selected_ >= static_cast<int>(stock_.size()) && selected_ > 0) selected_--;
        return true;
    } else {
        // Sell
        if (selected_ < 0 || selected_ >= static_cast<int>(inv.items.size())) return false;
        Entity item_e = inv.items[selected_];
        if (!world.has<Item>(item_e)) return false;
        auto& item = world.get<Item>(item_e);

        int sell_price = item.gold_value / 2;
        if (sell_price < 1) sell_price = 1;
        *gold += sell_price;

        if (inv.is_equipped(item_e)) {
            inv.unequip(item.slot);
        }
        inv.remove(item_e);
        world.destroy(item_e);

        if (selected_ >= static_cast<int>(inv.items.size()) && selected_ > 0) selected_--;
        return true;
    }
}

void ShopScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                         const SpriteManager& sprites, World& world,
                         int screen_w, int screen_h) const {
    if (!open_ || !font) return;

    int line_h = TTF_FontLineSkip(font);

    SDL_Color title_col = {220, 200, 140, 255};
    SDL_Color tab_active = {255, 220, 100, 255};
    SDL_Color tab_inactive = {100, 90, 80, 255};
    SDL_Color item_col = {180, 175, 170, 255};
    SDL_Color sel_col = {255, 240, 180, 255};
    SDL_Color price_col = {220, 200, 80, 255};
    SDL_Color cant_col = {120, 80, 80, 255};
    SDL_Color hint_col = {120, 110, 100, 255};
    SDL_Color stat_col = {140, 160, 180, 255};

    // Darken background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, screen_w, screen_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &overlay);

    // Panel
    int panel_w = std::min(600, screen_w - 40);
    int panel_h = screen_h - 60;
    int px = (screen_w - panel_w) / 2;
    int py = 30;
    ui::draw_panel(renderer, px, py, panel_w, panel_h);

    int y = py + 10;
    ui::draw_text_centered(renderer, font, "Shop", title_col, screen_w / 2, y);
    y += line_h + 4;

    // Gold display
    char gold_buf[64];
    snprintf(gold_buf, sizeof(gold_buf), "Gold: %d", gold_ ? *gold_ : 0);
    ui::draw_text(renderer, font, gold_buf, price_col, px + panel_w - 180, py + 10);

    // Tabs
    ui::draw_text(renderer, font, "[Buy]", buy_tab_ ? tab_active : tab_inactive, px + 20, y);
    ui::draw_text(renderer, font, "[Sell]", !buy_tab_ ? tab_active : tab_inactive, px + 120, y);
    y += line_h + 8;

    // Separator
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderDrawLine(renderer, px + 10, y, px + panel_w - 10, y);
    y += 6;

    if (buy_tab_) {
        // Buy tab — show shop stock
        int count = static_cast<int>(stock_.size());
        int sel = std::min(selected_, count - 1);

        if (count == 0) {
            ui::draw_text(renderer, font, "(sold out)", hint_col, px + 20, y);
        }

        for (int i = 0; i < count; i++) {
            int row_h = std::max(line_h + 8, 36);
            if (y > py + panel_h - line_h * 6) break;
            auto& si = stock_[i];
            bool is_sel = (i == sel);
            bool can_afford = gold_ && *gold_ >= si.item.gold_value;

            if (is_sel) {
                SDL_Rect sel_rect = {px + 8, y - 2, panel_w - 16, row_h};
                SDL_SetRenderDrawColor(renderer, 35, 30, 48, 255);
                SDL_RenderFillRect(renderer, &sel_rect);
            }

            // Item sprite (32x32)
            sprites.draw_sprite(renderer, SHEET_ITEMS, si.sprite_x, si.sprite_y,
                               px + 20, y, 1);

            // Item name
            char buf[128];
            snprintf(buf, sizeof(buf), "%s", si.item.name.c_str());
            ui::draw_text(renderer, font, buf, is_sel ? sel_col : item_col, px + 56, y + 2);

            // Inline stats
            char stat_buf[64];
            if (si.item.type == ItemType::WEAPON) {
                snprintf(stat_buf, sizeof(stat_buf), "+%d dmg", si.item.damage_bonus);
            } else if (si.item.type == ItemType::POTION || si.item.type == ItemType::FOOD) {
                snprintf(stat_buf, sizeof(stat_buf), "heal %d", si.item.heal_amount);
            } else {
                snprintf(stat_buf, sizeof(stat_buf), "+%d arm", si.item.armor_bonus);
            }
            ui::draw_text(renderer, font, stat_buf, stat_col, px + 56, y + 2 + line_h);

            // Price right-aligned
            char price[32];
            snprintf(price, sizeof(price), "%dg", si.item.gold_value);
            ui::draw_text(renderer, font, price, can_afford ? price_col : cant_col,
                         px + panel_w - 80, y + 8);

            y += row_h;
        }

        // Show selected item stats
        if (sel >= 0 && sel < count) {
            y = py + panel_h - line_h * 4 - 10;
            SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
            SDL_RenderDrawLine(renderer, px + 10, y - 4, px + panel_w - 10, y - 4);

            auto& si = stock_[sel];
            ui::draw_text(renderer, font, si.item.description.c_str(), hint_col, px + 20, y);
            y += line_h + 2;

            char stats[128];
            if (si.item.type == ItemType::WEAPON) {
                snprintf(stats, sizeof(stats), "Damage: +%d  Attack: +%d",
                         si.item.damage_bonus, si.item.attack_bonus);
            } else if (si.item.type == ItemType::POTION || si.item.type == ItemType::FOOD) {
                snprintf(stats, sizeof(stats), "Heals: %d HP", si.item.heal_amount);
            } else {
                snprintf(stats, sizeof(stats), "Armor: +%d  Dodge: +%d",
                         si.item.armor_bonus, si.item.dodge_bonus);
            }
            ui::draw_text(renderer, font, stats, stat_col, px + 20, y);
        }
    } else {
        // Sell tab — show player inventory
        if (!world.has<Inventory>(player_)) return;
        auto& inv = world.get<Inventory>(player_);
        int count = static_cast<int>(inv.items.size());
        int sel = std::min(selected_, count - 1);

        if (count == 0) {
            ui::draw_text(renderer, font, "(nothing to sell)", hint_col, px + 20, y);
        }

        for (int i = 0; i < count; i++) {
            if (y > py + panel_h - line_h * 6) break;
            Entity item_e = inv.items[i];
            if (!world.has<Item>(item_e)) continue;
            auto& item = world.get<Item>(item_e);
            bool is_sel = (i == sel);
            int sell_price = std::max(1, item.gold_value / 2);

            if (is_sel) {
                SDL_Rect sel_rect = {px + 8, y - 2, panel_w - 16, line_h + 4};
                SDL_SetRenderDrawColor(renderer, 35, 30, 48, 255);
                SDL_RenderFillRect(renderer, &sel_rect);
            }

            char buf[128];
            snprintf(buf, sizeof(buf), "%s%s", item.display_name().c_str(),
                     inv.is_equipped(item_e) ? " [E]" : "");
            ui::draw_text(renderer, font, buf, is_sel ? sel_col : item_col, px + 20, y);

            char price[32];
            snprintf(price, sizeof(price), "%dg", sell_price);
            ui::draw_text(renderer, font, price, price_col, px + panel_w - 80, y);

            y += line_h + 4;
        }

        // Show selected item stats
        if (sel >= 0 && sel < count) {
            Entity item_e = inv.items[sel];
            if (world.has<Item>(item_e)) {
                y = py + panel_h - line_h * 4 - 10;
                SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
                SDL_RenderDrawLine(renderer, px + 10, y - 4, px + panel_w - 10, y - 4);

                auto& item = world.get<Item>(item_e);
                if (!item.description.empty()) {
                    ui::draw_text(renderer, font, item.description.c_str(), hint_col, px + 20, y);
                    y += line_h + 2;
                }

                char stats[128];
                if (item.type == ItemType::WEAPON) {
                    snprintf(stats, sizeof(stats), "Damage: +%d  Attack: +%d",
                             item.damage_bonus, item.attack_bonus);
                } else if (item.type == ItemType::POTION || item.type == ItemType::FOOD) {
                    snprintf(stats, sizeof(stats), "Heals: %d HP", item.heal_amount);
                } else {
                    snprintf(stats, sizeof(stats), "Armor: +%d  Dodge: +%d",
                             item.armor_bonus, item.dodge_bonus);
                }
                ui::draw_text(renderer, font, stats, stat_col, px + 20, y);
            }
        }
    }

    // Hints at bottom
    int hint_y = py + panel_h - line_h - 8;
    ui::draw_text(renderer, font, "[Tab] switch  [Enter] buy/sell  [Esc/s] close",
                  hint_col, px + 20, hint_y);
}
