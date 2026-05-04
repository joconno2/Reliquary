#include "generation/overworld.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/stats.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/npc.h"
#include "components/player.h"
#include "components/sign.h"
#include "components/item.h"
#include "core/spritesheet.h"
#include "core/engine.h" // DungeonEntry
#include "data/world_data.h"
#include "generation/mapfile.h"
#include "systems/combat.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace overworld {

// =============================================
// populate — spawn wilderness content
// =============================================

void populate(World& world, TileMap& map, RNG& rng,
              const std::vector<DungeonEntry>& dungeon_registry,
              const std::vector<MapEntity>& map_entities) {
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
        {437, 350, "The roads aren't safe. But then, nothing is."},
        {575, 355, "I'm heading to Greywatch. They say there's work there."},
        {475, 425, "Used to be farmers here. Before the barrow opened."},
        {550, 300, "The cold gets worse the further north you go."},
        {350, 375, "Bramblewood's seen better days. The forest is closing in."},
        {650, 425, "Ironhearth's forges never stop. You can hear them for miles."},
        {400, 525, "Something's wrong with the water south of here."},
        {525, 475, "I saw lights in the hills last night. Moving."},
    };
    for (auto& t : TRAVELERS) {
        spawn_ow_npc(t.x, t.y, "Traveler", t.dialogue, NPCRole::FARMER, 1, 6); // peasant sprite
    }

    // Pilgrims (near dungeon entrances or holy sites)
    spawn_ow_npc(530, 365, "Pilgrim", "The barrow calls to the faithful. And the foolish.", NPCRole::FARMER, 4, 6);
    spawn_ow_npc(725, 260, "Pilgrim", "Soleth's fire burns in Candlemere. I go to pray.", NPCRole::FARMER, 4, 6);
    spawn_ow_npc(290, 280, "Pilgrim", "The seal at Hollowgate. Have you seen it? It's cracking.", NPCRole::FARMER, 4, 6);

    // Hunters in the deep wilderness
    spawn_ow_npc(150, 250, "Hunter", "The game's thin out here. Something's scaring them deeper into the woods.", NPCRole::FARMER, 2, 6);
    spawn_ow_npc(850, 350, "Hunter", "I track wolves. They've been moving in packs larger than I've ever seen.", NPCRole::FARMER, 1, 6);
    spawn_ow_npc(250, 550, "Hunter", "Don't go south. The swamp takes people.", NPCRole::FARMER, 2, 6);

    // Hermits (isolated, deeper dialogue)
    spawn_ow_npc(100, 150, "Hermit", "I left the towns years ago. The gods are louder out here.", NPCRole::PRIEST, 4, 6);
    spawn_ow_npc(900, 200, "Old Woman", "I remember when there were no dungeons. Then the ground opened.", NPCRole::FARMER, 3, 6);
    spawn_ow_npc(200, 600, "Hermit", "The Reliquary isn't what they think. It was here before the gods.", NPCRole::PRIEST, 4, 6);
    spawn_ow_npc(800, 550, "Madman", "I HEARD IT. Under the stone. Breathing.", NPCRole::FARMER, 1, 6);

    // =============================================
    // PROVINCE-SPECIFIC WANDERING NPCs
    // =============================================

    // Frozen Marches (Gathruun) — fur traders, mountain folk
    spawn_ow_npc(525, 175, "Fur Trader", "Pelts from the deep north fetch good coin in the Heartlands.", NPCRole::SHOPKEEPER, 2, 5);
    spawn_ow_npc(450, 150, "Mountain Guide", "I know every pass in the Marches. For a price.", NPCRole::FARMER, 2, 5);
    spawn_ow_npc(600, 140, "Ice Miner", "The glacier caves have veins of mithril. If you survive the cold.", NPCRole::FARMER, 4, 5);

    // Pale Reach (Soleth) — zealots, lamplighters
    spawn_ow_npc(550, 250, "Lamplighter", "I keep the road torches lit. Soleth's work, even out here.", NPCRole::FARMER, 1, 5);
    spawn_ow_npc(650, 275, "Soleth Zealot", "The pale flame cleanses all. Even you.", NPCRole::PRIEST, 5, 5);

    // Greenwood (Khael) — druids, woodcutters, herbalists
    spawn_ow_npc(250, 400, "Druid", "The forest remembers what you've done. Tread carefully.", NPCRole::PRIEST, 4, 5);
    spawn_ow_npc(310, 450, "Woodcutter", "The trees grow back faster than we can cut them. Khael's blessing.", NPCRole::FARMER, 1, 5);
    spawn_ow_npc(225, 350, "Herb Gatherer", "The best frostcap mushrooms grow near the old ruins.", NPCRole::SHOPKEEPER, 3, 5);

    // Iron Coast (Ossren) — smiths, merchant caravans
    spawn_ow_npc(675, 400, "Caravan Guard", "We move iron from the coast to the Heartlands. Dangerous work.", NPCRole::GUARD, 0, 5);
    spawn_ow_npc(750, 375, "Itinerant Smith", "I shoe horses and mend armor. The road is my forge.", NPCRole::SHOPKEEPER, 4, 5);
    spawn_ow_npc(700, 450, "Ore Hauler", "Ossren's gift is heavy. My back can testify.", NPCRole::FARMER, 1, 5);

    // Dust Provinces (Sythara) — refugees, scavengers, outcasts
    spawn_ow_npc(550, 550, "Scavenger", "The old towns south of here have been picked clean. Almost.", NPCRole::FARMER, 1, 5);
    spawn_ow_npc(425, 575, "Outcast", "Exiled from three towns. The dust is my home now.", NPCRole::FARMER, 0, 5);
    spawn_ow_npc(600, 600, "Plague Doctor", "I treat the blighted. Sythara's children need someone.", NPCRole::PRIEST, 5, 5);

    // Heartlands (Morreth) — soldiers, farmers, merchants
    spawn_ow_npc(500, 400, "Patrol Soldier", "Morreth's reach keeps the roads safe. Mostly.", NPCRole::GUARD, 2, 5);
    spawn_ow_npc(450, 350, "Merchant", "Good trade between Thornwall and Ashford. If the highwaymen don't get you.", NPCRole::SHOPKEEPER, 2, 5);

    // =============================================
    // ENCAMPMENTS (small NPC + lore clusters)
    // =============================================

    // Abandoned camp — between Ashford and Hollowgate
    paint_ruin(650, 600);
    place_lore(650, 600, "abandoned journal",
        "Day 3. We found the entrance. Day 5. Markus didn't come back. Day 7. None of us are going back in.");
    spawn_ow_npc(327, 300, "Deserter", "I was a guard once. Then I saw what's down there.", NPCRole::FARMER, 1, 6);

    // Mercenary camp — between Greywatch and Ironhearth
    spawn_ow_npc(675, 360, "Sellsword", "We're waiting for a contract. Know anyone who needs killing?", NPCRole::GUARD, 0, 6);
    spawn_ow_npc(677, 362, "Sellsword", "Gold talks. Everything else walks.", NPCRole::GUARD, 0, 6);

    // Scholar's camp — between Frostmere and Glacierveil
    spawn_ow_npc(540, 185, "Field Scholar", "The inscriptions up north predate the current pantheon by centuries.", NPCRole::PRIEST, 5, 6);
    place_lore(1075, 370, "field notes",
        "The symbols near Glacierveil match nothing in our records. They resemble the Sepulchre markings.");

    // Refugee camp — between Dustfall and Sandmoor
    spawn_ow_npc(475, 575, "Refugee", "The southern dungeons drove us out. We can't go home.", NPCRole::FARMER, 0, 6);
    spawn_ow_npc(477, 577, "Refugee", "My children are hungry. The road north is dangerous.", NPCRole::FARMER, 3, 6);

    // =============================================
    // POINTS OF INTEREST
    // =============================================

    // Standing stones — ancient, pre-god monuments
    auto paint_standing_stone = [&](int x, int y) {
        if (map.in_bounds(x, y)) map.at(x, y).type = TileType::FLOOR_STONE;
        if (map.in_bounds(x-1, y)) map.at(x-1, y).type = TileType::ROCK;
        if (map.in_bounds(x+1, y)) map.at(x+1, y).type = TileType::ROCK;
    };

    paint_standing_stone(200, 200);
    place_lore(400, 402, "worn inscription",
        "BEFORE THE SEVEN. BEFORE THE NAMING. THIS PLACE REMEMBERS.");

    paint_standing_stone(800, 150);
    place_lore(1600, 302, "cracked tablet",
        "The Reliquary was not made. It arrived. The stones grew around it.");

    paint_standing_stone(400, 650);
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
    spawn_ow_npc(460, 210, "Tower Guard", "I watch the north. Nothing comes from there anymore. That worries me.", NPCRole::GUARD, 3, 1);

    // Witch's hut — deep forest
    spawn_ow_npc(175, 350, "Hedge Witch", "I know what you seek. Everyone who comes here seeks the same thing.", NPCRole::PRIEST, 4, 6);
    place_lore(355, 700, "witch's note",
        "The herbs won't help. The prayers won't help. The only cure for what's down there is not going down there.");

    // =============================================
    // ADDITIONAL POINTS OF INTEREST (by province)
    // =============================================

    // --- Frozen Marches (Gathruun, y < 400) ---

    // Frozen waterfall — ice and rock formation
    paint_lake(1100, 200, 3);
    for (int i = 0; i < 6; i++) {
        int rx = 1100 + rng.range(-3, 3);
        int ry = 195 + rng.range(-2, 0);
        if (map.in_bounds(rx, ry)) map.at(rx, ry).type = TileType::ROCK;
    }
    place_lore(1102, 203, "frozen inscription",
        "The waterfall stopped in midwinter. It never started again. The ice remembers the shape of falling.");

    // Collapsed mine
    paint_ruin(800, 250);
    place_lore(800, 250, "mine foreman's log",
        "Shaft 3 broke through into something. Not rock. Not cave. The miners refuse to go back.");
    spawn_ow_npc(402, 126, "Old Miner", "We found mithril down there. And something that didn't want us finding it.", NPCRole::FARMER, 1, 5);

    // Stone circle — ritual site
    for (int a = 0; a < 6; a++) {
        float angle = a * 1.047f; // 60 degrees
        int sx = 1400 + static_cast<int>(4 * std::cos(angle));
        int sy = 200 + static_cast<int>(4 * std::sin(angle));
        if (map.in_bounds(sx, sy)) map.at(sx, sy).type = TileType::ROCK;
    }
    if (map.in_bounds(1400, 200)) map.at(1400, 200).type = TileType::FLOOR_STONE;
    place_lore(1400, 202, "charred altar stone",
        "The circle was here before Gathruun claimed the Marches. The ashes are not from any wood.");

    // --- Heartlands (Morreth, center) ---

    // War memorial — south of Thornwall
    paint_ruin(1050, 850);
    for (int i = 0; i < 5; i++) {
        int mx = 1050 + rng.range(-3, 3);
        int my = 850 + rng.range(-3, 3);
        if (map.in_bounds(mx, my) && map.is_walkable(mx, my))
            map.at(mx, my).type = TileType::FLOOR_STONE;
    }
    place_lore(1050, 850, "war memorial plaque",
        "To the garrison of the Fourth Watch, who held this crossing for eleven days. None survived.");

    // Crossroads shrine — between Thornwall and Millhaven
    if (map.in_bounds(920, 850)) map.at(920, 850).type = TileType::FLOOR_STONE;
    if (map.in_bounds(919, 850)) map.at(919, 850).type = TileType::ROCK;
    place_lore(921, 851, "roadside prayer stone",
        "Travelers leave coins here. The shrine takes them. No one knows where they go.");

    // Abandoned farmstead
    paint_ruin(1100, 800);
    place_lore(1100, 802, "scorched diary",
        "Day 40. The soldiers passed through again. They took the harvest. Day 41. There is nothing left to take.");

    // --- Pale Reach (Soleth, y < 600, x > 900) ---

    // Burned village
    paint_ruin(1300, 500);
    paint_ruin(1310, 505);
    for (int i = 0; i < 8; i++) {
        int bx = 1305 + rng.range(-8, 8);
        int by = 502 + rng.range(-5, 5);
        if (map.in_bounds(bx, by) && map.is_walkable(bx, by))
            map.at(bx, by).type = TileType::FLOOR_BONE;
    }
    place_lore(1305, 503, "charred signpost",
        "This was Ember's Rest. The name turned out to be prophetic.");
    spawn_ow_npc(654, 250, "Survivor", "Soleth's faithful burned it. Said the town was unclean. Maybe it was.", NPCRole::FARMER, 0, 5);

    // Signal beacon (intact)
    if (map.in_bounds(1150, 450)) map.at(1150, 450).type = TileType::ROCK;
    if (map.in_bounds(1151, 450)) map.at(1151, 450).type = TileType::ROCK;
    if (map.in_bounds(1150, 449)) map.at(1150, 449).type = TileType::ROCK;
    spawn_ow_npc(576, 225, "Beacon Keeper", "When I light this, help comes. I haven't lit it yet. I keep hoping I won't have to.", NPCRole::GUARD, 2, 5);

    // --- Iron Coast (Ossren, x > 1100, center-south) ---

    // Abandoned forge
    paint_ruin(1500, 900);
    place_lore(1500, 900, "smith's last work",
        "The anvil cracked under the last blow. The metal was wrong. It came from too deep.");
    spawn_ow_npc(752, 451, "Retired Smithy", "I made weapons for thirty years. Then I made something I couldn't unmake.", NPCRole::FARMER, 4, 5);

    // Quarry
    for (int i = 0; i < 10; i++) {
        int qx = 1600 + rng.range(-5, 5);
        int qy = 800 + rng.range(-4, 4);
        if (map.in_bounds(qx, qy)) map.at(qx, qy).type = TileType::ROCK;
    }
    for (int i = 0; i < 6; i++) {
        int qx = 1600 + rng.range(-3, 3);
        int qy = 800 + rng.range(-2, 2);
        if (map.in_bounds(qx, qy)) map.at(qx, qy).type = TileType::FLOOR_STONE;
    }
    place_lore(1600, 800, "quarry overseer's note",
        "Good stone here. Deep veins. But the workers hear tapping from below. Tapping that answers back.");

    // Merchant caravan wreck
    paint_ruin(1350, 1050);
    place_lore(1350, 1050, "torn manifest",
        "Shipment for Ironhearth. 40 ingots iron, 12 silver, 3 mithril. Ambushed at the crossing. All lost.");

    // --- Dust Provinces (Sythara, y > 1000) ---

    // Plague pit
    for (int i = 0; i < 12; i++) {
        int px = 1000 + rng.range(-6, 6);
        int py = 1200 + rng.range(-4, 4);
        if (map.in_bounds(px, py) && map.is_walkable(px, py))
            map.at(px, py).type = TileType::FLOOR_BONE;
    }
    place_lore(1000, 1200, "plague warden's marker",
        "CONDEMNED. Do not dig. Do not camp. Do not linger. The soil itself is sick.");

    // Dried oasis
    for (int i = 0; i < 4; i++) {
        int ox = 1150 + rng.range(-3, 3);
        int oy = 1250 + rng.range(-2, 2);
        if (map.in_bounds(ox, oy)) map.at(ox, oy).type = TileType::FLOOR_SAND;
    }
    if (map.in_bounds(1150, 1250)) map.at(1150, 1250).type = TileType::FLOOR_DIRT;
    place_lore(1150, 1252, "caravan marker",
        "Water was here. Ask old Dustfall traders. They remember the springs. The springs don't remember them.");

    // Bleached ruins
    paint_ruin(900, 1100);
    paint_ruin(908, 1103);
    place_lore(904, 1102, "faded town charter",
        "Township of Silt Crossing, established in the Year of the Third Reckoning. Population: 0.");
    spawn_ow_npc(455, 552, "Grave Tender", "Someone has to remember the names. The sand forgets everything else.", NPCRole::FARMER, 0, 5);

    // --- Greenwood (Khael, x < 700) ---

    // Druid grove
    for (int i = 0; i < 4; i++) {
        int gx = 450 + rng.range(-3, 3);
        int gy = 900 + rng.range(-3, 3);
        if (map.in_bounds(gx, gy)) map.at(gx, gy).type = TileType::TREE;
    }
    if (map.in_bounds(450, 900)) map.at(450, 900).type = TileType::FLOOR_GRASS;
    place_lore(450, 902, "carved trunk",
        "Khael's first grove. The oldest tree here has no rings. It stopped counting.");

    // Overgrown road
    place_lore(550, 750, "milestone",
        "This road once led to a city. The forest ate it. The trees here grow too fast and too straight.");

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

    // Helper: place doodads inside buildings (stone/cobble/dirt floor, near interior walls)
    auto place_interior = [&](int cx, int cy, int radius, int count,
                               int sx, int sy, SDL_Color tint = {255,255,255,255}) {
        int placed = 0;
        for (int attempt = 0; attempt < count * 15 && placed < count; attempt++) {
            int tx = cx + rng.range(-radius, radius);
            int ty = cy + rng.range(-radius, radius);
            if (!map.in_bounds(tx, ty)) continue;
            auto tt = map.at(tx, ty).type;
            if (tt != TileType::FLOOR_STONE && tt != TileType::FLOOR_COBBLE &&
                tt != TileType::FLOOR_DIRT) continue;
            // Must be near a wall (inside a building)
            if (!adjacent_to_wall(tx, ty)) continue;
            // Don't block doors
            bool near_door = false;
            for (int dy2 = -1; dy2 <= 1 && !near_door; dy2++)
                for (int dx2 = -1; dx2 <= 1; dx2++)
                    if (map.in_bounds(tx+dx2, ty+dy2) &&
                        (map.at(tx+dx2, ty+dy2).type == TileType::DOOR_CLOSED ||
                         map.at(tx+dx2, ty+dy2).type == TileType::DOOR_OPEN))
                        near_door = true;
            if (near_door) continue;
            Entity e = world.create();
            world.add<Position>(e, {tx, ty});
            world.add<Renderable>(e, {SHEET_TILES, sx, sy, tint, 0});
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
    // BUILDING INTERIORS — furnish based on NPC type in each building
    // =============================================
    // Place furniture near NPC positions from map entities so items match the building:
    //   B (blacksmith) -> anvil, equipment piles, barrels
    //   S (shopkeeper) -> barrels, jars, shelves
    //   P (priest/scholar) -> jars, barrels
    //   W/F (villager/farmer residential) -> beds, tables, stools
    //   Buildings with no NPC or innkeeper nearby -> beds, tables (inn/residential)
    for (int i = 0; i < TOWN_COUNT; i++) {
        int tx = ALL_TOWNS[i].x, ty = ALL_TOWNS[i].y;
        int r = 20;

        // Find NPC glyphs within this town's radius
        std::vector<std::pair<int,int>> blacksmith_pos, shopkeeper_pos, residential_pos, priest_pos;
        for (auto& me : map_entities) {
            int dx = me.x - tx, dy = me.y - ty;
            if (std::abs(dx) > r || std::abs(dy) > r) continue;
            if (me.glyph == 'B') blacksmith_pos.push_back({me.x, me.y});
            else if (me.glyph == 'S' || me.glyph == 'M') shopkeeper_pos.push_back({me.x, me.y});
            else if (me.glyph == 'P') priest_pos.push_back({me.x, me.y});
            else if (me.glyph == 'F' || me.glyph == 'W') residential_pos.push_back({me.x, me.y});
        }

        // Blacksmith buildings: anvil + equipment near the B glyph
        for (auto& [bx, by] : blacksmith_pos) {
            place_interior(bx, by, 4, 1, 2, 18);           // anvil
            place_interior(bx, by, 4, rng.range(1, 2), 6, 18); // equipment piles
            place_interior(bx, by, 4, rng.range(1, 2), 4, 17); // barrels
        }

        // Shop buildings: barrels + jars near the S glyph
        for (auto& [sx, sy] : shopkeeper_pos) {
            place_interior(sx, sy, 4, rng.range(2, 3), 4, 17); // barrels
            place_interior(sx, sy, 4, rng.range(1, 2), 2, 17); // jars
            place_interior(sx, sy, 4, rng.range(0, 1), 5, 17); // ore sacks
        }

        // Priest/scholar buildings: jars + barrels (study/temple feel)
        for (auto& [px, py] : priest_pos) {
            place_interior(px, py, 4, rng.range(1, 2), 2, 17); // jars (scrolls/potions)
            place_interior(px, py, 4, rng.range(1, 2), 4, 17); // barrels
        }

        // Residential buildings: beds + tables near F/W glyphs
        for (auto& [fx, fy] : residential_pos) {
            // Bed (vertical pair)
            for (int attempt = 0; attempt < 15; attempt++) {
                int bx2 = fx + rng.range(-3, 3);
                int by2 = fy + rng.range(-3, 3);
                if (!map.in_bounds(bx2, by2) || !map.in_bounds(bx2, by2+1)) continue;
                if (map.at(bx2, by2).type != TileType::FLOOR_STONE) continue;
                if (map.at(bx2, by2+1).type != TileType::FLOOR_STONE) continue;
                if (!adjacent_to_wall(bx2, by2)) continue;
                bool door_near = false;
                for (int dy2 = -1; dy2 <= 2 && !door_near; dy2++)
                    for (int dx2 = -1; dx2 <= 1; dx2++)
                        if (map.in_bounds(bx2+dx2, by2+dy2) &&
                            (map.at(bx2+dx2, by2+dy2).type == TileType::DOOR_CLOSED ||
                             map.at(bx2+dx2, by2+dy2).type == TileType::DOOR_OPEN))
                            door_near = true;
                if (door_near) continue;
                Entity bed_top = world.create();
                world.add<Position>(bed_top, {bx2, by2});
                world.add<Renderable>(bed_top, {SHEET_TILES, 8, 17, {255,255,255,255}, 0});
                Entity bed_bot = world.create();
                world.add<Position>(bed_bot, {bx2, by2+1});
                world.add<Renderable>(bed_bot, {SHEET_TILES, 8, 18, {255,255,255,255}, 0});
                break;
            }
        }

        // Tables in residential/inn buildings (near residential NPCs or town center if no residential)
        int table_count = rng.range(1, 3);
        for (int t = 0; t < table_count; t++) {
            // Place near a residential NPC if any, otherwise town center
            int tcx = tx, tcy = ty;
            if (!residential_pos.empty()) {
                auto& rp = residential_pos[rng.range(0, static_cast<int>(residential_pos.size()) - 1)];
                tcx = rp.first; tcy = rp.second;
            }
            for (int attempt = 0; attempt < 20; attempt++) {
                int tbx = tcx + rng.range(-5, 5);
                int tby = tcy + rng.range(-5, 5);
                if (!map.in_bounds(tbx, tby) || !map.in_bounds(tbx+1, tby)) continue;
                if (map.at(tbx, tby).type != TileType::FLOOR_STONE) continue;
                if (map.at(tbx+1, tby).type != TileType::FLOOR_STONE) continue;
                bool door_near = false;
                for (int dy2 = -1; dy2 <= 1 && !door_near; dy2++)
                    for (int dx2 = -1; dx2 <= 2; dx2++)
                        if (map.in_bounds(tbx+dx2, tby+dy2) &&
                            (map.at(tbx+dx2, tby+dy2).type == TileType::DOOR_CLOSED ||
                             map.at(tbx+dx2, tby+dy2).type == TileType::DOOR_OPEN))
                            door_near = true;
                if (door_near) continue;
                Entity tl = world.create();
                world.add<Position>(tl, {tbx, tby});
                world.add<Renderable>(tl, {SHEET_TILES, 4, 18, {255,255,255,255}, 0});
                Entity tr = world.create();
                world.add<Position>(tr, {tbx+1, tby});
                world.add<Renderable>(tr, {SHEET_TILES, 5, 18, {255,255,255,255}, 0});
                if (map.in_bounds(tbx, tby+1) && map.is_walkable(tbx, tby+1)) {
                    Entity st = world.create();
                    world.add<Position>(st, {tbx, tby+1});
                    world.add<Renderable>(st, {SHEET_TILES, 3, 18, {255,255,255,255}, 0});
                }
                break;
            }
        }

        // General town barrels (outdoor, against walls)
        place_interior(tx, ty, r, rng.range(1, 2), 4, 17);
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
        // Interior furnishing: bed + barrel
        int icx = cd.x + cd.w/2, icy = cd.y + cd.h/2;
        place_interior(icx, icy, 2, 1, 4, 17); // barrel
        place_interior(icx, icy, 2, 1, 3, 18); // stool
        // Barrel or log pile against the outside wall
        place_against_walls(cd.x - 1, cd.y, cd.w + 2, 1, 4, 17); // barrel
        place_against_walls(cd.x - 1, cd.y, cd.w + 2, 1, 6, 17); // log pile
        // Torch by the door
        place_lights(cd.x + cd.w/2, cd.y + cd.h, 3, 1, 5); // torch lit = row 5
    }

    // Small hamlets — 2-3 cabins clustered together
    struct HamletDef { int x, y; const char* name; };
    static const HamletDef HAMLETS[] = {
        {150, 325, "Thornbrook"},
        {600, 100, "Icewind Post"},
        {550, 625, "Dry Creek"},
        {250, 450, "Mosshaven"},
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

        // Interior furnishing for hamlet cabins
        place_interior(hm.x + 2, hm.y + 2, 2, 1, 4, 17);  // barrel in cabin 1
        place_interior(hm.x + 2, hm.y + 2, 2, 1, 3, 18);  // stool in cabin 1
        place_interior(hm.x + 9, hm.y + 3, 2, 1, 4, 17);  // barrel in cabin 2

        // Doodads around hamlet
        place_against_walls(hm.x - 1, hm.y - 1, 14, rng.range(2, 4), 4, 17); // barrels
        place_against_walls(hm.x - 1, hm.y - 1, 14, rng.range(1, 2), 6, 17); // log piles
    }

    // Outposts — single fortified structures (guard post at crossroads)
    struct OutpostDef { int x, y; const char* dialogue; };
    static const OutpostDef OUTPOSTS[] = {
        {525, 330, "Road's clear, last I checked. That was yesterday."},
        {425, 250, "I watch the northern pass. Nothing human comes through anymore."},
        {350, 475, "The southern road gets worse every year. We need more guards."},
    };

    for (auto& op : OUTPOSTS) {
        build_cabin(op.x, op.y, 6, 5);
        spawn_ow_npc(op.x + 3, op.y + 2, "Road Guard", op.dialogue, NPCRole::GUARD, 3, 1);
        // Interior: equipment pile + barrel
        place_interior(op.x + 3, op.y + 2, 2, 1, 6, 18); // equipment pile
        place_interior(op.x + 3, op.y + 2, 2, 1, 4, 17); // barrel
        // Exterior
        place_against_walls(op.x - 1, op.y - 1, 8, 2, 4, 17); // barrels
        place_lights(op.x + 3, op.y + 5, 3, 1, 5); // torch at entrance
    }

    // =============================================
    // OVERWORLD VEGETATION BY REGION
    // =============================================

    // Temperate zone: varied flowers and grasses
    for (int i = 0; i < 80; i++) {
        int x = rng.range(50, 950);
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
        int x = rng.range(50, 950);
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
        int x = rng.range(50, 950);
        int y = rng.range(500, 700);
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
        int x = rng.range(50, 950);
        int y = rng.range(50, 700);
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

    // Saplings near forest edges (col 0, row 25 of tiles sheet)
    for (int i = 0; i < 40; i++) {
        int x = rng.range(50, 950);
        int y = rng.range(100, 650);
        if (!map.in_bounds(x, y) || !map.is_walkable(x, y)) continue;
        // Place near trees but not in dense forest
        int tree_count = 0;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (map.in_bounds(x+dx, y+dy) && map.at(x+dx, y+dy).type == TileType::TREE)
                    tree_count++;
        if (tree_count < 1 || tree_count > 4) continue; // forest edge, not deep forest
        Entity e = world.create();
        world.add<Position>(e, {x, y});
        world.add<Renderable>(e, {SHEET_TILES, 0, 25, {255,255,255,255}, 0});
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

    // =============================================
    // MONSTER LAIRS — small clusters that hint at dungeon proximity
    // =============================================
    struct LairDef { int x, y; const char* type; int count; };
    static const LairDef LAIRS[] = {
        // Wolf dens (Frozen Marches, Greenwood)
        {175, 175, "wolf", 3},      // north wilderness
        {575, 125, "wolf", 4},     // near Glacierveil
        {275, 425, "wolf", 3},      // Greenwood fringe
        // Spider nests (Greenwood, caves)
        {225, 325, "spider", 3},    // deep Greenwood
        {300, 475, "spider", 4},    // near Tanglewood
        // Skeleton patrols (near catacombs/ruins)
        {425, 325, "skeleton", 3},  // near Ashford
        {600, 340, "skeleton", 4}, // near Greywatch
        {675, 525, "skeleton", 3},// Dust Province ruins
        // Bandit camps (roads, trade routes)
        {625, 390, "bandit", 3},   // Iron Coast road
        {475, 525, "bandit", 3},   // Dustfall approach
        {350, 300, "bandit", 2},    // Greenwood road
        // Boar wallows (temperate zones)
        {400, 400, "boar", 3},      // central Heartlands
        {325, 500, "boar", 3},     // south Greenwood
        // Lion pride (Dust Provinces, warm)
        {600, 575, "lion", 2},    // south desert
        {525, 600, "lion", 3},    // near Sandmoor
        // Scorpion nest (Dust Provinces)
        {650, 600, "scorpion", 4}, // deep desert
        {450, 625, "scorpion", 3},  // far south
        // Hyena pack (Dust Provinces, Iron Coast)
        {700, 450, "hyena", 3},     // Iron Coast border
        {550, 550, "hyena", 3},    // Dust Provinces
        // Crocodile (rivers, swamps)
        {200, 550, "crocodile", 2}, // southern swamp
        {500, 525, "crocodile", 2},// river crossing
        // Lynx (cold forests)
        {250, 200, "lynx", 2},       // Greenwood north
        {450, 150, "lynx", 2},       // Frozen Marches
    };

    for (auto& lair : LAIRS) {
        for (int i = 0; i < lair.count; i++) {
            int lx = lair.x + rng.range(-8, 8);
            int ly = lair.y + rng.range(-8, 8);
            if (!map.in_bounds(lx, ly) || !map.is_walkable(lx, ly)) continue;

            // Determine monster stats by type
            const char* name = lair.type;
            int sheet, sx, sy, hp, str, dex, con, dmg, arm, spd, flee, xp;
            if (std::string(name) == "wolf") {
                sheet = SHEET_ANIMALS; sx = 6; sy = 4;
                hp = 14; str = 12; dex = 14; con = 10; dmg = 3; arm = 0; spd = 120; flee = 25; xp = 15;
            } else if (std::string(name) == "spider") {
                sheet = SHEET_MONSTERS; sx = 8; sy = 6;
                hp = 10; str = 8; dex = 14; con = 6; dmg = 3; arm = 0; spd = 120; flee = 30; xp = 15;
            } else if (std::string(name) == "skeleton") {
                sheet = SHEET_MONSTERS; sx = 0; sy = 4;
                hp = 16; str = 10; dex = 10; con = 10; dmg = 3; arm = 2; spd = 100; flee = 0; xp = 20;
            } else if (std::string(name) == "bandit") {
                sheet = SHEET_ROGUES; sx = 4; sy = 0;
                hp = 14; str = 11; dex = 13; con = 10; dmg = 3; arm = 1; spd = 105; flee = 30; xp = 20;
            } else if (std::string(name) == "lion") {
                sheet = SHEET_ANIMALS; sx = 5; sy = 2;
                hp = 28; str = 18; dex = 12; con = 14; dmg = 6; arm = 1; spd = 95; flee = 10; xp = 40;
            } else if (std::string(name) == "scorpion") {
                sheet = SHEET_ANIMALS; sx = 1; sy = 5;
                hp = 10; str = 8; dex = 12; con = 8; dmg = 4; arm = 1; spd = 100; flee = 20; xp = 18;
            } else if (std::string(name) == "hyena") {
                sheet = SHEET_ANIMALS; sx = 2; sy = 4;
                hp = 14; str = 12; dex = 14; con = 10; dmg = 3; arm = 0; spd = 115; flee = 25; xp = 18;
            } else if (std::string(name) == "crocodile") {
                sheet = SHEET_ANIMALS; sx = 0; sy = 6;
                hp = 22; str = 14; dex = 6; con = 16; dmg = 5; arm = 3; spd = 70; flee = 5; xp = 35;
            } else if (std::string(name) == "lynx") {
                sheet = SHEET_ANIMALS; sx = 2; sy = 3;
                hp = 10; str = 8; dex = 16; con = 8; dmg = 3; arm = 0; spd = 135; flee = 40; xp = 12;
            } else { // boar
                sheet = SHEET_ANIMALS; sx = 7; sy = 9;
                hp = 18; str = 14; dex = 8; con = 12; dmg = 4; arm = 1; spd = 90; flee = 20; xp = 20;
            }

            Entity e = world.create();
            world.add<Position>(e, {lx, ly});
            world.add<Renderable>(e, {sheet, sx, sy, {255,255,255,255}, 5});
            Stats ms; ms.name = name; ms.hp = hp; ms.hp_max = hp;
            ms.set_attr(Attr::STR, str); ms.set_attr(Attr::DEX, dex); ms.set_attr(Attr::CON, con);
            ms.base_damage = dmg; ms.natural_armor = arm; ms.base_speed = spd; ms.xp_value = xp;
            world.add<Stats>(e, std::move(ms));
            AI ai; ai.flee_threshold = flee;
            if (std::string(name) == "wolf") ai.behavior = BehaviorType::PACK;
            world.add<AI>(e, ai);
            world.add<Energy>(e, {0, spd});
        }
    }

    // =============================================
    // WANDERING MERCHANTS — buy/sell on the road
    // =============================================
    struct MerchPos { int x, y; const char* name; const char* dialogue; };
    static const MerchPos MERCHANTS[] = {
        {450, 350,  "Traveling Merchant", "I sell what I find. Everything has a price."},
        {600, 425, "Peddler",           "Trinkets, potions, and a few things I shouldn't have."},
        {375, 475,  "Herb Seller",       "Fresh stock. The forest provides."},
        {700, 300, "Arms Dealer",       "I supply both sides. Business is business."},
        {550, 550,"Dustland Trader",   "Water's more valuable than gold out here. I sell both."},
    };
    for (auto& m : MERCHANTS) {
        spawn_ow_npc(m.x, m.y, m.name, m.dialogue, NPCRole::SHOPKEEPER, 3, 6, 25);
    }

    // =============================================
    // ANIMAL HERDS — passive wildlife clusters
    // =============================================
    struct HerdDef { int x, y; int count; int spr_x, spr_y; const char* name; };
    static const HerdDef HERDS[] = {
        // Deer/stags (temperate/cold)
        {750, 550, 4, 0, 7, "deer"},
        {950, 650, 3, 0, 7, "deer"},
        {600, 800, 5, 0, 7, "deer"},
        {1100, 400, 3, 0, 7, "deer"},
        // Goats (mountains/hills)
        {900, 350, 3, 5, 3, "mountain goat"},
        {1200, 300, 4, 5, 3, "mountain goat"},
        // Cows/oxen (farmland near towns)
        {800, 750, 3, 3, 7, "cow"},
        {1050, 850, 3, 3, 7, "cow"},
        // Horses (Heartlands, Iron Coast roads)
        {1000, 700, 2, 1, 6, "horse"},
        {1350, 750, 2, 1, 6, "horse"},
        // Rabbits (temperate zones)
        {700, 700, 4, 6, 4, "rabbit"},
        {850, 600, 3, 6, 4, "rabbit"},
        {550, 900, 4, 6, 4, "rabbit"},
        // Ravens (everywhere, atmospheric)
        {400, 700, 3, 2, 8, "raven"},
        {1300, 500, 2, 2, 8, "raven"},
        {1000, 1000, 3, 2, 8, "raven"},
        // Hawks (mountain/cliff areas)
        {1000, 300, 2, 1, 8, "hawk"},
        {800, 250, 2, 1, 8, "hawk"},
        // Roosters (near towns)
        {1010, 760, 2, 1, 7, "rooster"},
        {760, 660, 2, 1, 7, "rooster"},
        // Foxes (Greenwood, Pale Reach)
        {550, 700, 2, 4, 2, "fox"},
        {1300, 450, 2, 4, 2, "fox"},
        // Lizards (Dust Provinces, warm areas)
        {1100, 1200, 3, 0, 5, "lizard"},
        {900, 1150, 2, 0, 5, "lizard"},
        // Wolves (Frozen Marches, Greenwood edges)
        {1050, 350, 3, 4, 3, "wolf"},
        {600, 650, 2, 4, 3, "wolf"},
        {700, 900, 2, 4, 3, "wolf"},
        // Bears (mountain/forest borders)
        {650, 550, 1, 5, 7, "bear"},
        {900, 300, 1, 5, 7, "bear"},
        // Scorpions (deep Dust Provinces)
        {1000, 1250, 3, 3, 5, "scorpion"},
        {1200, 1150, 2, 3, 5, "scorpion"},
        // Snowy owls (Frozen Marches)
        {1050, 280, 2, 2, 8, "owl"},
        {850, 350, 2, 2, 8, "owl"},
        // Boars (Heartlands)
        {900, 800, 2, 5, 7, "boar"},
        {1100, 750, 2, 5, 7, "boar"},
    };
    for (auto& h : HERDS) {
        for (int i = 0; i < h.count; i++) {
            int hx = h.x + rng.range(-10, 10);
            int hy = h.y + rng.range(-10, 10);
            if (!map.in_bounds(hx, hy) || !map.is_walkable(hx, hy)) continue;
            Entity e = world.create();
            world.add<Position>(e, {hx, hy});
            world.add<Renderable>(e, {SHEET_ANIMALS, h.spr_x, h.spr_y, {255,255,255,255}, 3});
            Stats ds; ds.name = h.name; ds.hp = 6; ds.hp_max = 6;
            ds.base_damage = 0; ds.base_speed = 80; ds.xp_value = 3;
            world.add<Stats>(e, std::move(ds));
            AI dai; dai.flee_threshold = 100; // always flee
            world.add<AI>(e, dai);
            world.add<Energy>(e, {0, 80});
        }
    }

    // =============================================
    // ROADSIDE CAMPS — small structures with NPCs
    // =============================================

    // Bandit roadblock (Iron Coast road)
    paint_ruin(1350, 720);
    spawn_ow_npc(675, 360, "Bandit Leader",
                 "Pay the toll or bleed. Your choice.", NPCRole::FARMER, 4, 0, 30);
    place_lore(1352, 721, "threatening note",
               "Any merchant who tries the Iron Coast road without paying answers to us. No exceptions.");

    // Abandoned caravan (Dust Provinces)
    paint_ruin(1150, 1150);
    place_lore(1152, 1151, "trade manifest",
               "Fifty bolts of silk, twelve casks of wine, and a sealed chest marked DO NOT OPEN. Destination: Endgate. The chest is gone.");

    // Hunting lodge (Greenwood)
    {
        int lx = 500, ly = 750;
        for (int dy = 0; dy < 3; dy++)
            for (int dx = 0; dx < 4; dx++)
                if (map.in_bounds(lx+dx, ly+dy))
                    map.at(lx+dx, ly+dy).type = TileType::FLOOR_STONE;
        // Walls
        for (int dx = 0; dx < 4; dx++) {
            if (map.in_bounds(lx+dx, ly-1)) map.at(lx+dx, ly-1).type = TileType::WALL_WOOD;
            if (map.in_bounds(lx+dx, ly+3)) map.at(lx+dx, ly+3).type = TileType::WALL_WOOD;
        }
        for (int dy = 0; dy < 3; dy++) {
            if (map.in_bounds(lx-1, ly+dy)) map.at(lx-1, ly+dy).type = TileType::WALL_WOOD;
            if (map.in_bounds(lx+4, ly+dy)) map.at(lx+4, ly+dy).type = TileType::WALL_WOOD;
        }
        if (map.in_bounds(lx+1, ly+3)) map.at(lx+1, ly+3).type = TileType::DOOR_CLOSED;
        spawn_ow_npc(lx+2, ly+1, "Lodge Keeper",
                     "The Greenwood's full of things that hunt back. Rest here if you need to.",
                     NPCRole::FARMER, 2, 6, 20);
    }

    // Watchtower camp (Pale Reach border)
    paint_ruin(1350, 450);
    spawn_ow_npc(675, 225, "Border Watcher",
                 "Past here, you're in Soleth's land. The zealots don't take kindly to outsiders.",
                 NPCRole::GUARD, 3, 2, 30);

    // Fisherman's shack (near lake)
    spawn_ow_npc(425, 350, "Fisherman",
                 "The fish have been strange lately. Eyes where eyes shouldn't be.",
                 NPCRole::FARMER, 1, 6, 20);

    // =============================================
    // PROCEDURAL LANDMARKS — scatter across the overworld
    // =============================================
    // Each landmark type has: terrain painting, optional NPC/enemies, lore.
    // Placed with minimum spacing (60 tiles apart), avoiding towns and dungeons.

    enum class LandmarkType {
        RUINS, GRAVEYARD, STANDING_STONES, BANDIT_CAMP, BATTLEFIELD,
        HERMIT_HUT, ABANDONED_SHRINE, BRIDGE_CROSSING, COUNT
    };

    struct PlacedLandmark { int x, y; };
    std::vector<PlacedLandmark> placed;

    auto too_close_to_anything = [&](int x, int y) -> bool {
        // Towns
        for (int i = 0; i < TOWN_COUNT; i++) {
            int dx = x - ALL_TOWNS[i].x, dy = y - ALL_TOWNS[i].y;
            if (dx*dx + dy*dy < 50*50) return true;
        }
        // Dungeons
        for (auto& de : dungeon_registry) {
            int dx = x - de.x, dy = y - de.y;
            if (dx*dx + dy*dy < 40*40) return true;
        }
        // Other landmarks
        for (auto& lm : placed) {
            int dx = x - lm.x, dy = y - lm.y;
            if (dx*dx + dy*dy < 60*60) return true;
        }
        return false;
    };

    // Helper: spawn a hostile NPC at a landmark
    auto spawn_hostile = [&](int x, int y, const char* name, int hp, int dmg, int arm,
                              int spr_x, int spr_y, int xp_val = 20) {
        for (int a = 0; a < 15; a++) {
            int tx = x + rng.range(-3, 3);
            int ty = y + rng.range(-3, 3);
            if (!map.in_bounds(tx, ty) || !map.is_walkable(tx, ty)) continue;
            if (combat::entity_at(world, tx, ty, 0) != NULL_ENTITY) continue;
            Entity e = world.create();
            world.add<Position>(e, {tx, ty});
            world.add<Renderable>(e, {SHEET_ROGUES, spr_x, spr_y, {255,255,255,255}, 5});
            Stats ms; ms.name = name; ms.hp = hp; ms.hp_max = hp;
            ms.base_damage = dmg; ms.natural_armor = arm; ms.base_speed = 100; ms.xp_value = xp_val;
            world.add<Stats>(e, std::move(ms));
            AI ai; ai.state = AIState::IDLE;
            world.add<AI>(e, ai);
            world.add<Energy>(e, {0, 100});
            world.add<StatusEffects>(e);
            return;
        }
    };

    // Helper: paint a graveyard (headstones + dirt)
    auto paint_graveyard = [&](int cx, int cy) {
        for (int dy = -3; dy <= 3; dy++) {
            for (int dx = -4; dx <= 4; dx++) {
                int tx = cx + dx, ty = cy + dy;
                if (!map.in_bounds(tx, ty)) continue;
                auto& t = map.at(tx, ty);
                if (t.type != TileType::FLOOR_GRASS && t.type != TileType::FLOOR_DIRT
                    && t.type != TileType::FLOOR_SAND) continue;
                t.type = TileType::FLOOR_DIRT;
            }
        }
    };

    // Helper: paint standing stones (ring of rough walls)
    auto paint_stone_circle = [&](int cx, int cy) {
        for (int i = 0; i < 6; i++) {
            float angle = static_cast<float>(i) * 6.2832f / 6.0f;
            int sx = cx + static_cast<int>(std::cos(angle) * 3);
            int sy = cy + static_cast<int>(std::sin(angle) * 2);
            if (map.in_bounds(sx, sy)) {
                auto& t = map.at(sx, sy);
                if (t.type == TileType::FLOOR_GRASS || t.type == TileType::FLOOR_DIRT
                    || t.type == TileType::FLOOR_SAND || t.type == TileType::FLOOR_SNOW)
                    t.type = TileType::WALL_STONE_ROUGH;
            }
        }
        // Center is stone floor
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (map.in_bounds(cx+dx, cy+dy))
                    map.at(cx+dx, cy+dy).type = TileType::FLOOR_STONE;
    };

    // Helper: paint a small hut (3x3 wood walls, door, stone floor inside)
    auto paint_hut = [&](int cx, int cy) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (map.in_bounds(cx+dx, cy+dy))
                    map.at(cx+dx, cy+dy).type = TileType::FLOOR_STONE;
        for (int dx = -2; dx <= 2; dx++) {
            if (map.in_bounds(cx+dx, cy-2)) map.at(cx+dx, cy-2).type = TileType::WALL_WOOD;
            if (map.in_bounds(cx+dx, cy+2)) map.at(cx+dx, cy+2).type = TileType::WALL_WOOD;
        }
        for (int dy = -1; dy <= 1; dy++) {
            if (map.in_bounds(cx-2, cy+dy)) map.at(cx-2, cy+dy).type = TileType::WALL_WOOD;
            if (map.in_bounds(cx+2, cy+dy)) map.at(cx+2, cy+dy).type = TileType::WALL_WOOD;
        }
        if (map.in_bounds(cx, cy+2)) map.at(cx, cy+2).type = TileType::DOOR_CLOSED;
    };

    // Helper: paint a battlefield (scattered stone, bone floor)
    auto paint_battlefield = [&](int cx, int cy) {
        for (int dy = -4; dy <= 4; dy++) {
            for (int dx = -5; dx <= 5; dx++) {
                int tx = cx + dx, ty = cy + dy;
                if (!map.in_bounds(tx, ty)) continue;
                auto& t = map.at(tx, ty);
                if (t.type != TileType::FLOOR_GRASS && t.type != TileType::FLOOR_DIRT) continue;
                if (rng.chance(30)) t.type = TileType::FLOOR_BONE;
                else t.type = TileType::FLOOR_DIRT;
            }
        }
    };

    // Helper: paint a bridge crossing (stone path over water)
    auto paint_bridge = [&](int cx, int cy) {
        // Small water crossing with cobble path
        for (int dx = -5; dx <= 5; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int tx = cx + dx, ty = cy + dy;
                if (!map.in_bounds(tx, ty)) continue;
                if (std::abs(dx) <= 1) {
                    map.at(tx, ty).type = TileType::FLOOR_COBBLE;
                } else if (std::abs(dy) == 0) {
                    map.at(tx, ty).type = TileType::FLOOR_COBBLE;
                } else {
                    auto& t = map.at(tx, ty);
                    if (t.type == TileType::FLOOR_GRASS || t.type == TileType::FLOOR_DIRT)
                        t.type = TileType::WATER;
                }
            }
        }
    };

    // Lore tables per landmark type
    static const char* RUIN_LORE[][2] = {
        {"crumbling journal", "The walls still stand but whoever lived here left in a hurry. The door was barred from outside."},
        {"broken tablet", "Names are carved here. Dozens of them. The last few are scratched out."},
        {"faded map fragment", "Roads marked in red. One leads to a place that isn't on any current map."},
        {"scattered pages", "A merchant's ledger. The last entry reads: 'They came from the north. We couldn't hold.'"},
    };
    static const char* GRAVEYARD_LORE[][2] = {
        {"gravestone", "The inscription is worn smooth. Only the date remains: 312 AR."},
        {"epitaph", "HERE LIES ONE WHO SOUGHT THE RELIQUARY. THE BRAND CONSUMED THEM."},
        {"burial marker", "A child's grave. Wildflowers grow here despite the dead soil."},
        {"cracked headstone", "Seven graves in a row. Same date. Same cause: 'the sickness.'"},
    };
    static const char* STONES_LORE[][2] = {
        {"rune-carved stone", "The symbols predate every language you know. They hum when touched."},
        {"offering bowl", "Old coins and dried flowers fill the bowl. Someone still visits."},
        {"etched pillar", "A star map carved into stone. Three constellations are circled."},
    };
    static const char* CAMP_LORE[][2] = {
        {"threatening note", "Stay off the road after dark. We own this stretch."},
        {"crude map", "Patrol routes and ambush points marked in charcoal. Fresh."},
        {"stolen purse", "Empty except for a love letter. The handwriting is shaky."},
    };
    static const char* BATTLEFIELD_LORE[][2] = {
        {"rusted sword", "The blade is notched beyond repair. The grip still has finger marks."},
        {"torn banner", "Blue and gold. You don't recognize the sigil."},
        {"soldier's last letter", "Tell my son I stood. Tell him I didn't run."},
        {"cracked shield", "An arrow is still embedded in it. The shaft snapped at the entry."},
    };
    static const char* HERMIT_LORE[][2] = {
        {"hermit's journal", "Day 412. Still no visitors. The birds are enough company."},
        {"recipe notes", "Moonpetal, ground bone, river clay. Mix under a new moon. For what, it doesn't say."},
        {"scrawled warning", "Do not follow the lights past the treeline. I learned this the hard way."},
    };
    static const char* SHRINE_LORE[][2] = {
        {"broken idol", "The face has been chiseled off. Someone wanted this god forgotten."},
        {"faded prayer", "Grant me passage through the dark. I have paid what was asked."},
        {"offering pile", "Old bones, dried herbs, a lock of hair. Whatever god was here, the faithful were desperate."},
    };
    static const char* BRIDGE_LORE[][2] = {
        {"milestone", "THORNWALL 8 LEAGUES. The distance is wrong by half."},
        {"traveler's mark", "Safe crossing here. Watch the banks at night."},
    };

    // Hermit dialogues
    static const char* HERMIT_DIALOGUE[] = {
        "I came here to forget. Looks like you came here to remember something.",
        "Don't mind the wards. They keep out the curious, not the desperate.",
        "I was a scholar once. Now I'm just old. Ask your question and go.",
        "The forest talks if you're quiet enough. Mostly it says 'leave.'",
    };

    // Attempt to place ~50 landmarks across the map
    int attempts = 0;
    int landmark_count = 0;
    int target = 50;

    while (landmark_count < target && attempts < 500) {
        attempts++;
        int lx = rng.range(100, map.width() - 100);
        int ly = rng.range(100, map.height() - 100);

        if (!map.in_bounds(lx, ly) || !map.is_walkable(lx, ly)) continue;
        if (too_close_to_anything(lx, ly)) continue;

        // Check terrain is natural (not water, not wall)
        auto tt = map.at(lx, ly).type;
        if (tt == TileType::WATER || tt == TileType::WALL_STONE_BRICK ||
            tt == TileType::WALL_STONE_ROUGH || tt == TileType::WALL_WOOD) continue;

        // Pick a type biased by region
        auto type = static_cast<LandmarkType>(rng.range(0, static_cast<int>(LandmarkType::COUNT) - 1));

        // Region biases: graveyards more common in north, battlefields in heartlands,
        // bandit camps on roads (east/south), hermits in forests (west)
        GodId region = get_town_god(lx, ly);
        if (region == GodId::KHAEL && rng.chance(40)) type = LandmarkType::HERMIT_HUT;
        if (region == GodId::GATHRUUN && rng.chance(30)) type = LandmarkType::STANDING_STONES;
        if (region == GodId::SYTHARA && rng.chance(30)) type = LandmarkType::GRAVEYARD;
        if (region == GodId::MORRETH && rng.chance(30)) type = LandmarkType::BATTLEFIELD;
        if (region == GodId::OSSREN && rng.chance(30)) type = LandmarkType::BANDIT_CAMP;

        placed.push_back({lx, ly});
        landmark_count++;

        switch (type) {
            case LandmarkType::RUINS: {
                paint_ruin(lx, ly);
                int li = rng.range(0, 3);
                place_lore(lx, ly, RUIN_LORE[li][0], RUIN_LORE[li][1]);
                // 40% chance a scavenger or squatter
                if (rng.chance(40)) {
                    spawn_ow_npc(lx, ly, "Scavenger",
                                 "I was here first. Pick through what's left if you want.",
                                 NPCRole::FARMER, 5, 6, 25);
                }
                break;
            }
            case LandmarkType::GRAVEYARD: {
                paint_graveyard(lx, ly);
                int li = rng.range(0, 3);
                place_lore(lx, ly, GRAVEYARD_LORE[li][0], GRAVEYARD_LORE[li][1]);
                // Spawn 2-3 skeletons
                int skel_count = rng.range(2, 3);
                for (int i = 0; i < skel_count; i++)
                    spawn_hostile(lx, ly, "skeleton", 12, 3, 0, 0, 4, 15);
                break;
            }
            case LandmarkType::STANDING_STONES: {
                paint_stone_circle(lx, ly);
                int li = rng.range(0, 2);
                place_lore(lx, ly, STONES_LORE[li][0], STONES_LORE[li][1]);
                break;
            }
            case LandmarkType::BANDIT_CAMP: {
                paint_ruin(lx, ly);
                int li = rng.range(0, 2);
                place_lore(lx, ly, CAMP_LORE[li][0], CAMP_LORE[li][1]);
                // 2-3 bandits
                int bandit_count = rng.range(2, 3);
                for (int i = 0; i < bandit_count; i++)
                    spawn_hostile(lx, ly, "bandit", 18, 5, 1, 4, 0, 20);
                break;
            }
            case LandmarkType::BATTLEFIELD: {
                paint_battlefield(lx, ly);
                int li = rng.range(0, 3);
                place_lore(lx, ly, BATTLEFIELD_LORE[li][0], BATTLEFIELD_LORE[li][1]);
                // Scattered equipment piles
                for (int ep = 0; ep < rng.range(1, 3); ep++) {
                    int ex = lx + rng.range(-3, 3), ey = ly + rng.range(-3, 3);
                    if (map.in_bounds(ex, ey) && map.is_walkable(ex, ey)) {
                        Entity pile = world.create();
                        world.add<Position>(pile, {ex, ey});
                        world.add<Renderable>(pile, {SHEET_TILES, 6, 18, {180,160,140,255}, 0});
                    }
                }
                // Scavengeable gear: drop a random weapon on the ground
                {
                    static const char* WPNS[] = {"rusted sword", "dented helm", "broken spear", "notched axe"};
                    int wi = rng.range(0, 3);
                    for (int a = 0; a < 10; a++) {
                        int wx = lx + rng.range(-3, 3), wy = ly + rng.range(-3, 3);
                        if (!map.in_bounds(wx, wy) || !map.is_walkable(wx, wy)) continue;
                        Entity e = world.create();
                        world.add<Position>(e, {wx, wy});
                        world.add<Renderable>(e, {SHEET_ITEMS, 0, 0, {180,160,140,255}, 1});
                        Item item; item.name = WPNS[wi];
                        item.description = "Scavenged from an old battlefield. Barely functional.";
                        item.type = ItemType::WEAPON; item.slot = EquipSlot::MAIN_HAND;
                        item.damage_bonus = rng.range(1, 3); item.gold_value = rng.range(3, 10);
                        item.identified = true;
                        world.add<Item>(e, std::move(item));
                        break;
                    }
                }
                // Chance of undead
                if (rng.chance(50)) {
                    spawn_hostile(lx, ly, "ghoul", 20, 5, 1, 2, 4, 20);
                }
                break;
            }
            case LandmarkType::HERMIT_HUT: {
                paint_hut(lx, ly);
                int li = rng.range(0, 2);
                place_lore(lx, ly, HERMIT_LORE[li][0], HERMIT_LORE[li][1]);
                int di = rng.range(0, 3);
                spawn_ow_npc(lx, ly, "Hermit", HERMIT_DIALOGUE[di],
                             NPCRole::FARMER, 2, 5, 20);
                break;
            }
            case LandmarkType::ABANDONED_SHRINE: {
                paint_stone_circle(lx, ly);
                int li = rng.range(0, 2);
                place_lore(lx, ly, SHRINE_LORE[li][0], SHRINE_LORE[li][1]);
                // Place an actual shrine tile at center
                if (map.in_bounds(lx, ly)) {
                    map.at(lx, ly).type = TileType::SHRINE;
                    GodId shrine_god = static_cast<GodId>(rng.range(0, GOD_COUNT - 1));
                    map.at(lx, ly).variant = static_cast<uint8_t>(shrine_god);
                }
                break;
            }
            case LandmarkType::BRIDGE_CROSSING: {
                paint_bridge(lx, ly);
                int li = rng.range(0, 1);
                place_lore(lx, ly, BRIDGE_LORE[li][0], BRIDGE_LORE[li][1]);
                break;
            }
            default: break;
        }
    }

    // =============================================
    // ROADSIDE CONTENT — campfires, wagons, mile markers along roads
    // =============================================
    {
        // Scatter roadside content along road tiles (not near towns)
        int roadside_placed = 0;
        int roadside_target = 30;
        for (int attempt = 0; attempt < 400 && roadside_placed < roadside_target; attempt++) {
            int rx = rng.range(100, map.width() - 100);
            int ry = rng.range(100, map.height() - 100);
            if (!map.in_bounds(rx, ry)) continue;
            auto rt = map.at(rx, ry).type;
            // Must be on or adjacent to a road
            if (rt != TileType::FLOOR_DIRT && rt != TileType::FLOOR_COBBLE &&
                rt != TileType::FLOOR_SAND) continue;
            // Not near towns
            if (near_town(rx, ry, 35) >= 0) continue;
            // Not near other roadside content or landmarks
            bool too_close = false;
            for (auto& lm : placed) {
                int dx = rx - lm.x, dy = ry - lm.y;
                if (dx*dx + dy*dy < 30*30) { too_close = true; break; }
            }
            if (too_close) continue;
            // Find an adjacent walkable off-road tile for the doodad
            int ox = rx, oy = ry;
            for (int dy = -1; dy <= 1 && ox == rx; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int tx = rx + dx, ty = ry + dy;
                    if (!map.in_bounds(tx, ty)) continue;
                    auto tt = map.at(tx, ty).type;
                    if (tt == TileType::FLOOR_GRASS || tt == TileType::FLOOR_SNOW ||
                        tt == TileType::FLOOR_SAND || tt == TileType::FLOOR_DIRT) {
                        ox = tx; oy = ty; break;
                    }
                }
            }
            if (ox == rx && oy == ry) continue; // no adjacent open tile

            placed.push_back({rx, ry});
            roadside_placed++;

            unsigned h = static_cast<unsigned>(rx * 7919 + ry * 1301);
            int type = h % 5;

            switch (type) {
                case 0: // Campfire remains (burnt-out fire pit)
                    place_lore(ox, oy, "cold campfire",
                               "Ashes and boot prints. Someone camped here recently.");
                    break;
                case 1: // Abandoned wagon
                {
                    // Barrel + log pile = wagon remains
                    Entity e1 = world.create();
                    world.add<Position>(e1, {ox, oy});
                    world.add<Renderable>(e1, {SHEET_TILES, 4, 17, {180, 160, 140, 255}, 0}); // barrel
                    if (map.in_bounds(ox+1, oy) && map.is_walkable(ox+1, oy)) {
                        Entity e2 = world.create();
                        world.add<Position>(e2, {ox+1, oy});
                        world.add<Renderable>(e2, {SHEET_TILES, 6, 17, {160, 140, 120, 255}, 0}); // logs
                    }
                    break;
                }
                case 2: // Mile marker
                {
                    // Find nearest town for the sign text
                    int best_ti = -1; int best_d = 9999;
                    for (int ti = 0; ti < TOWN_COUNT; ti++) {
                        int dx = rx - ALL_TOWNS[ti].x, dy = ry - ALL_TOWNS[ti].y;
                        int d = dx*dx + dy*dy;
                        if (d < best_d) { best_d = d; best_ti = ti; }
                    }
                    if (best_ti >= 0) {
                        Entity e = world.create();
                        world.add<Position>(e, {ox, oy});
                        world.add<Renderable>(e, {SHEET_TILES, 7, 17, {255,255,255,255}, 3});
                        char sbuf[128];
                        int dist = static_cast<int>(std::sqrt(static_cast<float>(best_d)) / 20);
                        snprintf(sbuf, sizeof(sbuf), "%s, %d leagues %s.",
                                 ALL_TOWNS[best_ti].name, std::max(1, dist),
                                 compass_dir(rx, ry, ALL_TOWNS[best_ti].x, ALL_TOWNS[best_ti].y));
                        world.add<Sign>(e, {sbuf});
                    }
                    break;
                }
                case 3: // Supply cache
                {
                    Entity e = world.create();
                    world.add<Position>(e, {ox, oy});
                    world.add<Renderable>(e, {SHEET_TILES, 5, 17, {200, 190, 170, 255}, 0}); // sack
                    break;
                }
                case 4: // Roadside shrine (small stone)
                {
                    Entity e = world.create();
                    world.add<Position>(e, {ox, oy});
                    world.add<Renderable>(e, {SHEET_TILES, rng.range(0, 1), 18, {200, 200, 210, 255}, 0}); // rock
                    GodId road_god = get_town_god(rx, ry);
                    auto& gi = get_god_info(road_god);
                    char pbuf[128];
                    snprintf(pbuf, sizeof(pbuf), "A small %s shrine. %s watches this road.", gi.name, gi.name);
                    place_lore(ox, oy, "roadside prayer stone", pbuf);
                    break;
                }
            }
        }
    }
}

