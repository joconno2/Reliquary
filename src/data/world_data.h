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
    const char* rumor4;          // additional local color
    const char* rumor5;
};

static constexpr TownData ALL_TOWNS[] = {
    {500, 375, "Thornwall", true,
     "Crossroads town.", "The Barrow opened up.",
     "Aldric's been in the library all week.", "Priests are nervous.",
     "Trouble underground.",
     "Elder Maren's worried about something.",
     "Someone with a brand came through before. Didn't last."},
    {425, 475, "Millhaven", true,
     "Farm town.", "The Catacombs are open.",
     "Tremors at night.", "Old gate's failing.",
     "Dead are moving underground.",
     "Crops are growing wrong near the dungeon.",
     "Three farmers missing last month."},
    {725, 250, "Candlemere", true,
     "Temple city.", "The priests are upset.",
     "Greywatch garrison on alert.", "Trouble from the north.",
     "The flame keeps flickering.",
     "Temple bells ring at midnight on their own.",
     "Scorch marks inside the sanctum."},
    {525, 225, "Frostmere", true,
     "Cold town.", "Sage Yeva might know something.",
     "The depths are restless.", "Strange reports from the peaks.",
     "Getting colder.",
     "Lake froze in summer.",
     "Yeva's apprentice went north. Hasn't come back."},
    {650, 335, "Greywatch", true,
     "Fortress town.", "Stonekeep is getting worse.",
     "Patrols coming back short.", "Dungeon under the watchtower.",
     "Voss says prepare.",
     "Garrison doubled since spring.",
     "They sealed the lowest cell."},
    {400, 200, "Whitepeak", false,
     "Mountain town.", "Ground shakes sometimes.",
     "Nothing grows up here.", "Quiet.",
     "Cold.",
     "Halvard says the glacier moved.",
     "Sound carries wrong in the peaks."},
    {325, 400, "Bramblewood", true,
     "Forest town.", "Animals acting strange.",
     "Deep woods getting darker.", "Hollowgate is nearby.",
     "Forest is thick here.",
     "Ranger Fael won't go into the deep wood anymore.",
     "Old road got overgrown fast."},
    {275, 275, "Hollowgate", true,
     "Sealed entrance.", "The seal hums.",
     "Nobody goes below.", "Fragments will open it.",
     "Big dungeon below.",
     "The seal is warm.",
     "Daven reads the inscriptions. Says they're instructions."},
    {700, 375, "Ironhearth", true,
     "Forge town.", "The Molten Depths are active.",
     "Good steel here.", "Coast road is dangerous.",
     "Ore's been strange lately.",
     "Brynn's blades keep cracking.",
     "Forges lit up on their own last week."},
    {500, 550, "Dustfall", false,
     "Dry town.", "Rot spreading north.",
     "Ground is sick.", "Hard to heal here.",
     "Bad soil.",
     "Kess burns pyres every week.",
     "Rain was the wrong color last month."},
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
    if (y < 200) return "Frozen Marches";
    if (y < 300 && x > 450) return "Pale Reach";
    if (x < 350) return "Greenwood";
    if (y > 500) return "Dust Provinces";
    if (x > 550) return "Iron Coast";
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
    { 2, GodId::SOLETH},     // Candlemere (Pale Reach)
    { 3, GodId::GATHRUUN},   // Frostmere (Frozen Marches)
    { 6, GodId::KHAEL},      // Bramblewood (Greenwood)
    { 8, GodId::OSSREN},     // Ironhearth (Iron Coast)
    { 9, GodId::SYTHARA},    // Dustfall (Dust Provinces)
};
static constexpr int CHURCH_COUNT = sizeof(CHURCH_LOCATIONS) / sizeof(CHURCH_LOCATIONS[0]);

inline float world_dist(int x1, int y1, int x2, int y2) {
    float dx = static_cast<float>(x1 - x2);
    float dy = static_cast<float>(y1 - y2);
    return std::sqrt(dx * dx + dy * dy);
}
