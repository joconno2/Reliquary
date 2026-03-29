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
};