// =============================================
// process_npc_wander — move NPCs around their homes
// =============================================

void process_npc_wander(World& world, TileMap& map, RNG& rng) {
    // Only wander NPCs near the player (within 30 tiles) to avoid O(n²) cost
    int player_x = 0, player_y = 0;
    {
        auto& pp = world.pool<Player>();
        if (pp.size() > 0) {
            Entity pe = pp.entity_at(0);
            if (world.has<Position>(pe)) {
                auto& ppos = world.get<Position>(pe);
                player_x = ppos.x; player_y = ppos.y;
            }
        }
    }

    auto& npc_pool = world.pool<NPC>();
    for (size_t i = 0; i < npc_pool.size(); i++) {
        Entity e = npc_pool.entity_at(i);
        auto& npc = npc_pool.at_index(i);

        // Stationary NPCs: shopkeepers, innkeepers, blacksmiths, guards, elders, priests
        if (npc.role == NPCRole::SHOPKEEPER || npc.role == NPCRole::INNKEEPER ||
            npc.role == NPCRole::BLACKSMITH || npc.role == NPCRole::GUARD ||
            npc.role == NPCRole::ELDER || npc.role == NPCRole::PRIEST) continue;

        if (!world.has<Position>(e) || !world.has<Energy>(e)) continue;

        // Skip NPCs far from player — no point animating what you can't see
        auto& pos_check = world.get<Position>(e);
        if (std::abs(pos_check.x - player_x) > 30 || std::abs(pos_check.y - player_y) > 30) continue;
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

        // Don't cross indoor/outdoor boundaries (prevents farmers entering buildings,
        // villagers wandering out of houses through doors)
        auto home_type = map.at(npc.home_x, npc.home_y).type;
        auto target_type = map.at(nx, ny).type;
        bool home_indoor = (home_type == TileType::FLOOR_STONE || home_type == TileType::FLOOR_COBBLE);
        bool target_indoor = (target_type == TileType::FLOOR_STONE || target_type == TileType::FLOOR_COBBLE);
        if (home_indoor != target_indoor) continue;

        // Don't walk into other entities (use fast check)
        if (combat::entity_at(world, nx, ny, e) != NULL_ENTITY) continue;

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
    enum OWId { WOLF, BOAR, HIGHWAYMAN, SPIDER, BEAR, BANDIT, SNAKE, DIRE_WOLF, SKELETON,
                FOX, SCORPION, LION, CROCODILE, HYENA, LYNX, OW_COUNT };
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
        // New animals using unused sprites
        {"fox",          SHEET_ANIMALS,  4, 2,  8,  6, 16,  6, 2, 0, 140, 60, 8},
        {"scorpion",     SHEET_ANIMALS,  1, 5, 10,  8, 12,  8, 4, 1, 100, 20, 18},
        {"lion",         SHEET_ANIMALS,  5, 2, 28, 18, 12, 14, 6, 1,  95, 10, 40},
        {"crocodile",    SHEET_ANIMALS,  0, 6, 22, 14,  6, 16, 5, 3,  70,  5, 35},
        {"hyena",        SHEET_ANIMALS,  2, 4, 14, 12, 14, 10, 3, 0, 115, 25, 18},
        {"lynx",         SHEET_ANIMALS,  2, 3, 10,  8, 16,  8, 3, 0, 135, 40, 12},
    };

    // Climate-zoned enemy tables per province (expanded)
    static const OWId FROZEN[]    = {DIRE_WOLF, WOLF, WOLF, SKELETON, BEAR, LYNX};
    static const OWId PALE[]      = {WOLF, BEAR, SPIDER, HIGHWAYMAN, SKELETON, FOX};
    static const OWId GREENWOOD[] = {SPIDER, BOAR, SNAKE, BEAR, WOLF, LYNX, FOX};
    static const OWId HEARTLAND[] = {WOLF, BOAR, HIGHWAYMAN, BANDIT, SPIDER, FOX};
    static const OWId IRON[]      = {BANDIT, HIGHWAYMAN, SPIDER, BOAR, SKELETON, HYENA};
    static const OWId DUST[]      = {SNAKE, SKELETON, BANDIT, SCORPION, LION, CROCODILE, HYENA};

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
        int table_size = 6; // HEARTLAND default size
        GodId region = get_town_god(sx, sy);
        switch (region) {
            case GodId::GATHRUUN: table = FROZEN;    table_size = 6; break;
            case GodId::SOLETH:   table = PALE;      table_size = 6; break;
            case GodId::KHAEL:    table = GREENWOOD;  table_size = 7; break;
            case GodId::OSSREN:   table = IRON;      table_size = 6; break;
            case GodId::SYTHARA:  table = DUST;      table_size = 7; break;
            default:              table = HEARTLAND;  table_size = 6; break;
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
        { AI ow_ai; ow_ai.flee_threshold = def.flee; world.add<AI>(e, ow_ai); }
        world.add<Energy>(e, {0, def.speed});
        return; // one at a time
    }
}

} // namespace overworld
