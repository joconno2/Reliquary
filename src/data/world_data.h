#pragma once
#include "components/god.h"
#include <cmath>
#include <cstdlib>

// Canonical town data — single source of truth for all towns.
// Referenced by: NPC spawning, quest generation, music proximity,
// overworld population, sign generation, enemy spawn avoidance.

struct TownData {
    int x, y;
    const char* name;
    bool is_quest_town;
    const char* description;     // shown when player enters town
    const char* rumor1;          // NPC gossip about the town/region
    const char* rumor2;
    const char* rumor3;
    const char* nearby_warning;  // what NPCs warn you about nearby
};

static constexpr TownData ALL_TOWNS[] = {
    {500, 375, "Thornwall", true,
     "The crossroads. Every road meets here.", "The Barrow woke something.",
     "Scholar Aldric paces and mutters.", "The priests look afraid.",
     "Something beneath us stirs."},
    {425, 475, "Millhaven", true,
     "Farmland in every direction.", "The Catacombs south are unsealed.",
     "Tremors at night.", "The old gate groans.",
     "Underground, the dead are restless."},
    {725, 250, "Candlemere", true,
     "A temple city. The flame burns eternal.", "The priests weep.",
     "Greywatch garrison is on alert.", "Something in the north calls.",
     "The flame flickers. It never used to."},
    {525, 225, "Frostmere", true,
     "Ice clings to rooftops. Frostmere endures.", "Sage Yeva knows old things.",
     "The frozen depths are restless.", "Strange reports from the peaks.",
     "Cold beyond cold."},
    {650, 335, "Greywatch", true,
     "Grey stone walls. A fortress town.", "Stonekeep northeast is groaning.",
     "Patrols return with fewer men.", "The dungeon below the watchtower...",
     "Captain Voss says prepare."},
    {400, 200, "Whitepeak", false,
     "High in the frozen peaks. Stone and silence.", "The mountain trembles.",
     "Nothing grows but everything endures.", "Deeper stone, deeper silence.",
     "The cold is patient here."},
    {325, 400, "Bramblewood", true,
     "The forest presses in. Moss and green.", "Animals act strangely.",
     "The deep woods are darker.", "Hollowgate is near. Sealed tight.",
     "Nature watches."},
    {275, 275, "Hollowgate", true,
     "Deep in the Greenwood. Ancient and sealed.", "The seal hums.",
     "Nobody goes below.", "The fragments will open it.",
     "Something vast waits."},
    {700, 375, "Ironhearth", true,
     "Hammers never stop. Forge and flame.", "The Molten Depths burn hotter.",
     "The craft blesses the steel.", "The coast road is dangerous.",
     "Something pulls at metal."},
    {500, 550, "Dustfall", false,
     "Dry wind and empty wells.", "The rot creeps north.",
     "The ground is sick.", "Nothing heals here.",
     "Plague-touched soil."},
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
    if (y < 200) return GodId::GATHRUUN;
    if (y < 300 && x > 450) return GodId::SOLETH;
    if (x < 350) return GodId::KHAEL;
    if (y > 500) return GodId::SYTHARA;
    if (x > 550) return GodId::OSSREN;
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

// Province capital towns — one per province, these get churches.
// Each maps to a town index in ALL_TOWNS.
struct ChurchLocation {
    int town_idx;  // index into ALL_TOWNS
    GodId god;
};

// 6 churches, one per province god
static constexpr ChurchLocation CHURCH_LOCATIONS[] = {
    { 0, GodId::MORRETH},   // Thornwall (Heartlands)
    {12, GodId::SOLETH},     // Candlemere (Pale Reach)
    { 5, GodId::GATHRUUN},   // Frostmere (Frozen Marches)
    { 6, GodId::KHAEL},      // Bramblewood (Greenwood)
    { 7, GodId::OSSREN},     // Ironhearth (Iron Coast)
    { 8, GodId::SYTHARA},    // Dustfall (Dust Provinces)
};
static constexpr int CHURCH_COUNT = sizeof(CHURCH_LOCATIONS) / sizeof(CHURCH_LOCATIONS[0]);

inline float world_dist(int x1, int y1, int x2, int y2) {
    float dx = static_cast<float>(x1 - x2);
    float dy = static_cast<float>(y1 - y2);
    return std::sqrt(dx * dx + dy * dy);
}
