#pragma once
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/rng.h"

namespace ai {

// Process all AI entities that can act this tick
void process(World& world, TileMap& map, Entity player, RNG& rng);

} // namespace ai
