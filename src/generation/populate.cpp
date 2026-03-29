#include "generation/populate.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/stats.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/item.h"
#include "core/spritesheet.h"
#include <algorithm>
#include <string>

namespace populate {

struct MonsterDef {
    const char* name;
    int sheet;
    int sprite_x, sprite_y;
    int hp;
    int str, dex, con;
    int base_damage;
    int natural_armor;
    int speed;
    int flee_threshold;
    int xp_value;
};

// Monster table — row/col in monsters.png
//                                          sheet            sx sy  hp  str dex con dmg arm spd flee xp
static const MonsterDef MONSTER_TABLE[] = {
    // Early game
    {"giant rat",       SHEET_MONSTERS, 11, 6,  6,   6, 14,  6,  2, 0, 130, 40,  10},
    {"kobold",          SHEET_MONSTERS,  0, 9,  6,   6, 12,  6,  1, 0, 120, 35,  10},
    {"goblin",          SHEET_MONSTERS,  2, 0,  8,   8, 12,  8,  2, 0, 110, 30,  15},
    {"giant spider",    SHEET_MONSTERS,  8, 6, 12,  10, 12,  8,  3, 1, 120, 20,  20},
    {"goblin archer",   SHEET_MONSTERS,  5, 0, 10,   8, 14,  8,  3, 0, 110, 25,  20},
    {"orc",             SHEET_MONSTERS,  0, 0, 18,  14,  8, 12,  4, 1,  90, 15,  30},
    {"skeleton",        SHEET_MONSTERS,  0, 4, 14,  10, 10, 10,  3, 2, 100,  0,  25},
    // Mid game
    {"zombie",          SHEET_MONSTERS,  4, 4, 20,  14,  6, 16,  5, 1,  60,  0,  30},
    {"warg",            SHEET_MONSTERS, 10, 6, 16,  14, 14, 12,  5, 1, 130, 20,  35},
    {"orc warchief",    SHEET_MONSTERS,  4, 0, 30,  16, 10, 14,  6, 2,  85, 10,  50},
    {"troll",           SHEET_MONSTERS,  2, 1, 35,  18,  8, 18,  7, 2,  80, 10,  60},
    // Late game
    {"ghoul",           SHEET_MONSTERS,  5, 4, 22,  14, 12, 14,  5, 1, 110,  0,  40},
    {"lich",            SHEET_MONSTERS,  2, 4, 50,  10, 10, 12, 12, 0, 100,  0,  80},
    {"death knight",    SHEET_MONSTERS,  3, 4, 65,  18, 12, 16, 14, 4,  90,  0, 100},
    {"manticore",       SHEET_MONSTERS,  3, 6, 35,  16, 14, 14,  7, 2, 110, 10,  70},
    {"minotaur",        SHEET_MONSTERS,  7, 7, 45,  20, 10, 18,  9, 3,  85, 10,  90},
    {"naga",            SHEET_MONSTERS,  4, 7, 30,  14, 16, 12,  6, 1, 120, 15,  60},
    {"dragon",          SHEET_MONSTERS,  2, 8,120,  22, 12, 22, 18, 5,  80,  0, 200},
};

static constexpr int MONSTER_COUNT = sizeof(MONSTER_TABLE) / sizeof(MONSTER_TABLE[0]);

void spawn_monsters(World& world, const TileMap& map,
                     const std::vector<Room>& rooms, RNG& rng,
                     int dungeon_level) {
    // Monster pool range scales with dungeon depth
    // Depth 1: indices 0-5 (rats, kobolds, goblins, spiders)
    // Depth 3+: indices 0-10 (adds warchiefs, trolls)
    // Depth 5+: full table including dragons
    int max_idx = std::min(MONSTER_COUNT - 1, 4 + dungeon_level * 2);

    for (size_t r = 1; r < rooms.size(); r++) {
        auto& room = rooms[r];

        int count = rng.range(2, 3 + dungeon_level / 2);
        for (int i = 0; i < count; i++) {
            int x = rng.range(room.x + 1, room.x + room.w - 2);
            int y = rng.range(room.y + 1, room.y + room.h - 2);

            if (!map.is_walkable(x, y)) continue;

            // Single roll — depth gating handles difficulty curve
            int idx = rng.range(0, max_idx);

            auto& def = MONSTER_TABLE[idx];

            // Scale HP and damage with depth
            float hp_scale = 1.0f + dungeon_level * 0.2f;
            float dmg_scale = 1.0f + dungeon_level * 0.15f;
            int scaled_hp = static_cast<int>(def.hp * hp_scale);
            int scaled_dmg = static_cast<int>(def.base_damage * dmg_scale);

            Entity e = world.create();
            world.add<Position>(e, {x, y});
            world.add<Renderable>(e, {def.sheet, def.sprite_x, def.sprite_y,
                                      {255, 255, 255, 255}, 5});

            Stats stats;
            stats.name = def.name;
            stats.hp = scaled_hp;
            stats.hp_max = scaled_hp;
            stats.set_attr(Attr::STR, def.str);
            stats.set_attr(Attr::DEX, def.dex);
            stats.set_attr(Attr::CON, def.con);
            stats.base_damage = scaled_dmg;
            stats.natural_armor = def.natural_armor;
            stats.base_speed = def.speed;
            stats.xp_value = def.xp_value;
            world.add<Stats>(e, std::move(stats));

            world.add<AI>(e, {AIState::IDLE, -1, -1, 0, def.flee_threshold});
            world.add<Energy>(e, {0, def.speed});
        }
    }
}

struct ItemDef {
    const char* name;
    const char* description;
    ItemType type;
    EquipSlot slot;
    int sprite_x, sprite_y; // in items.png
    int damage_bonus, armor_bonus, attack_bonus, dodge_bonus;
    int heal_amount;
    int gold_value;
    const char* unid_name; // empty = always identified
};

static const ItemDef WEAPON_TABLE[] = {
    {"dagger",         "A short, sharp blade.",           ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0,  2, 0, 1, 0, 0,  15, ""},
    {"short sword",    "A reliable sidearm.",             ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 0,  3, 0, 0, 0, 0,  30, ""},
    {"mace",           "Blunt and merciless.",            ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5,  4, 0, 0, 0, 0,  40, ""},
    {"spear",          "Long reach. Keeps them at bay.",  ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 6,  4, 0, 1, 0, 0,  35, ""},
    {"long sword",     "A well-balanced blade.",          ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0,  5, 0, 0, 0, 0,  60, ""},
    {"battle axe",     "Heavy. Splits bone.",             ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 3,  6, 0,-1, 0, 0,  55, ""},
    {"bastard sword",  "Long blade. Versatile grip.",     ItemType::WEAPON, EquipSlot::MAIN_HAND, 2, 0,  7, 0, 0, 0, 0,  80, ""},
    {"great axe",      "Two-handed devastation.",         ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 3,  9, 0,-2, 0, 0, 100, ""},
    {"war hammer",     "Crushes plate like parchment.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5,  8, 0,-1, 0, 0,  90, ""},
};

static const ItemDef ARMOR_TABLE[] = {
    {"leather armor",  "Supple hide. Quiet.",             ItemType::ARMOR_CHEST, EquipSlot::CHEST,  1, 12, 0, 2, 0, 0, 0, 40, ""},
    {"leather helm",   "Better than nothing.",            ItemType::ARMOR_HEAD,  EquipSlot::HEAD,   1, 15, 0, 1, 0, 0, 0, 20, ""},
    {"leather boots",  "Worn but sturdy.",                ItemType::ARMOR_FEET,  EquipSlot::FEET,   1, 14, 0, 1, 0, 0, 0, 20, ""},
    {"buckler",        "A small, round shield.",          ItemType::SHIELD,      EquipSlot::OFF_HAND,0, 11, 0, 2, 0, 1, 0, 30, ""},
    {"chain mail",     "Rings of iron. Heavy.",           ItemType::ARMOR_CHEST, EquipSlot::CHEST,  3, 12, 0, 4, 0,-1, 0, 80, ""},
    {"iron helm",      "Cold iron on your skull.",        ItemType::ARMOR_HEAD,  EquipSlot::HEAD,   4, 15, 0, 2, 0, 0, 0, 45, ""},
    {"kite shield",    "Large enough to hide behind.",    ItemType::SHIELD,      EquipSlot::OFF_HAND,1, 11, 0, 3, 0, 0, 0, 50, ""},
    {"plate armor",    "Full plate. Weighs a fortune.",   ItemType::ARMOR_CHEST, EquipSlot::CHEST,  3, 12, 0, 6, 0,-2, 0,150, ""},
    {"plate helm",     "A helm of tempered steel.",       ItemType::ARMOR_HEAD,  EquipSlot::HEAD,   4, 15, 0, 3, 0, 0, 0, 70, ""},
    {"iron boots",     "Heavy. Unyielding.",              ItemType::ARMOR_FEET,  EquipSlot::FEET,   1, 14, 0, 2, 0, 0, 0, 50, ""},
    {"tower shield",   "A wall of wood and iron.",        ItemType::SHIELD,      EquipSlot::OFF_HAND,1, 11, 0, 5, 0,-1, 0, 80, ""},
};

static const ItemDef CONSUMABLE_TABLE[] = {
    {"healing potion",  "Mends flesh.",                  ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 15, 25, "red potion"},
    {"strong healing",  "Flesh knits together.",          ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 30, 50, "bright red potion"},
    {"bread",           "Stale. Nourishing enough.",      ItemType::FOOD,   EquipSlot::NONE, 1, 25, 0, 0, 0, 0,  5, 5,  ""},
    {"cheese",          "Pungent.",                       ItemType::FOOD,   EquipSlot::NONE, 0, 25, 0, 0, 0, 0,  3, 3,  ""},
};

static constexpr int WEAPON_COUNT = sizeof(WEAPON_TABLE) / sizeof(WEAPON_TABLE[0]);
static constexpr int ARMOR_COUNT = sizeof(ARMOR_TABLE) / sizeof(ARMOR_TABLE[0]);
static constexpr int CONSUMABLE_COUNT = sizeof(CONSUMABLE_TABLE) / sizeof(CONSUMABLE_TABLE[0]);

static Entity create_item_from_def(World& world, const ItemDef& def, int x, int y) {
    Entity e = world.create();
    world.add<Position>(e, {x, y});
    world.add<Renderable>(e, {SHEET_ITEMS, def.sprite_x, def.sprite_y,
                              {255, 255, 255, 255}, 1}); // z_order 1 = under creatures

    Item item;
    item.name = def.name;
    item.description = def.description;
    item.type = def.type;
    item.slot = def.slot;
    item.damage_bonus = def.damage_bonus;
    item.armor_bonus = def.armor_bonus;
    item.attack_bonus = def.attack_bonus;
    item.dodge_bonus = def.dodge_bonus;
    item.heal_amount = def.heal_amount;
    item.gold_value = def.gold_value;
    item.unid_name = def.unid_name;
    item.identified = (def.unid_name[0] == '\0'); // auto-ID if no unid name
    world.add<Item>(e, std::move(item));
    return e;
}

// Apply quality prefix and bonus based on dungeon depth
static void apply_quality(Item& item, int dungeon_level, RNG& rng) {
    // Quality tiers: depth 5+ = Fine (+1), depth 10+ = Superior (+2), depth 15+ = Masterwork (+3)
    int max_tier = 0;
    if (dungeon_level >= 15) max_tier = 3;
    else if (dungeon_level >= 10) max_tier = 2;
    else if (dungeon_level >= 5) max_tier = 1;

    if (max_tier == 0) return;

    // 30% chance of quality item
    if (!rng.chance(30)) return;

    int tier = rng.range(1, max_tier);

    const char* prefix = "";
    switch (tier) {
        case 1: prefix = "Fine"; break;
        case 2: prefix = "Superior"; break;
        case 3: prefix = "Masterwork"; break;
    }

    item.name = std::string(prefix) + " " + item.name;
    item.gold_value = item.gold_value * (1 + tier);

    // Apply bonus based on item type
    if (item.type == ItemType::WEAPON) {
        item.damage_bonus += tier;
    } else {
        item.armor_bonus += tier;
    }
}

void spawn_items(World& world, const TileMap& map,
                  const std::vector<Room>& rooms, RNG& rng,
                  int dungeon_level) {
    for (size_t r = 0; r < rooms.size(); r++) {
        auto& room = rooms[r];

        // ~40% chance of an item per room
        if (!rng.chance(40)) continue;

        int x = rng.range(room.x + 1, room.x + room.w - 2);
        int y = rng.range(room.y + 1, room.y + room.h - 2);
        if (!map.is_walkable(x, y)) continue;

        // Pick item category
        int roll = rng.range(1, 100);
        if (roll <= 25) {
            // Weapon — deeper levels bias toward better weapons
            int min_idx = std::min(dungeon_level / 3, WEAPON_COUNT - 1);
            int idx = rng.range(min_idx, WEAPON_COUNT - 1);
            Entity e = create_item_from_def(world, WEAPON_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_quality(item, dungeon_level, rng);
        } else if (roll <= 50) {
            // Armor — deeper levels bias toward better armor
            int min_idx = std::min(dungeon_level / 3, ARMOR_COUNT - 1);
            int idx = rng.range(min_idx, ARMOR_COUNT - 1);
            Entity e = create_item_from_def(world, ARMOR_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_quality(item, dungeon_level, rng);
        } else if (roll <= 85) {
            // Consumable
            int idx = rng.range(0, CONSUMABLE_COUNT - 1);
            create_item_from_def(world, CONSUMABLE_TABLE[idx], x, y);
        } else {
            // Gold pile
            Entity e = world.create();
            world.add<Position>(e, {x, y});
            world.add<Renderable>(e, {SHEET_ITEMS, 1, 24, {255, 255, 255, 255}, 1});
            Item gold;
            gold.name = "gold coins";
            gold.type = ItemType::GOLD;
            gold.gold_value = rng.range(5, 20 + dungeon_level * 10);
            gold.stack = gold.gold_value;
            gold.stackable = true;
            gold.identified = true;
            world.add<Item>(e, std::move(gold));
        }
    }
}

Entity spawn_boss(World& world, [[maybe_unused]] const TileMap& map,
                   const std::vector<Room>& rooms, const char* name,
                   int sheet, int sx, int sy,
                   int hp, int str, int dex, int con,
                   int dmg, int armor, int speed, int xp_value) {
    if (rooms.size() < 2) return NULL_ENTITY;

    // Spawn in the last room center
    auto& room = rooms.back();
    int x = room.cx();
    int y = room.cy();

    Entity e = world.create();
    world.add<Position>(e, {x, y});
    world.add<Renderable>(e, {sheet, sx, sy, {255, 255, 255, 255}, 5});

    Stats stats;
    stats.name = name;
    stats.hp = hp;
    stats.hp_max = hp;
    stats.set_attr(Attr::STR, str);
    stats.set_attr(Attr::DEX, dex);
    stats.set_attr(Attr::CON, con);
    stats.base_damage = dmg;
    stats.natural_armor = armor;
    stats.base_speed = speed;
    stats.xp_value = xp_value;
    world.add<Stats>(e, std::move(stats));

    world.add<AI>(e, {AIState::IDLE, -1, -1, 0, 0}); // bosses don't flee
    world.add<Energy>(e, {0, speed});
    return e;
}

} // namespace populate
