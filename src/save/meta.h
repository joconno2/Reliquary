#pragma once
#include "components/god.h"
#include <string>
#include <array>
#include <map>
#include <set>

// Persistent bestiary entry (survives across runs)
struct MetaBestiaryEntry {
    int hp = 0, damage = 0, armor = 0, speed = 0;
    int total_kills = 0;
};

// Persistent progression that survives across runs.
// Stored in save/meta.json, loaded on startup, saved on death/victory.
struct MetaSave {
    // Cumulative counters (across all runs)
    int total_kills = 0;
    int total_undead_kills = 0;
    int total_hp_healed = 0;
    int total_dark_arts_casts = 0;
    int total_quests_completed = 0;
    int total_creatures_examined = 0;
    int max_dungeon_depth = 0;
    int max_gold_single_run = 0;

    // God completions (won the game as each god)
    std::array<bool, GOD_COUNT> gods_completed = {};

    // Per-class max level achieved
    static constexpr int MAX_CLASSES = 21; // 4 base + 17 unlockable
    std::array<int, MAX_CLASSES> class_max_level = {};

    // One-time achievement flags
    bool killed_unarmed = false;       // killed an enemy with no weapon equipped
    bool died_deep = false;            // died on dungeon level 4+
    bool killed_paragon = false;       // killed a rival paragon
    int total_deaths = 0;             // total deaths across all runs
    bool killed_dragon = false;        // killed a dragon
    int max_diseases = 0;             // max simultaneous diseases in one run

    // Persistent bestiary (name → stats + total kills across runs)
    std::map<std::string, MetaBestiaryEntry> bestiary;

    // Identified potion names (persist across runs)
    std::set<std::string> identified_potions; // real names, e.g. "healing potion"

    // Derived: count how many gods completed
    int gods_completed_count() const {
        int n = 0;
        for (bool b : gods_completed) if (b) n++;
        return n;
    }
};

namespace meta {

MetaSave load(const std::string& path = "save/meta.json");
bool save(const MetaSave& data, const std::string& path = "save/meta.json");

} // namespace meta
