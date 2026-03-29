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

// Spawn dungeon doodads (chests, jars, mushrooms, coffins, blood splatters)
void spawn_doodads(World& world, const TileMap& map,
                    const std::vector<Room>& rooms, RNG& rng,
                    int dungeon_level, const std::string& zone = "");

// Spawn a rival paragon (PC-like enemy with god affiliation).
// Returns the entity, or NULL_ENTITY if no suitable room/spawn.
// player_god: don't spawn a paragon of the player's own god.
Entity spawn_paragon(World& world, const TileMap& map,
                      const std::vector<Room>& rooms, RNG& rng,
                      int dungeon_level, GodId player_god);

} // namespace populate
