#pragma once
#include <string>

enum class TraitId : int {
    // Positive traits (12)
    IRON_WILLED = 0,
    EAGLE_EYED,
    QUICK_FOOTED,
    TOUGH_HIDE,
    LEARNED,
    CHARMING,
    KEEN_NOSE,
    DEATH_TOUCHED,
    SURE_HANDED,
    PATIENT_HUNTER,
    BATTLE_SCARRED,
    SILVER_BLOODED,
    // Negative traits (10)
    COWARDLY,
    FRAIL,
    CLUMSY,
    SLOW_WITTED,
    ILL_FAVORED,
    HAUNTED,
    BRITTLE_BONES,
    BLUNT_SENSES,
    RECKLESS,
    MARKED,
    COUNT
};

constexpr int TRAIT_COUNT = static_cast<int>(TraitId::COUNT);
constexpr int POSITIVE_TRAIT_COUNT = static_cast<int>(TraitId::COWARDLY);
constexpr int NEGATIVE_TRAIT_COUNT = TRAIT_COUNT - POSITIVE_TRAIT_COUNT;

struct TraitInfo {
    const char* name;
    const char* description;
    bool is_positive;
    // Stat modifiers
    int str_mod, dex_mod, con_mod, int_mod, wil_mod, per_mod, cha_mod;
    // Special flags
    bool see_invisible;
    bool immune_poison;
    bool immune_disease;
    bool fear_resist;
    bool extra_fov;     // +2 to FOV radius
};

inline const TraitInfo& get_trait_info(TraitId id) {
    static const TraitInfo TRAITS[] = {
        // --- Positive Traits ---

        // IRON_WILLED
        {"Iron Willed",
         "Your mind bends but does not break. Mental effects have less hold on you.",
         true,
         0,0,0,0,2,0,0,
         false,false,false,true,false},
        // EAGLE_EYED
        {"Eagle-Eyed",
         "Nothing escapes your notice. You see farther and spot hidden things.",
         true,
         0,0,0,0,0,2,0,
         false,false,false,false,true},
        // QUICK_FOOTED
        {"Quick-Footed",
         "Light on your feet. Harder to pin down.",
         true,
         0,2,0,0,0,0,0,
         false,false,false,false,false},
        // TOUGH_HIDE
        {"Tough Hide",
         "Your skin is thick and scarred. You absorb a little punishment.",
         true,
         0,0,2,0,0,0,0,
         false,false,false,false,false},
        // LEARNED
        {"Learned",
         "Years of study sharpen the mind.",
         true,
         0,0,0,2,0,0,0,
         false,false,false,false,false},
        // CHARMING
        {"Charming",
         "People want to like you. You've learned to use that.",
         true,
         0,0,0,0,0,0,2,
         false,false,false,false,false},
        // KEEN_NOSE
        {"Keen Nose",
         "You smell danger before you see it. Ambushes rarely catch you flat-footed.",
         true,
         0,0,0,0,0,1,0,
         false,false,false,false,false},
        // DEATH_TOUCHED
        {"Death-Touched",
         "You have brushed close to death enough times that undead sense a kinship.",
         true,
         0,0,0,0,1,0,0,
         true,false,false,false,false},
        // SURE_HANDED
        {"Sure-Handed",
         "Your grip is steady and your aim is true.",
         true,
         1,1,0,0,0,0,0,
         false,false,false,false,false},
        // PATIENT_HUNTER
        {"Patient Hunter",
         "You know how to wait for the right moment.",
         true,
         0,0,0,0,1,1,0,
         false,false,false,false,false},
        // BATTLE_SCARRED
        {"Battle-Scarred",
         "Every scar is a lesson. You've had a lot of lessons.",
         true,
         1,0,1,0,0,0,-1,
         false,false,false,false,false},
        // SILVER_BLOODED
        {"Silver-Blooded",
         "Something in your constitution rejects toxins.",
         true,
         0,0,1,0,0,0,0,
         false,true,true,false,false},

        // --- Negative Traits ---

        // COWARDLY
        {"Cowardly",
         "Your instinct is to run. Staying takes deliberate effort.",
         false,
         0,0,0,0,-2,0,-1,
         false,false,false,false,false},
        // FRAIL
        {"Frail",
         "Never the robust sort. A bad winter lays you low.",
         false,
         0,0,-2,0,0,0,0,
         false,false,false,false,false},
        // CLUMSY
        {"Clumsy",
         "You trip over things that aren't there.",
         false,
         0,-2,0,0,0,-1,0,
         false,false,false,false,false},
        // SLOW_WITTED
        {"Slow-Witted",
         "It takes you longer to work things out.",
         false,
         0,0,0,-2,0,0,0,
         false,false,false,false,false},
        // ILL_FAVORED
        {"Ill-Favored",
         "People take an instant dislike to you. You're used to it.",
         false,
         0,0,0,0,0,0,-2,
         false,false,false,false,false},
        // HAUNTED
        {"Haunted",
         "Something follows you. You haven't seen it clearly yet.",
         false,
         0,0,0,0,-1,0,-1,
         false,false,false,false,false},
        // BRITTLE_BONES
        {"Brittle Bones",
         "Your skeleton was not built for punishment.",
         false,
         -1,0,-1,0,0,0,0,
         false,false,false,false,false},
        // BLUNT_SENSES
        {"Blunt Senses",
         "The world is a little dimmer, a little quieter.",
         false,
         0,0,0,0,0,-2,0,
         false,false,false,false,false},
        // RECKLESS
        {"Reckless",
         "You act before you think. Sometimes it works out.",
         false,
         1,0,0,0,-1,-1,0,
         false,false,false,false,false},
        // MARKED
        {"Marked",
         "Something dark has noticed you. You feel it watching.",
         false,
         0,0,0,0,-1,0,-1,
         false,false,false,false,false},
    };
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= TRAIT_COUNT) {
        static const TraitInfo NONE_TRAIT = {
            "None", "No trait.", true, 0,0,0,0,0,0,0,
            false,false,false,false,false
        };
        return NONE_TRAIT;
    }
    return TRAITS[idx];
}
