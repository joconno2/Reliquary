#pragma once
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/rng.h"
#include "ui/message_log.h"
#include "components/spellbook.h"

namespace magic {

struct CastResult {
    bool success = false;
    bool consumed_turn = true;
};

// Cast a spell. Handles MP cost, effect application, messages.
CastResult cast(World& world, Entity caster, SpellId spell,
                 TileMap& map, RNG& rng, MessageLog& log,
                 int target_x = -1, int target_y = -1);

// Get the nearest hostile entity in range (for auto-targeting)
Entity nearest_enemy(World& world, Entity caster, const TileMap& map, int range);

} // namespace magic
