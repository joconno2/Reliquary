#pragma once
#include <cstdint>
#include <random>

// Seeded PRNG for reproducible dungeon generation and save compatibility.
// Uses xoshiro256** — fast, high quality, small state.
class RNG {
public:
    explicit RNG(uint64_t seed = 0);

    // Core generation
    uint64_t next();
    int range(int min, int max); // inclusive [min, max]
    float range_f(float min, float max);
    bool chance(int percent); // true with given % probability

    // Utility
    uint64_t get_seed() const { return seed_; }
    void reseed(uint64_t seed);

    // Shuffle a container
    template<typename T>
    void shuffle(T& container) {
        for (int i = static_cast<int>(container.size()) - 1; i > 0; --i) {
            int j = range(0, i);
            std::swap(container[i], container[j]);
        }
    }

private:
    uint64_t seed_;
    uint64_t state_[4];

    static uint64_t splitmix64(uint64_t& x);
    static uint64_t rotl(uint64_t x, int k);
};
