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
    {"Thornwall",    500, 375},
    {"Millhaven",    425, 475},
    {"Candlemere",   725, 250},
    {"Frostmere",    525, 225},
    {"Greywatch",    650, 335},
    {"Whitepeak",    400, 200},
    {"Bramblewood",  325, 400},
    {"Hollowgate",   275, 275},
    {"Ironhearth",   700, 375},
    {"Dustfall",     500, 550},
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
    // Use actual overworld creature names that the player can find and kill
    struct HuntTarget { const char* name; const char* plural; int count; };
    static const HuntTarget TARGETS[] = {
        {"wolf",         "wolves",         3},
        {"giant spider", "giant spiders",  3},
        {"wild boar",    "wild boars",     2},
        {"bear",         "bears",          1},
        {"dire wolf",    "dire wolves",    2},
        {"hyena",        "hyenas",         3},
    };
    int ci = rng.range(0, 5);
    auto& t = TARGETS[ci];

    DynamicQuest q;
    char buf[256];
    if (t.count == 1) {
        snprintf(buf, sizeof(buf), "The %s of %s", t.name, town.c_str());
    } else {
        snprintf(buf, sizeof(buf), "The %s of %s", t.plural, town.c_str());
    }
    q.name = buf;
    snprintf(buf, sizeof(buf),
        "%s have been threatening the farms outside %s. "
        "Kill %d of them in the wilderness.",
        t.plural, town.c_str(), t.count);
    // Capitalize first letter
    buf[0] = static_cast<char>(toupper(buf[0]));
    q.description = buf;
    snprintf(buf, sizeof(buf), "Kill %d %s in the wilderness.",
             t.count, t.count == 1 ? t.name : t.plural);
    q.objective = buf;
    snprintf(buf, sizeof(buf), "The %s are dealt with. The farmers can sleep again.",
             t.plural);
    q.complete_text = buf;
    q.xp_reward = 25 + rng.range(0, 15);
    q.gold_reward = 10 + rng.range(0, 10);
    q.kill_type = t.name;
    q.kills_needed = t.count;
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

static DynamicQuest make_bounty_quest(RNG& rng, const std::string& town) {
    static const struct { const char* name; const char* title; const char* desc; } BOUNTIES[] = {
        {"Grishnakh the Scarred", "an orc warchief", "terrorizing travelers near"},
        {"The Pale Widow", "a giant spider", "nesting in the caves near"},
        {"Ironjaw", "a troll", "blocking the road outside"},
        {"Ashclaw", "a manticore", "hunting livestock around"},
        {"The Barrow King", "a death knight", "risen from the crypts near"},
        {"Bloodmaw", "a dire wolf", "leading a pack near"},
    };
    int bi = rng.range(0, 5);
    DynamicQuest q;
    char buf[256];

    snprintf(buf, sizeof(buf), "Bounty: %s", BOUNTIES[bi].name);
    q.name = buf;
    snprintf(buf, sizeof(buf),
        "%s, %s, has been %s %s. The town is offering a reward for its head.",
        BOUNTIES[bi].name, BOUNTIES[bi].title, BOUNTIES[bi].desc, town.c_str());
    q.description = buf;
    snprintf(buf, sizeof(buf), "Kill %s in the dungeon near %s.", BOUNTIES[bi].name, town.c_str());
    q.objective = buf;
    snprintf(buf, sizeof(buf), "%s is dead. The bounty is yours.", BOUNTIES[bi].name);
    q.complete_text = buf;
    q.xp_reward = 50 + rng.range(0, 25);
    q.gold_reward = 40 + rng.range(0, 20);
    q.requires_dungeon = true;
    return q;
}

static DynamicQuest make_rumor_quest(RNG& rng, const std::string& town) {
    static const struct { const char* hook; const char* objective; const char* complete; } RUMORS[] = {
        {"People have been disappearing on the road at night.",
         "Investigate the road near %s after dark.",
         "You find tracks leading into the wilderness. Something hunts here."},
        {"Strange lights were seen in the dungeon entrance last night.",
         "Explore the dungeon near %s and report what you find.",
         "The source of the lights: phosphorescent mushrooms. Nothing sinister. But the dungeon holds other dangers."},
        {"A trader claims he saw a ghost in the ruins near town.",
         "Search the ruins near %s for signs of the undead.",
         "No ghost. But you found old bones and a silver ring worth keeping."},
        {"The well water has turned bitter. Someone suspects poison.",
         "Check the water source near %s.",
         "Dead rats in the cistern. Natural causes. The water should clear up."},
        {"Livestock have been found drained of blood near the farms.",
         "Patrol the outskirts of %s at night.",
         "Giant bats from a nearby cave. The colony is too large to clear alone, but the farmers can seal the entrance."},
    };
    int ri = rng.range(0, 4);
    DynamicQuest q;
    char buf[256];

    snprintf(buf, sizeof(buf), "Rumor in %s", town.c_str());
    q.name = buf;
    q.description = RUMORS[ri].hook;
    snprintf(buf, sizeof(buf), RUMORS[ri].objective, town.c_str());
    q.objective = buf;
    q.complete_text = RUMORS[ri].complete;
    q.xp_reward = 20 + rng.range(0, 15);
    q.gold_reward = 10 + rng.range(0, 15);
    q.min_turns = 40 + rng.range(0, 30); // takes time to investigate
    return q;
}

