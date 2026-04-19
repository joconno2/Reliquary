#pragma once
#include "core/ecs.h"
#include "core/rng.h"
#include "ui/message_log.h"
#include "components/item.h"

namespace combat {

struct AttackResult {
    bool hit = false;
    int damage = 0;
    bool critical = false;
    bool killed = false;
    bool attacker_killed = false; // riposte killed the attacker
    bool teleport_behind = false; // unique ring: blinked behind target
};

// Resolve a melee attack from attacker to defender
AttackResult melee_attack(World& world, Entity attacker, Entity defender,
                           RNG& rng, MessageLog& log);

// Resolve a ranged attack (uses DEX instead of STR)
AttackResult ranged_attack(World& world, Entity attacker, Entity defender,
                            int weapon_damage, RNG& rng, MessageLog& log);

// Check if there's an attackable entity at position
Entity entity_at(World& world, int x, int y, Entity ignore = 0);

// Kill an entity — remove combat components, add corpse. Returns XP value.
int kill(World& world, Entity e, MessageLog& log);

// Check if an entity has any equipped item with the given unique effect
bool has_unique_effect(World& world, Entity e, UniqueEffect ue);

} // namespace combat
