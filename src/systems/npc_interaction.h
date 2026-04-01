#pragma once
#include "core/ecs.h"

// Forward declarations
class World;
class MessageLog;
class Audio;
class RNG;
class ShopScreen;
class QuestOffer;
class LevelUpScreen;
class ParticleSystem;
struct QuestJournal;
struct MetaSave;

namespace npc_interaction {

// All the engine state an NPC interaction might need.
// Passed by reference to avoid coupling npc_interaction to Engine.
struct Context {
    World& world;
    Entity player;
    MessageLog& log;
    Audio& audio;
    RNG& rng;
    ParticleSystem& particles;
    ShopScreen& shop_screen;
    QuestOffer& quest_offer;
    LevelUpScreen& levelup_screen;
    QuestJournal& journal;
    MetaSave& meta;
    int& gold;
    int& game_turn;
    int dungeon_level;
    bool& pending_levelup;
    Entity& pending_quest_npc;
};

// Handle bumping into an NPC. Returns true if the interaction was handled
// (caller should not move the player or process further).
bool interact(Context& ctx, Entity target, int target_x, int target_y);

} // namespace npc_interaction
