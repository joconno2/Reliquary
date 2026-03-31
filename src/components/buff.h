#pragma once
#include <vector>

enum class BuffType : int {
    HARDEN_SKIN,     // +natural_armor
    HASTEN,          // +base_speed
    STONE_FIST,      // +base_damage
    IRON_BODY,       // +natural_armor, -base_speed
    FORESIGHT,       // +natural_armor (dodge proxy)
    SHIELD_OF_FAITH, // +natural_armor
    BARKSKIN,        // +natural_armor, +poison_resist
    SANCTUARY,       // +natural_armor
};

struct Buff {
    BuffType type;
    int turns_remaining;
    int value;         // primary stat amount to revert
    int value2;        // secondary stat amount (e.g. speed penalty for Iron Body)
};

struct Buffs {
    std::vector<Buff> active;

    void add(BuffType type, int turns, int val, int val2 = 0) {
        // Refresh if already active
        for (auto& b : active) {
            if (b.type == type) {
                b.turns_remaining = turns;
                return; // don't re-apply stat change
            }
        }
        active.push_back({type, turns, val, val2});
    }

    bool has(BuffType type) const {
        for (auto& b : active) if (b.type == type) return true;
        return false;
    }

    void tick() {
        for (auto& b : active) b.turns_remaining--;
    }

    // Returns expired buffs so caller can revert stats
    std::vector<Buff> collect_expired() {
        std::vector<Buff> expired;
        for (auto it = active.begin(); it != active.end();) {
            if (it->turns_remaining <= 0) {
                expired.push_back(*it);
                it = active.erase(it);
            } else {
                ++it;
            }
        }
        return expired;
    }
};
