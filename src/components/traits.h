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
    // Gameplay modifiers
    int bonus_hp;            // flat HP bonus
    int bonus_natural_armor; // flat armor bonus
    int bonus_speed;         // energy speed bonus
    int fire_resist;         // resistance percentage
    int poison_resist;
    int bleed_resist;
    int hp_on_kill;          // heal this much per kill
    int bonus_fov;           // FOV radius bonus
    bool immune_fear;
    bool immune_confuse;
};

inline const TraitInfo& get_trait_info(TraitId id) {
    //                                    name              desc                                          pos   STR DEX CON INT WIL PER CHA  HP  ARM SPD FRES PRES BRES KHP FOV  FEAR CNF
    static const TraitInfo TRAITS[] = {
        // --- Positive Traits (12) ---
        {"Iron Willed",    "Immune to fear and confusion.",                  true,  0, 0, 0, 0, 2, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, true, true},
        {"Eagle-Eyed",     "+3 FOV, +1 PER.",                               true,  0, 0, 0, 0, 0, 1, 0,  0, 0, 0,  0, 0, 0,  0, 3, false,false},
        {"Quick-Footed",   "+15 speed, +1 DEX.",                             true,  0, 1, 0, 0, 0, 0, 0,  0, 0,15,  0, 0, 0,  0, 0, false,false},
        {"Tough Hide",     "+2 natural armor, +1 CON.",                      true,  0, 0, 1, 0, 0, 0, 0,  0, 2, 0,  0, 0, 0,  0, 0, false,false},
        {"Learned",        "+2 INT, +5 MP.",                                 true,  0, 0, 0, 2, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Charming",       "+2 CHA. Shop prices reduced.",                   true,  0, 0, 0, 0, 0, 0, 2,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Bloodlust",      "Heal 2 HP per kill.",                            true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  2, 0, false,false},
        {"Fire-Hardened",  "30% fire resistance.",                           true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 30, 0, 0,  0, 0, false,false},
        {"Sure-Handed",    "+1 STR, +1 DEX.",                                true,  1, 1, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Thick-Blooded",  "50% poison resistance, 25% bleed resistance.",   true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0,50,25,  0, 0, false,false},
        {"Battle-Scarred", "+10 HP, +1 STR, -1 CHA.",                       true,  1, 0, 0, 0, 0, 0,-1, 10, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Born Lucky",     "+1 to all stats except CHA.",                    true,  1, 1, 1, 1, 1, 1, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},

        // --- Negative Traits (10) ---
        {"Cowardly",       "-2 WIL. Enemies cause FEARED on crit.",          false, 0, 0, 0, 0,-2, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Frail",          "-10 HP, -1 CON.",                                false, 0, 0,-1, 0, 0, 0, 0,-10, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Clumsy",         "-2 DEX, -10 speed.",                             false, 0,-2, 0, 0, 0, 0, 0,  0, 0,-10, 0, 0, 0,  0, 0, false,false},
        {"Slow-Witted",    "-2 INT. Confusion lasts longer.",                false, 0, 0, 0,-2, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Ill-Favored",    "-2 CHA. Shop prices increased.",                 false, 0, 0, 0, 0, 0, 0,-2,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Haemophilia",    "Bleeding lasts twice as long. -25% bleed resist.",false,0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0,-25, 0, 0, false,false},
        {"Brittle Bones",  "-1 STR, -1 CON, -1 natural armor.",             false,-1, 0,-1, 0, 0, 0, 0,  0,-1, 0,  0, 0, 0,  0, 0, false,false},
        {"Blunt Senses",   "-2 FOV, -1 PER.",                               false, 0, 0, 0, 0, 0,-1, 0,  0, 0, 0,  0, 0, 0,  0,-2, false,false},
        {"Reckless",       "+2 STR, -2 PER, -1 natural armor.",             false, 2, 0, 0, 0, 0,-2, 0,  0,-1, 0,  0, 0, 0,  0, 0, false,false},
        {"Glass Jaw",      "Stun and freeze effects last +2 turns.",         false, 0, 0, 0, 0,-1, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
    };
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= TRAIT_COUNT) {
        static const TraitInfo NONE_TRAIT = {
            "None", "No trait.", true, 0,0,0,0,0,0,0, 0,0,0, 0,0,0, 0,0, false,false
        };
        return NONE_TRAIT;
    }
    return TRAITS[idx];
}
