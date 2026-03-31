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

    // Resistances (percentage reduction, 0-100)
    int fire_resist = 0;
    int poison_resist = 0;
    int bleed_resist = 0;

    // God prayer / passive status fields
    int invisible_turns = 0;   // Zhavek: invisible until attack or expiry
    int sleep_turns = 0;       // Lethis: sleeping, skip turns
    int drown_turns = 0;       // Thalara: taking drown damage over time
    int drown_damage = 0;      // Thalara: damage per drown tick
    int unyielding_turns = 0;  // Ossren: armor doubled
    int stone_skin_turns = 0;  // Gathruun: bonus armor, can't move
    int stone_skin_armor = 0;  // Gathruun: armor bonus while stone skin active

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
            xp_next = level * level * 75 + 75; // steeper scaling curve
            // No free HP/MP on level — real gains come from level-up choice screen only
            return true;
        }
        return false;
    }
};
