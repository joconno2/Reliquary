#pragma once
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/rng.h"
#include <vector>
#include <string>

struct DungeonEntry;

namespace overworld {

// Populate the overworld with wilderness NPCs, cabins, hamlets, vegetation,
// water features, signposts, wandering priests, and decorations.
// Called once when the overworld map is first generated.
void populate(World& world, TileMap& map, RNG& rng,
              const std::vector<DungeonEntry>& dungeon_registry);

// Process NPC wandering movement on the overworld (shopkeepers stay put).
void process_npc_wander(World& world, TileMap& map, RNG& rng);

// Try to spawn a random overworld enemy near the player.
void try_spawn_overworld_enemy(World& world, TileMap& map, RNG& rng,
                                Entity player);

} // namespace overworld
