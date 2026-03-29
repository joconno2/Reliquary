#include "generation/quest_gen.h"
#include "components/position.h"
#include "components/npc.h"
#include "components/dynamic_quest.h"
#include <cmath>
#include <cstdio>

namespace quest_gen {

// Town names used for cross-town delivery quests
static const char* TOWN_NAMES[] = {
    "Thornwall", "Ashford", "Greywatch", "Millhaven", "Stonehollow",
    "Frostmere", "Bramblewood", "Ironhearth", "Dustfall", "Whitepeak",
    "Drywell", "Hollowgate", "Candlemere", "Sandmoor", "Glacierveil",
    "Tanglewood", "Redrock", "Ravenshold", "Fenwatch", "Endgate",
};
static constexpr int TOWN_COUNT = sizeof(TOWN_NAMES) / sizeof(TOWN_NAMES[0]);

// Pick a random town name that isn't the current one
static const char* other_town(RNG& rng, const std::string& current) {
    for (int i = 0; i < 20; i++) {
        const char* pick = TOWN_NAMES[rng.range(0, TOWN_COUNT - 1)];
        if (current != pick) return pick;
    }
    return "a distant settlement";
}

static DynamicQuest make_farmer_quest(RNG& rng, const std::string& town) {
    static const char* CREATURES[] = {"bear", "wolf pack", "spider nest", "boar", "wild dog pack"};
    static const char* LOCATIONS[] = {"the hills", "the forest", "the fields", "the riverbank"};
    int ci = rng.range(0, 4);
    int li = rng.range(0, 3);

    DynamicQuest q;
    char buf[256];
    snprintf(buf, sizeof(buf), "The %s of %s", CREATURES[ci], town.c_str());
    q.name = buf;
    snprintf(buf, sizeof(buf),
        "A %s near %s has been threatening the farms. "
        "Someone needs to deal with it before the next harvest.",
        CREATURES[ci], town.c_str());
    q.description = buf;
    snprintf(buf, sizeof(buf), "Kill the %s near %s in %s.",
             CREATURES[ci], town.c_str(), LOCATIONS[li]);
    q.objective = buf;
    snprintf(buf, sizeof(buf), "The %s is dealt with. The farmers can sleep again.", CREATURES[ci]);
    q.complete_text = buf;
    q.xp_reward = 25 + rng.range(0, 15);
    q.gold_reward = 10 + rng.range(0, 10);
    return q;
}

static DynamicQuest make_guard_quest(RNG& rng, const std::string& town) {
    int variant = rng.range(0, 2);
    DynamicQuest q;
    char buf[256];

    if (variant == 0) {
        snprintf(buf, sizeof(buf), "Clear the Depths near %s", town.c_str());
        q.name = buf;
        int depth = rng.range(1, 3);
        snprintf(buf, sizeof(buf),
            "The guard wants the dungeon entrance near %s cleared. "
            "Monsters have been wandering out at night.",
            town.c_str());
        q.description = buf;
        snprintf(buf, sizeof(buf), "Clear dungeon level %d near %s.", depth, town.c_str());
        q.objective = buf;
        q.complete_text = "The guard nods. One less thing to worry about.";
        q.xp_reward = 30 + rng.range(0, 20);
        q.gold_reward = 15 + rng.range(0, 15);
    } else if (variant == 1) {
        snprintf(buf, sizeof(buf), "Patrol the Road to %s", other_town(rng, town));
        q.name = buf;
        snprintf(buf, sizeof(buf),
            "The roads near %s have grown dangerous. "
            "The guard needs someone to patrol the route and report what they find.",
            town.c_str());
        q.description = buf;
        snprintf(buf, sizeof(buf), "Patrol the road near %s and return.", town.c_str());
        q.objective = buf;
        q.complete_text = "The guard marks your report. The road is a little safer.";
        q.xp_reward = 20 + rng.range(0, 15);
        q.gold_reward = 10 + rng.range(0, 10);
    } else {
        snprintf(buf, sizeof(buf), "Bandit Trouble near %s", town.c_str());
        q.name = buf;
        snprintf(buf, sizeof(buf),
            "Bandits have been spotted on the roads near %s. "
            "The guard doesn't have the men to spare.",
            town.c_str());
        q.description = buf;
        snprintf(buf, sizeof(buf), "Deal with the bandits near %s.", town.c_str());
        q.objective = buf;
        q.complete_text = "The bandits have scattered. For now.";
        q.xp_reward = 35 + rng.range(0, 15);
        q.gold_reward = 20 + rng.range(0, 15);
    }
    return q;
}

static DynamicQuest make_blacksmith_quest(RNG& rng, const std::string& town) {
    int variant = rng.range(0, 1);
    DynamicQuest q;
    char buf[256];

    if (variant == 0) {
        const char* dest = other_town(rng, town);
        snprintf(buf, sizeof(buf), "Delivery to %s", dest);
        q.name = buf;
        snprintf(buf, sizeof(buf),
            "The blacksmith in %s has a commission to deliver to %s. "
            "The roads are too dangerous for a smith to travel alone.",
            town.c_str(), dest);
        q.description = buf;
        snprintf(buf, sizeof(buf), "Deliver the smithwork to %s.", dest);
        q.objective = buf;
        q.complete_text = "The delivery is made. The smith will be pleased.";
        q.xp_reward = 25 + rng.range(0, 10);
        q.gold_reward = 20 + rng.range(0, 15);
    } else {
        static const char* ORES[] = {"dark iron", "moonstone ore", "red copper", "deep tin"};
        int oi = rng.range(0, 3);
        snprintf(buf, sizeof(buf), "Rare %s", ORES[oi]);
        q.name = buf;
        snprintf(buf, sizeof(buf),
            "The blacksmith in %s needs %s from the nearby dungeons. "
            "The veins are deep and the tunnels aren't empty.",
            town.c_str(), ORES[oi]);
        q.description = buf;
        snprintf(buf, sizeof(buf), "Find %s in the dungeon near %s.", ORES[oi], town.c_str());
        q.objective = buf;
        snprintf(buf, sizeof(buf), "Good ore. The smith weighs it and nods.");
        q.complete_text = buf;
        q.xp_reward = 30 + rng.range(0, 15);
        q.gold_reward = 15 + rng.range(0, 10);
    }
    return q;
}

static DynamicQuest make_scholar_quest(RNG& rng, const std::string& town) {
    int variant = rng.range(0, 1);
    DynamicQuest q;
    char buf[256];

    if (variant == 0) {
        static const char* ARTIFACTS[] = {
            "a carved idol", "an ancient scroll case", "a stone sigil", "a runed tablet"
        };
        int ai = rng.range(0, 3);
        snprintf(buf, sizeof(buf), "The %s Scholar's Request", town.c_str());
        q.name = buf;
        snprintf(buf, sizeof(buf),
            "The scholar in %s has read about %s in the dungeons nearby. "
            "It could answer questions that have gone unanswered for centuries.",
            town.c_str(), ARTIFACTS[ai]);
        q.description = buf;
        snprintf(buf, sizeof(buf), "Find %s in the dungeon near %s.", ARTIFACTS[ai], town.c_str());
        q.objective = buf;
        q.complete_text = "The scholar examines the artifact with trembling hands. Knowledge has a price.";
        q.xp_reward = 35 + rng.range(0, 15);
        q.gold_reward = 10 + rng.range(0, 10);
    } else {
        snprintf(buf, sizeof(buf), "Inscriptions of %s", town.c_str());
        q.name = buf;
        snprintf(buf, sizeof(buf),
            "The scholar in %s believes there are old inscriptions in the dungeon nearby. "
            "They need to be transcribed before the stone crumbles.",
            town.c_str());
        q.description = buf;
        snprintf(buf, sizeof(buf), "Read the inscription deep in the dungeon near %s.", town.c_str());
        q.objective = buf;
        q.complete_text = "The transcription is complete. The scholar reads in silence.";
        q.xp_reward = 30 + rng.range(0, 10);
        q.gold_reward = 5 + rng.range(0, 10);
    }
    return q;
}

static DynamicQuest make_herbalist_quest(RNG& rng, const std::string& town) {
    static const char* HERBS[] = {
        "moonpetal", "bittervine", "frostmoss", "ashbloom", "nightroot"
    };
    int hi = rng.range(0, 4);
    DynamicQuest q;
    char buf[256];

    snprintf(buf, sizeof(buf), "Gathering %s", HERBS[hi]);
    q.name = buf;
    snprintf(buf, sizeof(buf),
        "The herbalist in %s needs %s. It grows in the wild places "
        "where most people have the sense not to wander.",
        town.c_str(), HERBS[hi]);
    q.description = buf;
    snprintf(buf, sizeof(buf), "Gather %s from the wilderness near %s.", HERBS[hi], town.c_str());
    q.objective = buf;
    snprintf(buf, sizeof(buf), "The herbalist inspects the %s. Fresh enough. It will do.", HERBS[hi]);
    q.complete_text = buf;
    q.xp_reward = 20 + rng.range(0, 10);
    q.gold_reward = 10 + rng.range(0, 10);
    return q;
}

static DynamicQuest make_merchant_quest(RNG& rng, const std::string& town) {
    const char* dest = other_town(rng, town);
    DynamicQuest q;
    char buf[256];

    snprintf(buf, sizeof(buf), "Trade Route to %s", dest);
    q.name = buf;
    snprintf(buf, sizeof(buf),
        "The merchant in %s wants to establish a trade route to %s. "
        "Someone needs to make the journey and negotiate terms.",
        town.c_str(), dest);
    q.description = buf;
    snprintf(buf, sizeof(buf), "Travel to %s and negotiate a trade agreement.", dest);
    q.objective = buf;
    q.complete_text = "The route is established. Goods will flow. Coin will follow.";
    q.xp_reward = 25 + rng.range(0, 10);
    q.gold_reward = 25 + rng.range(0, 20);
    return q;
}

void generate_town_quests(World& world, [[maybe_unused]] const TileMap& map, RNG& rng,
                           int town_cx, int town_cy, const std::string& town_name) {
    // Find all NPCs within 30 tiles of town center
    auto& npc_pool = world.pool<NPC>();
    int quests_assigned = 0;
    constexpr int MAX_QUESTS_PER_TOWN = 3;

    for (size_t i = 0; i < npc_pool.size(); i++) {
        if (quests_assigned >= MAX_QUESTS_PER_TOWN) break;

        Entity e = npc_pool.entity_at(i);
        if (!world.has<Position>(e)) continue;

        auto& pos = world.get<Position>(e);
        if (std::abs(pos.x - town_cx) > 30 || std::abs(pos.y - town_cy) > 30) continue;

        auto& npc = npc_pool.at_index(i);

        // Skip NPCs that already have a static quest
        if (npc.quest_id >= 0) continue;

        // Skip NPCs that already have a dynamic quest
        if (world.has<DynamicQuest>(e)) continue;

        // 40% chance to assign a quest
        if (!rng.chance(40)) continue;

        DynamicQuest dq;
        switch (npc.role) {
            case NPCRole::FARMER:
                dq = make_farmer_quest(rng, town_name);
                break;
            case NPCRole::GUARD:
                dq = make_guard_quest(rng, town_name);
                break;
            case NPCRole::BLACKSMITH:
                dq = make_blacksmith_quest(rng, town_name);
                break;
            case NPCRole::PRIEST:
                // Scholar or priest — different quest if herbalist
                if (npc.name == "Herbalist") {
                    dq = make_herbalist_quest(rng, town_name);
                } else {
                    dq = make_scholar_quest(rng, town_name);
                }
                break;
            case NPCRole::SHOPKEEPER:
                if (npc.name == "Merchant") {
                    dq = make_merchant_quest(rng, town_name);
                } else {
                    continue; // regular shopkeepers open the shop, not quest givers
                }
                break;
            case NPCRole::ELDER:
                continue; // Elder has main quest
        }

        world.add<DynamicQuest>(e, std::move(dq));
        quests_assigned++;
    }
}

} // namespace quest_gen
