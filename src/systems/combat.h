#pragma once
#include "core/ecs.h"
#include "core/rng.h"
#include "ui/message_log.h"

namespace combat {

struct AttackResult {
    bool hit = false;
    int damage = 0;
    bool critical = false;
    bool killed = false;
    int quest_target_id = -1; // QuestId if killed entity was a quest target, else -1
};

// Resolve a melee attack from attacker to defender
AttackResult melee_attack(World& world, Entity attacker, Entity defender,
                           RNG& rng, MessageLog& log);

// Check if there's an attackable entity at position
Entity entity_at(World& world, int x, int y, Entity ignore = 0);

// Kill an entity — remove combat components, add corpse. Returns XP value.
int kill(World& world, Entity e, MessageLog& log);

} // namespace combat
