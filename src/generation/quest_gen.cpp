#include "generation/quest_gen.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/npc.h"
#include "components/item.h"
#include "components/quest.h"
#include "components/quest_target.h"
#include "components/dynamic_quest.h"
#include "core/spritesheet.h"
#include "generation/populate.h"
#include "ui/message_log.h"
#include <cmath>
#include <cstdio>

namespace quest_gen {

// Town registry with coordinates — used for cross-town delivery quests
struct TownRef {
    const char* name;
    int x, y;
};

static const TownRef ALL_TOWNS[] = {
    {"Thornwall",    1000, 750},
    {"Ashford",       750, 650},
    {"Greywatch",    1300, 670},
    {"Millhaven",     850, 950},
    {"Stonehollow",  1200, 930},
    {"Frostmere",    1050, 450},
    {"Bramblewood",   650, 800},
    {"Ironhearth",   1400, 750},
    {"Dustfall",     1000, 1100},
    {"Whitepeak",     800, 400},
    {"Drywell",      1250, 1100},
    {"Hollowgate",    550, 550},
    {"Candlemere",   1450, 500},
    {"Sandmoor",      900, 1200},
    {"Glacierveil",  1100, 300},
    {"Tanglewood",    700, 1050},
    {"Redrock",      1350, 1000},
    {"Ravenshold",   1150, 550},
    {"Fenwatch",      600, 700},
    {"Endgate",      1500, 850},
};
static constexpr int TOWN_REF_COUNT = sizeof(ALL_TOWNS) / sizeof(ALL_TOWNS[0]);

// Pick a random town that isn't the current one
static const TownRef& other_town(RNG& rng, const std::string& current) {
    for (int i = 0; i < 20; i++) {
        int pick = rng.range(0, TOWN_REF_COUNT - 1);
        if (current != ALL_TOWNS[pick].name) return ALL_TOWNS[pick];
    }
    return ALL_TOWNS[0]; // fallback
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
    // Must spend time in the wilderness
    q.min_turns = 40 + rng.range(0, 30);
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
        q.requires_dungeon = true;
    } else if (variant == 1) {
        auto& dest = other_town(rng, town);
        snprintf(buf, sizeof(buf), "Patrol the Road to %s", dest.name);
        q.name = buf;
        snprintf(buf, sizeof(buf),
            "The roads near %s have grown dangerous. "
            "The guard needs someone to patrol the route to %s and report what they find.",
            town.c_str(), dest.name);
        q.description = buf;
        snprintf(buf, sizeof(buf), "Travel to %s and return.", dest.name);
        q.objective = buf;
        q.complete_text = "The guard marks your report. The road is a little safer.";
        q.xp_reward = 20 + rng.range(0, 15);
        q.gold_reward = 10 + rng.range(0, 10);
        // Must actually travel to the destination town
        q.target_town_x = dest.x;
        q.target_town_y = dest.y;
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
        q.min_turns = 30 + rng.range(0, 20);
    }
    return q;
}

