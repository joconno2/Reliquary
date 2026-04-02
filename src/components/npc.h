#pragma once
#include <string>
#include <vector>
#include "components/god.h"

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
    std::string dialogue = "...";  // primary line (quest-relevant)
    std::vector<std::string> idle_lines; // additional lines, cycled on repeat bumps
    mutable int line_idx = 0; // current idle line index
    int quest_id = -1;  // quest to give on interaction (-1 = none)
    int home_x = 0, home_y = 0;  // spawn position — wander anchor
    GodId god_affiliation = GodId::NONE; // town's patron god

    // Get the next dialogue line (cycles through idle_lines after first bump)
    const std::string& next_line() const {
        if (idle_lines.empty()) return dialogue;
        const std::string& line = idle_lines[line_idx % idle_lines.size()];
        line_idx = (line_idx + 1) % static_cast<int>(idle_lines.size());
        return line;
    }
};
