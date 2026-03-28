#pragma once

// Energy-based turn system. Each tick, entities gain energy equal to their speed.
// When energy >= ACTION_COST, the entity can act and pays the cost.
// Faster entities act more frequently.

struct Energy {
    int current = 0;
    int speed = 100; // energy gained per tick. 100 = normal, 150 = fast, 50 = slow

    static constexpr int ACTION_COST = 100;

    bool can_act() const { return current >= ACTION_COST; }
    void spend() { current -= ACTION_COST; }
    void gain() { current += speed; }
};
