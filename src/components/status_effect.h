#pragma once
#include <vector>
#include <algorithm>

enum class StatusType : int {
    POISON = 0, // nature damage over time (green)
    BURN,       // fire damage over time (orange)
    BLEED,      // physical damage over time (red)
};

struct StatusEffect {
    StatusType type;
    int damage;          // per tick
    int turns_remaining;
};

struct StatusEffects {
    std::vector<StatusEffect> effects;

    void add(StatusType type, int damage, int turns) {
        // Refresh if already have this type (don't stack, just refresh)
        for (auto& e : effects) {
            if (e.type == type) {
                if (damage > e.damage) e.damage = damage;
                if (turns > e.turns_remaining) e.turns_remaining = turns;
                return;
            }
        }
        effects.push_back({type, damage, turns});
    }

    bool has(StatusType type) const {
        for (auto& e : effects) if (e.type == type) return true;
        return false;
    }

    void tick() {
        for (auto& e : effects) e.turns_remaining--;
        effects.erase(std::remove_if(effects.begin(), effects.end(),
            [](const StatusEffect& e) { return e.turns_remaining <= 0; }), effects.end());
    }

    bool empty() const { return effects.empty(); }
};
