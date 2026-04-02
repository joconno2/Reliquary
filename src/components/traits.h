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
    // Extra positive traits
    VENOMOUS,       // 10% chance to poison on melee hit
    NIGHT_OWL,      // +4 FOV in dungeons
    SECOND_WIND,    // 10% chance to heal 3 HP when taking damage
    // Negative traits (13)
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
    CURSED_BLOOD,   // healing potions 50% less effective
    HEAVY_SLEEPER,  // rest has higher interruption chance
    FRAGILE_MIND,   // -3 WIL, +2 INT
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
        {"Eagle-Eyed",     "+3 FOV. Examine reveals enemy HP even through fog.", true,  0, 0, 0, 0, 0, 1, 0,  0, 0, 0,  0, 0, 0,  0, 3, false,false},
        {"Quick-Footed",   "+15 speed. 15% chance to dodge melee attacks.",  true,  0, 1, 0, 0, 0, 0, 0,  0, 0,15,  0, 0, 0,  0, 0, false,false},
        {"Tough Hide",     "+2 armor. Attacks that deal 1 damage are ignored.", true,  0, 0, 1, 0, 0, 0, 0,  0, 2, 0,  0, 0, 0,  0, 0, false,false},
        {"Learned",        "+2 INT, +5 MP. Spellbook tomes teach an extra spell.", true,  0, 0, 0, 2, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Charming",       "+2 CHA. Shops 10% cheaper. NPCs occasionally give hints.", true,  0, 0, 0, 0, 0, 0, 2,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Bloodlust",      "Heal 3 HP per kill. Violence sustains you.",             true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  3, 0, false,false},
        {"Fire-Hardened",  "30% fire resistance. Burn effects last half as long.", true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 30, 0, 0,  0, 0, false,false},
        {"Sure-Handed",    "+1 STR, +1 DEX. 10% chance to strike twice.",    true,  1, 1, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Thick-Blooded",  "50% poison resist, 25% bleed resist. Diseases contract 50% less.", true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0,50,25,  0, 0, false,false},
        {"Battle-Scarred", "+10 HP, +1 STR, -1 CHA. Gain +1 armor per 5 levels.", true,  1, 0, 0, 0, 0, 0,-1, 10, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Born Lucky",     "+1 to all stats. Containers have 25% better loot.", true,  1, 1, 1, 1, 1, 1, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Venomous",       "10% chance to poison enemies on melee hit.",    true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Night Owl",      "+4 FOV in dungeons, -2 FOV on overworld.",      true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Second Wind",    "10% chance to heal 3 HP when taking damage.",   true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},

        // --- Negative Traits (13) ---
        {"Cowardly",       "-2 WIL. Enemies cause FEARED on crit.",          false, 0, 0, 0, 0,-2, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Frail",          "-10 HP, -1 CON. Crits deal double damage to you.", false, 0, 0,-1, 0, 0, 0, 0,-10, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Clumsy",         "-2 DEX, -10 speed. 10% chance to fumble attacks.", false, 0,-2, 0, 0, 0, 0, 0,  0, 0,-10, 0, 0, 0,  0, 0, false,false},
        {"Slow-Witted",    "-2 INT. Confusion +2 turns. Spells cost +2 MP.", false, 0, 0, 0,-2, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Ill-Favored",    "-2 CHA. Shops 10% more expensive. NPCs less helpful.", false, 0, 0, 0, 0, 0, 0,-2,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Haemophilia",    "-25% bleed resist. Any bleed effect lasts twice as long.", false, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0,-25, 0, 0, false,false},
        {"Brittle Bones",  "-1 STR, -1 CON, -1 armor. Fall damage from stairs.", false,-1, 0,-1, 0, 0, 0, 0,  0,-1, 0,  0, 0, 0,  0, 0, false,false},
        {"Blunt Senses",   "-2 FOV, -1 PER. Enemies spot you before you see them.", false, 0, 0, 0, 0, 0,-1, 0,  0, 0, 0,  0, 0, 0,  0,-2, false,false},
        {"Reckless",       "+2 STR, -2 PER, -1 armor. Take 50% more crit damage.", false, 2, 0, 0, 0, 0,-2, 0,  0,-1, 0,  0, 0, 0,  0, 0, false,false},
        {"Glass Jaw",      "Stun and freeze effects last +2 turns.",         false, 0, 0, 0, 0,-1, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Cursed Blood",   "Healing potions are 50% less effective.",       false, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Heavy Sleeper",  "Resting is more likely to be interrupted.",     false, 0, 0, 0, 0,-1, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Fragile Mind",   "-3 WIL, +2 INT. Glass mind, sharp thoughts.",  false, 0, 0, 0, 2,-3, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
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
