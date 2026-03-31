#pragma once
#include "core/ecs.h"

enum class AIState : int {
    IDLE,       // wander randomly
    HUNTING,    // chasing the player (has line of sight or recently saw)
    FLEEING,    // running away (low HP)
};

struct AI {
    AIState state = AIState::IDLE;
    int last_seen_x = -1;  // last known player position
    int last_seen_y = -1;
    int alert_turns = 0;   // turns since last seeing player (hunting memory)
    int flee_threshold = 20; // HP% below which monster flees
    int ranged_range = 0;  // >0 = can shoot at this range
    int ranged_damage = 0; // base ranged damage
    bool forget_player = false; // Lethis: permanently ignores player
    // Entity target field for prayer targeting
    Entity target = 0;  // NULL_ENTITY equivalent (0)
};
