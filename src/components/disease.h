#pragma once
#include <vector>
#include <algorithm>

enum class DiseaseId : int {
    LYCANTHROPY = 0,  // warg bite — +3 STR, +2 CON, -3 CHA, -2 INT. Berserk <25% HP.
    VAMPIRISM,        // lich drain — +2 STR, +2 DEX, -3 CON. Heal on kill. Surface hurts.
    STONESCALE,       // basilisk/dragon — +4 natural armor, -3 DEX, -15 speed.
    MINDFIRE,         // death knight — +3 INT, +2 PER, -2 WIL, -2 CON. Hallucinations.
    SPOREBLOOM,       // giant spider — +2 CON, -2 CHA, -1 STR. Regen in dungeons.
    HOLLOW_BONES,     // skeleton — -3 CON, +3 DEX, +15 speed.
    BLACKBLOOD,       // naga — immune to poison, -2 CHA. Biters take damage.
    DISEASE_COUNT
};

constexpr int DISEASE_COUNT = static_cast<int>(DiseaseId::DISEASE_COUNT);

struct DiseaseInfo {
    const char* name;
    const char* description;
    const char* contraction_msg; // message when contracted
    const char* hud_tag;         // 3-letter HUD indicator
};

inline const DiseaseInfo& get_disease_info(DiseaseId id) {
    static const DiseaseInfo INFO[] = {
        {"Lycanthropy",
         "The beast stirs within. +3 STR, +2 CON, -3 CHA, -2 INT. Berserk below 25% HP.",
         "Your blood runs hot. Something feral takes root inside you.",
         "LYC"},
        {"Vampirism",
         "The living envy you. +2 STR, +2 DEX, -3 CON. No regen. Heal by killing.",
         "A cold hunger settles in your veins. The sun feels wrong.",
         "VMP"},
        {"Stonescale",
         "Your skin hardens, your joints stiffen. +4 armor, -3 DEX, slower movement.",
         "Grey patches spread across your skin. Stone where flesh should be.",
         "STN"},
        {"Mindfire",
         "Thoughts too bright, too fast. +3 INT, +2 PER, -2 WIL, -2 CON.",
         "Colors bleed at the edges. Your thoughts race, sharp and wrong.",
         "MND"},
        {"Sporebloom",
         "Something grows in you. +2 CON, -2 CHA, -1 STR. Regen in darkness.",
         "Pale tendrils curl from your wounds. They smell of earth and rot.",
         "SPR"},
        {"Hollow Bones",
         "Light as a skeleton. -3 CON, +3 DEX, faster movement.",
         "Your bones ache, then feel hollow. You are lighter than you should be.",
         "HLW"},
        {"Blackblood",
         "Your blood is poison. Immune to poison, -2 CHA. Biters take damage.",
         "Your blood darkens to pitch. It burns where it touches stone.",
         "BLK"},
    };
    return INFO[static_cast<int>(id)];
}

struct Diseases {
    std::vector<DiseaseId> active;

    bool has(DiseaseId id) const {
        for (auto d : active) if (d == id) return true;
        return false;
    }

    // Returns true if newly contracted (false if already had it)
    bool contract(DiseaseId id) {
        if (has(id)) return false;
        active.push_back(id);
        return true;
    }

    bool empty() const { return active.empty(); }
    int count() const { return static_cast<int>(active.size()); }
};
