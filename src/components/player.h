#pragma once
#include "components/class_def.h"
#include "components/traits.h"
#include <vector>

struct Player {
    // Marker component — entity with this is the player
    bool active = true;
    ClassId class_id = ClassId::FIGHTER;
    std::vector<TraitId> traits; // copy of build traits for system access
};
