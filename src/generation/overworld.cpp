#include "generation/overworld.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/stats.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/npc.h"
#include "components/sign.h"
#include "components/item.h"
#include "core/spritesheet.h"
#include "core/engine.h" // DungeonEntry
#include "data/world_data.h"
#include "systems/combat.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace overworld {

// =============================================
// populate — spawn wilderness content
// =============================================

void populate(World& world, TileMap& map, RNG& rng,
              const std::vector<DungeonEntry>& dungeon_registry) {
    // Helper: spawn a wilderness NPC
    auto spawn_ow_npc = [&](int x, int y, const char* name, const char* dialogue,
                             NPCRole role, int spr_x, int spr_y,
                             int wander_speed = 35, GodId god = GodId::NONE) {
        // Find a walkable tile near the target
        for (int a = 0; a < 20; a++) {
            int tx = x + rng.range(-3, 3);
            int ty = y + rng.range(-3, 3);
            if (!map.in_bounds(tx, ty) || !map.is_walkable(tx, ty)) continue;
            Entity e = world.create();
            world.add<Position>(e, {tx, ty});
            NPC nc;
            nc.role = role; nc.name = name; nc.dialogue = dialogue;
            nc.home_x = tx; nc.home_y = ty;
            nc.god_affiliation = god;
            world.add<NPC>(e, std::move(nc));
            world.add<Renderable>(e, {SHEET_ROGUES, spr_x, spr_y, {255,255,255,255}, 5});
            Stats ns; ns.name = name; ns.hp = 999; ns.hp_max = 999;
            world.add<Stats>(e, std::move(ns));
            world.add<Energy>(e, {0, wander_speed});
            return;
        }
    };

    // Helper: place a lore item on the ground
    auto place_lore = [&](int x, int y, const char* name, const char* text) {
        for (int a = 0; a < 10; a++) {
            int tx = x + rng.range(-2, 2);
            int ty = y + rng.range(-2, 2);
            if (!map.in_bounds(tx, ty) || !map.is_walkable(tx, ty)) continue;
            Entity e = world.create();
            world.add<Position>(e, {tx, ty});
            world.add<Renderable>(e, {SHEET_ITEMS, 2, 20, {255,255,255,255}, 1});
            Item item;
            item.name = name; item.description = text;
            item.type = ItemType::SCROLL; item.identified = true; item.gold_value = 5;
            world.add<Item>(e, std::move(item));
            return;
        }
    };

    // Helper: paint a small water feature
    auto paint_lake = [&](int cx, int cy, int radius) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx*dx + dy*dy > radius*radius) continue;
                int tx = cx + dx, ty = cy + dy;
                if (!map.in_bounds(tx, ty)) continue;
                auto& t = map.at(tx, ty);
                if (t.type == TileType::FLOOR_GRASS || t.type == TileType::FLOOR_DIRT)
                    t.type = TileType::WATER;
            }
        }
    };

    // Helper: paint a small ruin (scattered stone walls + floor)
    auto paint_ruin = [&](int cx, int cy) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                int tx = cx + dx, ty = cy + dy;
                if (!map.in_bounds(tx, ty)) continue;
                auto& t = map.at(tx, ty);
                if (t.type != TileType::FLOOR_GRASS && t.type != TileType::FLOOR_DIRT) continue;
                // Outer ring = scattered walls, inner = stone floor
                if (std::abs(dx) == 2 || std::abs(dy) == 2) {
                    if (rng.chance(40)) t.type = TileType::WALL_STONE_ROUGH;
                } else {
                    t.type = TileType::FLOOR_STONE;
                }
            }
        }
    };

    // =============================================
    // WANDERING WILDERNESS NPCs
    // =============================================

    // Travelers on roads between towns
    static const struct { int x, y; const char* dialogue; } TRAVELERS[] = {
        {875, 700, "The roads aren't safe. But then, nothing is."},
        {1150, 710, "I'm heading to Greywatch. They say there's work there."},
        {950, 850, "Used to be farmers here. Before the barrow opened."},
        {1100, 600, "The cold gets worse the further north you go."},
        {700, 750, "Bramblewood's seen better days. The forest is closing in."},
        {1300, 850, "Ironhearth's forges never stop. You can hear them for miles."},
        {800, 1050, "Something's wrong with the water south of here."},
        {1050, 950, "I saw lights in the hills last night. Moving."},
    };
    for (auto& t : TRAVELERS) {
        spawn_ow_npc(t.x, t.y, "Traveler", t.dialogue, NPCRole::FARMER, 1, 6); // peasant sprite
    }

    // Pilgrims (near dungeon entrances or holy sites)
    spawn_ow_npc(1060, 730, "Pilgrim", "The barrow calls to the faithful. And the foolish.", NPCRole::FARMER, 4, 6);
    spawn_ow_npc(1450, 520, "Pilgrim", "Soleth's fire burns in Candlemere. I go to pray.", NPCRole::FARMER, 4, 6);
    spawn_ow_npc(580, 560, "Pilgrim", "The seal at Hollowgate. Have you seen it? It's cracking.", NPCRole::FARMER, 4, 6);

    // Hunters in the deep wilderness
    spawn_ow_npc(300, 500, "Hunter", "The game's thin out here. Something's scaring them deeper into the woods.", NPCRole::FARMER, 2, 6);
    spawn_ow_npc(1700, 700, "Hunter", "I track wolves. They've been moving in packs larger than I've ever seen.", NPCRole::FARMER, 1, 6);
    spawn_ow_npc(500, 1100, "Hunter", "Don't go south. The swamp takes people.", NPCRole::FARMER, 2, 6);

    // Hermits (isolated, deeper dialogue)
    spawn_ow_npc(200, 300, "Hermit", "I left the towns years ago. The gods are louder out here.", NPCRole::PRIEST, 4, 6);
    spawn_ow_npc(1800, 400, "Old Woman", "I remember when there were no dungeons. Then the ground opened.", NPCRole::FARMER, 3, 6);
    spawn_ow_npc(400, 1200, "Hermit", "The Reliquary isn't what they think. It was here before the gods.", NPCRole::PRIEST, 4, 6);
    spawn_ow_npc(1600, 1100, "Madman", "I HEARD IT. Under the stone. Breathing.", NPCRole::FARMER, 1, 6);

    // =============================================
    // PROVINCE-SPECIFIC WANDERING NPCs
    // =============================================

    // Frozen Marches (Gathruun) — fur traders, mountain folk
    spawn_ow_npc(1050, 350, "Fur Trader", "Pelts from the deep north fetch good coin in the Heartlands.", NPCRole::SHOPKEEPER, 2, 5);
    spawn_ow_npc(900, 300, "Mountain Guide", "I know every pass in the Marches. For a price.", NPCRole::FARMER, 2, 5);
    spawn_ow_npc(1200, 280, "Ice Miner", "The glacier caves have veins of mithril. If you survive the cold.", NPCRole::FARMER, 4, 5);

    // Pale Reach (Soleth) — zealots, lamplighters
    spawn_ow_npc(1100, 500, "Lamplighter", "I keep the road torches lit. Soleth's work, even out here.", NPCRole::FARMER, 1, 5);
    spawn_ow_npc(1300, 550, "Soleth Zealot", "The pale flame cleanses all. Even you.", NPCRole::PRIEST, 5, 5);

    // Greenwood (Khael) — druids, woodcutters, herbalists
    spawn_ow_npc(500, 800, "Druid", "The forest remembers what you've done. Tread carefully.", NPCRole::PRIEST, 4, 5);
    spawn_ow_npc(620, 900, "Woodcutter", "The trees grow back faster than we can cut them. Khael's blessing.", NPCRole::FARMER, 1, 5);
    spawn_ow_npc(450, 700, "Herb Gatherer", "The best frostcap mushrooms grow near the old ruins.", NPCRole::SHOPKEEPER, 3, 5);

    // Iron Coast (Ossren) — smiths, merchant caravans
    spawn_ow_npc(1350, 800, "Caravan Guard", "We move iron from the coast to the Heartlands. Dangerous work.", NPCRole::GUARD, 0, 5);
    spawn_ow_npc(1500, 750, "Itinerant Smith", "I shoe horses and mend armor. The road is my forge.", NPCRole::SHOPKEEPER, 4, 5);
    spawn_ow_npc(1400, 900, "Ore Hauler", "Ossren's gift is heavy. My back can testify.", NPCRole::FARMER, 1, 5);

    // Dust Provinces (Sythara) — refugees, scavengers, outcasts
    spawn_ow_npc(1100, 1100, "Scavenger", "The old towns south of here have been picked clean. Almost.", NPCRole::FARMER, 1, 5);
    spawn_ow_npc(850, 1150, "Outcast", "Exiled from three towns. The dust is my home now.", NPCRole::FARMER, 0, 5);
    spawn_ow_npc(1200, 1200, "Plague Doctor", "I treat the blighted. Sythara's children need someone.", NPCRole::PRIEST, 5, 5);

    // Heartlands (Morreth) — soldiers, farmers, merchants
    spawn_ow_npc(1000, 800, "Patrol Soldier", "Morreth's reach keeps the roads safe. Mostly.", NPCRole::GUARD, 2, 5);
    spawn_ow_npc(900, 700, "Merchant", "Good trade between Thornwall and Ashford. If the highwaymen don't get you.", NPCRole::SHOPKEEPER, 2, 5);

    // =============================================
    // ENCAMPMENTS (small NPC + lore clusters)
    // =============================================

    // Abandoned camp — between Ashford and Hollowgate
    paint_ruin(650, 600);
    place_lore(650, 600, "abandoned journal",
        "Day 3. We found the entrance. Day 5. Markus didn't come back. Day 7. None of us are going back in.");
    spawn_ow_npc(655, 600, "Deserter", "I was a guard once. Then I saw what's down there.", NPCRole::FARMER, 1, 6);

    // Mercenary camp — between Greywatch and Ironhearth
    spawn_ow_npc(1350, 720, "Sellsword", "We're waiting for a contract. Know anyone who needs killing?", NPCRole::GUARD, 0, 6);
    spawn_ow_npc(1355, 725, "Sellsword", "Gold talks. Everything else walks.", NPCRole::GUARD, 0, 6);

    // Scholar's camp — between Frostmere and Glacierveil
    spawn_ow_npc(1080, 370, "Field Scholar", "The inscriptions up north predate the current pantheon by centuries.", NPCRole::PRIEST, 5, 6);
    place_lore(1075, 370, "field notes",
        "The symbols near Glacierveil match nothing in our records. They resemble the Sepulchre markings.");

    // Refugee camp — between Dustfall and Sandmoor
    spawn_ow_npc(950, 1150, "Refugee", "The southern dungeons drove us out. We can't go home.", NPCRole::FARMER, 0, 6);
    spawn_ow_npc(955, 1155, "Refugee", "My children are hungry. The road north is dangerous.", NPCRole::FARMER, 3, 6);

    // =============================================
    // POINTS OF INTEREST
    // =============================================

    // Standing stones — ancient, pre-god monuments
    auto paint_standing_stone = [&](int x, int y) {
        if (map.in_bounds(x, y)) map.at(x, y).type = TileType::FLOOR_STONE;
        if (map.in_bounds(x-1, y)) map.at(x-1, y).type = TileType::ROCK;
        if (map.in_bounds(x+1, y)) map.at(x+1, y).type = TileType::ROCK;
    };

    paint_standing_stone(400, 400);
    place_lore(400, 402, "worn inscription",
        "BEFORE THE SEVEN. BEFORE THE NAMING. THIS PLACE REMEMBERS.");

    paint_standing_stone(1600, 300);
    place_lore(1600, 302, "cracked tablet",
        "The Reliquary was not made. It arrived. The stones grew around it.");

    paint_standing_stone(800, 1300);
    place_lore(800, 1302, "eroded pillar text",
        "Seven gods claimed it. None of them made it. Who will claim it next?");

    // Graveyard — north of Thornwall
    for (int i = 0; i < 8; i++) {
        int gx = 980 + (i % 4) * 4;
        int gy = 690 + (i / 4) * 4;
        if (map.in_bounds(gx, gy)) map.at(gx, gy).type = TileType::FLOOR_BONE;
    }
    place_lore(982, 695, "gravestone",
        "Here lies the second paragon of Morreth. He did not fail. He chose to stop.");

    // Old battlefield — between Redrock and Stonehollow
    for (int i = 0; i < 12; i++) {
        int bx = 1280 + rng.range(-8, 8);
        int by = 960 + rng.range(-8, 8);
        if (map.in_bounds(bx, by) && map.is_walkable(bx, by))
            map.at(bx, by).type = TileType::FLOOR_BONE;
    }
    place_lore(1280, 960, "rusted helm",
        "Hundreds died here. The grass grew back. The bones didn't leave.");

    // Shrine of the older gods — deep wilderness
    paint_ruin(250, 800);
    place_lore(250, 800, "ancient shrine inscription",
        "This shrine predates the Seven. It honors something that has no name. The stone is warm.");

    // Watchtower ruins — hilltop between Whitepeak and Frostmere
    paint_ruin(920, 420);
    spawn_ow_npc(920, 420, "Tower Guard", "I watch the north. Nothing comes from there anymore. That worries me.", NPCRole::GUARD, 3, 1);

    // Witch's hut — deep forest
    spawn_ow_npc(350, 700, "Hedge Witch", "I know what you seek. Everyone who comes here seeks the same thing.", NPCRole::PRIEST, 4, 6);
    place_lore(355, 700, "witch's note",
        "The herbs won't help. The prayers won't help. The only cure for what's down there is not going down there.");

    // =============================================
    // WANDERING PRIESTS — one for each non-provincial god
    // Provincial gods (Morreth, Soleth, Gathruun, Khael, Ossren, Sythara) have town priests.
    // The remaining 7 gods get wandering missionaries on roads.
    // =============================================
    {
        struct WanderingPriest { int x, y; GodId god; const char* name; const char* line; };
        static const WanderingPriest WANDERING_PRIESTS[] = {
            {900, 600,  GodId::VETHRIK,   "Priest of Vethrik",   "All things end. I am here to remind you."},
            {1200, 700, GodId::THESSARKA, "Acolyte of Thessarka","Knowledge is the only treasure worth keeping. The rest is ash."},
            {800, 800,  GodId::YASHKHET,  "Disciple of Yashkhet","Pain is the only honest teacher. Everything else lies."},
            {1100, 900, GodId::IXUUL,     "Apostle of Ixuul",    "Form is prison. The Formless offers liberation."},
            {600, 900,  GodId::ZHAVEK,    "Shadow of Zhavek",    "You didn't see me. That's the point."},
            {1300, 400, GodId::THALARA,   "Tide Priest",         "The sea remembers everything. Every ship, every shore, every drowned prayer."},
            {750, 500,  GodId::LETHIS,    "Dreamer of Lethis",   "Sleep is the gate. What waits beyond it is older than the gods."},
        };
        for (auto& wp : WANDERING_PRIESTS) {
            spawn_ow_npc(wp.x, wp.y, wp.name, wp.line, NPCRole::PRIEST, 5, 6, 35, wp.god);
        }
    }

    // =============================================
    // WATER FEATURES
    // =============================================

    // Small lakes
    paint_lake(300, 600, 4);  // western wilderness lake
    paint_lake(1700, 500, 3); // northeastern lake
    paint_lake(900, 1350, 5); // southern marsh
    paint_lake(500, 350, 3);  // northwestern pond
    paint_lake(1400, 1200, 4); // southeastern lake

    // River segments (short chains of water tiles)
    auto paint_river = [&](int x1, int y1, int x2, int y2) {
        int steps = std::max(std::abs(x2-x1), std::abs(y2-y1));
        for (int i = 0; i <= steps; i++) {
            float t = (steps > 0) ? static_cast<float>(i) / steps : 0;
            int rx = x1 + static_cast<int>((x2-x1) * t);
            int ry = y1 + static_cast<int>((y2-y1) * t);
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 0; dx++) {
                    int tx = rx + dx, ty = ry + dy;
                    if (map.in_bounds(tx, ty)) {
                        auto& tile = map.at(tx, ty);
                        if (tile.type == TileType::FLOOR_GRASS || tile.type == TileType::FLOOR_DIRT)
                            tile.type = TileType::WATER;
                    }
                }
            }
        }
    };

    paint_river(300, 580, 350, 620);  // flows into western lake
    paint_river(1680, 490, 1720, 510); // northeastern stream
    paint_river(880, 1330, 920, 1370); // southern marsh outflow

    // =============================================
    // TOWN DECORATIONS
    // =============================================

    // Helper: check if a tile is adjacent to a wall (building exterior)
    auto is_wall = [&](int x, int y) -> bool {
        if (!map.in_bounds(x, y)) return false;
        auto t = map.at(x, y).type;
        return t == TileType::WALL_STONE_BRICK || t == TileType::WALL_STONE_ROUGH ||
               t == TileType::WALL_DIRT || t == TileType::WALL_WOOD || t == TileType::DOOR_CLOSED;
    };
    auto adjacent_to_wall = [&](int x, int y) -> bool {
        return is_wall(x-1,y) || is_wall(x+1,y) || is_wall(x,y-1) || is_wall(x,y+1);
    };

    // Helper: place doodads against building walls in a town
    auto place_against_walls = [&](int cx, int cy, int radius, int count,
                                    int sx, int sy) {
        int placed = 0;
        for (int attempt = 0; attempt < count * 10 && placed < count; attempt++) {
            int tx = cx + rng.range(-radius, radius);
            int ty = cy + rng.range(-radius, radius);
            if (!map.in_bounds(tx, ty) || !map.is_walkable(tx, ty)) continue;
            if (!adjacent_to_wall(tx, ty)) continue;
            // Don't block doors
            if (map.at(tx, ty).type == TileType::DOOR_OPEN) continue;
            Entity e = world.create();
            world.add<Position>(e, {tx, ty});
            world.add<Renderable>(e, {SHEET_TILES, sx, sy, {255,255,255,255}, 0});
            placed++;
        }
    };

    // Helper: place plants on open ground (not near walls)
    auto place_on_open_ground = [&](int cx, int cy, int radius, int count,
                                     int sx, int sy) {
        int placed = 0;
        for (int attempt = 0; attempt < count * 8 && placed < count; attempt++) {
            int tx = cx + rng.range(-radius, radius);
            int ty = cy + rng.range(-radius, radius);
            if (!map.in_bounds(tx, ty)) continue;
            auto tt = map.at(tx, ty).type;
            if (tt != TileType::FLOOR_GRASS && tt != TileType::FLOOR_DIRT) continue;
            if (adjacent_to_wall(tx, ty)) continue; // not against buildings
            Entity e = world.create();
            world.add<Position>(e, {tx, ty});
            world.add<Renderable>(e, {SHEET_TILES, sx, sy, {255,255,255,255}, 0});
            placed++;
        }
    };

    // Helper: place animated light sources against walls
    auto place_lights = [&](int cx, int cy, int radius, int count, int anim_row) {
        int placed = 0;
        for (int attempt = 0; attempt < count * 15 && placed < count; attempt++) {
            int tx = cx + rng.range(-radius, radius);
            int ty = cy + rng.range(-radius, radius);
            if (!map.in_bounds(tx, ty) || !map.is_walkable(tx, ty)) continue;
            if (!adjacent_to_wall(tx, ty)) continue;
            Entity e = world.create();
            world.add<Position>(e, {tx, ty});
            world.add<Renderable>(e, {SHEET_ANIMATED, 0, anim_row, {255,255,255,255}, 0});
            placed++;
        }
    };

    for (int i = 0; i < TOWN_COUNT; i++) {
        int tx = ALL_TOWNS[i].x, ty = ALL_TOWNS[i].y;
        GodId town_god = get_town_god(tx, ty);

        // Base doodads all towns get
        place_against_walls(tx, ty, 36, rng.range(2, 4), 4, 17); // barrels
        place_against_walls(tx, ty, 36, rng.range(1, 2), 6, 17); // log piles

        // Province-themed decorations based on patron god
        switch (town_god) {
            case GodId::SOLETH: // Pale Reach — fire/light, brazier-heavy
                place_lights(tx, ty, 36, rng.range(4, 6), 1); // many braziers
                place_lights(tx, ty, 36, rng.range(2, 3), 5); // wall torches
                place_on_open_ground(tx, ty, 50, rng.range(2, 4), rng.range(0, 15), 19);
                break;
            case GodId::GATHRUUN: // Frozen Marches — stone/earth, rocks, sparse
                place_lights(tx, ty, 36, rng.range(1, 2), 1); // few braziers
                place_on_open_ground(tx, ty, 40, rng.range(2, 4), rng.range(0, 1), 18); // large rocks
                break;
            case GodId::MORRETH: // Heartlands — war, supply depots
                place_lights(tx, ty, 36, rng.range(2, 3), 1);
                place_against_walls(tx, ty, 36, rng.range(2, 4), 5, 17); // ore sacks (supplies)
                place_against_walls(tx, ty, 30, rng.range(1, 2), 4, 17); // extra barrels
                place_on_open_ground(tx, ty, 50, rng.range(4, 7), rng.range(0, 15), 19); // crops
                break;
            case GodId::KHAEL: // Greenwood — nature, lush vegetation
                place_lights(tx, ty, 36, rng.range(1, 2), 1);
                place_on_open_ground(tx, ty, 50, rng.range(8, 14), rng.range(0, 15), 19); // many crops
                place_on_open_ground(tx, ty, 40, rng.range(2, 4), 0, 20); // mushrooms
                break;
            case GodId::OSSREN: // Iron Coast — forge/craft, industrial
                place_lights(tx, ty, 36, rng.range(3, 5), 1); // forge braziers
                place_against_walls(tx, ty, 36, rng.range(3, 5), 5, 17); // ore sacks
                place_against_walls(tx, ty, 30, rng.range(2, 3), 6, 17); // extra log piles (fuel)
                place_on_open_ground(tx, ty, 50, rng.range(2, 4), rng.range(0, 15), 19);
                break;
            case GodId::SYTHARA: // Dust Provinces — decay, sparse and bleak
                place_lights(tx, ty, 36, rng.range(1, 2), 5); // dim torches only
                place_on_open_ground(tx, ty, 40, rng.range(1, 3), rng.range(0, 1), 18); // rocks
                // Bone piles scattered around
                place_on_open_ground(tx, ty, 30, rng.range(1, 3), rng.range(0, 1), 21);
                break;
            default: // Fallback
                place_lights(tx, ty, 36, rng.range(1, 2), 1);
                place_on_open_ground(tx, ty, 50, rng.range(4, 8), rng.range(0, 15), 19);
                break;
        }
    }

    // =============================================
    // WOOD BUILDINGS — cabins, outposts, hamlets
    // =============================================

    // Helper: build a small wood cabin (exterior walls + dirt floor + door)
    auto build_cabin = [&](int cx, int cy, int w, int h) {
        for (int dy = 0; dy < h; dy++) {
            for (int dx = 0; dx < w; dx++) {
                int tx = cx + dx, ty = cy + dy;
                if (!map.in_bounds(tx, ty)) continue;
                auto& t = map.at(tx, ty);
                if (t.type == TileType::TREE || t.type == TileType::ROCK) continue; // don't overwrite trees
                bool is_edge = (dx == 0 || dx == w-1 || dy == 0 || dy == h-1);
                if (is_edge) {
                    t.type = TileType::WALL_WOOD;
                } else {
                    t.type = TileType::FLOOR_DIRT;
                }
            }
        }
        // Door on the south wall (bottom center)
        int door_x = cx + w / 2;
        int door_y = cy + h - 1;
        if (map.in_bounds(door_x, door_y))
            map.at(door_x, door_y).type = TileType::DOOR_CLOSED;
    };

    // Isolated cabins with hermits/NPCs
    struct CabinDef { int x, y, w, h; const char* npc_name; const char* dialogue; };
    static const CabinDef CABINS[] = {
        {180, 450, 5, 4, "Woodsman",
         "I built this place with my hands. The forest gives. The forest takes."},
        {1750, 350, 4, 4, "Recluse",
         "Go away. No, wait. When did you last see another person on the road?"},
        {450, 1150, 5, 4, "Swamp Hermit",
         "The water here glows some nights. I don't drink from it."},
        {1650, 950, 4, 4, "Retired Soldier",
         "I fought in the wars before the barrows opened. We thought THAT was bad."},
        {350, 250, 5, 4, "Old Hunter",
         "There's a standing stone to the east. Don't touch it."},
        {1550, 200, 4, 4, "Cartographer",
         "I've mapped every road. The map changes. I don't think the roads are moving."},
    };

    for (auto& cd : CABINS) {
        build_cabin(cd.x, cd.y, cd.w, cd.h);
        // Spawn NPC inside the cabin
        spawn_ow_npc(cd.x + cd.w/2, cd.y + cd.h/2, cd.npc_name, cd.dialogue,
                      NPCRole::FARMER, 4, 6); // elderly man sprite
        // Barrel or log pile against the outside wall
        place_against_walls(cd.x - 1, cd.y, cd.w + 2, 1, 4, 17); // barrel
        place_against_walls(cd.x - 1, cd.y, cd.w + 2, 1, 6, 17); // log pile
        // Torch by the door
        place_lights(cd.x + cd.w/2, cd.y + cd.h, 3, 1, 5); // torch lit = row 5
    }

    // Small hamlets — 2-3 cabins clustered together
    struct HamletDef { int x, y; const char* name; };
    static const HamletDef HAMLETS[] = {
        {300, 650, "Thornbrook"},
        {1200, 200, "Icewind Post"},
        {1100, 1250, "Dry Creek"},
        {500, 900, "Mosshaven"},
    };

    for (auto& hm : HAMLETS) {
        // 2-3 cabins in a cluster
        build_cabin(hm.x, hm.y, 5, 4);
        build_cabin(hm.x + 7, hm.y + 1, 4, 4);
        if (rng.chance(60))
            build_cabin(hm.x + 2, hm.y + 6, 5, 3);

        // Hamlet NPCs
        spawn_ow_npc(hm.x + 2, hm.y + 2, "Villager",
            "This place has no name on the maps. We like it that way.", NPCRole::FARMER, 1, 6);
        spawn_ow_npc(hm.x + 9, hm.y + 3, "Villager",
            "Trade comes through once a season. If we're lucky.", NPCRole::FARMER, 0, 6);

        // Doodads around hamlet
        place_against_walls(hm.x - 1, hm.y - 1, 14, rng.range(2, 4), 4, 17); // barrels
        place_against_walls(hm.x - 1, hm.y - 1, 14, rng.range(1, 2), 6, 17); // log piles
    }

    // Outposts — single fortified structures (guard post at crossroads)
    struct OutpostDef { int x, y; const char* dialogue; };
    static const OutpostDef OUTPOSTS[] = {
        {1050, 660, "Road's clear, last I checked. That was yesterday."},
        {850, 500, "I watch the northern pass. Nothing human comes through anymore."},
        {700, 950, "The southern road gets worse every year. We need more guards."},
    };

    for (auto& op : OUTPOSTS) {
        build_cabin(op.x, op.y, 6, 5);
        spawn_ow_npc(op.x + 3, op.y + 2, "Road Guard", op.dialogue, NPCRole::GUARD, 3, 1);
        place_against_walls(op.x - 1, op.y - 1, 8, 2, 4, 17); // barrels
        place_lights(op.x + 3, op.y + 5, 3, 1, 5); // torch at entrance
    }

    // =============================================
    // OVERWORLD VEGETATION BY REGION
    // =============================================

    // Temperate zone: varied flowers and grasses
    for (int i = 0; i < 80; i++) {
        int x = rng.range(100, 1900);
        int y = rng.range(500, 900);
        if (!map.in_bounds(x, y)) continue;
        auto tt = map.at(x, y).type;
        if (tt != TileType::FLOOR_GRASS) continue;
        int crop = rng.range(0, 15); // random plant
        Entity e = world.create();
        world.add<Position>(e, {x, y});
        world.add<Renderable>(e, {SHEET_TILES, crop, 19, {255,255,255,255}, 0});
    }

    // Northern cold zone: sparse, icy-blue tinted plants
    for (int i = 0; i < 30; i++) {
        int x = rng.range(100, 1900);
        int y = rng.range(100, 400);
        if (!map.in_bounds(x, y)) continue;
        auto tt = map.at(x, y).type;
        if (tt != TileType::FLOOR_GRASS && tt != TileType::FLOOR_ICE) continue;
        Entity e = world.create();
        world.add<Position>(e, {x, y});
        // Frosty blue-tinted plants
        world.add<Renderable>(e, {SHEET_TILES, rng.range(0, 5), 19,
                                    {180, 200, 240, 255}, 0});
    }

    // Southern warm zone: warm-tinted plants, more variety
    for (int i = 0; i < 60; i++) {
        int x = rng.range(100, 1900);
        int y = rng.range(1000, 1400);
        if (!map.in_bounds(x, y)) continue;
        auto tt = map.at(x, y).type;
        if (tt != TileType::FLOOR_GRASS && tt != TileType::FLOOR_SAND
            && tt != TileType::FLOOR_DIRT) continue;
        int crop = rng.range(0, 15);
        Entity e = world.create();
        world.add<Position>(e, {x, y});
        SDL_Color tint = (tt == TileType::FLOOR_SAND)
            ? SDL_Color{220, 200, 140, 255}  // sandy tint
            : SDL_Color{255, 255, 255, 255};
        world.add<Renderable>(e, {SHEET_TILES, crop, 19, tint, 0});
    }

    // Mushrooms near dungeon entrances and in dark forest areas
    for (int i = 0; i < 25; i++) {
        int x = rng.range(100, 1900);
        int y = rng.range(100, 1400);
        if (!map.in_bounds(x, y)) continue;
        // Only place near trees (forest areas)
        bool near_tree = false;
        for (int dy = -2; dy <= 2 && !near_tree; dy++)
            for (int dx = -2; dx <= 2 && !near_tree; dx++)
                if (map.in_bounds(x+dx, y+dy) && map.at(x+dx, y+dy).type == TileType::TREE)
                    near_tree = true;
        if (!near_tree) continue;
        if (!map.is_walkable(x, y)) continue;
        Entity e = world.create();
        world.add<Position>(e, {x, y});
        world.add<Renderable>(e, {SHEET_TILES, rng.range(0, 1), 20, {255,255,255,255}, 0});
    }

    // =============================================
    // SIGNPOSTS — directions to nearby POIs
    // =============================================

    struct POI { int x, y; const char* name; bool is_dungeon; };
    std::vector<POI> pois;
    // Towns
    for (int i = 0; i < TOWN_COUNT; i++)
        pois.push_back({ALL_TOWNS[i].x, ALL_TOWNS[i].y, ALL_TOWNS[i].name, false});
    // Named dungeons from registry
    for (auto& de : dungeon_registry) {
        if (!de.quest.empty()) // only named quest dungeons
            pois.push_back({de.x, de.y, de.name.c_str(), true});
    }

    // Compass direction helper (local version using atan2 for sign text)
    auto sign_compass_dir = [](int from_x, int from_y, int to_x, int to_y) -> const char* {
        int dx = to_x - from_x, dy = to_y - from_y;
        float angle = std::atan2(static_cast<float>(dy), static_cast<float>(dx));
        // Convert to 8 directions (atan2: 0=E, pi/2=S, -pi/2=N)
        if (angle < -2.749f) return "W";
        if (angle < -1.963f) return "NW";
        if (angle < -1.178f) return "N";
        if (angle < -0.393f) return "NE";
        if (angle < 0.393f)  return "E";
        if (angle < 1.178f)  return "SE";
        if (angle < 1.963f)  return "S";
        if (angle < 2.749f)  return "SW";
        return "W";
    };

    // Generate sign text for a position: list 2-4 nearest POIs with directions
    auto make_sign_text = [&](int sx, int sy, int max_entries = 3) -> std::string {
        struct Nearby { float d; const char* name; const char* dir; };
        std::vector<Nearby> nearby;
        for (auto& p : pois) {
            float d = world_dist(sx, sy, p.x, p.y);
            if (d < 30) continue; // skip the POI we're standing at
            nearby.push_back({d, p.name, sign_compass_dir(sx, sy, p.x, p.y)});
        }
        std::sort(nearby.begin(), nearby.end(),
                  [](const Nearby& a, const Nearby& b) { return a.d < b.d; });

        std::string text = "Signpost:";
        int count = std::min(max_entries, static_cast<int>(nearby.size()));
        for (int i = 0; i < count; i++) {
            text += "  ";
            text += nearby[i].name;
            text += " (";
            text += nearby[i].dir;
            text += ")";
        }
        return text;
    };

    // Place a sign entity at a walkable tile near (x,y)
    auto place_sign = [&](int x, int y) {
        for (int a = 0; a < 20; a++) {
            int tx = x + rng.range(-2, 2);
            int ty = y + rng.range(-2, 2);
            if (!map.in_bounds(tx, ty)) continue;
            if (!map.is_walkable(tx, ty)) continue;
            // Don't place on or adjacent to doors (blocks entry)
            bool near_door = false;
            for (int dy = -1; dy <= 1 && !near_door; dy++)
                for (int dx = -1; dx <= 1 && !near_door; dx++)
                    if (map.in_bounds(tx+dx, ty+dy) &&
                        (map.at(tx+dx, ty+dy).type == TileType::DOOR_CLOSED ||
                         map.at(tx+dx, ty+dy).type == TileType::DOOR_OPEN))
                        near_door = true;
            if (near_door) continue;
            // Don't place on top of existing entities
            if (combat::entity_at(world, tx, ty, NULL_ENTITY) != NULL_ENTITY) continue;
            Entity e = world.create();
            world.add<Position>(e, {tx, ty});
            world.add<Renderable>(e, {SHEET_TILES, 7, 17, {255, 255, 255, 255}, 3});
            world.add<Sign>(e, {make_sign_text(tx, ty)});
            return;
        }
    };

    // Signs outside each town (offset from center toward cardinal directions)
    for (int i = 0; i < TOWN_COUNT; i++) {
        // Place 1-2 signs on the outskirts of each town
        place_sign(ALL_TOWNS[i].x + 25, ALL_TOWNS[i].y);      // east side
        place_sign(ALL_TOWNS[i].x - 25, ALL_TOWNS[i].y);      // west side
        if (rng.chance(50))
            place_sign(ALL_TOWNS[i].x, ALL_TOWNS[i].y + 25);  // south side
    }

    // Signs near named dungeon entrances
    for (auto& de : dungeon_registry) {
        if (!de.quest.empty())
            place_sign(de.x - 5, de.y);
    }

    // Signs at road crossings / midpoints between towns
    // Sample points along major routes
    struct { int x, y; } road_signs[] = {
        // Heartlands crossroads
        {900, 700},   // between Thornwall and Ashford
        {1150, 710},  // between Thornwall and Greywatch
        {950, 850},   // between Thornwall and Millhaven
        // Pale Reach
        {1050, 600},  // between Thornwall and Frostmere
        {1200, 500},  // between Ravenshold and Candlemere
        {1075, 375},  // between Frostmere and Glacierveil
        {900, 575},   // between Whitepeak and Thornwall
        // Greenwood
        {625, 650},   // between Fenwatch and Hollowgate
        {675, 900},   // between Bramblewood and Tanglewood
        // Iron Coast
        {1375, 650},  // between Greywatch and Ironhearth
        {1475, 675},  // between Ironhearth and Candlemere
        {1425, 925},  // between Ironhearth and Redrock
        // Dust Provinces
        {950, 1050},  // between Millhaven and Dustfall
        {1125, 1100}, // between Dustfall and Drywell
        {850, 1150},  // between Sandmoor and Tanglewood
        // Far routes
        {775, 525},   // between Whitepeak and Hollowgate
        {1500, 700},  // approaching Endgate
        {1050, 200},  // approaching The Sepulchre
    };
    for (auto& rs : road_signs) {
        place_sign(rs.x, rs.y);
    }
}

