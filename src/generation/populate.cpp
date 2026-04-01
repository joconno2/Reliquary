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
#include "components/container.h"
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
            float hp_scale = 1.0f + dungeon_level * 0.3f;
            float dmg_scale = 1.0f + dungeon_level * 0.2f;
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
    // Ordered weakest to strongest — depth gating uses min index
    //                                                                                  sx  sy  dmg arm atk dge heal gold unid
    {"club",           "+1 dmg.",                        ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 8,  1, 0, 0, 0, 0,   5, ""},
    {"dagger",         "+2 dmg, +2 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0,  2, 0, 2, 0, 0,  15, ""},
    {"short sword",    "+3 dmg.",                         ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 0,  3, 0, 0, 0, 0,  30, ""},
    {"hand axe",       "+3 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 3,  3, 0, 1, 0, 0,  25, ""},
    {"short spear",    "+3 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 6,  3, 0, 1, 0, 0,  28, ""},
    {"mace",           "+4 dmg.",                         ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5,  4, 0, 0, 0, 0,  40, ""},
    {"spear",          "+4 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 6,  4, 0, 1, 0, 0,  35, ""},
    {"scimitar",       "+4 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 2,  4, 0, 1, 0, 0,  45, ""},
    {"rapier",         "+3 dmg, +3 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 2, 1,  3, 0, 3, 0, 0,  50, ""},
    {"spiked club",    "+5 dmg, -1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 8,  5, 0,-1, 0, 0,  35, ""},
    {"flail",          "+5 dmg, -1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 7,  5, 0,-1, 0, 0,  42, ""},
    {"long sword",     "+5 dmg.",                         ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0,  5, 0, 0, 0, 0,  60, ""},
    {"battle axe",     "+6 dmg, -1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 3,  6, 0,-1, 0, 0,  55, ""},
    {"war mace",       "+6 dmg.",                         ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 5,  6, 0, 0, 0, 0,  55, ""},
    {"trident",        "+5 dmg, +1 atk, +1 dodge.",      ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 6,  5, 0, 1, 1, 0,  60, ""},
    {"halberd",        "+7 dmg, -1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 2, 3,  7, 0,-1, 0, 0,  65, ""},
    {"kukri",          "+5 dmg, +2 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 4, 2,  5, 0, 2, 0, 0,  55, ""},
    {"bastard sword",  "+7 dmg.",                         ItemType::WEAPON, EquipSlot::MAIN_HAND, 4, 0,  7, 0, 0, 0, 0,  80, ""},
    {"war hammer",     "+8 dmg, -1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 4,  8, 0,-1, 0, 0,  90, ""},
    {"great mace",     "+8 dmg, -2 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 2, 5,  8, 0,-2, 0, 0,  85, ""},
    {"long rapier",    "+5 dmg, +4 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 1,  5, 0, 4, 0, 0,  90, ""},
    {"great scimitar", "+7 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 2,  7, 0, 1, 0, 0,  80, ""},
    {"great axe",      "+9 dmg, -2 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 3,  9, 0,-2, 0, 0, 100, ""},
    {"large flamberge","+10 dmg, -2 atk.",               ItemType::WEAPON, EquipSlot::MAIN_HAND, 5, 1, 10, 0,-2, 0, 0, 110, ""},  // 2.f
    {"great sword",    "+11 dmg, -3 atk.",               ItemType::WEAPON, EquipSlot::MAIN_HAND, 6, 1, 11, 0,-3, 0, 0, 125, ""},  // 2.g
    {"giant axe",      "+11 dmg, -3 atk.",               ItemType::WEAPON, EquipSlot::MAIN_HAND, 4, 3, 11, 0,-3, 0, 0, 130, ""},  // 4.e
    {"great hammer",   "+10 dmg, -3 atk.",               ItemType::WEAPON, EquipSlot::MAIN_HAND, 4, 4, 10, 0,-3, 0, 0, 115, ""},  // 5.e
    {"trident",        "+6 dmg, +1 atk, +1 dodge.",      ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 6,  6, 0, 1, 1, 0,  65, ""},  // 7.d
    {"spiked flail",   "+7 dmg, -1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 8,  7, 0,-1, 0, 0,  60, ""},  // 9.d club with nails
};

// Legendary weapons — genuinely unique sprites only. NOT in random drops.
static const ItemDef LEGENDARY_WEAPON_TABLE[] = {
    // Unique sword sprites (1.g-k)
    {"Sanguine Edge",   "+8 dmg, +2 atk.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 6, 0,  8, 0, 2, 0, 0, 0, ""},  // 1.g sanguine dagger
    {"Nullblade",       "+7 dmg, +3 atk.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 7, 0,  7, 0, 3, 0, 0, 0, ""},  // 1.h magic dagger
    {"Crystal Fang",    "+9 dmg, +2 atk.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 8, 0,  9, 0, 2, 0, 0, 0, ""},  // 1.i crystal sword
    {"Doomhilt",        "+11 dmg, -1 atk.",  ItemType::WEAPON, EquipSlot::MAIN_HAND, 9, 0, 11, 0,-1, 0, 0, 0, ""},  // 1.j evil sword
    {"Emberbrand",      "+10 dmg, +1 atk.",  ItemType::WEAPON, EquipSlot::MAIN_HAND,10, 0, 10, 0, 1, 0, 0, 0, ""},  // 1.k flame sword
    // Unique spear sprite (7.e)
    {"Stormcaller",     "+9 dmg, +2 atk.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 4, 6,  9, 0, 2, 0, 0, 0, ""},  // 7.e magic spear
    // Unique staff sprites (11.f, 11.h, 11.j)
    {"Red Pyre",        "+6 dmg, +2 atk.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 5, 10, 6, 0, 2, 0, 0, 0, ""},  // 11.f red crystal staff
    {"Frostspire",      "+7 dmg, +2 atk.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 7, 10, 7, 0, 2, 0, 0, 0, ""},  // 11.h blue crystal staff
    {"Saint's Rest",    "+6 dmg, +3 atk.",   ItemType::WEAPON, EquipSlot::MAIN_HAND, 9, 10, 6, 0, 3, 0, 0, 0, ""},  // 11.j saint's staff
};

// Legendary armor/accessories — unique sprites only.
static const ItemDef LEGENDARY_ARMOR_TABLE[] = {
    {"Crown of Iron",  "+5 AC, -1 dodge.",  ItemType::ARMOR_HEAD,  EquipSlot::HEAD,    7, 15, 0, 5, 0,-1, 0, 0, ""},  // 16.h plate helm 2
    {"Cross Shield",   "+5 AC, +1 atk.",   ItemType::SHIELD,      EquipSlot::OFF_HAND,2, 11, 0, 5, 1, 0, 0, 0, ""},  // 12.c cross shield
    {"Cross Pendant",  "+2 atk, +1 AC.",   ItemType::AMULET,      EquipSlot::AMULET,  4, 16, 0, 1, 2, 0, 0, 0, ""},  // 17.e cross pendant
};

// God relics — one per god, index matches GodId enum (0-12).
// Each has powerful bonuses + a stat penalty (str/dex/con_bonus on ItemDef are repurposed).
// Relics are always blessed, can't be unequipped, and priceless.
struct RelicDef {
    const char* name;
    const char* description;
    ItemType type;
    EquipSlot slot;
    int sprite_x, sprite_y;
    int damage_bonus, armor_bonus, attack_bonus, dodge_bonus;
    int str_bonus, dex_bonus, con_bonus;
};

static const RelicDef RELIC_TABLE[] = {
    // [0] VETHRIK (death) — bone crown
    {"Skull of the Ossuary",  "+3 dmg, +3 CON, -3 CHA.",
     ItemType::ARMOR_HEAD, EquipSlot::HEAD,       7, 15,   3, 1, 0, 0,   0, 0, 3},
    // [1] THESSARKA (knowledge) — all-seeing pendant
    {"Eye of the Eyeless",    "+3 atk, +5 INT, -3 STR.",
     ItemType::AMULET,    EquipSlot::AMULET,      6, 16,   0, 0, 3, 0,  -3, 0, 0},
    // [2] MORRETH (war) — iron gauntlet weapon
    {"Fist of the Iron Father", "+10 dmg, +3 STR, -3 DEX.",
     ItemType::WEAPON,    EquipSlot::MAIN_HAND,   9, 0,   10, 0, 0, 0,   3,-3, 0},
    // [3] YASHKHET (blood) — sacrificial dagger
    {"Heartseeker",           "+7 dmg, +2 atk, +3 CON, -3 WIL.",
     ItemType::WEAPON,    EquipSlot::MAIN_HAND,   6, 0,    7, 0, 2, 0,   0, 0, 3},
    // [4] KHAEL (nature) — antler crown
    {"Antler Crown",          "+3 AC, +3 CON, -3 INT.",
     ItemType::ARMOR_HEAD, EquipSlot::HEAD,       7, 15,   0, 3, 0, 0,   0, 0, 3},
    // [5] SOLETH (fire) — flame ring
    {"Ember of the Pale Flame", "+3 dmg, +2 atk, -3 DEX.",
     ItemType::RING,      EquipSlot::RING_1,      3, 17,   3, 0, 2, 0,   0,-3, 0},
    // [6] IXUUL (chaos) — void shard ring
    {"Void Shard",            "+4 dmg, +2 dodge, -3 CON.",
     ItemType::RING,      EquipSlot::RING_1,      5, 17,   4, 0, 0, 2,   0, 0,-3},
    // [7] ZHAVEK (shadow) — shadow cloak amulet
    {"Shroud of the Unseen",  "+4 dodge, +2 atk, -3 STR.",
     ItemType::AMULET,    EquipSlot::AMULET,      2, 16,   0, 0, 2, 4,  -3, 0, 0},
    // [8] THALARA (sea) — drowned queen's trident
    {"Tide of the Drowned",   "+8 dmg, +3 DEX, -3 CON.",
     ItemType::WEAPON,    EquipSlot::MAIN_HAND,   4, 6,    8, 0, 0, 0,   0, 3,-3},
    // [9] OSSREN (craft) — perfect hammer
    {"Hammer Unworn",         "+7 dmg, +4 AC, -3 DEX.",
     ItemType::WEAPON,    EquipSlot::MAIN_HAND,   0, 5,    7, 4, 0, 0,   0,-3, 0},
    // [10] LETHIS (dreams) — dream veil pendant
    {"Dream Veil",            "+2 AC, +2 dodge, -3 STR.",
     ItemType::AMULET,    EquipSlot::AMULET,      3, 16,   0, 2, 0, 2,  -3, 0, 0},
    // [11] GATHRUUN (stone) — heart of the mountain ring
    {"Heart of the Mountain", "+5 AC, +3 CON, -3 DEX.",
     ItemType::RING,      EquipSlot::RING_1,      4, 18,   0, 5, 0, 0,   0,-3, 3},
    // [12] SYTHARA (plague) — rot blossom ring
    {"Rot Blossom",           "+3 dmg, +3 CON, -3 CHA.",
     ItemType::RING,      EquipSlot::RING_1,      2, 18,   3, 0, 0, 0,   0, 0, 3},
};

static constexpr int RELIC_COUNT = sizeof(RELIC_TABLE) / sizeof(RELIC_TABLE[0]);

//                                                                                            sx sy dmg arm atk dge heal gold unid          range
static const ItemDef RANGED_TABLE[] = {
    {"short bow",      "+3 dmg, +1 atk, range 6.",       ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 9,  3, 0, 1, 0, 0,  25, "",  6},
    {"hunting bow",    "+4 dmg, range 7.",               ItemType::WEAPON, EquipSlot::MAIN_HAND, 2, 9,  4, 0, 0, 0, 0,  45, "",  7},
    {"crossbow",       "+6 dmg, -1 atk, range 7.",      ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 9,  6, 0,-1, 0, 0,  70, "",  7},
    {"long bow",       "+7 dmg, range 9.",               ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 9,  7, 0, 0, 0, 0,  85, "",  9},
    {"heavy crossbow", "+9 dmg, -2 atk, range 8.",      ItemType::WEAPON, EquipSlot::MAIN_HAND, 4, 9,  9, 0,-2, 0, 0, 120, "",  8},
};

static constexpr int RANGED_COUNT = sizeof(RANGED_TABLE) / sizeof(RANGED_TABLE[0]);

static const ItemDef ARMOR_TABLE[] = {
    // Ordered weakest to strongest — mixed slots so depth gating gives variety
    //                                                                              sx  sy  dmg arm atk dge heal gold unid
    {"cloth robes",    "AC +1, no penalty.",              ItemType::ARMOR_CHEST, EquipSlot::CHEST,   0, 12, 0, 1, 0, 1, 0, 10, ""},
    {"cloth hood",     "AC +0, +1 dodge.",                ItemType::ARMOR_HEAD,  EquipSlot::HEAD,    0, 15, 0, 0, 0, 1, 0,  8, ""},
    {"shoes",          "AC +0, +1 dodge.",                ItemType::ARMOR_FEET,  EquipSlot::FEET,    0, 14, 0, 0, 0, 1, 0,  8, ""},
    {"cloth gloves",   "AC +0, +1 dodge.",                ItemType::ARMOR_HANDS, EquipSlot::HANDS,   0, 13, 0, 0, 0, 1, 0,  8, ""},
    {"buckler",        "AC +2, +1 dodge, light.",         ItemType::SHIELD,      EquipSlot::OFF_HAND,0, 11, 0, 2, 0, 1, 0, 30, ""},
    {"leather armor",  "AC +2, light armor.",             ItemType::ARMOR_CHEST, EquipSlot::CHEST,   1, 12, 0, 2, 0, 0, 0, 40, ""},
    {"leather helm",   "AC +1.",                          ItemType::ARMOR_HEAD,  EquipSlot::HEAD,    1, 15, 0, 1, 0, 0, 0, 20, ""},
    {"leather boots",  "AC +1.",                          ItemType::ARMOR_FEET,  EquipSlot::FEET,    1, 14, 0, 1, 0, 0, 0, 20, ""},
    {"leather gloves", "AC +1.",                          ItemType::ARMOR_HANDS, EquipSlot::HANDS,   1, 13, 0, 1, 0, 0, 0, 18, ""},
    {"round shield",   "AC +3.",                          ItemType::SHIELD,      EquipSlot::OFF_HAND,4, 11, 0, 3, 0, 0, 0, 45, ""},
    {"chain mail",     "AC +4, -1 dodge, medium armor.",  ItemType::ARMOR_CHEST, EquipSlot::CHEST,   3, 12, 0, 4, 0,-1, 0, 80, ""},
    {"chain coif",     "AC +2, medium armor.",            ItemType::ARMOR_HEAD,  EquipSlot::HEAD,    3, 15, 0, 2, 0, 0, 0, 40, ""},
    {"iron boots",     "AC +2, medium armor.",            ItemType::ARMOR_FEET,  EquipSlot::FEET,    3, 14, 0, 2, 0, 0, 0, 50, ""},
    {"gauntlets",      "AC +2, +1 attack.",               ItemType::ARMOR_HANDS, EquipSlot::HANDS,   3, 13, 0, 2, 1, 0, 0, 45, ""},
    {"kite shield",    "AC +4.",                          ItemType::SHIELD,      EquipSlot::OFF_HAND,1, 11, 0, 4, 0, 0, 0, 60, ""},
    {"scale mail",     "AC +5, -1 dodge, medium armor.",  ItemType::ARMOR_CHEST, EquipSlot::CHEST,   4, 12, 0, 5, 0,-1, 0,110, ""},
    {"iron helm",      "AC +3.",                          ItemType::ARMOR_HEAD,  EquipSlot::HEAD,    4, 15, 0, 3, 0, 0, 0, 55, ""},
    {"greaves",        "AC +3, -1 dodge, heavy.",         ItemType::ARMOR_FEET,  EquipSlot::FEET,    3, 14, 0, 3, 0,-1, 0, 65, ""},
    {"dark shield",    "AC +4, black iron.",              ItemType::SHIELD,      EquipSlot::OFF_HAND,3, 11, 0, 4, 0, 0, 0, 70, ""},
    {"scholar's robe", "AC +1, +2 dodge.",                ItemType::ARMOR_CHEST, EquipSlot::CHEST,   2, 12, 0, 1, 0, 2, 0, 55, ""},
};

static const ItemDef AMULET_TABLE[] = {
    {"red pendant",     "+1 attack.",                     ItemType::AMULET, EquipSlot::AMULET,  0, 16, 0, 0, 1, 0, 0, 40, "dull pendant"},
    {"metal pendant",   "+1 AC.",                         ItemType::AMULET, EquipSlot::AMULET,  1, 16, 0, 1, 0, 0, 0, 50, "heavy pendant"},
    {"crystal pendant", "+1 dodge.",                      ItemType::AMULET, EquipSlot::AMULET,  2, 16, 0, 0, 0, 1, 0, 55, "bright pendant"},
    {"disc pendant",    "+2 attack.",                     ItemType::AMULET, EquipSlot::AMULET,  3, 16, 0, 0, 2, 0, 0, 65, "round pendant"},
    {"stone pendant",   "+2 AC.",                         ItemType::AMULET, EquipSlot::AMULET,  5, 16, 0, 2, 0, 0, 0, 75, "grey pendant"},
    {"golden ankh",     "+1 attack, +1 dodge.",           ItemType::AMULET, EquipSlot::AMULET,  6, 16, 0, 0, 1, 1, 0, 90, "golden pendant"},
};
static constexpr int AMULET_COUNT = sizeof(AMULET_TABLE) / sizeof(AMULET_TABLE[0]);

static const ItemDef RING_TABLE[] = {
    //                                                                              sx  sy  dmg arm atk dge heal gold unid
    {"gold band",       "+0. Plain gold.",                ItemType::RING, EquipSlot::RING_1,  1, 17, 0, 0, 0, 0, 0, 30, "plain ring"},
    {"jade ring",       "+1 dodge.",                      ItemType::RING, EquipSlot::RING_1,  2, 18, 0, 0, 0, 1, 0, 45, "green ring"},
    {"silver signet",   "+1 AC.",                         ItemType::RING, EquipSlot::RING_1,  1, 18, 0, 1, 0, 0, 0, 50, "silver ring"},
    {"ruby ring",       "+1 damage, +1 attack.",          ItemType::RING, EquipSlot::RING_1,  3, 17, 1, 0, 1, 0, 0, 65, "red ring"},
    {"sapphire ring",   "+2 dodge.",                      ItemType::RING, EquipSlot::RING_1,  4, 17, 0, 0, 0, 2, 0, 70, "blue ring"},
    {"onyx ring",       "+2 AC.",                         ItemType::RING, EquipSlot::RING_1,  5, 17, 0, 2, 0, 0, 0, 80, "dark ring"},
    {"twisted gold",    "+2 damage, +1 attack.",          ItemType::RING, EquipSlot::RING_1,  4, 18, 2, 0, 1, 0, 0, 95, "twisted ring"}
};
static constexpr int RING_COUNT = sizeof(RING_TABLE) / sizeof(RING_TABLE[0]);

static const ItemDef STAFF_TABLE[] = {
    {"wooden staff",    "+2 dmg.",                         ItemType::WEAPON, EquipSlot::MAIN_HAND, 2, 10, 2, 0, 0, 0, 0, 20, ""},
    {"crystal staff",   "+3 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 10, 3, 0, 1, 0, 0, 50, ""},
    {"holy staff",      "+3 dmg, +1 dodge.",              ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 10, 3, 0, 0, 1, 0, 55, ""},
    {"blue staff",      "+4 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 10, 4, 0, 1, 0, 0, 70, ""},
    {"golden staff",    "+4 dmg, +2 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 4, 10, 4, 0, 2, 0, 0, 85, ""},
    {"flame staff",     "+5 dmg, +1 atk.",                ItemType::WEAPON, EquipSlot::MAIN_HAND, 6, 10, 5, 0, 1, 0, 0,100, ""},
};
static constexpr int STAFF_COUNT = sizeof(STAFF_TABLE) / sizeof(STAFF_TABLE[0]);

static const ItemDef CONSUMABLE_TABLE[] = {
    //                                                                          sx  sy  dmg arm atk dge heal gold unid
    {"healing potion",  "Restores 15 HP.",               ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 15, 25, "red potion"},
    {"strong healing",  "Restores 30 HP.",               ItemType::POTION, EquipSlot::NONE, 1, 19, 0, 0, 0, 0, 30, 50, "bright red potion"},
    {"mana potion",     "Restores 15 MP.",               ItemType::POTION, EquipSlot::NONE, 3, 20, 0, 0, 0, 0,  0, 30, "blue potion"},
    {"antidote",        "Cures poison.",                  ItemType::POTION, EquipSlot::NONE, 4, 19, 0, 0, 0, 0,  0, 20, "green potion"},
    {"speed draught",   "Grants 3 bonus actions.",         ItemType::POTION, EquipSlot::NONE, 4, 20, 0, 0, 0, 0,  0, 35, "orange potion"},
    {"strength elixir", "Permanently +4 STR.",            ItemType::POTION, EquipSlot::NONE, 0, 19, 0, 0, 0, 0,  0, 40, "purple potion"},
    {"bread",           "Restores 5 HP. Stale.",          ItemType::FOOD,   EquipSlot::NONE, 1, 25, 0, 0, 0, 0,  5, 5,  ""},
    {"cheese",          "Restores 3 HP.",                 ItemType::FOOD,   EquipSlot::NONE, 0, 25, 0, 0, 0, 0,  3, 3,  ""},
    {"dried meat",      "Restores 8 HP.",                 ItemType::FOOD,   EquipSlot::NONE, 2, 25, 0, 0, 0, 0,  8, 8,  ""},
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
    // Only melee and ranged weapons get materials — not armor, staves, rings, amulets
    if (item.type != ItemType::WEAPON) return;

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

// Weighted item pick: center around target index, triangular distribution
// Higher effective_level = higher target. Items far above target are rare.
static int weighted_item_pick(RNG& rng, int effective_level, int table_size, int divisor = 2) {
    // Target index: where we center the distribution
    int target = std::min(effective_level / divisor, table_size - 1);
    // Pick two random indices and average them (triangular distribution centered on target)
    int lo = std::max(0, target - 3);
    int hi = std::min(table_size - 1, target + 4);
    int a = rng.range(lo, hi);
    int b = rng.range(lo, hi);
    return (a + b) / 2; // tends toward the center
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
        if (roll <= 15) {
            // Melee weapon — weighted toward effective level, better items rarer
            int idx = weighted_item_pick(rng, dungeon_level, WEAPON_COUNT, 2);
            Entity e = create_item_from_def(world, WEAPON_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_material(item, dungeon_level, rng);
            apply_tags(item);
            apply_quality(item, dungeon_level, rng);
            apply_curse_bless(item, dungeon_level, rng);
        } else if (roll <= 21) {
            // Ranged weapon
            int idx = weighted_item_pick(rng, dungeon_level, RANGED_COUNT, 3);
            Entity e = create_item_from_def(world, RANGED_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_material(item, dungeon_level, rng);
            apply_tags(item);
            apply_quality(item, dungeon_level, rng);
        } else if (roll <= 25) {
            // Staff (mage weapon)
            int idx = weighted_item_pick(rng, dungeon_level, STAFF_COUNT, 3);
            Entity e = create_item_from_def(world, STAFF_TABLE[idx], x, y);
            apply_tags(world.get<Item>(e));
        } else if (roll <= 44) {
            // Armor — no material, the sprite IS the tier
            int idx = weighted_item_pick(rng, dungeon_level, ARMOR_COUNT, 2);
            Entity e = create_item_from_def(world, ARMOR_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_tags(item);
            apply_quality(item, dungeon_level, rng);
            apply_curse_bless(item, dungeon_level, rng);
        } else if (roll <= 52) {
            // Spellbook — teaches a random spell
            // Ordered by power — weak first, powerful last. 50 spells.
            static const SpellId LEARNABLE[] = {
                // Tier 1 (early)
                SpellId::SPARK, SpellId::MINOR_HEAL, SpellId::DETECT_MONSTERS,
                SpellId::IDENTIFY, SpellId::CURE_POISON, SpellId::HARDEN_SKIN,
                SpellId::SWARM, SpellId::ACID_SPLASH,
                // Tier 2 (mid-early)
                SpellId::FORCE_BOLT, SpellId::ENTANGLE, SpellId::FEAR,
                SpellId::FORESIGHT, SpellId::HASTEN, SpellId::REJUVENATE,
                SpellId::BARKSKIN, SpellId::SLOW, SpellId::SCRY,
                // Tier 3 (mid)
                SpellId::FIREBALL, SpellId::ICE_SHARD, SpellId::DRAIN_LIFE,
                SpellId::MAJOR_HEAL, SpellId::REVEAL_MAP, SpellId::STONE_FIST,
                SpellId::POISON_CLOUD, SpellId::HEX, SpellId::SHIELD_OF_FAITH,
                SpellId::WITHER, SpellId::THORNWALL,
                // Tier 4 (late)
                SpellId::LIGHTNING, SpellId::SOUL_REND, SpellId::FROST_NOVA,
                SpellId::PHASE, SpellId::CLEANSE, SpellId::BEAST_CALL,
                SpellId::TRUESIGHT, SpellId::DARKNESS, SpellId::RESTORE,
                SpellId::IRON_BODY, SpellId::CLAIRVOYANCE, SpellId::SANCTUARY,
                SpellId::EARTHQUAKE,
                // Tier 5 (endgame)
                SpellId::CHAIN_LIGHTNING, SpellId::METEOR, SpellId::RAISE_DEAD,
                SpellId::LIGHTNING_STORM, SpellId::POLYMORPH, SpellId::BLOOD_PACT,
                SpellId::DISINTEGRATE, SpellId::DOOM, SpellId::RESURRECTION,
            };
            static constexpr int LEARNABLE_COUNT = sizeof(LEARNABLE) / sizeof(LEARNABLE[0]);
            int sp_idx = weighted_item_pick(rng, dungeon_level, LEARNABLE_COUNT, 2);
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
        } else if (roll <= 56 && dungeon_level >= 2) {
            // Amulet — depth 2+
            int idx = weighted_item_pick(rng, dungeon_level, AMULET_COUNT, 3);
            Entity e = create_item_from_def(world, AMULET_TABLE[idx], x, y);
            apply_curse_bless(world.get<Item>(e), dungeon_level, rng);
        } else if (roll <= 60 && dungeon_level >= 2) {
            // Ring — depth 2+
            int idx = weighted_item_pick(rng, dungeon_level, RING_COUNT, 3);
            Entity e = create_item_from_def(world, RING_TABLE[idx], x, y);
            apply_curse_bless(world.get<Item>(e), dungeon_level, rng);
        } else if (roll <= 63) {
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
        } else if (roll <= 85) {
            // Consumable
            int idx = rng.range(0, CONSUMABLE_COUNT - 1);
            Entity ce = create_item_from_def(world, CONSUMABLE_TABLE[idx], x, y);
            apply_tags(world.get<Item>(ce));
        } else if (roll <= 88 && dungeon_level >= 2) {
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

    // Apply material palette swaps or tints to all items
    // Palette swap sheet covers weapon groups 1-8 (items.png rows 0-7)
    // Color blocks: rows 0-7 = bone, 8-15 = silver, 16-23 = mithril, 24-31 = adamantine
    // Default steel/iron/wood/obsidian uses original items.png sprites
    auto& item_pool = world.pool<Item>();
    for (size_t i = 0; i < item_pool.size(); i++) {
        Entity e = item_pool.entity_at(i);
        auto& item = item_pool.at_index(i);
        if (item.material == MaterialType::NONE || item.material == MaterialType::IRON ||
            item.material == MaterialType::STEEL || item.material == MaterialType::WOOD)
            continue; // default sprite
        if (!world.has<Renderable>(e)) continue;
        auto& rend = world.get<Renderable>(e);

        // Check if this item's sprite is a weapon on the palette swap sheet (rows 0-7 in items.png)
        bool on_palette_sheet = (rend.sprite_sheet == SHEET_ITEMS && rend.sprite_y >= 0 && rend.sprite_y <= 7);

        if (on_palette_sheet) {
            int palette_row_offset = -1;
            switch (item.material) {
                case MaterialType::BONE:       palette_row_offset = 0;  break; // orange
                case MaterialType::SILVER:     palette_row_offset = 8;  break; // grayish purple
                case MaterialType::MITHRIL:    palette_row_offset = 16; break; // blue
                case MaterialType::ADAMANTINE: palette_row_offset = 24; break; // light green
                default: break;
            }
            if (palette_row_offset >= 0) {
                rend.sprite_sheet = SHEET_ITEMS_PALETTE;
                rend.sprite_y = palette_row_offset + rend.sprite_y; // same row within group + color offset
                // sprite_x stays the same (same column = same item within group)
            }
        } else {
            // Weapon not on palette sheet (e.g. bows row 9) — tint fallback
            if (item.material == MaterialType::BONE || item.material == MaterialType::SILVER ||
                item.material == MaterialType::MITHRIL || item.material == MaterialType::ADAMANTINE ||
                item.material == MaterialType::OBSIDIAN) {
                rend.tint = material_tint(item.material);
            }
        }
    }
}

static constexpr int LEGENDARY_WEAPON_COUNT = sizeof(LEGENDARY_WEAPON_TABLE) / sizeof(LEGENDARY_WEAPON_TABLE[0]);
static constexpr int LEGENDARY_ARMOR_COUNT = sizeof(LEGENDARY_ARMOR_TABLE) / sizeof(LEGENDARY_ARMOR_TABLE[0]);

Entity spawn_relic(World& world, const std::vector<Room>& rooms, RNG& rng,
                    int patron_god_idx) {
    if (rooms.size() < 2 || patron_god_idx < 0 || patron_god_idx >= RELIC_COUNT)
        return NULL_ENTITY;

    auto& def = RELIC_TABLE[patron_god_idx];

    // Place in the last room (deepest point), offset from center
    auto& room = rooms.back();
    int x = room.cx() + rng.range(-1, 1);
    int y = room.cy() + rng.range(-1, 1);

    Entity e = world.create();
    world.add<Position>(e, {x, y});

    // God-colored tint for the relic
    auto& ginfo = get_god_info(static_cast<GodId>(patron_god_idx));
    Uint8 tr = static_cast<Uint8>(200 + (255 - ginfo.color.r) / 4);
    Uint8 tg = static_cast<Uint8>(200 + (255 - ginfo.color.g) / 4);
    Uint8 tb = static_cast<Uint8>(200 + (255 - ginfo.color.b) / 4);
    world.add<Renderable>(e, {SHEET_ITEMS, def.sprite_x, def.sprite_y,
                               {tr, tg, tb, 255}, 3}); // high z-order

    Item item;
    item.name = def.name;
    item.description = def.description;
    item.type = def.type;
    item.slot = def.slot;
    item.damage_bonus = def.damage_bonus;
    item.armor_bonus = def.armor_bonus;
    item.attack_bonus = def.attack_bonus;
    item.dodge_bonus = def.dodge_bonus;
    item.str_bonus = def.str_bonus;
    item.dex_bonus = def.dex_bonus;
    item.con_bonus = def.con_bonus;
    item.gold_value = 0;
    item.identified = true;
    item.curse_state = 1; // can't unequip
    item.relic_god = patron_god_idx;
    world.add<Item>(e, std::move(item));
    return e;
}

Entity spawn_legendary(World& world, const std::vector<Room>& rooms, [[maybe_unused]] RNG& rng,
                        const std::string& dungeon_name) {
    if (rooms.size() < 2) return NULL_ENTITY;

    // Legendaries only in specific hard dungeons — not every dungeon has one
    const ItemDef* def = nullptr;
    // 0=Sanguine Edge, 1=Nullblade, 2=Crystal Fang, 3=Doomhilt, 4=Emberbrand,
    // 5=Stormcaller, 6=Red Pyre, 7=Frostspire, 8=Saint's Rest
    if (dungeon_name == "The Hollowgate")          def = &LEGENDARY_WEAPON_TABLE[3]; // Doomhilt
    else if (dungeon_name == "The Molten Depths")   def = &LEGENDARY_WEAPON_TABLE[4]; // Emberbrand
    else if (dungeon_name == "The Sunken Halls")    def = &LEGENDARY_WEAPON_TABLE[2]; // Crystal Fang
    else if (dungeon_name == "The Sepulchre")       def = &LEGENDARY_WEAPON_TABLE[5]; // Stormcaller
    else return NULL_ENTITY;

    // Place in the last room (deepest point)
    auto& room = rooms.back();
    int x = room.cx();
    int y = room.cy();

    Entity e = world.create();
    world.add<Position>(e, {x, y});
    world.add<Renderable>(e, {SHEET_ITEMS, def->sprite_x, def->sprite_y,
                               {255, 240, 180, 255}, 2}); // golden tint, higher z

    Item item;
    item.name = def->name;
    item.description = def->description;
    item.type = def->type;
    item.slot = def->slot;
    item.damage_bonus = def->damage_bonus;
    item.armor_bonus = def->armor_bonus;
    item.attack_bonus = def->attack_bonus;
    item.dodge_bonus = def->dodge_bonus;
    item.gold_value = 0; // priceless
    item.identified = true;
    item.curse_state = 2; // blessed
    world.add<Item>(e, std::move(item));
    return e;
}

void spawn_doodads(World& world, const TileMap& map,
                    const std::vector<Room>& rooms, RNG& rng,
                    int dungeon_level, const std::string& zone,
                    int patron_god_idx) {
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

                Container cont;
                cont.open_sprite_x = 1; cont.open_sprite_y = 17; // chest open sprite
                int roll = rng.range(1, 100);
                if (roll <= 50) {
                    cont.contents.name = "gold coins"; cont.contents.type = ItemType::GOLD;
                    cont.contents.gold_value = rng.range(8, 25 + dungeon_level * 8);
                    cont.contents.stack = cont.contents.gold_value; cont.contents.stackable = true;
                    cont.contents.identified = true;
                } else if (roll <= 80) {
                    cont.contents.name = "healing potion"; cont.contents.description = "Restores 15 HP.";
                    cont.contents.type = ItemType::POTION; cont.contents.heal_amount = 15;
                    cont.contents.gold_value = 25; cont.contents.unid_name = "red potion";
                } else {
                    cont.contents.name = "dried meat"; cont.contents.description = "Restores 8 HP.";
                    cont.contents.type = ItemType::FOOD; cont.contents.heal_amount = 8;
                    cont.contents.gold_value = 8; cont.contents.identified = true;
                }
                world.add<Container>(e, std::move(cont));
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

                Container cont;
                cont.open_sprite_x = 3; cont.open_sprite_y = 17; // jar open sprite
                cont.contents.name = "gold coins"; cont.contents.type = ItemType::GOLD;
                cont.contents.gold_value = rng.range(2, 10 + dungeon_level * 3);
                cont.contents.stack = cont.contents.gold_value; cont.contents.stackable = true;
                cont.contents.identified = true;
                world.add<Container>(e, std::move(cont));
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

    // God shrine — ~20% chance per floor, placed in a mid-room
    // Uses the dungeon's patron god if available, otherwise random
    if (rng.chance(20) && rooms.size() >= 3) {
        int room_idx = rng.range(1, static_cast<int>(rooms.size()) - 2);
        auto& room = rooms[room_idx];
        int sx = room.cx();
        int sy = room.cy();
        if (map.in_bounds(sx, sy) && map.is_walkable(sx, sy)) {
            auto& tile = const_cast<TileMap&>(map).at(sx, sy);
            tile.type = TileType::SHRINE;
            tile.variant = (patron_god_idx >= 0)
                ? static_cast<uint8_t>(patron_god_idx)
                : static_cast<uint8_t>(rng.range(0, GOD_COUNT - 1));
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