static DynamicQuest make_rescue_quest(RNG& rng, const std::string& town) {
    static const char* NAMES[] = {"Elara", "Gareth", "Petra", "Orin", "Talia", "Mace"};
    int ni = rng.range(0, 5);
    DynamicQuest q;
    char buf[256];

    snprintf(buf, sizeof(buf), "Rescue %s", NAMES[ni]);
    q.name = buf;
    snprintf(buf, sizeof(buf),
        "%s went into the dungeon near %s three days ago and hasn't returned. "
        "Their family fears the worst.",
        NAMES[ni], town.c_str());
    q.description = buf;
    snprintf(buf, sizeof(buf), "Find %s in the dungeon near %s. They may be on a deeper floor.",
             NAMES[ni], town.c_str());
    q.objective = buf;
    snprintf(buf, sizeof(buf), "You found %s, alive but shaken. They won't be going back.", NAMES[ni]);
    q.complete_text = buf;
    q.xp_reward = 35 + rng.range(0, 15);
    q.gold_reward = 20 + rng.range(0, 15);
    q.requires_dungeon = true;
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
                // Farmers: regular farmer quest or rescue quest
                dq = rng.chance(30) ? make_rescue_quest(rng, town_name)
                                     : make_farmer_quest(rng, town_name);
                break;
            case NPCRole::GUARD:
                // Guards: regular guard quest, bounty, or rescue
                {
                    int groll = rng.range(1, 100);
                    if (groll <= 35) dq = make_bounty_quest(rng, town_name);
                    else if (groll <= 55) dq = make_rescue_quest(rng, town_name);
                    else dq = make_guard_quest(rng, town_name);
                }
                break;
            case NPCRole::BLACKSMITH:
                dq = make_blacksmith_quest(rng, town_name);
                break;
            case NPCRole::PRIEST:
                if (npc.name == "Herbalist") {
                    dq = make_herbalist_quest(rng, town_name);
                } else {
                    // Scholars: regular or rumor investigation
                    dq = rng.chance(40) ? make_rumor_quest(rng, town_name)
                                         : make_scholar_quest(rng, town_name);
                }
                break;
            case NPCRole::SHOPKEEPER:
                if (npc.name == "Merchant") {
                    dq = make_merchant_quest(rng, town_name);
                } else {
                    continue;
                }
                break;
            case NPCRole::ELDER:
                // Elders without main quests can give rumors
                if (npc.quest_id < 0) {
                    dq = make_rumor_quest(rng, town_name);
                } else {
                    continue;
                }
                break;
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
    bool in_warrens = dungeon_ctx && dungeon_ctx->zone == "warrens";
    if (dungeon_level == 3 && in_warrens) {
        // Barrow Wight — bottom of The Warrens (first dungeon)
        // Uses death knight sprite (row 4 col 3) for a menacing undead look
        Entity wight = populate::spawn_boss(world, map, rooms,
            "Barrow Wight", SHEET_MONSTERS, 3, 4,
            45, 16, 12, 14, 8, 3, 90, 100);
        if (wight != NULL_ENTITY) {
            world.add<QuestTarget>(wight, {QuestId::MQ_01_BARROW_WIGHT, true});
        }
    }

    // The Sepulchre depth-triggered quests (MQ_08/MQ_09)
    bool in_sepulchre = dungeon_ctx && dungeon_ctx->zone == "sepulchre";
    if (in_sepulchre) {
        // MQ_15: auto-activate on entering The Sepulchre
        if (dungeon_level == 1 && !journal.has_quest(QuestId::MQ_08_ENTER_SEPULCHRE)) {
            auto prereq = QuestId::MQ_07_BREAK_SEAL;
            if (journal.has_quest(prereq) && journal.get_state(prereq) == QuestState::FINISHED) {
                journal.add_quest(QuestId::MQ_08_ENTER_SEPULCHRE);
                log.add("Quest started: Enter The Sepulchre", {220, 200, 100, 255});
                log.add("Your god is screaming.", {180, 80, 80, 255});
            }
        }
        // MQ_09: auto-activate at depth 4 (claim reliquary)
        if (dungeon_level >= 4 && !journal.has_quest(QuestId::MQ_09_CLAIM_RELIQUARY)) {
            if (journal.has_quest(QuestId::MQ_08_ENTER_SEPULCHRE) &&
                journal.get_state(QuestId::MQ_08_ENTER_SEPULCHRE) == QuestState::ACTIVE) {
                journal.set_state(QuestId::MQ_08_ENTER_SEPULCHRE, QuestState::COMPLETE);
                journal.set_state(QuestId::MQ_08_ENTER_SEPULCHRE, QuestState::FINISHED);
                journal.add_quest(QuestId::MQ_09_CLAIM_RELIQUARY);
                log.add("Quest started: The Descent", {220, 200, 100, 255});
                log.add("The architecture stops making sense. You hear other footsteps.", {180, 80, 80, 255});
            }
        }
        // Sepulchre atmospheric entry messages
        static const char* SEPULCHRE_ENTRY[] = {
            "The air changes. Something is wrong with this place.",
            "The walls here are older than stone should be.",
            "The geometry stops making sense. Corners that shouldn't exist.",
            "The Reliquary is here. You can feel it pulling.",
        };
        if (dungeon_level >= 1 && dungeon_level <= 4) {
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
            {"warrens", 3}, {"stonekeep", 4}, {"deep_halls", 4},
            {"catacombs", 4}, {"molten", 4}, {"sunken", 4},
            {"sepulchre", 4},
        };
        int zone_max = 3; // default
        for (auto& zd : ZONE_DEPTHS) {
            if (dungeon_ctx->zone == zd.key) { zone_max = zd.max_depth; break; }
        }
        bool is_bottom = (dungeon_level >= zone_max);

        if (is_bottom) {
            // (disabled: Ashford removed from quest chain)
            if (false) {
                spawn_quest_item("Stone Tablet",
                    "A heavy stone tablet. The inscriptions shift when you aren't looking.",
                    0, 21, QuestId::MQ_03_FIRST_FRAGMENT);
            }
            // MQ_05: Ancient Inscription in Stonekeep
            if (dungeon_ctx->quest == "MQ_03") {
                spawn_quest_item("Ancient Inscription",
                    "A page of burned stone. The words are too heavy for the rock.",
                    7, 21, QuestId::MQ_03_FIRST_FRAGMENT);
            }
            // MQ_07: Frozen Key in Frostmere Depths
            if (false) {
                spawn_quest_item("Frozen Key",
                    "A key of impossible cold. It burns your hand.",
                    2, 22, QuestId::MQ_05_SECOND_FRAGMENT);
            }
            // MQ_09: Reliquary Fragment in The Catacombs
            if (dungeon_ctx->quest == "MQ_05") {
                spawn_quest_item("Reliquary Fragment",
                    "A shard of solidified memory. It hums with warmth.",
                    2, 16, QuestId::MQ_05_SECOND_FRAGMENT);
            }
            // MQ_11: Molten Fragment in The Molten Depths
            if (dungeon_ctx->quest == "MQ_06") {
                spawn_quest_item("Molten Fragment",
                    "Cold even in the heart of the furnace. Two of three.",
                    2, 16, QuestId::MQ_06_THIRD_FRAGMENT,
                    {255, 120, 80, 255}); // red tint
            }
            // MQ_13: Sunken Fragment in The Sunken Halls
            if (false) {
                spawn_quest_item("Sunken Fragment",
                    "The water remembers. Three fragments. They pull toward each other.",
                    2, 16, QuestId::MQ_06_THIRD_FRAGMENT,
                    {100, 160, 255, 255}); // blue tint
            }
            // MQ_14: Seal Stone in The Hollowgate
            if (dungeon_ctx->quest == "MQ_07") {
                spawn_quest_item("Seal Stone",
                    "The fragments resonate near it. Break the seal.",
                    5, 16, QuestId::MQ_07_BREAK_SEAL);
            }
            // MQ_17: The Reliquary in The Sepulchre (depth 6)
            if (dungeon_ctx->quest == "MQ_09" && dungeon_level >= 4) {
                spawn_quest_item("The Reliquary",
                    "A vessel of light that hurts to look at. It was here before the gods.",
                    6, 16, QuestId::MQ_09_CLAIM_RELIQUARY,
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
