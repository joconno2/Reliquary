#include "ui/shop_screen.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
#include "components/inventory.h"
#include "components/renderable.h"
#include "core/spritesheet.h"
#include "components/spellbook.h"
#include "components/tenet.h"
#include "components/god.h"
#include "components/player.h"
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

// tags field packed into heal_amount for weapons (repurposed since weapons don't heal)
static const ShopItemDef SHOP_WEAPONS[] = {
    // Daggers (TAG_DAGGER)
    {"dagger",        "+2 dmg, +2 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0,  2, 0, 2, 0, 0,  15},
    {"stiletto",      "+3 dmg, +3 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0,  3, 0, 3, 0, 0,  35},
    // Swords (TAG_SWORD)
    {"short sword",   "+3 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 0,  3, 0, 1, 0, 0,  30},
    {"long sword",    "+5 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0,  5, 0, 1, 0, 0,  60},
    {"falchion",      "+6 dmg.",                         ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0,  6, 0, 0, 0, 0,  75},
    // Axes (TAG_AXE)
    {"hand axe",      "+3 dmg.",                         ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 3,  3, 0, 0, 0, 0,  25},
    {"battle axe",    "+6 dmg, -1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 3,  6, 0,-1, 0, 0,  55},
    // Blunt (TAG_BLUNT)
    {"mace",          "+4 dmg.",                         ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5,  4, 0, 0, 0, 0,  40},
    {"warhammer",     "+7 dmg, -1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5,  7, 0,-1, 0, 0,  70},
    // Spears
    {"spear",         "+4 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 6,  4, 0, 1, 0, 0,  35},
    // Bows (TAG_BOW)
    {"short bow",     "+2 dmg, +2 atk, range 6.",       ItemType::WEAPON, EquipSlot::MAIN_HAND, 2, 6,  2, 0, 2, 0, 0,  30},
    {"hunting bow",   "+4 dmg, +3 atk, range 6.",       ItemType::WEAPON, EquipSlot::MAIN_HAND, 2, 6,  4, 0, 3, 0, 0,  55},
};

static const ShopItemDef SHOP_ARMOR[] = {
    // Chest
    {"leather vest",  "AC +2.",                         ItemType::ARMOR_CHEST, EquipSlot::CHEST,    1, 12, 0, 2, 0, 0, 0, 35},
    {"chain shirt",   "AC +3.",                         ItemType::ARMOR_CHEST, EquipSlot::CHEST,    3, 12, 0, 3, 0, 0, 0, 60},
    {"chain mail",    "AC +4, -1 dodge.",               ItemType::ARMOR_CHEST, EquipSlot::CHEST,    3, 12, 0, 4, 0,-1, 0, 80},
    {"scale armor",   "AC +5, -2 dodge.",               ItemType::ARMOR_CHEST, EquipSlot::CHEST,    4, 12, 0, 5, 0,-2, 0,100},
    // Head
    {"leather cap",   "AC +1.",                         ItemType::ARMOR_HEAD,  EquipSlot::HEAD,     1, 15, 0, 1, 0, 0, 0, 20},
    {"iron helm",     "AC +3.",                         ItemType::ARMOR_HEAD,  EquipSlot::HEAD,     4, 15, 0, 3, 0, 0, 0, 55},
    // Hands
    {"leather gloves","AC +1.",                         ItemType::ARMOR_HANDS, EquipSlot::HANDS,    1, 13, 0, 1, 0, 0, 0, 20},
    {"iron gauntlets","AC +2.",                         ItemType::ARMOR_HANDS, EquipSlot::HANDS,    4, 13, 0, 2, 0, 0, 0, 45},
    // Feet
    {"leather boots", "AC +1.",                         ItemType::ARMOR_FEET,  EquipSlot::FEET,     1, 14, 0, 1, 0, 0, 0, 20},
    {"iron greaves",  "AC +2.",                         ItemType::ARMOR_FEET,  EquipSlot::FEET,     4, 14, 0, 2, 0, 0, 0, 45},
    // Shields
    {"buckler",       "AC +2, +1 dodge.",               ItemType::SHIELD,      EquipSlot::OFF_HAND, 0, 11, 0, 2, 0, 1, 0, 30},
    {"kite shield",   "AC +3.",                         ItemType::SHIELD,      EquipSlot::OFF_HAND, 1, 11, 0, 3, 0, 0, 0, 55},
};

static const ShopItemDef SHOP_CONSUMABLES[] = {
    {"healing potion", "Restores 15 HP.",               ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 15, 25},
    {"strong healing", "Restores 30 HP.",               ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 30, 50},
    {"bread",          "Restores 5 HP.",                ItemType::FOOD,   EquipSlot::NONE, 1, 25, 0, 0, 0, 0,  5,  5},
    {"dried meat",     "Restores 8 HP.",                ItemType::FOOD,   EquipSlot::NONE, 1, 25, 0, 0, 0, 0,  8, 10},
    {"herbal poultice","Restores 10 HP.",               ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 10, 15},
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
    // Auto-assign weapon tags and properties from name
    if (def.type == ItemType::WEAPON) {
        std::string n = def.name;
        if (n.find("dagger") != std::string::npos || n.find("stiletto") != std::string::npos)
            si.item.tags |= TAG_DAGGER;
        else if (n.find("sword") != std::string::npos || n.find("falchion") != std::string::npos)
            si.item.tags |= TAG_SWORD;
        else if (n.find("axe") != std::string::npos)
            si.item.tags |= TAG_AXE;
        else if (n.find("mace") != std::string::npos || n.find("hammer") != std::string::npos)
            si.item.tags |= TAG_BLUNT;
        else if (n.find("bow") != std::string::npos) {
            si.item.tags |= TAG_BOW;
            si.item.range = 6;
        }
    }
    return si;
}

void ShopScreen::generate_stock(RNG& rng, int difficulty, GodId province_god) {
    stock_.clear();


    // Difficulty scales: 0 = starting area, 4 = mid-game, 8 = endgame
    // Higher difficulty = bias toward better items + bonus stats + higher prices

    // Minimum weapon/armor index increases with difficulty (skip weak items)
    int min_weapon = std::min(difficulty / 2, SHOP_WEAPON_COUNT - 2);
    int min_armor = std::min(difficulty / 2, SHOP_ARMOR_COUNT - 2);

    // Province modifiers: more weapons/less on Iron Coast, more potions in Greenwood, etc.
    int extra_weapons = 0, extra_armor = 0, extra_potions = 0;
    bool stock_antidote = false;
    bool stock_bows = false;     // Greenwood/Finesse provinces
    bool stock_heavy = false;    // Iron Coast / Heartlands
    if (province_god == GodId::OSSREN) { extra_weapons = 2; extra_armor = 1; stock_heavy = true; }
    else if (province_god == GodId::KHAEL) { extra_potions = 2; stock_antidote = true; stock_bows = true; }
    else if (province_god == GodId::MORRETH) { extra_weapons = 1; extra_armor = 1; stock_heavy = true; }
    else if (province_god == GodId::SYTHARA) { stock_antidote = true; extra_potions = 1; }
    else if (province_god == GodId::GATHRUUN) { extra_armor = 1; } // cold = more armor

    // Stat scaling: steeper curve so distant towns feel meaningfully stronger
    // diff 0: +0, diff 1-2: +1, diff 3-4: +2, diff 5-6: +3, diff 7-8: +4
    int stat_bonus = (difficulty + 1) / 2;
    // Price multiplier from distance (further = more expensive but better gear)
    int price_scale = 100 + difficulty * 12; // 100% at diff 0, 196% at diff 8

    // Pick 3-4 weapons (+ province bonus) — assign materials based on town difficulty
    int n_weapons = rng.range(3, 4) + extra_weapons;
    for (int i = 0; i < n_weapons; i++) {
        int idx = rng.range(min_weapon, SHOP_WEAPON_COUNT - 1);
        auto si = make_shop_item(SHOP_WEAPONS[idx]);
        si.item.damage_bonus += stat_bonus;
        si.item.gold_value = si.item.gold_value * price_scale / 100;

        // Material based on difficulty
        // 0-1: iron, 2-3: steel, 4-5: steel/silver, 6+: silver/obsidian
        if (difficulty >= 6) {
            int mroll = rng.range(1, 100);
            if (mroll <= 35) {
                si.item.material = MaterialType::SILVER;
                si.item.damage_bonus += material_damage_mod(MaterialType::SILVER);
                si.item.gold_value += 40;
            } else if (mroll <= 60) {
                si.item.material = MaterialType::OBSIDIAN;
                si.item.damage_bonus += material_damage_mod(MaterialType::OBSIDIAN);
                si.item.gold_value += 50;
            } else {
                si.item.material = MaterialType::STEEL;
                si.item.damage_bonus += material_damage_mod(MaterialType::STEEL);
                si.item.gold_value += 20;
            }
        } else if (difficulty >= 4) {
            if (rng.chance(50)) {
                si.item.material = MaterialType::SILVER;
                si.item.damage_bonus += material_damage_mod(MaterialType::SILVER);
                si.item.gold_value += 30;
            } else {
                si.item.material = MaterialType::STEEL;
                si.item.damage_bonus += material_damage_mod(MaterialType::STEEL);
                si.item.gold_value += 15;
            }
        } else if (difficulty >= 2) {
            si.item.material = MaterialType::STEEL;
            si.item.damage_bonus += material_damage_mod(MaterialType::STEEL);
            si.item.gold_value += 15;
        }

        // Update display name with material
        if (si.item.material != MaterialType::NONE && si.item.material != MaterialType::IRON) {
            si.item.name = std::string(material_name(si.item.material)) + " " + si.item.name;
        } else if (stat_bonus >= 3) {
            si.item.name = "Superior " + si.item.name;
        } else if (stat_bonus >= 2) {
            si.item.name = "Fine " + si.item.name;
        } else if (stat_bonus >= 1) {
            si.item.name = "Sturdy " + si.item.name;
        }

        stock_.push_back(std::move(si));
    }
    // Pick 3-4 armor pieces (+ province bonus)
    int n_armor = rng.range(3, 4) + extra_armor;
    for (int i = 0; i < n_armor; i++) {
        int idx = rng.range(min_armor, SHOP_ARMOR_COUNT - 1);
        auto si = make_shop_item(SHOP_ARMOR[idx]);
        si.item.armor_bonus += stat_bonus / 2; // armor scales slower than damage
        si.item.gold_value = si.item.gold_value * price_scale / 100;
        // Material upgrade for armor
        if (difficulty >= 6 && rng.chance(50)) {
            si.item.material = MaterialType::STEEL;
            si.item.armor_bonus += 1;
            si.item.gold_value += 30;
            si.item.name = "Steel " + si.item.name;
        } else if (difficulty >= 4 && rng.chance(40)) {
            si.item.material = MaterialType::STEEL;
            si.item.armor_bonus += 1;
            si.item.gold_value += 20;
            si.item.name = "Steel " + si.item.name;
        } else if (stat_bonus >= 3) {
            si.item.name = "Reinforced " + si.item.name;
        } else if (stat_bonus >= 2) {
            si.item.name = "Sturdy " + si.item.name;
        }
        stock_.push_back(std::move(si));
    }

    // Province specialty: bows guaranteed in Greenwood
    if (stock_bows) {
        // Pick bow from weapon table (indices 10-11 are bows)
        int bow_idx = rng.range(SHOP_WEAPON_COUNT - 2, SHOP_WEAPON_COUNT - 1);
        auto si = make_shop_item(SHOP_WEAPONS[bow_idx]);
        si.item.damage_bonus += stat_bonus;
        si.item.gold_value += stat_bonus * 10;
        stock_.push_back(std::move(si));
    }

    // Province specialty: extra heavy weapons/armor in forge towns
    if (stock_heavy && difficulty >= 2) {
        // Add a high-end weapon (warhammer, battle axe, falchion)
        int heavy_choices[] = {4, 6, 8}; // falchion, battle axe, warhammer
        int idx = heavy_choices[rng.range(0, 2)];
        if (idx < SHOP_WEAPON_COUNT) {
            auto si = make_shop_item(SHOP_WEAPONS[idx]);
            si.item.damage_bonus += stat_bonus + 1;
            si.item.gold_value += stat_bonus * 15 + 20;
            if (difficulty >= 5) {
                si.item.material = MaterialType::STEEL;
                si.item.damage_bonus += material_damage_mod(MaterialType::STEEL);
                si.item.name = "Forged " + si.item.name;
            }
            stock_.push_back(std::move(si));
        }
    }
    // Pick 2-3 consumables (+ province bonus) — skip weak food at high difficulty
    int n_cons = rng.range(2, 3) + extra_potions;
    for (int i = 0; i < n_cons; i++) {
        // At higher difficulty, bias toward healing potions over food
        int min_cons = (difficulty >= 4) ? 0 : 0; // always full range
        int max_cons = SHOP_CONS_COUNT - 1;
        if (difficulty >= 4) min_cons = 0; // keep strong healing accessible
        int idx = rng.range(min_cons, max_cons);
        auto si = make_shop_item(SHOP_CONSUMABLES[idx]);
        // Potion healing scales with difficulty
        if (si.item.heal_amount > 0) {
            si.item.heal_amount += difficulty * 3;
            si.item.gold_value = si.item.gold_value * price_scale / 100;
        }
        stock_.push_back(std::move(si));
    }

    // Province-specific: antidote in Greenwood/Dust Provinces
    if (stock_antidote) {
        ShopItem si;
        si.item.name = "antidote";
        si.item.description = "Cures poison.";
        si.item.type = ItemType::POTION;
        si.item.heal_amount = 0;
        si.item.gold_value = 20;
        si.item.unid_name = "green potion";
        si.sprite_x = 1; si.sprite_y = 19;
        stock_.push_back(std::move(si));
    }

    // Spell tomes in knowledge/magic-aligned towns (50% chance, 1-2 tomes)
    if (province_god == GodId::THESSARKA || province_god == GodId::SOLETH || rng.chance(20)) {
        int n_tomes = rng.range(1, 2);
        static const SpellId SHOP_SPELLS[] = {
            SpellId::SPARK, SpellId::MINOR_HEAL, SpellId::FORCE_BOLT,
            SpellId::IDENTIFY, SpellId::DETECT_MONSTERS, SpellId::HARDEN_SKIN,
            SpellId::CURE_POISON, SpellId::ENTANGLE, SpellId::SLOW,
            SpellId::FIREBALL, SpellId::ICE_SHARD, SpellId::DRAIN_LIFE,
            SpellId::MAJOR_HEAL, SpellId::REVEAL_MAP,
        };
        static constexpr int SHOP_SPELL_COUNT = sizeof(SHOP_SPELLS) / sizeof(SHOP_SPELLS[0]);
        for (int i = 0; i < n_tomes; i++) {
            auto spell = SHOP_SPELLS[rng.range(0, SHOP_SPELL_COUNT - 1)];
            auto& sinfo = get_spell_info(spell);
            ShopItem si;
            si.item.name = std::string("Tome of ") + sinfo.name;
            si.item.description = sinfo.description;
            si.item.type = ItemType::SCROLL;
            si.item.gold_value = 40 + sinfo.mp_cost * 8;
            si.item.identified = true;
            si.item.teaches_spell = static_cast<int>(spell);
            si.item.tags |= TAG_BOOK;
            si.sprite_x = 1; si.sprite_y = 21; // book sprite
            stock_.push_back(std::move(si));
        }
    }

    // Chance of interesting accessories — amulets, rings, staves
    // 40% chance of an amulet
    if (rng.chance(40)) {
        static const ShopItemDef SHOP_AMULETS[] = {
            {"red pendant",     "+1 attack.",         ItemType::AMULET, EquipSlot::AMULET, 0, 16, 0, 0, 1, 0, 0, 40},
            {"metal pendant",   "+1 AC.",             ItemType::AMULET, EquipSlot::AMULET, 1, 16, 0, 1, 0, 0, 0, 50},
            {"crystal pendant", "+1 dodge.",           ItemType::AMULET, EquipSlot::AMULET, 2, 16, 0, 0, 0, 1, 0, 55},
            {"disc pendant",    "+2 attack.",         ItemType::AMULET, EquipSlot::AMULET, 3, 16, 0, 0, 2, 0, 0, 65},
            {"stone pendant",   "+2 AC.",             ItemType::AMULET, EquipSlot::AMULET, 5, 16, 0, 2, 0, 0, 0, 75},
        };
        int max_a = std::min(difficulty / 2 + 1, 4);
        int idx = rng.range(0, max_a);
        auto si = make_shop_item(SHOP_AMULETS[idx]);
        si.item.gold_value = si.item.gold_value * price_scale / 100;
        si.item.identified = true;
        stock_.push_back(std::move(si));
    }
    // 30% chance of a ring
    if (rng.chance(30)) {
        static const ShopItemDef SHOP_RINGS[] = {
            {"gold band",       "+0.",                ItemType::RING, EquipSlot::RING_1, 1, 17, 0, 0, 0, 0, 0, 30},
            {"jade ring",       "+1 dodge.",           ItemType::RING, EquipSlot::RING_1, 2, 18, 0, 0, 0, 1, 0, 45},
            {"silver signet",   "+1 AC.",             ItemType::RING, EquipSlot::RING_1, 1, 18, 0, 1, 0, 0, 0, 50},
            {"ruby ring",       "+1 damage, +1 attack.", ItemType::RING, EquipSlot::RING_1, 3, 17, 1, 0, 1, 0, 0, 65},
        };
        int max_r = std::min(difficulty / 2, 3);
        int idx = rng.range(0, max_r);
        auto si = make_shop_item(SHOP_RINGS[idx]);
        si.item.gold_value = si.item.gold_value * price_scale / 100;
        si.item.identified = true;
        stock_.push_back(std::move(si));
    }
    // 20% chance of a staff (difficulty 3+)
    if (difficulty >= 3 && rng.chance(20)) {
        static const ShopItemDef SHOP_STAVES[] = {
            {"wooden staff",    "+2 dmg.",            ItemType::WEAPON, EquipSlot::MAIN_HAND, 2, 10, 2, 0, 0, 0, 0, 20},
            {"crystal staff",   "+3 dmg, +1 atk.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 10, 3, 0, 1, 0, 0, 50},
            {"holy staff",      "+3 dmg, +1 dodge.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 10, 3, 0, 0, 1, 0, 55},
            {"blue staff",      "+4 dmg, +1 atk.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 10, 4, 0, 1, 0, 0, 70},
        };
        int max_s = std::min((difficulty - 2) / 2, 3);
        int idx = rng.range(0, max_s);
        auto si = make_shop_item(SHOP_STAVES[idx]);
        si.item.damage_bonus += stat_bonus;
        si.item.gold_value = si.item.gold_value * price_scale / 100;
        stock_.push_back(std::move(si));
    }
}

void ShopScreen::open(Entity player, [[maybe_unused]] World& world, RNG& rng, int* gold, int difficulty, int price_mult, GodId province_god, Entity shopkeeper) {
    open_ = true;
    selected_ = 0;
    player_ = player;
    gold_ = gold;
    price_mult_ = price_mult;
    buy_tab_ = true;
    current_shopkeeper_ = shopkeeper;

    // Per-NPC persistent stock
    if (shopkeeper != 0) {
        auto it = stock_cache_.find(shopkeeper);
        if (it != stock_cache_.end()) {
            // Use cached stock for this shopkeeper
            stock_ = it->second;
        } else {
            // Generate new stock and cache it
            generate_stock(rng, difficulty, province_god);
            if (price_mult_ != 100) {
                for (auto& si : stock_)
                    si.item.gold_value = std::max(1, si.item.gold_value * price_mult_ / 100);
            }
            stock_cache_[shopkeeper] = stock_;
        }
    } else {
        // No shopkeeper entity (caravan etc): always generate fresh
        stock_.clear();
        generate_stock(rng, difficulty, province_god);
        if (price_mult_ != 100) {
            for (auto& si : stock_)
                si.item.gold_value = std::max(1, si.item.gold_value * price_mult_ / 100);
        }
    }
}

ShopAction ShopScreen::handle_input(SDL_Event& event) {
    if (!open_) return ShopAction::NONE;

    // Mouse click: select item or switch tabs
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mx = event.button.x, my = event.button.y;
        // Tab clicks
        if (mx >= tab_buy_rect_.x && mx < tab_buy_rect_.x + tab_buy_rect_.w &&
            my >= tab_buy_rect_.y && my < tab_buy_rect_.y + tab_buy_rect_.h) {
            buy_tab_ = true; selected_ = 0; return ShopAction::NONE;
        }
        if (mx >= tab_sell_rect_.x && mx < tab_sell_rect_.x + tab_sell_rect_.w &&
            my >= tab_sell_rect_.y && my < tab_sell_rect_.y + tab_sell_rect_.h) {
            buy_tab_ = false; selected_ = 0; return ShopAction::NONE;
        }
        // Item clicks
        for (int i = 0; i < static_cast<int>(item_rects_.size()); i++) {
            auto& r = item_rects_[i];
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
                if (i == selected_) {
                    return buy_tab_ ? ShopAction::BUY : ShopAction::SELL;
                }
                selected_ = i;
                return ShopAction::NONE;
            }
        }
        return ShopAction::NONE;
    }
    // Mouse hover
    if (event.type == SDL_MOUSEMOTION) {
        int mx = event.motion.x, my = event.motion.y;
        for (int i = 0; i < static_cast<int>(item_rects_.size()); i++) {
            auto& r = item_rects_[i];
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
                selected_ = i; break;
            }
        }
        return ShopAction::NONE;
    }
    // Mouse wheel scroll
    if (event.type == SDL_MOUSEWHEEL) {
        if (event.wheel.y > 0 && selected_ > 0) selected_--;
        else if (event.wheel.y < 0) selected_++;
        return ShopAction::NONE;
    }

    if (event.type != SDL_KEYDOWN) return ShopAction::NONE;

    switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
            return ShopAction::CLOSE;
        case SDLK_TAB:
            buy_tab_ = !buy_tab_;
            selected_ = 0;
            return ShopAction::NONE;
        case SDLK_UP:
        case SDLK_w:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return ShopAction::NONE;
        case SDLK_DOWN:
        case SDLK_s:
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

        // Create item entity with sprite
        Entity e = world.create();
        world.add<Renderable>(e, {SHEET_ITEMS, si.sprite_x, si.sprite_y,
                                   {255, 255, 255, 255}, 1});
        Item item = si.item; // copy
        world.add<Item>(e, std::move(item));
        inv.add(e);

        // Remove from stock and update cache
        stock_.erase(stock_.begin() + selected_);
        if (current_shopkeeper_ != 0)
            stock_cache_[current_shopkeeper_] = stock_;
        if (selected_ >= static_cast<int>(stock_.size()) && selected_ > 0) selected_--;
        return true;
    } else {
        // Sell
        if (selected_ < 0 || selected_ >= static_cast<int>(inv.items.size())) return false;
        Entity item_e = inv.items[selected_];
        if (!world.has<Item>(item_e)) return false;
        auto& item = world.get<Item>(item_e);

        // Ossren: can't sell equipment
        if (world.has<Player>(player_) && world.has<GodAlignment>(player_) &&
            world.get<GodAlignment>(player_).god == GodId::OSSREN &&
            item.slot != EquipSlot::NONE) {
            return false; // silently block
        }

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
    ui::draw_overlay(renderer, screen_w, screen_h);

    // Panel: 2/3 width, near-full height
    auto screen = ui::Layout::from_screen(screen_w, screen_h, line_h);
    auto outer = screen.panel_outer(2, 3, 9, 10);
    auto panel = ui::draw_panel_in(renderer, outer, line_h);

    // Title row
    auto title_row = panel.row();
    ui::draw_text_in(renderer, font, "Shop", title_col, title_row, ui::Align::CENTER);

    // Gold display -- right-aligned in the title row
    char gold_buf[64];
    snprintf(gold_buf, sizeof(gold_buf), "Gold: %d", gold_ ? *gold_ : 0);
    ui::draw_text_in(renderer, font, gold_buf, price_col, title_row, ui::Align::RIGHT);

    panel.skip(panel.gap);

    // Tabs row
    auto tab_row = panel.row();
    int tab_w = panel.bounds.w / 6;
    tab_buy_rect_ = {tab_row.x, tab_row.y, tab_w, tab_row.h};
    tab_sell_rect_ = {tab_row.x + tab_w, tab_row.y, tab_w, tab_row.h};
    ui::draw_text_in(renderer, font, "[Buy]", buy_tab_ ? tab_active : tab_inactive,
                     tab_row.left(tab_w), ui::Align::LEFT);
    ui::draw_text_in(renderer, font, "[Sell]", !buy_tab_ ? tab_active : tab_inactive,
                     {tab_row.x + tab_w, tab_row.y, tab_w, tab_row.h}, ui::Align::LEFT);

    panel.skip(panel.gap / 2);

    // Separator
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderDrawLine(renderer, panel.cursor.x, panel.cursor.y,
                       panel.cursor.x + panel.cursor.w, panel.cursor.y);
    panel.skip(panel.gap / 2);

    // Reserve bottom area: hint row + detail section (4 lines)
    auto hint_row = panel.row_bottom(line_h);
    panel.row_bottom(panel.gap / 2); // spacing above hint
    int detail_h = line_h * 4 + panel.gap;
    auto detail_area = panel.row_bottom(detail_h);

    // Separator above detail area
    SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
    SDL_RenderDrawLine(renderer, detail_area.x, detail_area.y - 2,
                       detail_area.x + detail_area.w, detail_area.y - 2);

    // Column positions derived from panel width
    int sprite_col_x = panel.cursor.x;                      // left edge for sprite
    int name_col_x = panel.cursor.x + 36;                   // after 32px sprite + 4px gap
    int price_col_w = panel.cursor.w / 6;                    // right 1/6 for price
    int price_col_x = panel.cursor.x + panel.cursor.w - price_col_w;
    int name_max_w = price_col_x - name_col_x - panel.gap;  // name fills the middle

    item_rects_.clear();

    if (buy_tab_) {
        // Buy tab -- show shop stock
        int count = static_cast<int>(stock_.size());
        int sel = std::min(selected_, count - 1);

        if (count == 0) {
            auto empty_row = panel.row();
            ui::draw_text_in(renderer, font, "(sold out)", hint_col, empty_row, ui::Align::LEFT);
        }

        for (int i = 0; i < count; i++) {
            int row_h = std::max(line_h + 8, 36);
            if (!panel.fits(row_h)) break;
            auto item_row = panel.row(row_h);
            item_rects_.push_back(item_row.sdl());
            auto& si = stock_[i];
            bool is_sel = (i == sel);
            bool can_afford = gold_ && *gold_ >= si.item.gold_value;

            if (is_sel) {
                SDL_Rect sel_rect = item_row.inset(0).sdl();
                SDL_SetRenderDrawColor(renderer, 35, 30, 48, 255);
                SDL_RenderFillRect(renderer, &sel_rect);
            }

            // Item sprite (32x32)
            sprites.draw_sprite(renderer, SHEET_ITEMS, si.sprite_x, si.sprite_y,
                               sprite_col_x, item_row.y, 1);

            // Item name
            char buf[128];
            snprintf(buf, sizeof(buf), "%s", si.item.name.c_str());
            ui::draw_text_clipped(renderer, font, buf, is_sel ? sel_col : item_col,
                                  name_col_x, item_row.y + 2, name_max_w);

            // Inline stats below name
            char stat_buf[64];
            if (si.item.type == ItemType::WEAPON) {
                snprintf(stat_buf, sizeof(stat_buf), "+%d dmg", si.item.damage_bonus);
            } else if (si.item.type == ItemType::POTION || si.item.type == ItemType::FOOD) {
                snprintf(stat_buf, sizeof(stat_buf), "heal %d", si.item.heal_amount);
            } else {
                snprintf(stat_buf, sizeof(stat_buf), "+%d arm", si.item.armor_bonus);
            }
            ui::draw_text(renderer, font, stat_buf, stat_col, name_col_x, item_row.y + 2 + line_h);

            // Price right-aligned
            char price[32];
            snprintf(price, sizeof(price), "%dg", si.item.gold_value);
            ui::Rect price_rect = {price_col_x, item_row.y, price_col_w, item_row.h};
            ui::draw_text_in(renderer, font, price, can_afford ? price_col : cant_col,
                             price_rect, ui::Align::RIGHT);

            // Owned count (below price, dim)
            if (world.has<Inventory>(player_)) {
                auto& pinv = world.get<Inventory>(player_);
                int owned = 0;
                for (auto ie : pinv.items) {
                    if (world.has<Item>(ie) && world.get<Item>(ie).name == si.item.name)
                        owned++;
                }
                if (owned > 0) {
                    char obuf[24];
                    snprintf(obuf, sizeof(obuf), "own:%d", owned);
                    ui::draw_text_in(renderer, font, obuf, hint_col,
                                     {price_col_x, item_row.y + line_h + 2, price_col_w, line_h},
                                     ui::Align::RIGHT);
                }
            }
        }

        // Detail area for selected item
        if (sel >= 0 && sel < count) {
            auto dl = ui::Layout::from_rect(detail_area, line_h);
            dl.skip(dl.gap);
            auto& si = stock_[sel];
            auto desc_row = dl.row();
            ui::draw_text_in(renderer, font, si.item.description.c_str(), hint_col,
                             desc_row, ui::Align::LEFT);

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
            dl.skip(2);
            auto stat_row = dl.row();
            ui::draw_text_in(renderer, font, stats, stat_col, stat_row, ui::Align::LEFT);
        }
    } else {
        // Sell tab -- show player inventory
        if (!world.has<Inventory>(player_)) return;
        auto& inv = world.get<Inventory>(player_);
        int count = static_cast<int>(inv.items.size());
        int sel = std::min(selected_, count - 1);

        if (count == 0) {
            auto empty_row = panel.row();
            ui::draw_text_in(renderer, font, "(nothing to sell)", hint_col, empty_row, ui::Align::LEFT);
        }

        for (int i = 0; i < count; i++) {
            int row_h = line_h + 4;
            if (!panel.fits(row_h)) break;
            auto item_row = panel.row(row_h);
            item_rects_.push_back(item_row.sdl());
            Entity item_e = inv.items[i];
            if (!world.has<Item>(item_e)) continue;
            auto& item = world.get<Item>(item_e);
            bool is_sel = (i == sel);
            int sell_price = std::max(1, item.gold_value / 2);

            if (is_sel) {
                SDL_Rect sel_rect = item_row.sdl();
                SDL_SetRenderDrawColor(renderer, 35, 30, 48, 255);
                SDL_RenderFillRect(renderer, &sel_rect);
            }

            char buf[128];
            snprintf(buf, sizeof(buf), "%s%s", item.display_name().c_str(),
                     inv.is_equipped(item_e) ? " [E]" : "");
            SDL_Color name_col = is_sel ? sel_col :
                (item.rarity != Rarity::COMMON ? rarity_color(item.rarity) : item_col);
            int sell_name_max = price_col_x - panel.cursor.x - panel.gap;
            ui::draw_text_clipped(renderer, font, buf, name_col, item_row.x, item_row.y, sell_name_max);

            char price[32];
            snprintf(price, sizeof(price), "%dg", sell_price);
            ui::Rect price_rect = {price_col_x, item_row.y, price_col_w, item_row.h};
            ui::draw_text_in(renderer, font, price, price_col, price_rect, ui::Align::RIGHT);
        }

        // Detail area for selected item
        if (sel >= 0 && sel < count) {
            Entity item_e = inv.items[sel];
            if (world.has<Item>(item_e)) {
                auto dl = ui::Layout::from_rect(detail_area, line_h);
                dl.skip(dl.gap);
                auto& item = world.get<Item>(item_e);
                if (!item.description.empty()) {
                    auto desc_row = dl.row();
                    ui::draw_text_in(renderer, font, item.description.c_str(), hint_col,
                                     desc_row, ui::Align::LEFT);
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
                dl.skip(2);
                auto stat_row = dl.row();
                ui::draw_text_in(renderer, font, stats, stat_col, stat_row, ui::Align::LEFT);
            }
        }
    }

    // Hints at bottom
    { auto* ig = InputGlyphs::get();
      char hbuf[256];
      if (ig && ig->using_gamepad())
          snprintf(hbuf, sizeof(hbuf), "(Y) switch  %s buy/sell  %s close",
                   ig->confirm().c_str(), ig->cancel().c_str());
      else
          snprintf(hbuf, sizeof(hbuf), "[Tab] switch  [Enter] buy/sell  [Esc/s] close");
      ui::draw_text_in(renderer, font, hbuf, hint_col, hint_row, ui::Align::LEFT); }
}