static DynamicQuest make_blacksmith_quest(RNG& rng, const std::string& town) {
    int variant = rng.range(0, 1);
    DynamicQuest q;
    char buf[256];

    if (variant == 0) {
        auto& dest = other_town(rng, town);
        snprintf(buf, sizeof(buf), "Delivery to %s", dest.name);
        q.name = buf;
        snprintf(buf, sizeof(buf),
            "The blacksmith in %s has a commission to deliver to %s. "
            "The roads are too dangerous for a smith to travel alone.",
            town.c_str(), dest.name);
        q.description = buf;
        snprintf(buf, sizeof(buf), "Deliver the smithwork to %s.", dest.name);
        q.objective = buf;
        q.complete_text = "The delivery is made. The smith will be pleased.";
        q.xp_reward = 25 + rng.range(0, 10);
        q.gold_reward = 20 + rng.range(0, 15);
        // Must travel to destination town
        q.target_town_x = dest.x;
        q.target_town_y = dest.y;
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
        q.requires_dungeon = true;
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
        q.requires_dungeon = true;
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
        q.requires_dungeon = true;
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
    // Must spend time in the wilderness
    q.min_turns = 30 + rng.range(0, 20);
    return q;
}

static DynamicQuest make_merchant_quest(RNG& rng, const std::string& town) {
    auto& dest = other_town(rng, town);
    DynamicQuest q;
    char buf[256];

    snprintf(buf, sizeof(buf), "Trade Route to %s", dest.name);
    q.name = buf;
    snprintf(buf, sizeof(buf),
        "The merchant in %s wants to establish a trade route to %s. "
        "Someone needs to make the journey and negotiate terms.",
        town.c_str(), dest.name);
    q.description = buf;
    snprintf(buf, sizeof(buf), "Travel to %s and negotiate a trade agreement.", dest.name);
    q.objective = buf;
    q.complete_text = "The route is established. Goods will flow. Coin will follow.";
    q.xp_reward = 25 + rng.range(0, 10);
    q.gold_reward = 25 + rng.range(0, 20);
    // Must travel to destination
    q.target_town_x = dest.x;
    q.target_town_y = dest.y;
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

void spawn_quest_content(World& world, const TileMap& map,
                          const std::vector<Room>& rooms,
                          int dungeon_level,
                          const DungeonContext* dungeon_ctx,
                          QuestJournal& journal, MessageLog& log) {
    // Spawn quest bosses at specific depths
    if (dungeon_level == 3) {
        // Barrow Wight — bottom of The Warrens (first dungeon)
        // Uses death knight sprite (row 4 col 3) for a menacing undead look
        Entity wight = populate::spawn_boss(world, map, rooms,
            "Barrow Wight", SHEET_MONSTERS, 3, 4,
            45, 16, 12, 14, 8, 3, 90, 100);
        if (wight != NULL_ENTITY) {
            world.add<QuestTarget>(wight, {QuestId::MQ_01_BARROW_WIGHT, true});
        }
    }

    // The Sepulchre depth-triggered quests (MQ_15/16/17)
    bool in_sepulchre = dungeon_ctx && dungeon_ctx->zone == "sepulchre";
    if (in_sepulchre) {
        // MQ_15: auto-activate on entering The Sepulchre
        if (dungeon_level == 1 && !journal.has_quest(QuestId::MQ_15_THE_SEPULCHRE)) {
            auto prereq = QuestId::MQ_14_HOLLOWGATE_SEAL;
            if (journal.has_quest(prereq) && journal.get_state(prereq) == QuestState::FINISHED) {
                journal.add_quest(QuestId::MQ_15_THE_SEPULCHRE);
                log.add("Quest started: Enter The Sepulchre", {220, 200, 100, 255});
                log.add("Your god is screaming.", {180, 80, 80, 255});
            }
        }
        // MQ_16: auto-activate at depth 4+
        if (dungeon_level >= 4 && !journal.has_quest(QuestId::MQ_16_THE_DESCENT)) {
            if (journal.has_quest(QuestId::MQ_15_THE_SEPULCHRE) &&
                journal.get_state(QuestId::MQ_15_THE_SEPULCHRE) == QuestState::ACTIVE) {
                journal.set_state(QuestId::MQ_15_THE_SEPULCHRE, QuestState::COMPLETE);
                journal.set_state(QuestId::MQ_15_THE_SEPULCHRE, QuestState::FINISHED);
                journal.add_quest(QuestId::MQ_16_THE_DESCENT);
                log.add("Quest started: The Descent", {220, 200, 100, 255});
                log.add("The architecture stops making sense. You hear other footsteps.", {180, 80, 80, 255});
            }
        }
        // MQ_17: auto-activate at depth 6 (the bottom)
        if (dungeon_level >= 6 && !journal.has_quest(QuestId::MQ_17_CLAIM_RELIQUARY)) {
            if (journal.has_quest(QuestId::MQ_16_THE_DESCENT) &&
                journal.get_state(QuestId::MQ_16_THE_DESCENT) == QuestState::ACTIVE) {
                journal.set_state(QuestId::MQ_16_THE_DESCENT, QuestState::COMPLETE);
                journal.set_state(QuestId::MQ_16_THE_DESCENT, QuestState::FINISHED);
                journal.add_quest(QuestId::MQ_17_CLAIM_RELIQUARY);
                log.add("Quest started: Claim the Reliquary", {220, 200, 100, 255});
                log.add("You see it. A vessel of light that hurts to look at.", {255, 220, 100, 255});
            }
        }
        // Sepulchre atmospheric entry messages
        static const char* SEPULCHRE_ENTRY[] = {
            "The air changes. Something is wrong with this place.",
            "The walls here are older than stone should be.",
            "The geometry stops making sense. Corners that shouldn't exist.",
            "You hear footsteps that aren't yours.",
            "The walls are breathing.",
            "The Reliquary is here. You can feel it pulling.",
        };
        if (dungeon_level >= 1 && dungeon_level <= 6) {
            log.add(SEPULCHRE_ENTRY[dungeon_level - 1], {160, 100, 140, 255});
        }
    }

    // Spawn quest items at the bottom of their respective dungeons
    if (dungeon_ctx) {
        // Helper: spawn a quest item in the last room
        auto spawn_quest_item = [&](const char* name, const char* desc,
                                    int sprite_x, int sprite_y,
                                    QuestId qid,
                                    SDL_Color tint = {255,255,255,255}) {
            if (rooms.size() < 2) return;
            auto& room = rooms.back();
            int x = room.cx();
            int y = room.cy() + 1;
            Entity e = world.create();
            world.add<Position>(e, {x, y});
            world.add<Renderable>(e, {SHEET_ITEMS, sprite_x, sprite_y, tint, 2});
            Item item;
            item.name = name;
            item.description = desc;
            item.type = ItemType::KEY;
            item.identified = true;
            item.quest_id = static_cast<int>(qid);
            item.gold_value = 0;
            world.add<Item>(e, std::move(item));
        };

        // Determine the zone's max depth to know if we're at the bottom
        struct ZoneMax { const char* key; int max_depth; };
        static const ZoneMax ZONE_DEPTHS[] = {
            {"warrens", 3}, {"stonekeep", 6}, {"deep_halls", 9},
            {"catacombs", 12}, {"molten", 15}, {"sunken", 18},
            {"sepulchre", 6}, // Sepulchre has 6 levels
        };
        int zone_max = 3; // default
        for (auto& zd : ZONE_DEPTHS) {
            if (dungeon_ctx->zone == zd.key) { zone_max = zd.max_depth; break; }
        }
        bool is_bottom = (dungeon_level >= zone_max);

        if (is_bottom) {
            // MQ_03: Stone Tablet in Ashford Ruins
            if (dungeon_ctx->quest == "MQ_03") {
                spawn_quest_item("Stone Tablet",
                    "A heavy stone tablet. The inscriptions shift when you aren't looking.",
                    0, 21, QuestId::MQ_03_ASHFORD_TABLET);
            }
            // MQ_05: Ancient Inscription in Stonekeep
            if (dungeon_ctx->quest == "MQ_05") {
                spawn_quest_item("Ancient Inscription",
                    "A page of burned stone. The words are too heavy for the rock.",
                    7, 21, QuestId::MQ_05_STONEKEEP_DEPTHS);
            }
            // MQ_07: Frozen Key in Frostmere Depths
            if (dungeon_ctx->quest == "MQ_07") {
                spawn_quest_item("Frozen Key",
                    "A key of impossible cold. It burns your hand.",
                    2, 22, QuestId::MQ_07_FROZEN_KEY);
            }
            // MQ_09: Reliquary Fragment in The Catacombs
            if (dungeon_ctx->quest == "MQ_08") {
                spawn_quest_item("Reliquary Fragment",
                    "A shard of solidified memory. It hums with warmth.",
                    2, 16, QuestId::MQ_09_OSSUARY_FRAGMENT);
            }
            // MQ_11: Molten Fragment in The Molten Depths
            if (dungeon_ctx->quest == "MQ_11") {
                spawn_quest_item("Molten Fragment",
                    "Cold even in the heart of the furnace. Two of three.",
                    2, 16, QuestId::MQ_11_MOLTEN_TRIAL,
                    {255, 120, 80, 255}); // red tint
            }
            // MQ_13: Sunken Fragment in The Sunken Halls
            if (dungeon_ctx->quest == "MQ_13") {
                spawn_quest_item("Sunken Fragment",
                    "The water remembers. Three fragments. They pull toward each other.",
                    2, 16, QuestId::MQ_13_SUNKEN_FRAGMENT,
                    {100, 160, 255, 255}); // blue tint
            }
            // MQ_14: Seal Stone in The Hollowgate
            if (dungeon_ctx->quest == "MQ_14") {
                spawn_quest_item("Seal Stone",
                    "The fragments resonate near it. Break the seal.",
                    5, 16, QuestId::MQ_14_HOLLOWGATE_SEAL);
            }
            // MQ_17: The Reliquary in The Sepulchre (depth 6)
            if (dungeon_ctx->quest == "MQ_15" && dungeon_level >= 6) {
                spawn_quest_item("The Reliquary",
                    "A vessel of light that hurts to look at. It was here before the gods.",
                    6, 16, QuestId::MQ_17_CLAIM_RELIQUARY,
                    {255, 220, 100, 255}); // golden tint

                // Spawn The Keeper — final boss guarding the Reliquary
                Entity keeper = populate::spawn_boss(world, map, rooms,
                    "The Keeper", SHEET_MONSTERS, 0, 11,
                    150, 24, 14, 20, 20, 5, 85, 500);
                if (keeper != NULL_ENTITY) {
                    // Give the Keeper a golden tint to match the Reliquary's glow
                    world.get<Renderable>(keeper).tint = {255, 220, 100, 255};
                }
            }
        }
    }

    // Side quest items — spawn in any dungeon when quest is active
    if (journal.has_quest(QuestId::SQ_LOST_AMULET) &&
        journal.get_state(QuestId::SQ_LOST_AMULET) == QuestState::ACTIVE &&
        dungeon_level >= 1 && rooms.size() >= 3) {
        auto& room = rooms[rooms.size() / 2]; // mid dungeon
        int ax = room.cx(), ay = room.cy();
        Entity ae = world.create();
        world.add<Position>(ae, {ax, ay});
        world.add<Renderable>(ae, {SHEET_ITEMS, 0, 16, {255, 255, 255, 255}, 1});
        Item amulet;
        amulet.name = "family amulet";
        amulet.description = "A tarnished silver amulet. Worthless to anyone but its owner.";
        amulet.type = ItemType::AMULET;
        amulet.slot = EquipSlot::NONE;
        amulet.quest_id = static_cast<int>(QuestId::SQ_LOST_AMULET);
        amulet.identified = true;
        amulet.gold_value = 0;
        world.add<Item>(ae, std::move(amulet));
    }
}

} // namespace quest_gen
