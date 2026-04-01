#pragma once
#include "core/ecs.h"
#include <string>

// Forward declarations
class World;
class TileMap;
class RNG;
class MessageLog;
class Audio;
class ParticleSystem;

struct DungeonEntry;

namespace status {

// Result of processing status effects — caller must check for death
struct EffectResult {
    bool player_died = false;
    std::string death_cause;
};

// Process all per-turn status effects on the player and monsters:
// - DOT ticking (poison, burn, bleed) with resistances
// - Disease tick effects (Sporebloom regen, Vampirism sun damage)
// - Buff expiration and stat reversion
// - God-specific passives (Soleth undead damage, Thalara fire zone,
//   Lethis lethal save)
// - Negative favor punishments (stat drain, HP damage, divine avengers)
// - Monster drown/sleep ticking
//
// dungeon_zone: the zone string for the current dungeon (empty if overworld/none)
EffectResult process(World& world, Entity player, TileMap& map, RNG& rng,
                     MessageLog& log, Audio& audio, ParticleSystem& particles,
                     int game_turn, int dungeon_level,
                     const std::string& dungeon_zone);

} // namespace status
