#pragma once
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/rng.h"
#include "components/quest.h"
#include "generation/dungeon.h"
#include <string>
#include <vector>

struct SaveData {
    // Player state
    int dungeon_level = 0;
    int game_turn = 0;
    int gold = 0;
    QuestJournal journal;

    // Overworld return position (where to place player when ascending from depth 1)
    int overworld_return_x = 0;
    int overworld_return_y = 0;

    // RNG state for reproducibility
    uint64_t rng_seed = 0;

    // Hardcore mode — save deleted on death, one-shot loads
    bool hardcore = false;

    bool valid = false;
};

namespace save {

// Save current game state to a file
bool save_game(const std::string& path, const SaveData& data,
               World& world, Entity player, const TileMap& map);

// Load game state from a file. Returns save data; populates world and map.
SaveData load_game(const std::string& path, World& world, TileMap& map);

// Check if a save file exists
bool save_exists(const std::string& path);

// Default save path
inline std::string default_path() { return "save/game.sav"; }

} // namespace save
