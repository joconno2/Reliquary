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
#include "components/trap.h"
#include "components/container.h"
#include "core/spritesheet.h"
#include <algorithm>
#include <string>

namespace populate {

// Monster table — row/col in monsters.png
//                                          sheet            sx sy  hp  str dex con dmg arm spd flee xp
static const MonsterDef MONSTER_TABLE[] = {
    // Early game
    {"giant rat",       SHEET_MONSTERS, 11, 6,  6,   6, 14,  6,  2, 0, 130, 40,  10},
    {"bat",             SHEET_MONSTERS,  6, 6,  4,   4, 16,  4,  1, 0, 150, 70,   5},
    {"kobold",          SHEET_MONSTERS,  0, 9,  6,   6, 12,  6,  1, 0, 120, 35,  10},
    {"slime",           SHEET_MONSTERS,  1, 2, 16,   6,  4, 14,  2, 0,  60,  0,  15}, // 3.b big slime
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
    // Mid-late game
    {"goblin shaman",   SHEET_MONSTERS,  6, 0, 14,   8, 10, 10,  3, 0, 100, 30,  35},
    {"wraith",          SHEET_MONSTERS,  2, 5, 25,  10, 14,  8,  6, 0, 110,  0,  50},
    {"bandit",          SHEET_MONSTERS,  3, 0, 16,  12, 16, 10,  4, 1, 120, 25,  30},
    // Late game
    {"ghoul",           SHEET_MONSTERS,  5, 4, 22,  14, 12, 14,  5, 1, 110,  0,  40},
    {"lich",            SHEET_MONSTERS,  2, 4, 50,  10, 10, 12, 12, 0, 100,  0,  80},
    {"death knight",    SHEET_MONSTERS,  3, 4, 65,  18, 12, 16, 14, 4,  90,  0, 100},
    {"manticore",       SHEET_MONSTERS,  3, 6, 35,  16, 14, 14,  7, 2, 110, 10,  70},
    {"minotaur",        SHEET_MONSTERS,  7, 7, 45,  20, 10, 18,  9, 3,  85, 10,  90},
    {"naga",            SHEET_MONSTERS,  4, 7, 30,  14, 16, 12,  6, 1, 120, 15,  60},
    {"dragon",          SHEET_MONSTERS,  2, 8, 90,  22, 12, 22, 18, 5,  80,  0, 200},
    // New monsters from unused sprites
    {"myconid",         SHEET_MONSTERS,  0,10, 20,   8,  6, 14,  4, 2,  60, 10,  30}, // mushroom creature (warrens)
    {"ogre",            SHEET_MONSTERS,  0, 1, 40,  20,  6, 16,  8, 2,  85,  5,  55}, // big brute
    {"golem",           SHEET_MONSTERS,  2, 7, 55,  16,  4, 22, 10, 5,  50,  0,  70}, // stone construct (8.c rock golem)
    {"basilisk",        SHEET_MONSTERS,  4, 8, 30,  12, 10, 14,  5, 2,  90, 10,  55}, // reptile, stun gaze (9.e)
    {"yeti",            SHEET_MONSTERS,  1, 7, 42,  18, 10, 16,  8, 2,  85, 10,  65}, // cold zones (8.b wendigo)
    {"centaur",         SHEET_MONSTERS,  3, 7, 28,  14, 16, 12,  5, 1, 130, 15,  45}, // fast, ranged (8.d centaur)
    {"imp",             SHEET_MONSTERS,  1,11, 12,   6, 18,  6,  3, 0, 140, 50,  20}, // small demon (12.b imp/devil)
    {"gargoyle",        SHEET_MONSTERS,  8, 7, 35,  14, 12, 18,  6, 4,  80,  0,  60}, // stone flyer (8.i harpy)
    {"lizardfolk",      SHEET_MONSTERS,  1, 9, 18,  12, 14, 10,  4, 1, 110, 20,  30}, // swamp dweller
};

static constexpr int MONSTER_COUNT = sizeof(MONSTER_TABLE) / sizeof(MONSTER_TABLE[0]);

const MonsterDef* get_monster_table() { return MONSTER_TABLE; }
int get_monster_count() { return MONSTER_COUNT; }

// Indices into MONSTER_TABLE for thematic filtering
// Undead: skeleton(8), zombie(9), wraith(14), ghoul(16), lich(17), death_knight(18)
static const int UNDEAD_INDICES[] = {8, 9, 14, 16, 17, 18};
static const int UNDEAD_COUNT = 6;
// Fire/volcanic: manticore(19), basilisk(26), gargoyle(30), golem(25), imp(29)
static const int FIRE_INDICES[] = {19, 25, 26, 29, 30};
static const int FIRE_COUNT = 5;
// Late/eldritch: wraith(14), lich(17), death_knight(18), naga(21), golem(25), gargoyle(30)
static const int ELDRITCH_INDICES[] = {14, 17, 18, 21, 25, 30};
static const int ELDRITCH_COUNT = 6;
// Beasts/warrens: giant_rat(0), bat(1), slime(3), giant_spider(5), warg(10), myconid(23)
static const int BEAST_INDICES[] = {0, 1, 3, 5, 10, 23};
static const int BEAST_COUNT = 6;
// Catacombs: skeleton(8), zombie(9), ghoul(16), wraith(14), goblin_shaman(13)
static const int CATACOMB_INDICES[] = {8, 9, 13, 14, 16};
static const int CATACOMB_COUNT = 5;
// Stonekeep/fortress: orc(7), orc_warchief(11), goblin(4), goblin_archer(6), ogre(24), bandit(15)
static const int FORTRESS_INDICES[] = {4, 6, 7, 11, 15, 24};
static const int FORTRESS_COUNT = 6;
// Sunken/swamp: naga(21), lizardfolk(31), slime(3), myconid(23), giant_spider(5)
static const int SWAMP_INDICES[] = {3, 5, 21, 23, 31};
static const int SWAMP_COUNT = 5;
// Deep halls: troll(12), minotaur(20), ogre(24), golem(25), yeti(27)
static const int DEEP_INDICES[] = {12, 20, 24, 25, 27};
static const int DEEP_COUNT = 5;

void spawn_monsters(World& world, const TileMap& map,
                     const std::vector<Room>& rooms, RNG& rng,
                     int dungeon_level,
                     const std::string& zone,
                     int floor_in_dungeon) {
    // Monster pool range scales with dungeon depth
    // Depth 1: indices 0-7 (rats, bats, kobolds, slimes, goblins, spiders)
    // Monster pool unlock: early floors safe, full table at high effective levels
    int max_idx = std::min(MONSTER_COUNT - 1, 4 + dungeon_level * 4);

    // Zone-themed monster selection helper
    // Returns -1 if no themed pick (use general pool)
    auto pick_themed = [&](const std::string& z, int floor) -> int {
        if (z == "sepulchre") {
            if (floor <= 3) return UNDEAD_INDICES[rng.range(0, UNDEAD_COUNT - 1)];
            if (floor <= 6) return FIRE_INDICES[rng.range(0, FIRE_COUNT - 1)];
            return ELDRITCH_INDICES[rng.range(0, ELDRITCH_COUNT - 1)];
        }
        if (z == "warrens") return BEAST_INDICES[rng.range(0, BEAST_COUNT - 1)];
        if (z == "catacombs") return CATACOMB_INDICES[rng.range(0, CATACOMB_COUNT - 1)];
        if (z == "stonekeep") return FORTRESS_INDICES[rng.range(0, FORTRESS_COUNT - 1)];
        if (z == "molten") return FIRE_INDICES[rng.range(0, FIRE_COUNT - 1)];
        if (z == "sunken") return SWAMP_INDICES[rng.range(0, SWAMP_COUNT - 1)];
        if (z == "deep_halls") return DEEP_INDICES[rng.range(0, DEEP_COUNT - 1)];
        return -1;
    };

    bool has_zone_theme = !zone.empty() && pick_themed(zone, floor_in_dungeon) >= 0;

    int dragons_this_floor = 0;

    for (size_t r = 1; r < rooms.size(); r++) {
        auto& room = rooms[r];

        int count = rng.range(2, 3 + dungeon_level / 2);
        for (int i = 0; i < count; i++) {
            int x = rng.range(room.x + 1, room.x + room.w - 2);
            int y = rng.range(room.y + 1, room.y + room.h - 2);

            if (!map.is_walkable(x, y)) continue;

            int idx;
            if (has_zone_theme && rng.chance(65)) {
                // 65% themed, 35% general pool for variety
                idx = pick_themed(zone, floor_in_dungeon);
            } else {
                // General pool roll
                idx = rng.range(0, max_idx);
            }

            // Dragon: max 1 per floor, only depth 3+
            bool is_dragon = (std::string(MONSTER_TABLE[idx].name) == "dragon");
            if (is_dragon && (dragons_this_floor >= 1 || dungeon_level < 3)) {
                idx = rng.range(0, std::min(max_idx, 20)); // reroll to non-dragon
                is_dragon = false;
            }
            if (is_dragon) dragons_this_floor++;

            auto& def = MONSTER_TABLE[idx];

            // Scale HP and damage with depth (steeper for shorter dungeons)
            float hp_scale = 1.0f + dungeon_level * 0.5f;
            float dmg_scale = 1.0f + dungeon_level * 0.35f;
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

            AI ai_comp;
            ai_comp.flee_threshold = def.flee_threshold;

            // Assign behaviors by monster name
            std::string mname(def.name);
            if (mname == "goblin archer") {
                ai_comp.behavior = BehaviorType::ARCHER;
                ai_comp.ranged_range = 5;
                ai_comp.ranged_damage = static_cast<int>(2 * dmg_scale);
            } else if (mname == "lich") {
                ai_comp.behavior = BehaviorType::LICH;
                ai_comp.ranged_range = 6;
                ai_comp.ranged_damage = static_cast<int>(8 * dmg_scale);
            } else if (mname == "troll") {
                ai_comp.behavior = BehaviorType::TROLL;
                ai_comp.regen_per_turn = 2;
            } else if (mname == "minotaur") {
                ai_comp.behavior = BehaviorType::CHARGER;
            } else if (mname == "dragon") {
                ai_comp.behavior = BehaviorType::DRAGON;
                ai_comp.flee_threshold = 0; // dragons don't flee
            } else if (mname == "wolf" || mname == "dire wolf" || mname == "warg") {
                ai_comp.behavior = BehaviorType::PACK;
            } else if (mname == "wraith") {
                ai_comp.behavior = BehaviorType::WRAITH;
            } else if (mname == "death knight") {
                ai_comp.behavior = BehaviorType::NECROMANCER;
                ai_comp.ranged_range = 4;
                ai_comp.ranged_damage = static_cast<int>(10 * dmg_scale);
            } else if (mname == "goblin shaman") {
                ai_comp.behavior = BehaviorType::SHAMAN;
                ai_comp.ranged_range = 5;
                ai_comp.ranged_damage = static_cast<int>(6 * dmg_scale);
            } else if (mname == "bandit") {
                ai_comp.behavior = BehaviorType::THIEF;
            } else if (mname == "centaur") {
                ai_comp.behavior = BehaviorType::ARCHER;
                ai_comp.ranged_range = 6;
                ai_comp.ranged_damage = static_cast<int>(3 * dmg_scale);
            } else if (mname == "ogre") {
                ai_comp.flee_threshold = 5; // ogres don't run
            } else if (mname == "golem") {
                ai_comp.flee_threshold = 0; // golems never flee
            } else if (mname == "imp") {
                ai_comp.behavior = BehaviorType::THIEF; // hit and run
            } else if (mname == "yeti") {
                ai_comp.behavior = BehaviorType::CHARGER; // charges like minotaur
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

// Unique items — hand-authored items with special effects, zone-specific drops.
// These are Legendary rarity with UniqueEffect passives.
struct UniqueDef {
    const char* name;
    const char* description;
    ItemType type;
    EquipSlot slot;
    int sprite_x, sprite_y;
    int damage_bonus, armor_bonus, attack_bonus, dodge_bonus;
    int str_bonus, dex_bonus, con_bonus;
    UniqueEffect effect;
    const char* zone;  // dungeon zone where this can drop ("" = any)
    int min_depth;     // minimum depth to appear
};

static const UniqueDef UNIQUE_TABLE[] = {
    // --- WARRENS (rat tunnels, goblins, tight corridors) ---
    {"Rat King's Fang",
     "A dagger caked in filth. It remembers every throat.",
     ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0,
     4, 0, 3, 0,  0, 2, 0,
     UniqueEffect::EXECUTE_THRESHOLD, "warrens", 2},

    {"Goblin King's Crown",
     "Too small for a human head. Fits anyway.",
     ItemType::ARMOR_HEAD, EquipSlot::HEAD, 5, 15,
     0, 3, 0, 1,  0, 0, 2,
     UniqueEffect::GOLD_FIND, "warrens", 2},

    {"Tunnel Rat's Blade",
     "Forged in darkness. Sees in darkness.",
     ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 0,
     5, 0, 2, 0,  0, 1, 0,
     UniqueEffect::LIGHT_RADIUS, "warrens", 3},

    // --- STONEKEEP (ancient fortifications, skeletons) ---
    {"Bonecleaver",
     "Etched with prayers against the risen dead.",
     ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 3,
     7, 0, 1, 0,  2, 0, 0,
     UniqueEffect::UNDEAD_SLAYER, "stonekeep", 2},

    {"Warden's Plate",
     "Worn by the last keeper of Stonekeep. Thorns line the inside.",
     ItemType::ARMOR_CHEST, EquipSlot::CHEST, 3, 12,
     0, 5, 0, -1,  0, 0, 2,
     UniqueEffect::THORNS, "stonekeep", 3},

    {"Keeper's Lantern",
     "Light that no wind can extinguish.",
     ItemType::AMULET, EquipSlot::AMULET, 3, 16,
     0, 0, 1, 0,  0, 0, 1,
     UniqueEffect::LIGHT_RADIUS, "stonekeep", 2},

    // --- CATACOMBS (undead, bone floors, coffins) ---
    {"Grave Warden's Mace",
     "Made to put the dead back down.",
     ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 5,
     8, 0, 0, 0,  2, 0, 0,
     UniqueEffect::UNDEAD_SLAYER, "catacombs", 3},

    {"Deathless Shroud",
     "The burial cloth of someone who refused to stay buried.",
     ItemType::ARMOR_CHEST, EquipSlot::CHEST, 2, 12,
     0, 2, 0, 2,  0, 0, 0,
     UniqueEffect::DEATHWARD, "catacombs", 4},

    {"Ossuary Ring",
     "Bone polished smooth by centuries of prayer.",
     ItemType::RING, EquipSlot::RING_1, 1, 18,
     0, 1, 0, 0,  0, 0, 2,
     UniqueEffect::REGEN, "catacombs", 3},

    {"Corpselight",
     "A pendant that glows when the dead are near. It never stops glowing here.",
     ItemType::AMULET, EquipSlot::AMULET, 2, 16,
     0, 0, 2, 0,  0, 0, 0,
     UniqueEffect::CORPSE_EXPLODE, "catacombs", 4},

    // --- MOLTEN (fire, lava, dragons) ---
    {"Cinderscale Shield",
     "Dragonhide stretched over iron. Still warm.",
     ItemType::SHIELD, EquipSlot::OFF_HAND, 3, 11,
     0, 5, 0, 0,  0, 0, 2,
     UniqueEffect::THORNS, "molten", 4},

    {"Flamecaller",
     "The blade sweats heat. Enemies nearby flinch.",
     ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0,
     8, 0, 1, 0,  0, 0, 0,
     UniqueEffect::CHAIN_LIGHTNING, "molten", 4},

    {"Ashen Crown",
     "Forged in eruption. The wearer sees through smoke and stone.",
     ItemType::ARMOR_HEAD, EquipSlot::HEAD, 4, 15,
     0, 3, 0, 0,  0, 0, 0,
     UniqueEffect::LIGHT_RADIUS, "molten", 3},

    // --- SUNKEN (water, flooded rooms, naga) ---
    {"Tidecaller",
     "A trident that hums near water. Lightning follows.",
     ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 6,
     7, 0, 2, 0,  0, 2, 0,
     UniqueEffect::CHAIN_LIGHTNING, "sunken", 3},

    {"Drowned Man's Ring",
     "Pried from a waterlogged corpse. It wants to keep you alive.",
     ItemType::RING, EquipSlot::RING_1, 2, 18,
     0, 0, 0, 1,  0, 0, 2,
     UniqueEffect::REGEN, "sunken", 3},

    {"Naiad's Veil",
     "Woven from river kelp. Traps slide off you like water.",
     ItemType::ARMOR_HEAD, EquipSlot::HEAD, 1, 15,
     0, 1, 0, 2,  0, 1, 0,
     UniqueEffect::TRAP_IMMUNITY, "sunken", 4},

    // --- DEEP HALLS (cavernous, minotaurs, darkness) ---
    {"Minotaur's Cleaver",
     "Bigger than it should be. Hits harder than it should.",
     ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 3,
     10, 0, -2, 0,  3, 0, 0,
     UniqueEffect::EXECUTE_THRESHOLD, "deep_halls", 4},

    {"Deepstone Gauntlets",
     "Stone that moves like leather. The mountain's gift.",
     ItemType::ARMOR_HANDS, EquipSlot::HANDS, 3, 13,
     0, 3, 1, 0,  1, 0, 2,
     UniqueEffect::TRAP_IMMUNITY, "deep_halls", 3},

    {"Delver's Eye",
     "A jewel from the deep. It knows things it shouldn't.",
     ItemType::AMULET, EquipSlot::AMULET, 5, 16,
     0, 0, 0, 0,  0, 0, 0,
     UniqueEffect::IDENTIFY_ON_PICKUP, "deep_halls", 2},

    // --- SEPULCHRE (final dungeon, death, endgame) ---
    {"The Terminus",
     "The last blade ever forged. Its edge is the end of things.",
     ItemType::WEAPON, EquipSlot::MAIN_HAND, 5, 0,
     12, 0, 2, 0,  0, 0, 0,
     UniqueEffect::EXECUTE_THRESHOLD, "sepulchre", 5},

    {"Death's Mantle",
     "Sewn from the dark between stars.",
     ItemType::ARMOR_CHEST, EquipSlot::CHEST, 4, 12,
     0, 6, 0, 1,  0, 0, 0,
     UniqueEffect::DEATHWARD, "sepulchre", 5},

    {"The Hungering Ring",
     "It feeds on death. So do you.",
     ItemType::RING, EquipSlot::RING_1, 5, 17,
     2, 0, 1, 0,  0, 0, 0,
     UniqueEffect::XP_BONUS, "sepulchre", 4},

    // --- ANY ZONE (general purpose, rarer) ---
    {"Scholar's Lens",
     "Ground from crystal found only in collapsed libraries.",
     ItemType::AMULET, EquipSlot::AMULET, 6, 16,
     0, 0, 1, 0,  0, 0, 0,
     UniqueEffect::IDENTIFY_ON_PICKUP, "", 3},

    {"Pilgrim's Sash",
     "Blessed by seven temples. The gods listen closer.",
     ItemType::ARMOR_CHEST, EquipSlot::CHEST, 0, 12,
     0, 1, 0, 0,  0, 0, 0,
     UniqueEffect::FAVOR_DOUBLED, "", 4},

    {"Poacher's Knife",
     "Notched from a hundred kills. Knows where to cut a beast.",
     ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0,
     4, 0, 3, 0,  0, 2, 0,
     UniqueEffect::BEAST_SLAYER, "", 2},

    {"Miser's Band",
     "Gold sticks to whoever wears this.",
     ItemType::RING, EquipSlot::RING_1, 1, 17,
     0, 0, 0, 0,  0, 0, 0,
     UniqueEffect::GOLD_FIND, "", 2},

    {"Channeler's Staff",
     "The wood is warm. It drinks mana from the air.",
     ItemType::WEAPON, EquipSlot::MAIN_HAND, 4, 10,
     4, 0, 2, 0,  0, 0, 0,
     UniqueEffect::FREE_CAST, "", 4},

    {"Shadowstep Boots",
     "They make no sound. Neither does anything that steps on them.",
     ItemType::ARMOR_FEET, EquipSlot::FEET, 3, 14,
     0, 2, 0, 3,  0, 2, 0,
     UniqueEffect::TRAP_IMMUNITY, "", 4},

    // --- UNIQUE RINGS ---
    {"Bloodthorn Ring",
     "Thorns grow inward. Every critical hit opens a wound that won't close.",
     ItemType::RING, EquipSlot::RING_1, 3, 18,
     1, 0, 1, 0,  0, 1, 0,
     UniqueEffect::CRIT_BLEED, "", 3},

    {"Coward's Loop",
     "Worn by a duelist who never lost. He fought dirty.",
     ItemType::RING, EquipSlot::RING_1, 4, 18,
     0, 0, 0, 3,  0, 2, 0,
     UniqueEffect::DODGE_COUNTER, "stonekeep", 3},

    {"Slayer's Band",
     "Warm to the touch after a kill. You move faster. You want another.",
     ItemType::RING, EquipSlot::RING_1, 5, 18,
     2, 0, 1, 0,  1, 0, 0,
     UniqueEffect::KILL_HASTE, "deep_halls", 4},

    {"Wychwood Ring",
     "Carved from a tree that grew in a graveyard. No venom touches you.",
     ItemType::RING, EquipSlot::RING_1, 0, 18,
     0, 1, 0, 0,  0, 0, 2,
     UniqueEffect::POISON_IMMUNE, "warrens", 2},

    {"Charred Signet",
     "The seal of a fire cult. The brand is permanent. So is the protection.",
     ItemType::RING, EquipSlot::RING_1, 1, 18,
     0, 1, 0, 0,  0, 0, 1,
     UniqueEffect::FIRE_IMMUNE, "molten", 3},

    {"Phasing Band",
     "The metal shifts when you aren't looking. Sometimes you shift with it.",
     ItemType::RING, EquipSlot::RING_1, 2, 18,
     1, 0, 2, 0,  0, 1, 0,
     UniqueEffect::TELEPORT_STRIKE, "sepulchre", 4},

    // --- UNIQUE AMULETS ---
    {"Medallion of Devotion",
     "Prayer heals the body when the faith is strong enough.",
     ItemType::AMULET, EquipSlot::AMULET, 4, 16,
     0, 1, 0, 0,  0, 0, 1,
     UniqueEffect::PRAYER_HEAL, "", 3},

    {"Mana Phylactery",
     "A lich's failed experiment. It drinks damage and turns it to power.",
     ItemType::AMULET, EquipSlot::AMULET, 5, 16,
     0, 0, 0, 0,  0, 0, 0,
     UniqueEffect::MP_SHIELD, "catacombs", 4},

    {"Shadow Pendant",
     "Cold to the touch. Wounds close when no one is watching.",
     ItemType::AMULET, EquipSlot::AMULET, 1, 16,
     0, 0, 0, 1,  0, 1, 0,
     UniqueEffect::STEALTH_REGEN, "", 3},

    {"Dread Gorget",
     "Something in the metal screams. Enemies hear it too.",
     ItemType::AMULET, EquipSlot::AMULET, 0, 16,
     1, 2, 0, 0,  1, 0, 0,
     UniqueEffect::FEAR_AURA, "deep_halls", 4},
};
static constexpr int UNIQUE_COUNT = sizeof(UNIQUE_TABLE) / sizeof(UNIQUE_TABLE[0]);

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
    // Quality tiers scaled for 4-floor dungeons
    int max_tier = 0;
    if (dungeon_level >= 4) max_tier = 3;      // Masterwork on final floor
    else if (dungeon_level >= 3) max_tier = 2;  // Superior on floor 3
    else if (dungeon_level >= 2) max_tier = 1;  // Fine on floor 2

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
        item.type != ItemType::ARMOR_HANDS && item.type != ItemType::SHIELD) return;

    // 10% cursed, 8% blessed
    int roll = rng.range(1, 100);
    if (roll <= 10) {
        item.curse_state = 1; // cursed (can't unequip)
        // Cursed items have slightly better stats as bait
        if (item.type == ItemType::WEAPON)
            item.damage_bonus += 1;
        else
            item.armor_bonus += 1;
        // Cursed items are always unidentified (you can't see the curse)
        item.identified = false;
        item.unid_name = "glowing " + std::string(item.name);
    } else if (roll <= 18) {
        item.curse_state = 2; // blessed
        item.name = "Blessed " + item.name;
        if (item.type == ItemType::WEAPON)
            item.damage_bonus += 1;
        else
            item.armor_bonus += 1;
        // Blessed items also start unidentified (reward for identifying)
        item.identified = false;
        item.unid_name = "glowing " + std::string(item.name);
    }
}

// Make items with affixes unidentified (they have hidden bonuses worth discovering)
static void apply_identification(Item& item, RNG& rng) {
    // Items that already have curse/bless state are handled in apply_curse_bless
    if (item.curse_state != 0) return;
    // Items with affixes start unidentified
    if (!item.affixes.empty()) {
        item.identified = false;
        // Generic unid name based on type
        std::string base = item.name;
        if (item.type == ItemType::WEAPON) item.unid_name = base;
        else if (item.type == ItemType::ARMOR_CHEST) item.unid_name = base;
        else if (item.type == ItemType::RING) item.unid_name = "ring";
        else if (item.type == ItemType::AMULET) item.unid_name = "amulet";
        else item.unid_name = base;
    }
}

// Assign material based on dungeon depth
static void apply_material(Item& item, int dungeon_level, RNG& rng) {
    // Only melee and ranged weapons get materials — not armor, staves, rings, amulets
    if (item.type != ItemType::WEAPON) return;

    // Depth-based material table (scaled for 4-floor dungeons)
    MaterialType mat = MaterialType::IRON; // default

    if (dungeon_level <= 1) {
        int roll = rng.range(1, 100);
        if (roll <= 15) mat = MaterialType::BONE;
        else if (roll <= 25) mat = MaterialType::WOOD;
        else mat = MaterialType::IRON;
    } else if (dungeon_level <= 2) {
        int roll = rng.range(1, 100);
        if (roll <= 30) mat = MaterialType::IRON;
        else if (roll <= 65) mat = MaterialType::STEEL;
        else mat = MaterialType::SILVER;
    } else if (dungeon_level <= 3) {
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

// Roll rarity and apply random affixes based on dungeon depth
static void apply_affixes(Item& item, int dungeon_level, RNG& rng) {
    // Only equippable gear gets affixes
    if (item.type == ItemType::POTION || item.type == ItemType::FOOD ||
        item.type == ItemType::SCROLL || item.type == ItemType::GOLD ||
        item.type == ItemType::KEY || item.type == ItemType::PET)
        return;

    // Don't affix items that are already legendary/relic
    if (item.relic_god >= 0) return;

    // Rarity roll scaled for 4-floor dungeons
    int roll = rng.range(1, 100);
    Rarity rarity = Rarity::COMMON;

    if (dungeon_level <= 1) {
        if (roll > 85) rarity = Rarity::MAGIC;
    } else if (dungeon_level <= 2) {
        if (roll > 90) rarity = Rarity::RARE;
        else if (roll > 60) rarity = Rarity::MAGIC;
    } else if (dungeon_level <= 3) {
        if (roll > 80) rarity = Rarity::RARE;
        else if (roll > 40) rarity = Rarity::MAGIC;
    } else {
        if (roll > 70) rarity = Rarity::RARE;
        else if (roll > 25) rarity = Rarity::MAGIC;
    }

    if (rarity == Rarity::COMMON) return;

    item.rarity = rarity;

    bool is_weapon = (item.type == ItemType::WEAPON);
    bool is_armor = (item.type == ItemType::ARMOR_HEAD || item.type == ItemType::ARMOR_CHEST ||
                     item.type == ItemType::ARMOR_HANDS || item.type == ItemType::ARMOR_FEET ||
                     item.type == ItemType::SHIELD);

    // Roll prefix (magic = prefix OR suffix, rare = both)
    auto roll_affix = [&](const AffixDef* table, int count) -> Affix {
        // Build eligible list
        std::vector<int> eligible;
        for (int i = 0; i < count; i++) {
            auto& ad = table[i];
            if (ad.min_depth > dungeon_level) continue;
            if (ad.weapons_only && !is_weapon) continue;
            if (ad.armor_only && !is_armor) continue;
            eligible.push_back(i);
        }
        if (eligible.empty()) return {};

        int pick = eligible[rng.range(0, static_cast<int>(eligible.size()) - 1)];
        auto& ad = table[pick];
        Affix a;
        a.name = ad.name;
        a.effect = ad.effect;
        a.magnitude = rng.range(ad.min_mag, ad.max_mag);
        a.is_prefix = ad.is_prefix;
        return a;
    };

    if (rarity == Rarity::MAGIC) {
        // 50/50 prefix or suffix
        if (rng.chance(50)) {
            Affix a = roll_affix(PREFIX_TABLE, PREFIX_COUNT);
            if (a.effect != AffixEffect::NONE) item.affixes.push_back(a);
        } else {
            Affix a = roll_affix(SUFFIX_TABLE, SUFFIX_COUNT);
            if (a.effect != AffixEffect::NONE) item.affixes.push_back(a);
        }
    } else {
        // Rare: one prefix + one suffix
        Affix pre = roll_affix(PREFIX_TABLE, PREFIX_COUNT);
        if (pre.effect != AffixEffect::NONE) item.affixes.push_back(pre);
        Affix suf = roll_affix(SUFFIX_TABLE, SUFFIX_COUNT);
        if (suf.effect != AffixEffect::NONE) item.affixes.push_back(suf);
    }

    // If we didn't actually get any affixes, revert to common
    if (item.affixes.empty()) {
        item.rarity = Rarity::COMMON;
        return;
    }

    // Build cached stat bonuses
    item.rebuild_affix_cache();

    // Affix stat bonuses add to the item's base stats
    item.damage_bonus += item.affix_damage;
    item.armor_bonus  += item.affix_armor;
    item.attack_bonus += item.affix_attack;
    item.dodge_bonus  += item.affix_dodge;
    item.str_bonus    += item.affix_str;
    item.dex_bonus    += item.affix_dex;
    item.con_bonus    += item.affix_con;

    // Bump gold value for magic/rare items
    int rarity_mult = (rarity == Rarity::RARE) ? 3 : 2;
    item.gold_value = item.gold_value * rarity_mult;
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
            apply_affixes(item, dungeon_level, rng);
            apply_identification(item, rng);
        } else if (roll <= 21) {
            // Ranged weapon
            int idx = weighted_item_pick(rng, dungeon_level, RANGED_COUNT, 3);
            Entity e = create_item_from_def(world, RANGED_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_material(item, dungeon_level, rng);
            apply_tags(item);
            apply_quality(item, dungeon_level, rng);
            apply_affixes(item, dungeon_level, rng);
            apply_identification(item, rng);
        } else if (roll <= 25) {
            // Staff (mage weapon)
            int idx = weighted_item_pick(rng, dungeon_level, STAFF_COUNT, 3);
            Entity e = create_item_from_def(world, STAFF_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_tags(item);
            apply_affixes(item, dungeon_level, rng);
            apply_identification(item, rng);
        } else if (roll <= 44) {
            // Armor — no material, the sprite IS the tier
            int idx = weighted_item_pick(rng, dungeon_level, ARMOR_COUNT, 2);
            Entity e = create_item_from_def(world, ARMOR_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_tags(item);
            apply_quality(item, dungeon_level, rng);
            apply_curse_bless(item, dungeon_level, rng);
            apply_affixes(item, dungeon_level, rng);
            apply_identification(item, rng);
        } else if (roll <= 56) {
            // Spellbook — teaches a random spell (increased: tomes are primary spell source)
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
        } else if (roll <= 60 && dungeon_level >= 2) {
            // Amulet — depth 2+
            int idx = weighted_item_pick(rng, dungeon_level, AMULET_COUNT, 3);
            Entity e = create_item_from_def(world, AMULET_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_curse_bless(item, dungeon_level, rng);
            apply_affixes(item, dungeon_level, rng);
            apply_identification(item, rng);
        } else if (roll <= 64 && dungeon_level >= 2) {
            // Ring — depth 2+
            int idx = weighted_item_pick(rng, dungeon_level, RING_COUNT, 3);
            Entity e = create_item_from_def(world, RING_TABLE[idx], x, y);
            auto& item = world.get<Item>(e);
            apply_curse_bless(item, dungeon_level, rng);
            apply_affixes(item, dungeon_level, rng);
            apply_identification(item, rng);
        } else if (roll <= 67) {
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
        } else if (roll <= 75) {
            // Consumable (reduced from 85 to make potions scarcer)
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
    item.rarity = Rarity::RELIC;
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
    item.rarity = Rarity::LEGENDARY;
    world.add<Item>(e, std::move(item));
    return e;
}

Entity spawn_unique(World& world, const std::vector<Room>& rooms, RNG& rng,
                     int dungeon_level, const std::string& zone) {
    if (rooms.size() < 3) return NULL_ENTITY;

    // Build eligible unique list for this zone and depth
    std::vector<int> eligible;
    for (int i = 0; i < UNIQUE_COUNT; i++) {
        auto& ud = UNIQUE_TABLE[i];
        if (dungeon_level < ud.min_depth) continue;
        // Zone match: empty string = any zone, otherwise must match
        if (ud.zone[0] != '\0' && zone != ud.zone) continue;
        eligible.push_back(i);
    }
    if (eligible.empty()) return NULL_ENTITY;

    // Pick one at random
    int pick = eligible[rng.range(0, static_cast<int>(eligible.size()) - 1)];
    auto& ud = UNIQUE_TABLE[pick];

    // Place in a mid-to-late room (not first, not last which is for legendaries/relics)
    int room_idx = rng.range(static_cast<int>(rooms.size()) / 2,
                              static_cast<int>(rooms.size()) - 2);
    if (room_idx < 1) room_idx = 1;
    auto& room = rooms[room_idx];
    int x = room.cx() + rng.range(-1, 1);
    int y = room.cy() + rng.range(-1, 1);

    Entity e = world.create();
    world.add<Position>(e, {x, y});
    world.add<Renderable>(e, {SHEET_ITEMS, ud.sprite_x, ud.sprite_y,
                               {255, 200, 100, 255}, 2}); // warm gold tint, high z

    Item item;
    item.name = ud.name;
    item.description = ud.description;
    item.type = ud.type;
    item.slot = ud.slot;
    item.damage_bonus = ud.damage_bonus;
    item.armor_bonus = ud.armor_bonus;
    item.attack_bonus = ud.attack_bonus;
    item.dodge_bonus = ud.dodge_bonus;
    item.str_bonus = ud.str_bonus;
    item.dex_bonus = ud.dex_bonus;
    item.con_bonus = ud.con_bonus;
    item.gold_value = 0; // priceless
    item.identified = true;
    item.unique_effect = ud.effect;
    item.rarity = Rarity::LEGENDARY;
    apply_tags(item);
    world.add<Item>(e, std::move(item));
    return e;
}

void spawn_doodads(World& world, TileMap& map,
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
    bool is_sunken = (zone == "sunken");
    bool is_warrens = (zone == "warrens");
    bool is_deep_halls = (zone == "deep_halls");
    bool is_sepulchre = (zone == "sepulchre");
    bool is_stonekeep = (zone == "stonekeep");

    // Sepulchre hazards: lava on floors 5-6, deep water on floors 7-9
    if (is_sepulchre && dungeon_level >= 5) {
        TileType hazard = (dungeon_level <= 6) ? TileType::LAVA : TileType::DEEP_WATER;
        for (size_t r = 2; r < rooms.size(); r++) {
            if (!rng.chance(50)) continue; // 50% of rooms get hazards
            auto& hr = rooms[r];
            int cx = rng.range(hr.x + 2, hr.x + hr.w - 3);
            int cy = rng.range(hr.y + 2, hr.y + hr.h - 3);
            for (int ti = 0; ti < rng.range(3, 6); ti++) {
                int hx = cx + rng.range(-1, 1);
                int hy = cy + rng.range(-1, 1);
                if (map.in_bounds(hx, hy) && map.is_walkable(hx, hy))
                    map.at(hx, hy).type = hazard;
            }
        }
    }

    // Hazard terrain: lava in molten zones, deep water in sunken zones
    if (is_molten || is_sunken) {
        TileType hazard = is_molten ? TileType::LAVA : TileType::DEEP_WATER;
        // Hazard pools: larger and more frequent
        for (size_t r = 2; r < rooms.size(); r++) {
            if (!rng.chance(60)) continue; // 60% of rooms get hazards
            auto& hr = rooms[r];
            int cx = rng.range(hr.x + 2, hr.x + hr.w - 3);
            int cy = rng.range(hr.y + 2, hr.y + hr.h - 3);
            for (int ti = 0; ti < rng.range(4, 8); ti++) {
                int hx = cx + rng.range(-1, 1);
                int hy = cy + rng.range(-1, 1);
                if (map.in_bounds(hx, hy) && map.is_walkable(hx, hy))
                    map.at(hx, hy).type = hazard;
            }
        }
    }

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
            world.add<Renderable>(e, {sheet, sx, sy, {255,255,255,255}, 0});
        };

        // === FURNITURE CLUSTERS: grouped doodads that look intentional ===
        // Rooms large enough (>= 7x7) get a furniture cluster 40% of the time
        if (room.w >= 7 && room.h >= 7 && rng.chance(40)) {
            // Pick cluster center (avoiding edges)
            int cx = rng.range(room.x + 2, room.x + room.w - 3);
            int cy = rng.range(room.y + 2, room.y + room.h - 3);
            auto place_at = [&](int ox, int oy, int sx, int sy) {
                int px = cx + ox, py = cy + oy;
                if (map.is_walkable(px, py)) {
                    Entity e = world.create();
                    world.add<Position>(e, {px, py});
                    world.add<Renderable>(e, {SHEET_TILES, sx, sy, {255,255,255,255}, 0});
                }
            };
            int cluster_type = rng.range(0, 5);
            switch (cluster_type) {
                case 0: // Storage: barrels + sack
                    place_at(0, 0, 4, 17); place_at(1, 0, 4, 17);
                    place_at(0, 1, 5, 17); break;
                case 1: // Burial: coffin + bones + blood
                    place_at(0, 0, rng.range(0, 2), 23);
                    place_at(1, 0, rng.range(0, 1), 21);
                    place_at(0, 1, rng.range(0, 1), 22); break;
                case 2: // Camp: log pile + barrel + mushroom
                    place_at(0, 0, 6, 17); place_at(1, 0, 4, 17);
                    place_at(-1, 0, 0, 20); break;
                case 3: // Rubble: rocks + bones
                    place_at(0, 0, 0, 18); place_at(1, 0, 1, 18);
                    place_at(0, 1, rng.range(0, 1), 21); break;
                case 4: // Shrine: sarcophagus + candles (bones flanking)
                    place_at(0, 0, 3, 23);
                    place_at(-1, 0, rng.range(0, 1), 21);
                    place_at(1, 0, rng.range(0, 1), 21); break;
                case 5: // Supplies: ore + barrel + log
                    place_at(0, 0, 5, 17); place_at(1, 0, 5, 17);
                    place_at(0, 1, 6, 17); place_at(1, 1, 4, 17); break;
            }
        }

        // === GUARANTEED MINIMUM: every room gets 1-2 doodads ===
        int min_doodads = 1 + rng.range(0, 1);
        for (int md = 0; md < min_doodads; md++) {
            int pick = rng.range(0, 3);
            if (pick == 0) place_decor(0, 20);       // small mushroom
            else if (pick == 1) place_decor(rng.range(0, 1), 22); // blood/scatter
            else if (pick == 2) place_decor(4, 17);  // barrel
            else place_decor(rng.range(0, 1), 21);   // bones
        }

        // === ZONE-SPECIFIC DOODADS (stacked on top of minimum) ===

        if (is_warrens) {
            // Damp, filthy tunnels: slime, mushrooms, scattered bones
            place_decor(rng.range(2, 3), 22); // slime always
            if (rng.chance(40)) place_decor(0, 20); // more mushrooms
            if (rng.chance(30)) place_decor(rng.range(2, 3), 22); // more slime
            if (rng.chance(20)) place_decor(rng.range(0, 1), 21); // bones
        } else if (is_stonekeep) {
            // Fortress: barrels, weapon racks, banners
            if (rng.chance(40)) place_decor(4, 17); // barrel
            if (rng.chance(30)) place_decor(5, 17); // ore sack (supplies)
            if (rng.chance(25)) place_decor(6, 17); // log pile
            if (rng.chance(20)) place_decor(rng.range(0, 1), 21); // old bones
        } else if (is_catacombs) {
            // Burial grounds: coffins, bones, sarcophagi everywhere
            place_decor(rng.range(0, 2), 23); // coffin always
            place_decor(rng.range(0, 1), 21); // bones always
            if (rng.chance(50)) place_decor(rng.range(0, 2), 23); // more coffins
            if (rng.chance(40)) place_decor(3, 23); // sarcophagus
            if (rng.chance(30)) place_decor(rng.range(0, 1), 22); // blood
        } else if (is_molten) {
            // Volcanic: ore, slag, heat shimmer (multiple ore sacks)
            place_decor(5, 17); // ore sack always
            if (rng.chance(50)) place_decor(5, 17); // more ore
            if (rng.chance(40)) place_decor(rng.range(0, 1), 22); // slag/scorch
            if (rng.chance(30)) place_decor(4, 17); // barrel (supplies)
        } else if (is_sunken) {
            // Flooded: mushrooms, slime, aquatic growth
            place_decor(0, 20); // mushroom always
            place_decor(1, 20); // large mushroom always
            if (rng.chance(50)) place_decor(rng.range(2, 3), 22); // slime
            if (rng.chance(40)) place_decor(0, 20); // more mushrooms
        } else if (is_deep_halls) {
            // Cavernous: rocks, rubble, crystal formations
            place_decor(rng.range(0, 1), 18); // large rock always
            if (rng.chance(50)) place_decor(rng.range(0, 1), 18); // more rocks
            if (rng.chance(30)) place_decor(0, 20); // cave mushroom
            if (rng.chance(20)) place_decor(4, 17); // fallen barrel
        } else if (is_sepulchre) {
            // The final dungeon: DENSE dread (bones, coffins, blood everywhere)
            int density = 2 + dungeon_level / 3;
            for (int dd = 0; dd < density; dd++) {
                if (rng.chance(60)) place_decor(rng.range(0, 1), 21);
                if (rng.chance(40)) place_decor(rng.range(0, 2), 23);
                if (rng.chance(30)) place_decor(rng.range(0, 1), 22);
            }
        }

        // Blood splatters scale with depth (all zones)
        if (rng.chance(15 + dungeon_level * 4)) place_decor(rng.range(0, 1), 22);

        // Catacombs: extra bone piles
        if (is_catacombs && rng.chance(20)) place_decor(rng.range(0, 1), 21); // corpse bones

        // Zone-exclusive rare spell tomes (~8% per room in matching zone)
        {
            SpellId zone_spell = SpellId::COUNT;
            if (is_molten && rng.chance(8))
                zone_spell = rng.chance(50) ? SpellId::METEOR : SpellId::FIREBALL;
            else if (is_catacombs && rng.chance(8))
                zone_spell = rng.chance(50) ? SpellId::RAISE_DEAD : SpellId::DOOM;
            else if (is_sunken && rng.chance(8))
                zone_spell = rng.chance(50) ? SpellId::FROST_NOVA : SpellId::ICE_SHARD;
            else if (is_deep_halls && rng.chance(8))
                zone_spell = rng.chance(50) ? SpellId::EARTHQUAKE : SpellId::IRON_BODY;
            else if (is_sepulchre && rng.chance(8))
                zone_spell = rng.chance(50) ? SpellId::DISINTEGRATE : SpellId::SOUL_REND;
            else if (is_warrens && rng.chance(8))
                zone_spell = rng.chance(50) ? SpellId::POISON_CLOUD : SpellId::ACID_SPLASH;

            if (zone_spell != SpellId::COUNT) {
                int x = rng.range(room.x + 1, room.x + room.w - 2);
                int y = rng.range(room.y + 1, room.y + room.h - 2);
                if (map.is_walkable(x, y)) {
                    auto& sinfo = get_spell_info(zone_spell);
                    Entity e = world.create();
                    world.add<Position>(e, {x, y});
                    world.add<Renderable>(e, {SHEET_ITEMS, 2, 21, {255, 240, 200, 255}, 1});
                    Item book;
                    book.name = std::string("Tome of ") + sinfo.name;
                    book.description = sinfo.description;
                    book.type = ItemType::SCROLL;
                    book.gold_value = 50 + sinfo.mp_cost * 5;
                    book.identified = true;
                    book.teaches_spell = static_cast<int>(zone_spell);
                    book.tags |= TAG_BOOK;
                    world.add<Item>(e, std::move(book));
                }
            }
        }

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
        // Molten zones get more light; catacombs/warrens get less
        int torch_chance = is_molten ? 70 : is_sepulchre ? 30 : (is_catacombs || is_warrens) ? 40 : 50;
        if (rng.chance(torch_chance)) {
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

    // Unlit torches / dead campfires — atmospheric in darker zones
    // Row 4 = unlit torch, Row 2 = dead campfire, Row 0 = dead plant
    if (is_catacombs || is_warrens || is_sepulchre) {
        for (size_t r = 2; r < rooms.size(); r++) {
            if (!rng.chance(25)) continue;
            auto& room = rooms[r];
            for (int a = 0; a < 10; a++) {
                int x = rng.range(room.x + 1, room.x + room.w - 2);
                int y = rng.range(room.y + 1, room.y + room.h - 2);
                if (!map.is_walkable(x, y)) continue;
                bool adj = false;
                for (auto [dx,dy] : std::initializer_list<std::pair<int,int>>{{-1,0},{1,0},{0,-1},{0,1}}) {
                    int nx = x+dx, ny = y+dy;
                    if (map.in_bounds(nx, ny) && !map.is_walkable(nx, ny)) adj = true;
                }
                if (!adj) continue;
                Entity e = world.create();
                world.add<Position>(e, {x, y});
                int row = rng.chance(60) ? 4 : 2; // unlit torch or dead campfire
                world.add<Renderable>(e, {SHEET_ANIMATED, 0, row, {180, 170, 160, 255}, 0});
                break;
            }
        }
    }

    // Glowing crystals — deep halls and molten zones (row 8 of animated sheet)
    if (is_deep_halls || is_molten) {
        for (size_t r = 1; r < rooms.size(); r++) {
            if (!rng.chance(15)) continue;
            auto& room = rooms[r];
            int x = rng.range(room.x + 1, room.x + room.w - 2);
            int y = rng.range(room.y + 1, room.y + room.h - 2);
            if (!map.is_walkable(x, y)) continue;
            Entity e = world.create();
            world.add<Position>(e, {x, y});
            world.add<Renderable>(e, {SHEET_ANIMATED, 0, 8, {255, 255, 255, 255}, 0});
        }
    }

    // Saplings in overworld (row 25 col 0 of tiles sheet) — near forests
    // (handled by overworld generation, not dungeon doodads)

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

    { AI boss_ai; boss_ai.flee_threshold = 0; world.add<AI>(e, boss_ai); } // bosses don't flee
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

    // Depth scaling for 4-floor dungeons (paragons appear floor 3-4)
    float scale = 1.0f + (dungeon_level - 2) * 0.25f;

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

    { AI para_ai; para_ai.flee_threshold = 10; world.add<AI>(e, para_ai); } // paragons flee early
    world.add<Energy>(e, {0, def.speed});
    world.add<GodAlignment>(e, {def.god, 50}); // high favor — devout champion

    return e;
}

void spawn_traps(World& world, const TileMap& map,
                  const std::vector<Room>& rooms, RNG& rng,
                  int dungeon_level) {
    // 1-3 traps per floor, scaling with depth
    int trap_count = rng.range(1, 2 + dungeon_level / 2);
    if (trap_count > 6) trap_count = 6;

    // Trap type weights scale with depth
    for (int i = 0; i < trap_count; i++) {
        // Pick a random room (skip room 0 = starting room)
        if (rooms.size() <= 1) break;
        size_t ri = rng.range(1, static_cast<int>(rooms.size()) - 1);
        auto& room = rooms[ri];

        // Place in corridor-adjacent tile or room interior
        int x = rng.range(room.x + 1, room.x + room.w - 2);
        int y = rng.range(room.y + 1, room.y + room.h - 2);
        if (!map.is_walkable(x, y)) continue;

        // Pick trap type
        TrapType type;
        int roll = rng.range(0, 99);
        if (dungeon_level <= 2) {
            // Early: mostly spikes and pits
            if (roll < 40) type = TrapType::SPIKE;
            else if (roll < 70) type = TrapType::PIT;
            else if (roll < 85) type = TrapType::DART;
            else type = TrapType::BEAR_TRAP;
        } else if (dungeon_level <= 4) {
            if (roll < 25) type = TrapType::SPIKE;
            else if (roll < 45) type = TrapType::PIT;
            else if (roll < 60) type = TrapType::DART;
            else if (roll < 75) type = TrapType::ALARM;
            else if (roll < 90) type = TrapType::BEAR_TRAP;
            else type = TrapType::POISON_GAS;
        } else {
            if (roll < 15) type = TrapType::SPIKE;
            else if (roll < 30) type = TrapType::PIT;
            else if (roll < 45) type = TrapType::DART;
            else if (roll < 60) type = TrapType::ALARM;
            else if (roll < 75) type = TrapType::BEAR_TRAP;
            else type = TrapType::POISON_GAS;
        }

        int dmg = 3 + dungeon_level * 2;
        int difficulty = 10 + dungeon_level;

        Entity trap = world.create();
        world.add<Position>(trap, {x, y});

        Trap t;
        t.type = type;
        t.damage = dmg;
        t.difficulty = difficulty;
        trap_sprite(type, false, t.sprite_x, t.sprite_y);
        world.add<Trap>(trap, t);
    }
}

} // namespace populate
