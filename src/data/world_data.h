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
    bool is_quest_town;
    const char* description;     // shown when player enters town
    const char* rumor1;          // NPC gossip about the town/region
    const char* rumor2;
    const char* rumor3;
    const char* nearby_warning;  // what NPCs warn you about nearby
};

static constexpr TownData ALL_TOWNS[] = {
    {1000,  750, "Thornwall",    true,
     "The crossroads of the Heartlands. Thornwall sits where every road meets.",
     "The Barrow north of town has been making noises at night.",
     "Traders from Ashford stopped coming last month. Nobody knows why.",
     "Morreth's church stands at the center. The priests have been on edge.",
     "The Barrow's seal broke. Whatever was inside is awake."},

    { 750,  650, "Ashford",      true,
     "A quiet town at the forest's edge. Smoke rises from old chimneys.",
     "The ruins east of here are older than the town. People avoid them.",
     "Wolves have been coming closer to the walls. Bigger ones than before.",
     "A scholar came through asking about inscriptions in the ruins. Haven't seen him since.",
     "The forest is deeper than it used to be. Trees where there weren't trees."},

    {1300,  670, "Greywatch",    true,
     "A fortress town on the eastern ridge. Grey stone walls, grey sky.",
     "The garrison sends patrols south but they come back with fewer men.",
     "Ironhearth's forges burn day and night. They're building for something.",
     "There's a dungeon beneath the old watch tower. The guards won't talk about it.",
     "Bandits on the coast road have gotten bolder."},

    { 850,  950, "Millhaven",    true,
     "Farmland stretches in every direction. Millhaven feeds the Heartlands.",
     "The southern provinces are drying up. Refugees trickle in every week.",
     "Old Stonehollow has the best smiths, but the road there is rough.",
     "Something's been killing cattle in the fields. Not wolves. The wounds are wrong.",
     "Don't go into the grain stores after dark. Rats the size of dogs."},

    {1200,  930, "Stonehollow",  false,
     "Built into the hillside. Half the town is underground.",
     "The mines go deeper than anyone's mapped. Some shafts just keep going.",
     "Redrock to the south is practically lawless. Avoid it if you can.",
     "The dwarven tunnels connect to something older. We don't dig that direction.",
     "Quakes have been getting stronger. The stone remembers."},

    {1050,  450, "Frostmere",    true,
     "Ice clings to the rooftops. Frostmere endures at the edge of the frozen north.",
     "Gathruun's followers say the mountain is speaking. They won't say what it says.",
     "Glacierveil is cut off. Snow blocked the pass two weeks ago.",
     "The lake froze solid last winter. Something moved under the ice.",
     "Fur traders from Whitepeak bring strange stories. Lights in the peaks."},

    { 650,  800, "Bramblewood",  false,
     "The forest presses in on every side. Bramblewood smells of moss and green things.",
     "Khael's druids say the trees are angry. That's never good.",
     "Fenwatch is close, but the path through the deep woods is treacherous.",
     "The herbalists here know poisons and cures in equal measure.",
     "Something old lives in the heart of the forest. The druids leave it offerings."},

    {1400,  750, "Ironhearth",   true,
     "The sound of hammers never stops. Ironhearth is forge and flame.",
     "Ossren's church runs the town. The smiths pray before every pour.",
     "The coast road to Endgate is dangerous. Slavers and worse.",
     "Steel from Ironhearth arms half the continent. The rest use what they find.",
     "The deep forge cracked last month. Something came up through the floor."},

    {1000, 1100, "Dustfall",     false,
     "Dry wind and empty wells. Dustfall clings to life in the dying south.",
     "Sythara's followers are growing. The plague gave them converts.",
     "Drywell earned its name. Three wells and not a drop.",
     "The sick are taken to the temple. Some come back. Most don't.",
     "The dust storms carry more than sand. Don't breathe deep."},

    { 800,  400, "Whitepeak",    false,
     "Perched on the mountainside. The air is thin and the people are hard.",
     "The mines pay well but miners keep disappearing into the deep shafts.",
     "Frostmere trades with us when the pass is open. That's half the year.",
     "Whitepeak goats are tougher than the people. That's saying something.",
     "Something howls in the peaks at night. Not wolves. Wolves don't echo like that."},

    {1250, 1100, "Drywell",      false,
     "Three wells, all dry. Drywell survives on rainwater and stubbornness.",
     "Caravans from Stonehollow bring water. When they come.",
     "The Dust Provinces are dying. Everyone knows it. Nobody says it.",
     "Sandmoor still has water. They charge for it.",
     "Bones in the sand. Not human, but not anything I recognize either."},

    { 550,  550, "Hollowgate",   true,
     "The town sits above a vast cavern. The gate below is older than memory.",
     "The gate was sealed for centuries. Now it's open and things come up at night.",
     "The scholars think the Reliquary's heart is down there. They're probably right.",
     "Hollowgate was built to watch the gate. Now it watches us.",
     "Don't look into the pit after sundown. Something looks back."},

    {1450,  500, "Candlemere",   true,
     "Every window holds a candle. Soleth's city of light against the dark.",
     "The priests say the fire wards off the old things. They're not wrong.",
     "Zealots patrol the streets. They mean well. Mostly.",
     "Candlemere burns more oil than any three towns. They can afford it.",
     "The undead avoid this place. The light hurts them. That should tell you something."},

    { 900, 1200, "Sandmoor",     false,
     "The last real settlement before the deep desert. Water is currency here.",
     "Trade caravans stop here or they don't make it to the coast.",
     "The old roads south lead nowhere anyone comes back from.",
     "Scorpions the size of dogs. You get used to them.",
     "The oasis is shrinking. Another decade and Sandmoor joins the sand."},

    {1100,  300, "Glacierveil",  false,
     "Half-buried in ice. Glacierveil persists where nothing should.",
     "The glacier moves. Slowly, but it moves. Buildings shift every year.",
     "The ice preserves things. Sometimes you find old things. Sometimes they're alive.",
     "No one comes here by accident. What brought you?",
     "The aurora speaks to those who listen. Don't listen."},

    { 700, 1050, "Tanglewood",   false,
     "The forest swallowed this town generations ago. The people stayed anyway.",
     "Bramblewood thinks they're wild. They should see Tanglewood.",
     "The trees move at night. Not the wind. The trees themselves.",
     "A witch lives deeper in. She trades in secrets. The price is always too high.",
     "Mushrooms grow on everything here. Some are edible. Good luck guessing which."},

    {1350, 1000, "Redrock",      false,
     "Red dust, red stone, red tempers. Redrock is where you go when nowhere else will have you.",
     "No law here worth the name. Keep your hand on your purse.",
     "The fighting pits run every night. Good money if you survive.",
     "Stonehollow looks down on us. We don't care.",
     "Bandits, smugglers, and the occasional honest person. Guess which is rarest."},

    {1150,  550, "Ravenshold",   false,
     "The ravens arrived before the people. They'll be here after.",
     "Corvids circle the old tower day and night. Scholars say it means nothing.",
     "Between Frostmere and Greywatch, Ravenshold gets forgotten. We prefer it that way.",
     "The tower has a library. Most of the books are ruined. The ones that aren't are worse.",
     "A raven followed me for three days once. It knew my name. I didn't ask how."},

    { 600,  700, "Fenwatch",     false,
     "Built on stilts above the marsh. The water rises every spring.",
     "The fen holds bodies from a war nobody remembers.",
     "Bramblewood is the nearest real town. An hour through the bog if you know the path.",
     "The bog lights lead you in circles. Follow the raven posts instead.",
     "Something lives in the deep fen. We don't name it."},

    {1500,  850, "Endgate",      false,
     "The last town before the eastern sea. Beyond here, there's nothing.",
     "Ships used to come. The harbor is empty now.",
     "Ironhearth sends steel. We send it across the water. To whom, I've stopped asking.",
     "The lighthouse still burns. Nobody lights it. Nobody knows how to stop it.",
     "Endgate is a good name. This is where the world ends."},
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
