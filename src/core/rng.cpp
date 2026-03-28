#include "core/rng.h"
#include <chrono>

RNG::RNG(uint64_t seed) {
    if (seed == 0) {
        seed = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }
    reseed(seed);
}

void RNG::reseed(uint64_t seed) {
    seed_ = seed;
    uint64_t s = seed;
    state_[0] = splitmix64(s);
    state_[1] = splitmix64(s);
    state_[2] = splitmix64(s);
    state_[3] = splitmix64(s);
}

uint64_t RNG::splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

uint64_t RNG::rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

uint64_t RNG::next() {
    const uint64_t result = rotl(state_[1] * 5, 7) * 9;
    const uint64_t t = state_[1] << 17;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = rotl(state_[3], 45);
    return result;
}

int RNG::range(int min, int max) {
    if (min >= max) return min;
    uint64_t range = static_cast<uint64_t>(max - min + 1);
    return min + static_cast<int>(next() % range);
}

float RNG::range_f(float min, float max) {
    double t = static_cast<double>(next()) / static_cast<double>(UINT64_MAX);
    return static_cast<float>(min + t * (max - min));
}

bool RNG::chance(int percent) {
    return range(1, 100) <= percent;
}
