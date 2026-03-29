#pragma once
#include <string>
#include <array>

// Primary attributes
enum class Attr : int {
    STR = 0, DEX, CON, INT, WIL, PER, CHA,
    COUNT
};

constexpr int ATTR_COUNT = static_cast<int>(Attr::COUNT);

struct Stats {
    std::string name = "Unknown";

    // Primary attributes (base values, before modifiers)
    std::array<int, ATTR_COUNT> attributes = {10, 10, 10, 10, 10, 10, 10};

    // Vitals
    int hp = 20;
    int hp_max = 20;
    int mp = 0;
    int mp_max = 0;

    // Combat
    int base_damage = 1;     // unarmed / natural weapon damage
    int natural_armor = 0;   // innate protection (monsters, traits)
    int base_speed = 100;    // energy gained per tick (100 = normal)

    // Level
    int level = 1;
    int xp = 0;
    int xp_next = 100;

    // Convenience
    int attr(Attr a) const { return attributes[static_cast<int>(a)]; }
    void set_attr(Attr a, int val) { attributes[static_cast<int>(a)] = val; }

    // XP reward when killed (monsters only)
    int xp_value = 0;

    // Derived combat stats
    int melee_attack() const { return attr(Attr::STR) + level; }
    int melee_damage() const { return base_damage + attr(Attr::STR) / 3; }
    int dodge_value() const { return attr(Attr::DEX) / 2; }
    int protection() const { return natural_armor; }
    int fov_radius() const { return 8 + attr(Attr::PER) / 3; }

    // Grant XP — returns true if leveled up
    bool grant_xp(int amount) {
        xp += amount;
        if (xp >= xp_next) {
            level++;
            xp -= xp_next;
            xp_next = level * level * 50 + 50; // scaling curve
            // Small HP/MP boost on level (real gains come from level-up choices)
            int hp_gain = 1;
            int mp_gain = 1;
            hp_max += hp_gain;
            hp += hp_gain;
            mp_max += mp_gain;
            mp += mp_gain;
            return true;
        }
        return false;
    }
};
