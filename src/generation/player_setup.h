#pragma once
#include "core/ecs.h"
#include "ui/creation_screen.h"
#include <vector>

namespace player_setup {

struct PlayerResult {
    Entity entity;
    std::vector<TraitId> traits; // copy for runtime checks (build_traits_)
};

// Create and fully initialize the player entity: stats, god alignment,
// spells, starting gear. Called once on new-game start.
PlayerResult create_player(World& world, const CharacterBuild& build,
                           int start_x, int start_y);

} // namespace player_setup
