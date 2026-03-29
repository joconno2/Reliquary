#pragma once
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/rng.h"
#include <string>

namespace quest_gen {

// Generate side quests for NPCs near a town center.
// Assigns DynamicQuest components to appropriate NPCs based on their role.
void generate_town_quests(World& world, const TileMap& map, RNG& rng,
                           int town_cx, int town_cy, const std::string& town_name);

} // namespace quest_gen
