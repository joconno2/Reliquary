#pragma once
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/rng.h"
#include "generation/dungeon.h"
#include <string>
#include <vector>

class MessageLog;
struct QuestJournal;

namespace quest_gen {

// Generate side quests for NPCs near a town center.
// Assigns DynamicQuest components to appropriate NPCs based on their role.
void generate_town_quests(World& world, const TileMap& map, RNG& rng,
                           int town_cx, int town_cy, const std::string& town_name);

// Context for the current dungeon — avoids depending on engine.h
struct DungeonContext {
    std::string zone;     // e.g. "warrens", "sepulchre"
    std::string quest;    // e.g. "MQ_03", "" for generic dungeons
    int max_depth = 3;    // deepest floor in this dungeon
};

// Spawn quest bosses, quest items, and trigger depth-based quest auto-starts.
// Called from generate_level() after dungeon population.
// dungeon_ctx is only used when dungeon_level > 0 and the dungeon is in the registry.
void spawn_quest_content(World& world, const TileMap& map,
                          const std::vector<Room>& rooms,
                          int dungeon_level,
                          const DungeonContext* dungeon_ctx,
                          QuestJournal& journal, MessageLog& log);

} // namespace quest_gen