// =============================================
// process_npc_wander — move NPCs around their homes
// =============================================

void process_npc_wander(World& world, TileMap& map, RNG& rng) {
    auto& npc_pool = world.pool<NPC>();
    for (size_t i = 0; i < npc_pool.size(); i++) {
        Entity e = npc_pool.entity_at(i);
        auto& npc = npc_pool.at_index(i);

        // Shopkeepers stay put (need to be findable for shops)
        if (npc.role == NPCRole::SHOPKEEPER) continue;

        if (!world.has<Position>(e) || !world.has<Energy>(e)) continue;
        auto& energy = world.get<Energy>(e);
        if (!energy.can_act()) continue;
        energy.spend();

        // 80% chance to just stand still (frequent pausing)
        if (rng.chance(80)) continue;

        auto& pos = world.get<Position>(e);

        // Don't stray more than 4 tiles from home
        int dx = rng.range(-1, 1);
        int dy = rng.range(-1, 1);
        if (dx == 0 && dy == 0) continue;

        int nx = pos.x + dx;
        int ny = pos.y + dy;
        int home_dist = std::max(std::abs(nx - npc.home_x), std::abs(ny - npc.home_y));
        if (home_dist > 4) continue;

        if (!map.is_walkable(nx, ny)) continue;

        // Don't walk into other entities
        bool blocked = false;
        auto& positions = world.pool<Position>();
        for (size_t j = 0; j < positions.size(); j++) {
            Entity other = positions.entity_at(j);
            if (other == e) continue;
            auto& op = positions.at_index(j);
            if (op.x == nx && op.y == ny && world.has<Stats>(other)) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;

        int old_x = pos.x;
        pos.x = nx;
        pos.y = ny;
        if (world.has<Renderable>(e) && old_x != nx) {
            world.get<Renderable>(e).flip_h = (nx > old_x);
        }
    }
}

// =============================================
// try_spawn_overworld_enemy — spawn random wilderness enemies
// =============================================

void try_spawn_overworld_enemy(World& world, TileMap& map, RNG& rng,
                                Entity player) {
    if (!world.has<Position>(player)) return;
    auto& ppos = world.get<Position>(player);

    // Don't spawn near towns
    for (int i = 0; i < TOWN_COUNT; i++) {
        int d = std::max(std::abs(ppos.x - ALL_TOWNS[i].x), std::abs(ppos.y - ALL_TOWNS[i].y));
        if (d < 70) return;
    }

    // Count nearby hostile entities
    int nearby = 0;
    auto& ai_pool = world.pool<AI>();
    for (size_t i = 0; i < ai_pool.size(); i++) {
        Entity e = ai_pool.entity_at(i);
        if (!world.has<Position>(e)) continue;
        auto& mp = world.get<Position>(e);
        int dist = std::max(std::abs(mp.x - ppos.x), std::abs(mp.y - ppos.y));
        if (dist <= 30) nearby++;
    }
    if (nearby >= 4) return;

    // Overworld monster definitions
    struct OWMonster {
        const char* name;
        int sheet, sx, sy, hp, str, dex, con, dmg, armor, speed, flee, xp;
    };
    // All overworld enemy definitions (indexed below by climate tables)
    enum OWId { WOLF, BOAR, HIGHWAYMAN, SPIDER, BEAR, BANDIT, SNAKE, DIRE_WOLF, SKELETON };
    static const OWMonster OW_TABLE[] = {
        {"wolf",         SHEET_ANIMALS,  6, 4, 12, 10, 14,  8, 3, 0, 120, 30, 15},
        {"wild boar",    SHEET_ANIMALS,  7, 9, 18, 14,  8, 12, 4, 1,  90, 20, 20},
        {"highwayman",   SHEET_ROGUES,   4, 0, 16, 12, 12, 10, 3, 1, 100, 25, 25},
        {"giant spider", SHEET_MONSTERS, 8, 6, 10,  8, 14,  6, 3, 0, 120, 30, 15},
        {"bear",         SHEET_ANIMALS,  0, 0, 24, 16,  8, 14, 5, 2,  80, 15, 30},
        {"bandit",       SHEET_ROGUES,   4, 0, 14, 11, 13, 10, 3, 1, 105, 30, 20},
        {"snake",        SHEET_ANIMALS,  0, 7,  6,  6, 16,  6, 2, 0, 130, 40, 10},
        {"dire wolf",    SHEET_ANIMALS,  6, 4, 20, 14, 14, 12, 5, 1, 125, 15, 35},
        {"wandering skeleton", SHEET_MONSTERS, 0, 4, 16, 10, 10, 10, 3, 2, 90, 0, 20},
    };

    // Climate-zoned enemy tables per province
    static const OWId FROZEN[]    = {DIRE_WOLF, WOLF, WOLF, SKELETON, BEAR};
    static const OWId PALE[]      = {WOLF, BEAR, SPIDER, HIGHWAYMAN, SKELETON};
    static const OWId GREENWOOD[] = {SPIDER, BOAR, SNAKE, BEAR, WOLF};
    static const OWId HEARTLAND[] = {WOLF, BOAR, HIGHWAYMAN, BANDIT, SPIDER};
    static const OWId IRON[]      = {BANDIT, HIGHWAYMAN, SPIDER, BOAR, SKELETON};
    static const OWId DUST[]      = {SNAKE, SKELETON, BANDIT, SNAKE, HIGHWAYMAN};

    // Try to spawn at edge of visibility
    for (int attempt = 0; attempt < 15; attempt++) {
        int dist = rng.range(14, 20);
        float angle = rng.range_f(0.0f, 6.283f);
        int sx = ppos.x + static_cast<int>(dist * std::cos(angle));
        int sy = ppos.y + static_cast<int>(dist * std::sin(angle));

        if (!map.in_bounds(sx, sy) || !map.is_walkable(sx, sy)) continue;

        // Only spawn on wilderness tiles
        auto tt = map.at(sx, sy).type;
        if (tt != TileType::FLOOR_GRASS && tt != TileType::FLOOR_DIRT &&
            tt != TileType::FLOOR_SAND && tt != TileType::FLOOR_ICE &&
            tt != TileType::BRUSH) continue;

        // Pick enemy from climate-appropriate table
        const OWId* table = HEARTLAND;
        int table_size = 5;
        GodId region = get_town_god(sx, sy);
        switch (region) {
            case GodId::GATHRUUN: table = FROZEN;    break; // Frozen Marches
            case GodId::SOLETH:   table = PALE;      break; // Pale Reach
            case GodId::KHAEL:    table = GREENWOOD;  break; // Greenwood
            case GodId::OSSREN:   table = IRON;      break; // Iron Coast
            case GodId::SYTHARA:  table = DUST;      break; // Dust Provinces
            default:              table = HEARTLAND;  break; // Heartlands
        }
        auto& def = OW_TABLE[table[rng.range(0, table_size - 1)]];

        Entity e = world.create();
        world.add<Position>(e, {sx, sy});
        world.add<Renderable>(e, {def.sheet, def.sx, def.sy,
                                    {255, 255, 255, 255}, 5});
        Stats stats;
        stats.name = def.name;
        stats.hp = def.hp;
        stats.hp_max = def.hp;
        stats.set_attr(Attr::STR, def.str);
        stats.set_attr(Attr::DEX, def.dex);
        stats.set_attr(Attr::CON, def.con);
        stats.base_damage = def.dmg;
        stats.natural_armor = def.armor;
        stats.base_speed = def.speed;
        stats.xp_value = def.xp;
        world.add<Stats>(e, std::move(stats));
        world.add<AI>(e, {AIState::IDLE, -1, -1, 0, def.flee});
        world.add<Energy>(e, {0, def.speed});
        return; // one at a time
    }
}

} // namespace overworld
