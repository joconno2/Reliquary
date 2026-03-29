#pragma once
#include <string>

// Dynamically generated quest attached to an NPC entity.
// Unlike static QuestId quests, these are generated at runtime per-town.
struct DynamicQuest {
    std::string name;
    std::string description;
    std::string objective;
    std::string complete_text;
    int xp_reward = 30;
    int gold_reward = 15;
    bool completed = false;
    bool accepted = false;

    // Completion conditions — all active conditions must be met before turn-in
    int accepted_turn = 0;        // game turn when accepted
    int min_turns = 0;            // minimum turns after acceptance (0 = no requirement)
    bool requires_dungeon = false; // must enter a dungeon after accepting
    bool visited_dungeon = false;  // tracks dungeon entry
    int target_town_x = -1;       // delivery destination (-1 = none)
    int target_town_y = -1;
    bool reached_target = false;   // tracks arrival at target town

    bool conditions_met(int current_turn) const {
        if (min_turns > 0 && (current_turn - accepted_turn) < min_turns) return false;
        if (requires_dungeon && !visited_dungeon) return false;
        if (target_town_x >= 0 && !reached_target) return false;
        return true;
    }
};
