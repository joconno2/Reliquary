#pragma once
#include "components/class_def.h"
#include "components/traits.h"
#include <vector>

struct Player {
    // Marker component — entity with this is the player
    bool active = true;
    ClassId class_id = ClassId::FIGHTER;
    std::vector<TraitId> traits; // copy of build traits for system access
    int bulwark_turns = 0;  // Knight Lv5 active bulwark (50% block)
    int unbreakable_turns = 0;  // Capstone: halve all damage
    int beast_form_turns = 0;   // Capstone: natural attacks override weapon
    int vanish_cooldown = 0;    // Rogue Lv5: turns before Vanish can trigger again
    bool weave_cast = false;    // Elf: next spell is a weave proc
    bool devour_cast = false;   // Heretic: next spell is a devoured proc
};
