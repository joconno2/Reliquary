#pragma once
#include <string>

enum class TraitId : int {
    // All traits are trade-offs: powerful upside + meaningful downside
    BERSERKER = 0,   // +50% damage below 30% HP, but can't use healing items
    GLASS_CANNON,    // +6 damage all attacks, but -20 max HP
    VAMPIRIC,        // Heal 3 per kill, but sunlight (overworld) drains 1 HP/5 turns
    IRON_SKIN,       // +4 armor always, but -20 speed
    FLEET_FOOT,      // +25 speed, but -2 armor, -1 damage
    SPELL_GLUTTON,   // Spells cost half MP, but melee damage halved
    BLOODLETTER,     // Crits apply 5-turn bleed, but YOU bleed 1/turn permanently
    CANNIBAL,        // Eat corpses for full heal (10 turns), but shops refuse you
    PARANOID,        // +5 FOV, see invisible, but confused 20% of the time
    LUCKY,           // 15% chance to dodge any hit, but crits against you do 3x
    HEAVY_HITTER,    // First hit each floor does 3x damage, but miss chance +20%
    NOCTURNAL,       // +4 damage in dungeons, but -4 damage on surface
    COUNT
};

constexpr int TRAIT_COUNT = static_cast<int>(TraitId::COUNT);

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
        // All traits are trade-offs. Upside first, downside after.
        {"Berserker",      "+50% damage below 30% HP. Cannot use healing items.",  true,  2, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Glass Cannon",   "+6 flat damage on all attacks. -20 max HP.",          true,  0, 0, 0, 0, 0, 0, 0,-20, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Vampiric",       "Heal 3 on kill. Overworld drains 1 HP/5 turns.",      true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  3, 0, false,false},
        {"Iron Skin",      "+4 armor permanently. -20 speed.",                    true,  0, 0, 0, 0, 0, 0, 0,  0, 4,-20, 0, 0, 0,  0, 0, false,false},
        {"Fleet Foot",     "+25 speed. -2 armor, -1 damage.",                     true,  0, 0, 0, 0, 0, 0, 0,  0,-2,25,  0, 0, 0,  0, 0, false,false},
        {"Spell Glutton",  "Spells cost half MP. Melee damage halved.",           true,  0, 0, 0, 2, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Bloodletter",    "Crits apply 5-turn bleed to enemies. You bleed 1/turn permanently.", true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Cannibal",       "Eat corpses for full heal (10 turns). Shops refuse to trade.", true,  0, 0, 2, 0, 0, 0,-3,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Paranoid",       "+5 FOV. See invisible. Confused 20% of turns.",       true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 5, false,false},
        {"Lucky",          "15% chance to dodge any attack. Crits against you deal 3x.", true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Heavy Hitter",   "First hit each floor deals 3x damage. +20% miss chance.", true,  2, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
        {"Nocturnal",      "+4 damage in dungeons. -4 damage on surface.",        true,  0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, false,false},
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
