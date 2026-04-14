#pragma once
#include "core/ecs.h"

enum class TrapType : int {
    PIT,          // fall damage, may drop to next floor
    SPIKE,        // spike damage when stepped on
    DART,         // ranged dart when triggered
    ALARM,        // summons 2-3 monsters
    BEAR_TRAP,    // immobilize for 3 turns
    POISON_GAS,   // poison cloud in 3x3
};

struct Trap {
    TrapType type = TrapType::SPIKE;
    bool revealed = false;    // visible to player (detected or triggered)
    bool triggered = false;   // already went off
    int damage = 5;           // base damage for damage traps
    int difficulty = 12;      // PER check DC to detect before triggering

    // Sprite info for revealed traps
    int sprite_x = 0;
    int sprite_y = 0;
};

// Trap sprite helpers (row 17 of tiles.png)
inline void trap_sprite(TrapType type, bool triggered, int& sx, int& sy) {
    sy = 16; // row 17 (0-indexed = 16)
    switch (type) {
        case TrapType::PIT:        sx = triggered ? 12 : 11; break; // pit / chute
        case TrapType::SPIKE:      sx = triggered ? 0  : 15; break; // spikes up / pressure plate
        case TrapType::DART:       sx = 9; break;  // pressure plate (up)
        case TrapType::ALARM:      sx = 9; break;  // pressure plate
        case TrapType::BEAR_TRAP:  sx = triggered ? 10 : 9;  break; // pressure plate down / up
        case TrapType::POISON_GAS: sx = 14; break; // pentagram (poison sigil)
    }
}
