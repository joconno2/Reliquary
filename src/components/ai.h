#pragma once
#include "core/ecs.h"

enum class AIState : int {
    IDLE,       // wander randomly
    HUNTING,    // chasing the player (has line of sight or recently saw)
    FLEEING,    // running away (low HP)
};

enum class BehaviorType : int {
    BASIC = 0,      // walk toward, melee, flee (default)
    ARCHER,         // stay at range, shoot
    LICH,           // teleport when low, drain at range, summon
    TROLL,          // regenerate HP each turn
    THIEF,          // hit and run, flees after attacking
    NECROMANCER,    // raise corpses, stay at range, cast drain
    SHAMAN,         // buff nearby allies, heal allies
    CHARGER,        // charge in a line when at range 2-4
    DRAGON,         // breath AoE cone, flee when low
    WRAITH,         // phase through walls, only hit by silver/magic
    PACK,           // coordinate with same-type, prefer flanking
    KEEPER,         // final boss: 3-phase (charge -> teleport/drain -> dragon breath)
};

struct AI {
    AIState state = AIState::IDLE;
    BehaviorType behavior = BehaviorType::BASIC;
    int last_seen_x = -1;  // last known player position
    int last_seen_y = -1;
    int alert_turns = 0;   // turns since last seeing player (hunting memory)
    int flee_threshold = 20; // HP% below which monster flees
    int ranged_range = 0;  // >0 = can shoot at this range
    int ranged_damage = 0; // base ranged damage
    bool forget_player = false; // Lethis: permanently ignores player
    int ability_cooldown = 0; // cooldown for special abilities
    int regen_per_turn = 0;   // HP regen (troll)
    int regen_msg_counter = 0; // throttle regen message
    bool friendly = false;    // summoned by player, attacks enemies instead
    Entity target = 0;
    // VFX flags (set by AI, consumed by engine each tick)
    enum class SpellVFX : int { NONE=0, DRAIN, SUMMON, HEAL_ALLY, BUFF_ALLY, BREATH_FIRE, BREATH_ICE };
    SpellVFX last_spell = SpellVFX::NONE;
};
