#pragma once
#include <string>

enum class NPCRole : int {
    SHOPKEEPER,
    BLACKSMITH,
    PRIEST,
    FARMER,
    GUARD,
};

struct NPC {
    NPCRole role = NPCRole::FARMER;
    std::string name = "someone";
    std::string dialogue = "..."; // what they say when you bump them
};
