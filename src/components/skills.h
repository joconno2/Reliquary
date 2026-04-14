#pragma once
#include <cstring>

enum class SkillId : int {
    BLADES = 0,     // sword/dagger hits
    AXES,           // axe hits
    BLUNT,          // mace/hammer/club hits
    UNARMED,        // fist hits
    ARCHERY,        // ranged weapon hits
    CONJURATION,    // conjuration spell casts
    TRANSMUTATION,  // transmutation spell casts
    DIVINATION,     // divination spell casts
    HEALING,        // healing spell casts
    NATURE_MAGIC,   // nature spell casts
    DARK_ARTS,      // dark arts spell casts
    STEALTH,        // turns invisible / stealth kills
    HEAVY_ARMOR,    // damage absorbed in heavy armor
    DODGE,          // successful dodge rolls
    PRAYER,         // successful prayers
    SKILL_COUNT
};

constexpr int SKILL_COUNT = static_cast<int>(SkillId::SKILL_COUNT);

inline const char* skill_name(SkillId id) {
    static const char* NAMES[] = {
        "Blades", "Axes", "Blunt", "Unarmed", "Archery",
        "Conjuration", "Transmutation", "Divination", "Healing",
        "Nature Magic", "Dark Arts", "Stealth", "Heavy Armor",
        "Dodge", "Prayer"
    };
    int i = static_cast<int>(id);
    if (i < 0 || i >= SKILL_COUNT) return "???";
    return NAMES[i];
}

struct Skills {
    // Each skill: 0-100 level, plus accumulated XP toward next level
    int level[SKILL_COUNT] = {};
    int xp[SKILL_COUNT] = {};

    // XP required for next level (diminishing returns)
    static int xp_for_level(int lv) {
        return 10 + lv * lv * 2; // 10, 12, 18, 28, 42, 60, 82...
    }

    // Grant skill XP. Returns true if leveled up.
    bool grant_xp(SkillId skill, int amount) {
        int idx = static_cast<int>(skill);
        if (idx < 0 || idx >= SKILL_COUNT) return false;
        if (level[idx] >= 100) return false;

        xp[idx] += amount;
        int needed = xp_for_level(level[idx]);
        if (xp[idx] >= needed) {
            xp[idx] -= needed;
            level[idx]++;
            return true;
        }
        return false;
    }

    int get_level(SkillId skill) const {
        int idx = static_cast<int>(skill);
        if (idx < 0 || idx >= SKILL_COUNT) return 0;
        return level[idx];
    }

    void clear() {
        std::memset(level, 0, sizeof(level));
        std::memset(xp, 0, sizeof(xp));
    }
};

// Threshold bonuses at skill levels 25, 50, 75
// These are checked in combat/magic/status as needed
namespace skill_bonus {
    // Blades: +crit%
    inline int blades_crit(int lv) { return (lv >= 75) ? 8 : (lv >= 50) ? 5 : (lv >= 25) ? 3 : 0; }
    // Axes: +damage
    inline int axes_damage(int lv) { return (lv >= 75) ? 3 : (lv >= 50) ? 2 : (lv >= 25) ? 1 : 0; }
    // Blunt: +stun chance %
    inline int blunt_stun(int lv) { return (lv >= 75) ? 15 : (lv >= 50) ? 10 : (lv >= 25) ? 5 : 0; }
    // Unarmed: +damage
    inline int unarmed_damage(int lv) { return (lv >= 75) ? 4 : (lv >= 50) ? 3 : (lv >= 25) ? 1 : 0; }
    // Archery: +crit%
    inline int archery_crit(int lv) { return (lv >= 75) ? 8 : (lv >= 50) ? 5 : (lv >= 25) ? 3 : 0; }
    // Spell schools: -cost%
    inline int spell_cost_reduce(int lv) { return (lv >= 75) ? 20 : (lv >= 50) ? 12 : (lv >= 25) ? 5 : 0; }
    // Stealth: backstab multiplier
    inline int stealth_backstab(int lv) { return (lv >= 75) ? 4 : (lv >= 50) ? 3 : 2; }
    // Heavy armor: -spell failure%
    inline int armor_spell_penalty_reduce(int lv) { return (lv >= 75) ? 15 : (lv >= 50) ? 10 : (lv >= 25) ? 5 : 0; }
    // Dodge: +dodge%
    inline int dodge_bonus(int lv) { return (lv >= 75) ? 8 : (lv >= 50) ? 5 : (lv >= 25) ? 2 : 0; }
    // Prayer: -favor cost%
    inline int prayer_cost_reduce(int lv) { return (lv >= 75) ? 20 : (lv >= 50) ? 12 : (lv >= 25) ? 5 : 0; }
}
