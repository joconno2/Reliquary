#pragma once
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/rng.h"
#include "components/god.h"
#include "generation/dungeon.h"
#include <vector>

namespace populate {

// Spawn monsters in dungeon rooms (skip the first room — that's the player's)
void spawn_monsters(World& world, const TileMap& map,
                     const std::vector<Room>& rooms, RNG& rng,
                     int dungeon_level = 1);

// Spawn items on the ground in dungeon rooms
void spawn_items(World& world, const TileMap& map,
                  const std::vector<Room>& rooms, RNG& rng,
                  int dungeon_level = 1);

// Spawn a boss monster in the last room. Returns the entity.
Entity spawn_boss(World& world, const TileMap& map,
                   const std::vector<Room>& rooms, const char* name,
                   int sheet, int sx, int sy,
                   int hp, int str, int dex, int con,
                   int dmg, int armor, int speed, int xp_value);

// Spawn dungeon doodads (chests, jars, mushrooms, coffins, blood splatters, god shrines)
void spawn_doodads(World& world, TileMap& map,
                    const std::vector<Room>& rooms, RNG& rng,
                    int dungeon_level, const std::string& zone = "",
                    int patron_god_idx = -1);

// Spawn a rival paragon (PC-like enemy with god affiliation).
// Returns the entity, or NULL_ENTITY if no suitable room/spawn.
// player_god: don't spawn a paragon of the player's own god.
Entity spawn_paragon(World& world, const TileMap& map,
                      const std::vector<Room>& rooms, RNG& rng,
                      int dungeon_level, GodId player_god);

// Spawn a god relic on the bottom floor of a dungeon with a patron god.
// One relic per run, late-game only (effective level 6+).
Entity spawn_relic(World& world, const std::vector<Room>& rooms, RNG& rng,
                    int patron_god_idx);

// Spawn a legendary item on the bottom floor of a named dungeon.
// Returns the entity, or NULL_ENTITY if this dungeon has no legendary.
Entity spawn_legendary(World& world, const std::vector<Room>& rooms, RNG& rng,
                        const std::string& dungeon_name);

// Spawn a unique item in a dungeon (zone-specific, chance-based).
// Returns the entity, or NULL_ENTITY if no eligible unique or failed roll.
Entity spawn_unique(World& world, const std::vector<Room>& rooms, RNG& rng,
                     int dungeon_level, const std::string& zone);

// Spawn traps in dungeon rooms
void spawn_traps(World& world, const TileMap& map,
                  const std::vector<Room>& rooms, RNG& rng,
                  int dungeon_level = 1);

// Monster definition for testing/validation
struct MonsterDef {
    const char* name;
    int sheet, sprite_x, sprite_y;
    int hp, str, dex, con, base_damage, natural_armor, speed, flee_threshold, xp_value;
};

const MonsterDef* get_monster_table();
int get_monster_count();

// Inline accessor for tests that can't link populate.cpp
// (populate.cpp has SDL dependencies through spritesheet.h)
namespace populate_data {
    inline const MonsterDef MONSTERS[] = {
        {"giant rat",       1, 11, 6,  6,   6, 14,  6,  2, 0, 130, 40,  10},
        {"bat",             1,  6, 6,  4,   4, 16,  4,  1, 0, 150, 70,   5},
        {"kobold",          1,  0, 9,  6,   6, 12,  6,  1, 0, 120, 35,  10},
        {"slime",           1,  1, 2, 16,   6,  4, 14,  2, 0,  60,  0,  15},
        {"goblin",          1,  2, 0,  8,   8, 12,  8,  2, 0, 110, 30,  15},
        {"giant spider",    1,  8, 6, 12,  10, 12,  8,  3, 1, 120, 20,  20},
        {"goblin archer",   1,  5, 0, 10,   8, 14,  8,  3, 0, 110, 25,  20},
        {"orc",             1,  0, 0, 18,  14,  8, 12,  4, 1,  90, 15,  30},
        {"skeleton",        1,  0, 4, 14,  10, 10, 10,  3, 2, 100,  0,  25},
        {"zombie",          1,  4, 4, 20,  14,  6, 16,  5, 1,  60,  0,  30},
        {"warg",            1, 10, 6, 16,  14, 14, 12,  5, 1, 130, 20,  35},
        {"orc warchief",    1,  4, 0, 30,  16, 10, 14,  6, 2,  85, 10,  50},
        {"troll",           1,  2, 1, 35,  18,  8, 18,  7, 2,  80, 10,  60},
        {"ghoul",           1,  5, 4, 22,  14, 12, 14,  5, 1, 110,  0,  40},
        {"lich",            1,  2, 4, 50,  10, 10, 12, 12, 0, 100,  0,  80},
        {"death knight",    1,  3, 4, 65,  18, 12, 16, 14, 4,  90,  0, 100},
        {"manticore",       1,  3, 6, 35,  16, 14, 14,  7, 2, 110, 10,  70},
        {"minotaur",        1,  7, 7, 45,  20, 10, 18,  9, 3,  85, 10,  90},
        {"naga",            1,  4, 7, 30,  14, 16, 12,  6, 1, 120, 15,  60},
        {"dragon",          1,  2, 8,120,  22, 12, 22, 18, 5,  80,  0, 200},
        {"myconid",         1,  0,10, 20,   8,  6, 14,  4, 2,  60, 10,  30},
        {"ogre",            1,  0, 1, 40,  20,  6, 16,  8, 2,  85,  5,  55},
        {"golem",           1,  2, 7, 55,  16,  4, 22, 10, 5,  50,  0,  70},
        {"basilisk",        1,  4, 8, 30,  12, 10, 14,  5, 2,  90, 10,  55},
        {"yeti",            1,  1, 7, 42,  18, 10, 16,  8, 2,  85, 10,  65},
        {"centaur",         1,  3, 7, 28,  14, 16, 12,  5, 1, 130, 15,  45},
        {"imp",             1,  1,11, 12,   6, 18,  6,  3, 0, 140, 50,  20},
        {"gargoyle",        1,  8, 7, 35,  14, 12, 18,  6, 4,  80,  0,  60},
        {"lizardfolk",      1,  1, 9, 18,  12, 14, 10,  4, 1, 110, 20,  30},
    };
    inline constexpr int MONSTER_COUNT = sizeof(MONSTERS) / sizeof(MONSTERS[0]);
}

} // namespace populate
