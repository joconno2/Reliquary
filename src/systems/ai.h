#pragma once
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/rng.h"
#include "ui/message_log.h"

namespace ai {

// Process all AI entities that can act this tick
void process(World& world, TileMap& map, Entity player, RNG& rng,
             MessageLog& log, bool player_sneaking = false);

} // namespace ai
