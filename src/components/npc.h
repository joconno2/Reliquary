#pragma once
#include <string>

enum class NPCRole : int {
    SHOPKEEPER,
    BLACKSMITH,
    PRIEST,
    FARMER,
    GUARD,
    ELDER,  // quest giver
};

struct NPC {
    NPCRole role = NPCRole::FARMER;
    std::string name = "someone";
    std::string dialogue = "...";
    int quest_id = -1;  // quest to give on interaction (-1 = none)
    int home_x = 0, home_y = 0;  // spawn position — wander anchor
};
