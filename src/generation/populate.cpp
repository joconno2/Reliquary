#include "generation/populate.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/stats.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/item.h"
#include "components/spellbook.h"
#include "components/pet.h"
#include "components/god.h"
#include "components/tenet.h"
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

            AI ai_comp = {AIState::IDLE, -1, -1, 0, def.flee_threshold};
            // Ranged monsters
            if (std::string(def.name) == "goblin archer") {
                ai_comp.ranged_range = 5; // reduced from 6
                ai_comp.ranged_damage = static_cast<int>(2 * dmg_scale); // reduced from 3
            }
            world.add<AI>(e, ai_comp);
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
    int range = 0; // >0 = ranged weapon
};

static const ItemDef WEAPON_TABLE[] = {
    //                                                                                  sx  sy  dmg arm atk dge heal gold unid
    {"dagger",         "A short, sharp blade.",           ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0,  2, 0, 1, 0, 0,  15, ""}, // 1.a
    {"short sword",    "A reliable sidearm.",             ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 0,  3, 0, 0, 0, 0,  30, ""}, // 1.b
    {"mace",           "Blunt and merciless.",            ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5,  4, 0, 0, 0, 0,  40, ""}, // 6.a
    {"spear",          "Long reach. Keeps them at bay.",  ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 6,  4, 0, 1, 0, 0,  35, ""}, // 7.a
    {"long sword",     "A well-balanced blade.",          ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0,  5, 0, 0, 0, 0,  60, ""}, // 1.d
    {"battle axe",     "Heavy. Splits bone.",             ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 3,  6, 0,-1, 0, 0,  55, ""}, // 4.b
    {"bastard sword",  "Long blade. Versatile grip.",     ItemType::WEAPON, EquipSlot::MAIN_HAND, 4, 0,  7, 0, 0, 0, 0,  80, ""}, // 1.e
    {"great axe",      "Two-handed devastation.",         ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 3,  9, 0,-2, 0, 0, 100, ""}, // 4.d
    {"war hammer",     "Crushes plate like parchment.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 4,  8, 0,-1, 0, 0,  90, ""}, // 5.b
};

//                                                                                            sx sy dmg arm atk dge heal gold unid          range
static const ItemDef RANGED_TABLE[] = {
    {"short bow",   "Light and quick.",              ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 9,  3, 0, 1, 0, 0,  25, "",  6}, // 10.b
    {"hunting bow", "A woodsman's bow. Reliable.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 2, 9,  4, 0, 0, 0, 0,  45, "",  7}, // 10.c
    {"long bow",    "Powerful draw. Long range.",     ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 9,  6, 0, 0, 0, 0,  70, "",  9}, // 10.d
    {"crossbow",    "Slow to load. Hits hard.",       ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 9,  8, 0,-1, 0, 0, 100, "",  7}, // 10.a
};

static constexpr int RANGED_COUNT = sizeof(RANGED_TABLE) / sizeof(RANGED_TABLE[0]);

static const ItemDef ARMOR_TABLE[] = {
    //                                                                              sx  sy  dmg arm atk dge heal gold unid
    {"leather armor",  "Supple hide. Quiet.",             ItemType::ARMOR_CHEST, EquipSlot::CHEST,   1, 12, 0, 2, 0, 0, 0, 40, ""}, // 13.b
    {"leather helm",   "Better than nothing.",            ItemType::ARMOR_HEAD,  EquipSlot::HEAD,    1, 15, 0, 1, 0, 0, 0, 20, ""}, // 16.b
    {"leather boots",  "Worn but sturdy.",                ItemType::ARMOR_FEET,  EquipSlot::FEET,    1, 14, 0, 1, 0, 0, 0, 20, ""}, // 15.b
    {"buckler",        "A small, round shield.",          ItemType::SHIELD,      EquipSlot::OFF_HAND,0, 11, 0, 2, 0, 1, 0, 30, ""}, // 12.a
    {"chain mail",     "Rings of iron. Heavy.",           ItemType::ARMOR_CHEST, EquipSlot::CHEST,   3, 12, 0, 4, 0,-1, 0, 80, ""}, // 13.d
    {"iron helm",      "Cold iron on your skull.",        ItemType::ARMOR_HEAD,  EquipSlot::HEAD,    4, 15, 0, 2, 0, 0, 0, 45, ""}, // 16.e
    {"kite shield",    "Large enough to hide behind.",    ItemType::SHIELD,      EquipSlot::OFF_HAND,1, 11, 0, 3, 0, 0, 0, 50, ""}, // 12.b
    {"plate armor",    "Full plate. Weighs a fortune.",   ItemType::ARMOR_CHEST, EquipSlot::CHEST,   5, 12, 0, 6, 0,-2, 0,150, ""}, // 13.f
    {"plate helm",     "A helm of tempered steel.",       ItemType::ARMOR_HEAD,  EquipSlot::HEAD,    6, 15, 0, 3, 0, 0, 0, 70, ""}, // 16.g
    {"iron boots",     "Heavy. Unyielding.",              ItemType::ARMOR_FEET,  EquipSlot::FEET,    3, 14, 0, 2, 0, 0, 0, 50, ""}, // 15.d
    {"tower shield",   "A wall of wood and iron.",        ItemType::SHIELD,      EquipSlot::OFF_HAND,6, 11, 0, 5, 0,-1, 0, 80, ""}, // 12.g
};

static const ItemDef CONSUMABLE_TABLE[] = {
    //                                                                          sx  sy  dmg arm atk dge heal gold unid
    {"healing potion",  "Mends flesh.",                  ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 15, 25, "red potion"},      // 20.b
    {"strong healing",  "Flesh knits together.",          ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 30, 50, "bright red potion"},// 20.b
    {"mana potion",     "Magic pools in your veins.",     ItemType::POTION, EquipSlot::NONE, 3, 20, 0, 0, 0, 0,  0, 30, "blue potion"},     // 21.d
    {"antidote",        "Purges what shouldn't be there.",ItemType::POTION, EquipSlot::NONE, 4, 19, 0, 0, 0, 0,  0, 20, "green potion"},    // 20.e
    {"speed draught",   "The world slows. You don't.",    ItemType::POTION, EquipSlot::NONE, 4, 20, 0, 0, 0, 0,  0, 35, "orange potion"},   // 21.e
    {"strength elixir", "Your muscles burn pleasantly.",   ItemType::POTION, EquipSlot::NONE, 0, 19, 0, 0, 0, 0,  0, 40, "purple potion"},   // 20.a
    {"bread",           "Stale. Nourishing enough.",      ItemType::FOOD,   EquipSlot::NONE, 1, 25, 0, 0, 0, 0,  5, 5,  ""},               // 26.b
    {"cheese",          "Pungent.",                       ItemType::FOOD,   EquipSlot::NONE, 0, 25, 0, 0, 0, 0,  3, 3,  ""},               // 26.a
    {"dried meat",      "Tough but filling.",              ItemType::FOOD,   EquipSlot::NONE, 2, 25, 0, 0, 0, 0,  8, 8,  ""},               // 26.c (apple)
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
    item.range = def.range;
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

// Cursed or blessed items (depth 3+)
static void apply_curse_bless(Item& item, int dungeon_level, RNG& rng) {
    if (dungeon_level < 3) return;
    if (item.type != ItemType::WEAPON && item.type != ItemType::ARMOR_CHEST &&
        item.type != ItemType::ARMOR_HEAD && item.type != ItemType::ARMOR_FEET &&
        item.type != ItemType::SHIELD) return;

    // 10% cursed, 8% blessed
    int roll = rng.range(1, 100);
    if (roll <= 10) {
        item.curse_state = 1; // cursed
        // Cursed items have slightly better stats as bait
        if (item.type == ItemType::WEAPON)
            item.damage_bonus += 1;
        else
            item.armor_bonus += 1;
    } else if (roll <= 18) {
        item.curse_state = 2; // blessed
        item.name = "Blessed " + item.name;
        if (item.type == ItemType::WEAPON)
            item.damage_bonus += 1;
        else
            item.armor_bonus += 1;
    }
}

// Assign material based on dungeon depth
static void apply_material(Item& item, int dungeon_level, RNG& rng) {
    // Only weapons and armor get materials
    bool is_weapon = (item.type == ItemType::WEAPON);
    bool is_armor = (item.type == ItemType::ARMOR_HEAD || item.type == ItemType::ARMOR_CHEST
        || item.type == ItemType::ARMOR_HANDS || item.type == ItemType::ARMOR_FEET
        || item.type == ItemType::SHIELD);
    if (!is_weapon && !is_armor) return;

    // Depth-based material table
    // Shallow (1-3): bone/wood/iron
    // Mid (4-6): iron/steel, small chance silver
    // Deep (7-9): steel/silver/obsidian, small chance mithril
    // Very deep (10+): mithril/adamantine possible
    MaterialType mat = MaterialType::IRON; // default

    if (dungeon_level <= 3) {
        int roll = rng.range(1, 100);
        if (roll <= 15) mat = MaterialType::BONE;
        else if (roll <= 25) mat = MaterialType::WOOD;
        else mat = MaterialType::IRON;
    } else if (dungeon_level <= 6) {
        int roll = rng.range(1, 100);
        if (roll <= 40) mat = MaterialType::IRON;
        else if (roll <= 75) mat = MaterialType::STEEL;
        else mat = MaterialType::SILVER;
    } else if (dungeon_level <= 9) {
        int roll = rng.range(1, 100);
        if (roll <= 20) mat = MaterialType::STEEL;
        else if (roll <= 45) mat = MaterialType::SILVER;
        else if (roll <= 75) mat = MaterialType::OBSIDIAN;
        else mat = MaterialType::MITHRIL;
    } else {
        int roll = rng.range(1, 100);
        if (roll <= 25) mat = MaterialType::OBSIDIAN;
        else if (roll <= 55) mat = MaterialType::MITHRIL;
        else if (roll <= 85) mat = MaterialType::STEEL;
        else mat = MaterialType::ADAMANTINE;
    }

    item.material = mat;
    item.damage_bonus += material_damage_mod(mat);

    // Silver: mark for undead bonus (applied in combat)
    // Obsidian: slight fragility (could track later)
}

// Assign item tags for sacred/profane system
static void apply_tags(Item& item) {
    // Weapon tags based on name
    const std::string& n = item.name;
    if (n.find("dagger") != std::string::npos) item.tags |= TAG_DAGGER;
    if (n.find("mace") != std::string::npos || n.find("hammer") != std::string::npos
        || n.find("club") != std::string::npos || n.find("flail") != std::string::npos)
        item.tags |= TAG_BLUNT;
    if (n.find("axe") != std::string::npos) item.tags |= TAG_AXE;
    if (n.find("sword") != std::string::npos || n.find("blade") != std::string::npos)
        item.tags |= TAG_SWORD;
    if (n.find("bow") != std::string::npos || n.find("crossbow") != std::string::npos)
        item.tags |= TAG_BOW;

    // Armor tags
    if (item.type == ItemType::ARMOR_CHEST || item.type == ItemType::ARMOR_HEAD
        || item.type == ItemType::ARMOR_HANDS || item.type == ItemType::ARMOR_FEET) {
        if (n.find("plate") != std::string::npos || n.find("full") != std::string::npos)
            item.tags |= TAG_HEAVY_ARMOR;
        else if (n.find("chain") != std::string::npos || n.find("scale") != std::string::npos)
            item.tags |= TAG_MEDIUM_ARMOR;
        else
            item.tags |= TAG_LIGHT_ARMOR;
    }
    if (item.type == ItemType::SHIELD) item.tags |= TAG_SHIELD;

    // Book/scroll
    if (item.teaches_spell >= 0) item.tags |= TAG_BOOK;

    // Potion
    if (item.type == ItemType::POTION) item.tags |= TAG_POTION;

    // Food
    if (item.type == ItemType::FOOD) {
        if (n.find("bread") != std::string::npos || n.find("cheese") != std::string::npos)
            item.tags |= TAG_FOOD_COOKED;
        else if (n.find("meat") != std::string::npos || n.find("dried") != std::string::npos)
            item.tags |= TAG_FOOD_RAW;
        if (n.find("mushroom") != std::string::npos) item.tags |= TAG_MUSHROOM;
        if (n.find("herb") != std::string::npos) item.tags |= TAG_HERB;
    }

    // Material-based tags
    if (item.material == MaterialType::BONE) item.tags |= TAG_BONE_ITEM;

    // Healing potions are profane to Sythara
    if (item.type == ItemType::POTION && item.heal_amount > 0) item.tags |= TAG_POTION;
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
        if (roll <= 18) {
            // Melee weapon
            int min_idx = std::min(dungeon_level / 3, WEAPON_COUNT - 1);
            int idx = rng.range(min_idx, WEAPON_COUNT - 1);
            Entity e = create_item_from_def(world, WEAPON_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_material(item, dungeon_level, rng);
            apply_tags(item);
            apply_quality(item, dungeon_level, rng);
            apply_curse_bless(item, dungeon_level, rng);
        } else if (roll <= 25) {
            // Ranged weapon
            int min_idx = std::min(dungeon_level / 4, RANGED_COUNT - 1);
            int idx = rng.range(min_idx, RANGED_COUNT - 1);
            Entity e = create_item_from_def(world, RANGED_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_material(item, dungeon_level, rng);
            apply_tags(item);
            apply_quality(item, dungeon_level, rng);
        } else if (roll <= 47) {
            // Armor
            int min_idx = std::min(dungeon_level / 3, ARMOR_COUNT - 1);
            int idx = rng.range(min_idx, ARMOR_COUNT - 1);
            Entity e = create_item_from_def(world, ARMOR_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_material(item, dungeon_level, rng);
            apply_tags(item);
            apply_quality(item, dungeon_level, rng);
            apply_curse_bless(item, dungeon_level, rng);
        } else if (roll <= 52) {
            // Spellbook — teaches a random spell
            static const SpellId LEARNABLE[] = {
                SpellId::SPARK, SpellId::FORCE_BOLT, SpellId::FIREBALL,
                SpellId::HARDEN_SKIN, SpellId::REVEAL_MAP, SpellId::DETECT_MONSTERS,
                SpellId::IDENTIFY, SpellId::MINOR_HEAL, SpellId::CURE_POISON,
                SpellId::MAJOR_HEAL, SpellId::ENTANGLE, SpellId::DRAIN_LIFE,
                SpellId::FEAR,
            };
            static constexpr int LEARNABLE_COUNT = sizeof(LEARNABLE) / sizeof(LEARNABLE[0]);
            // Bias toward more advanced spells at depth
            int min_sp = std::min(dungeon_level / 2, LEARNABLE_COUNT - 1);
            int sp_idx = rng.range(min_sp, LEARNABLE_COUNT - 1);
            auto spell = LEARNABLE[sp_idx];
            auto& sinfo = get_spell_info(spell);

            Entity e = world.create();
            world.add<Position>(e, {x, y});
            world.add<Renderable>(e, {SHEET_ITEMS, 1, 21, {255, 255, 255, 255}, 1}); // 22.b book
            Item book;
            book.name = std::string("Tome of ") + sinfo.name;
            book.description = sinfo.description;
            book.type = ItemType::SCROLL;
            book.gold_value = 30 + sinfo.mp_cost * 5;
            book.identified = true;
            book.teaches_spell = static_cast<int>(spell);
            book.tags |= TAG_BOOK;
            world.add<Item>(e, std::move(book));
        } else if (roll <= 56) {
            // Lore item — readable journal/inscription
            struct LoreEntry { const char* name; const char* text; };
            static const LoreEntry LORE[] = {
                {"tattered journal",
                 "...the seals were placed long before we came. Whoever made them knew what was below. They didn't want it found."},
                {"carved inscription",
                 "THE RELIQUARY PREDATES THE GODS. WHAT MADE IT HAS NO NAME."},
                {"bloodstained note",
                 "If you're reading this, turn back. I didn't listen either. — K."},
                {"pilgrim's diary",
                 "Day 14. The deeper I go, the more I hear it. Not a voice. A vibration. Like the stones remember something."},
                {"crumbling scroll",
                 "Seven gods. Seven claims. None of them made the Reliquary. That's the part they don't want you to know."},
                {"faded letter",
                 "My dearest, I descended to the third level today. The walls have faces here. I don't think they were carved."},
                {"explorer's log",
                 "The monsters down here aren't guarding anything. They're running from something deeper."},
                {"priest's confession",
                 "I stopped praying on the fourth day. Not because I lost faith. Because something answered that wasn't my god."},
                {"scratched warning",
                 "DON'T TAKE IT. DON'T TOUCH IT. IT REMEMBERS EVERYONE WHO HAS."},
                {"ancient tablet fragment",
                 "...before the first dawn, there was the Reliquary. It was not created. It simply was. Everything else followed."},
            };
            static constexpr int LORE_COUNT = sizeof(LORE) / sizeof(LORE[0]);
            auto& lore = LORE[rng.range(0, LORE_COUNT - 1)];
            Entity e = world.create();
            world.add<Position>(e, {x, y});
            world.add<Renderable>(e, {SHEET_ITEMS, 0, 21, {255, 255, 255, 255}, 1}); // 22.a scroll
            Item item;
            item.name = lore.name;
            item.description = lore.text;
            item.type = ItemType::SCROLL;
            item.gold_value = 5;
            item.identified = true;
            world.add<Item>(e, std::move(item));
        } else if (roll <= 82) {
            // Consumable
            int idx = rng.range(0, CONSUMABLE_COUNT - 1);
            Entity ce = create_item_from_def(world, CONSUMABLE_TABLE[idx], x, y);
            apply_tags(world.get<Item>(ce));
        } else if (roll <= 85 && dungeon_level >= 2) {
            // Pet — rare find
            int pid = rng.range(0, PET_TYPE_COUNT - 1);
            auto& pinfo = get_pet_info(static_cast<PetId>(pid));

            Entity e = world.create();
            world.add<Position>(e, {x, y});
            world.add<Renderable>(e, {pinfo.sprite_sheet, pinfo.sprite_x, pinfo.sprite_y,
                                       {static_cast<Uint8>(pinfo.tint_r),
                                        static_cast<Uint8>(pinfo.tint_g),
                                        static_cast<Uint8>(pinfo.tint_b), 255}, 1});
            Item pet;
            pet.name = pinfo.name;
            pet.description = pinfo.description;
            pet.type = ItemType::PET;
            pet.slot = EquipSlot::PET;
            pet.pet_id = pid;
            pet.gold_value = 0;
            pet.identified = true;
            world.add<Item>(e, std::move(pet));
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

void spawn_doodads(World& world, const TileMap& map,
                    const std::vector<Room>& rooms, RNG& rng,
                    int dungeon_level, const std::string& zone) {
    // Sprite coordinates (row = group-1, col = letter index)
    // Row 17: chest closed(0), chest open(1), jar closed(2), jar open(3), barrel(4), ore sack(5), log pile(6)
    // Row 20: mushrooms small(0), large(1)
    // Row 21: corpse bones 1(0), corpse bones 2(1)
    // Row 22: blood 1(0), blood 2(1), slime small(2), slime large(3)
    // Row 23: coffin closed(0), coffin ajar(1), coffin open(2), sarcophagus closed(3)

    bool is_catacombs = (zone == "catacombs");
    bool is_molten = (zone == "molten");

    for (size_t r = 1; r < rooms.size(); r++) { // skip first room (player start)
        auto& room = rooms[r];

        // Lootable chest — ~20% chance per room
        if (rng.chance(20)) {
            int x = rng.range(room.x + 1, room.x + room.w - 2);
            int y = rng.range(room.y + 1, room.y + room.h - 2);
            if (map.is_walkable(x, y)) {
                Entity e = world.create();
                world.add<Position>(e, {x, y});
                world.add<Renderable>(e, {SHEET_TILES, 0, 17, {255,255,255,255}, 1}); // chest closed

                // Random loot: gold, potion, or food
                Item item;
                int roll = rng.range(1, 100);
                if (roll <= 50) {
                    item.name = "gold coins"; item.type = ItemType::GOLD;
                    item.gold_value = rng.range(8, 25 + dungeon_level * 8);
                    item.stack = item.gold_value; item.stackable = true;
                } else if (roll <= 80) {
                    item.name = "healing potion"; item.description = "Mends flesh.";
                    item.type = ItemType::POTION; item.heal_amount = 15;
                    item.gold_value = 25; item.unid_name = "red potion";
                } else {
                    item.name = "dried meat"; item.description = "Tough but filling.";
                    item.type = ItemType::FOOD; item.heal_amount = 8;
                    item.gold_value = 8;
                }
                item.identified = (item.type != ItemType::POTION);
                world.add<Item>(e, std::move(item));
            }
        }

        // Lootable jar — ~15% chance per room
        if (rng.chance(15)) {
            int x = rng.range(room.x + 1, room.x + room.w - 2);
            int y = rng.range(room.y + 1, room.y + room.h - 2);
            if (map.is_walkable(x, y)) {
                Entity e = world.create();
                world.add<Position>(e, {x, y});
                world.add<Renderable>(e, {SHEET_TILES, 2, 17, {255,255,255,255}, 1}); // jar closed

                Item item;
                item.name = "gold coins"; item.type = ItemType::GOLD;
                item.gold_value = rng.range(2, 10 + dungeon_level * 3);
                item.stack = item.gold_value; item.stackable = true;
                item.identified = true;
                world.add<Item>(e, std::move(item));
            }
        }

        // Decorative doodads (non-interactive, just visual entities at z-order 0)
        auto place_decor = [&](int sx, int sy, int sheet = SHEET_TILES) {
            int x = rng.range(room.x + 1, room.x + room.w - 2);
            int y = rng.range(room.y + 1, room.y + room.h - 2);
            if (!map.is_walkable(x, y)) return;
            Entity e = world.create();
            world.add<Position>(e, {x, y});
            world.add<Renderable>(e, {sheet, sx, sy, {255,255,255,255}, 0}); // z=0, under everything
        };

        // Mushrooms — common in all dungeons
        if (rng.chance(25)) place_decor(0, 20); // small mushrooms
        if (rng.chance(10)) place_decor(1, 20); // large mushroom

        // Blood splatters — more in deeper levels
        if (rng.chance(10 + dungeon_level * 3)) place_decor(rng.range(0, 1), 22);

        // Coffins in catacombs
        if (is_catacombs && rng.chance(30)) {
            place_decor(rng.range(0, 2), 23); // coffin variants
        }
        if (is_catacombs && rng.chance(15)) {
            place_decor(rng.range(3, 5), 23); // sarcophagus variants
        }

        // Ore sacks in deep/molten dungeons
        if (is_molten && rng.chance(20)) place_decor(5, 17);

        // Slime in warrens/deep halls
        if (dungeon_level <= 3 && rng.chance(12)) place_decor(rng.range(2, 3), 22);

        // Barrels — placed against walls
        if (rng.chance(12)) {
            // Try to find a spot adjacent to a wall
            for (int a = 0; a < 15; a++) {
                int x = rng.range(room.x + 1, room.x + room.w - 2);
                int y = rng.range(room.y + 1, room.y + room.h - 2);
                if (!map.is_walkable(x, y)) continue;
                bool adj_wall = false;
                for (auto [dx,dy] : std::initializer_list<std::pair<int,int>>{{-1,0},{1,0},{0,-1},{0,1}}) {
                    int nx = x+dx, ny = y+dy;
                    if (map.in_bounds(nx, ny) && !map.is_walkable(nx, ny) &&
                        map.at(nx, ny).type != TileType::DOOR_CLOSED)
                        adj_wall = true;
                }
                if (!adj_wall) continue;
                Entity e = world.create();
                world.add<Position>(e, {x, y});
                world.add<Renderable>(e, {SHEET_TILES, 4, 17, {255,255,255,255}, 0});
                break;
            }
        }

        // Wall torches — animated, placed against walls
        // Animated tiles: row 5 = torch (lit), row 1 = brazier (lit)
        if (rng.chance(35)) {
            for (int a = 0; a < 20; a++) {
                int x = rng.range(room.x + 1, room.x + room.w - 2);
                int y = rng.range(room.y + 1, room.y + room.h - 2);
                if (!map.is_walkable(x, y)) continue;
                // Must be adjacent to a wall
                bool adj = false;
                for (auto [dx,dy] : std::initializer_list<std::pair<int,int>>{{-1,0},{1,0},{0,-1},{0,1}}) {
                    int nx = x+dx, ny = y+dy;
                    if (map.in_bounds(nx, ny) && !map.is_walkable(nx, ny) &&
                        map.at(nx, ny).type != TileType::DOOR_CLOSED)
                        adj = true;
                }
                if (!adj) continue;
                Entity e = world.create();
                world.add<Position>(e, {x, y});
                // torch (row 5) or brazier (row 1) — torches more common
                int anim_row = rng.chance(80) ? 5 : 1;
                world.add<Renderable>(e, {SHEET_ANIMATED, 0, anim_row, {255,255,255,255}, 0});
                break;
            }
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

// Rival paragon definitions — one per god
struct ParagonDef {
    const char* name;
    GodId god;
    int sprite_x, sprite_y; // rogues.png
    // Tint (god-colored)
    Uint8 tint_r, tint_g, tint_b;
    // Base stats (scaled with depth)
    int hp, str, dex, con, intel, wil, per;
    int base_damage, natural_armor, speed;
};

static const ParagonDef PARAGON_TABLE[] = {
    // VETHRIK — Osric the Gravewarden — Fighter, bone/death
    {"Osric the Gravewarden", GodId::VETHRIK,
     1, 1,  200, 200, 180,
     80, 18, 12, 16, 10, 14, 11, 10, 4, 95},
    // THESSARKA — Mirael the Sightless — Wizard, knowledge
    {"Mirael the Sightless", GodId::THESSARKA,
     1, 4,  180, 160, 220,
     50, 10, 12, 10, 20, 16, 18, 14, 1, 100},
    // MORRETH — Dain Ironhand — Fighter, war/iron
    {"Dain Ironhand", GodId::MORRETH,
     1, 1,  200, 180, 160,
     90, 20, 10, 18, 9, 12, 10, 12, 5, 85},
    // YASHKHET — Sera of the Red Mark — Rogue, blood
    {"Sera of the Red Mark", GodId::YASHKHET,
     3, 0,  220, 120, 120,
     55, 14, 18, 12, 12, 14, 12, 9, 2, 120},
    // KHAEL — Theron Greenbark — Ranger, nature
    {"Theron Greenbark", GodId::KHAEL,
     2, 0,  140, 200, 140,
     65, 13, 16, 13, 11, 12, 18, 8, 2, 110},
    // SOLETH — Brother Lucan — Fighter, fire/zealotry
    {"Brother Lucan", GodId::SOLETH,
     1, 1,  255, 200, 140,
     75, 16, 12, 15, 10, 16, 12, 10, 3, 95},
    // IXUUL — The Unnamed — Wizard, chaos/void
    {"The Unnamed", GodId::IXUUL,
     1, 4,  160, 120, 200,
     60, 12, 14, 11, 18, 10, 14, 12, 1, 105},
    // ZHAVEK — Whisper — Rogue, shadow/stealth
    {"Whisper", GodId::ZHAVEK,
     3, 0,  60, 60, 100,
     45, 12, 20, 10, 14, 12, 18, 14, 1, 130},
    // THALARA — Nerissa of the Depths — Ranger, sea
    {"Nerissa of the Depths", GodId::THALARA,
     2, 0,  80, 180, 200,
     70, 14, 14, 15, 12, 16, 14, 10, 3, 100},
    // OSSREN — Varn the Unbroken — Fighter, craft/forge
    {"Varn the Unbroken", GodId::OSSREN,
     1, 1,  220, 180, 80,
     85, 16, 10, 18, 10, 12, 10, 11, 6, 85},
    // LETHIS — The Sleeper — Wizard, dreams
    {"The Sleeper", GodId::LETHIS,
     1, 4,  160, 120, 200,
     55, 10, 12, 12, 16, 18, 10, 13, 2, 95},
    // GATHRUUN — Borek Deepdelver — Dwarf, stone/earth
    {"Borek Deepdelver", GodId::GATHRUUN,
     4, 1,  160, 130, 90,
     95, 18, 8, 20, 8, 14, 10, 8, 7, 75},
    // SYTHARA — Mother Rot — Nature, plague/decay
    {"Mother Rot", GodId::SYTHARA,
     1, 4,  120, 180, 60,
     50, 10, 12, 14, 16, 14, 12, 12, 1, 100},
};
static constexpr int PARAGON_COUNT = sizeof(PARAGON_TABLE) / sizeof(PARAGON_TABLE[0]);

Entity spawn_paragon(World& world, [[maybe_unused]] const TileMap& map,
                      const std::vector<Room>& rooms, RNG& rng,
                      int dungeon_level, GodId player_god) {
    if (rooms.size() < 3) return NULL_ENTITY;

    // Build list of eligible paragons (exclude player's god)
    int eligible[16]; // sized for up to 16 paragons
    int count = 0;
    for (int i = 0; i < PARAGON_COUNT; i++) {
        if (PARAGON_TABLE[i].god != player_god)
            eligible[count++] = i;
    }
    if (count == 0) return NULL_ENTITY;

    auto& def = PARAGON_TABLE[eligible[rng.range(0, count - 1)]];

    // Spawn in a mid-to-late room (not the first or last)
    int room_idx = rng.range(static_cast<int>(rooms.size()) / 2,
                              static_cast<int>(rooms.size()) - 2);
    auto& room = rooms[room_idx];
    int x = room.cx();
    int y = room.cy();

    // Depth scaling — paragons get stronger deeper
    float scale = 1.0f + (dungeon_level - 4) * 0.15f;

    Entity e = world.create();
    world.add<Position>(e, {x, y});
    world.add<Renderable>(e, {SHEET_ROGUES, def.sprite_x, def.sprite_y,
                               {def.tint_r, def.tint_g, def.tint_b, 255}, 8}); // z=8, above monsters

    Stats stats;
    stats.name = def.name;
    stats.hp = static_cast<int>(def.hp * scale);
    stats.hp_max = stats.hp;
    stats.set_attr(Attr::STR, def.str + dungeon_level / 2);
    stats.set_attr(Attr::DEX, def.dex + dungeon_level / 2);
    stats.set_attr(Attr::CON, def.con + dungeon_level / 2);
    stats.set_attr(Attr::INT, def.intel + dungeon_level / 2);
    stats.set_attr(Attr::WIL, def.wil);
    stats.set_attr(Attr::PER, def.per);
    stats.base_damage = static_cast<int>(def.base_damage * scale);
    stats.natural_armor = static_cast<int>(def.natural_armor * scale);
    stats.base_speed = def.speed;
    stats.xp_value = 150 + dungeon_level * 25;
    world.add<Stats>(e, std::move(stats));

    world.add<AI>(e, {AIState::IDLE, -1, -1, 0, 10}); // low flee threshold
    world.add<Energy>(e, {0, def.speed});
    world.add<GodAlignment>(e, {def.god, 50}); // high favor — devout champion

    return e;
}

} // namespace populate
