#pragma once
#include "components/god.h"
#include <cmath>
#include <cstdlib>

// Canonical town data — single source of truth for all 20 towns.
// Referenced by: NPC spawning, quest generation, music proximity,
// overworld population, sign generation, enemy spawn avoidance.

struct TownData {
    int x, y;
    const char* name;
    bool is_quest_town; // has main quest NPCs
};

static constexpr TownData ALL_TOWNS[] = {
    {1000,  750, "Thornwall",    true },
    { 750,  650, "Ashford",      true },
    {1300,  670, "Greywatch",    true },
    { 850,  950, "Millhaven",    true },
    {1200,  930, "Stonehollow",  false},
    {1050,  450, "Frostmere",    true },
    { 650,  800, "Bramblewood",  false},
    {1400,  750, "Ironhearth",   true },
    {1000, 1100, "Dustfall",     false},
    { 800,  400, "Whitepeak",    false},
    {1250, 1100, "Drywell",      false},
    { 550,  550, "Hollowgate",   true },
    {1450,  500, "Candlemere",   true },
    { 900, 1200, "Sandmoor",     false},
    {1100,  300, "Glacierveil",  false},
    { 700, 1050, "Tanglewood",   false},
    {1350, 1000, "Redrock",      false},
    {1150,  550, "Ravenshold",   false},
    { 600,  700, "Fenwatch",     false},
    {1500,  850, "Endgate",      false},
};

static constexpr int TOWN_COUNT = sizeof(ALL_TOWNS) / sizeof(ALL_TOWNS[0]);
static constexpr int TOWN_RADIUS = 30;

// Find which town an NPC/position is near. Returns index into ALL_TOWNS or -1.
inline int near_town(int x, int y, int radius = TOWN_RADIUS) {
    for (int i = 0; i < TOWN_COUNT; i++) {
        if (std::abs(x - ALL_TOWNS[i].x) < radius &&
            std::abs(y - ALL_TOWNS[i].y) < radius)
            return i;
    }
    return -1;
}

// Find which quest town (subset) an NPC is near. Returns quest town index or -1.
// Quest town indices match the order of quest towns in ALL_TOWNS.
inline int near_quest_town(int x, int y) {
    int qi = 0;
    for (int i = 0; i < TOWN_COUNT; i++) {
        if (!ALL_TOWNS[i].is_quest_town) continue;
        if (std::abs(x - ALL_TOWNS[i].x) < TOWN_RADIUS &&
            std::abs(y - ALL_TOWNS[i].y) < TOWN_RADIUS)
            return qi;
        qi++;
    }
    return -1;
}

// Province-based god affiliation from world position.
// Matches generate_overworld.py PROVINCES:
//   Pale Reach (north-east) = Soleth, Frozen Marches (far north) = Gathruun,
//   Heartlands (center) = Morreth, Greenwood (west) = Khael,
//   Iron Coast (east) = Ossren, Dust Provinces (south) = Sythara
inline GodId get_town_god(int x, int y) {
    if (y < 400) return GodId::GATHRUUN;
    if (y < 600 && x > 900) return GodId::SOLETH;
    if (x < 700) return GodId::KHAEL;
    if (y > 1000) return GodId::SYTHARA;
    if (x > 1100) return GodId::OSSREN;
    return GodId::MORRETH;
}

// Compass direction string from one point to another.
inline const char* compass_dir(int from_x, int from_y, int to_x, int to_y) {
    int dx = to_x - from_x;
    int dy = to_y - from_y;
    if (std::abs(dx) < 30 && std::abs(dy) < 30) return "nearby";
    bool n = dy < -30, s = dy > 30, e = dx > 30, w = dx < -30;
    if (n && e) return "northeast";
    if (n && w) return "northwest";
    if (s && e) return "southeast";
    if (s && w) return "southwest";
    if (n) return "north";
    if (s) return "south";
    if (e) return "east";
    return "west";
}

// Province name from world position (matches get_town_god zones).
inline const char* get_province_name(int x, int y) {
    if (y < 400) return "Frozen Marches";
    if (y < 600 && x > 900) return "Pale Reach";
    if (x < 700) return "Greenwood";
    if (y > 1000) return "Dust Provinces";
    if (x > 1100) return "Iron Coast";
    return "Heartlands";
}

inline float world_dist(int x1, int y1, int x2, int y2) {
    float dx = static_cast<float>(x1 - x2);
    float dy = static_cast<float>(y1 - y2);
    return std::sqrt(dx * dx + dy * dy);
}
