#pragma once
#include "components/class_def.h"

struct Player {
    // Marker component — entity with this is the player
    bool active = true;
    ClassId class_id = ClassId::FIGHTER;
};
