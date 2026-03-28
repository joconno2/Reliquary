#include "generation/populate.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/stats.h"
#include "components/ai.h"
#include "components/energy.h"
#include "core/spritesheet.h"

namespace populate {

struct MonsterDef {
    const char* name;
    int sheet;
    int sprite_x, sprite_y;
    int hp;
    int str, dex, con;
    int base_damage;
    int natural_armor;
    int speed;
    int flee_threshold;
};

// Monster table — row/col in monsters.png
// Row 0: orcs/goblins, Row 4: undead, Row 6: beasts, etc.
static const MonsterDef MONSTER_TABLE[] = {
    // Early game
    {"goblin",          SHEET_MONSTERS, 2, 0, 8,   8, 12, 8,  2, 0, 110, 30},
    {"goblin archer",   SHEET_MONSTERS, 5, 0, 10,  8, 14, 8,  3, 0, 110, 25},
    {"orc",             SHEET_MONSTERS, 0, 0, 18,  14, 8, 12, 4, 1, 90,  15},
    {"giant rat",       SHEET_MONSTERS, 11, 6, 6,  6, 14, 6,  2, 0, 130, 40},
    {"giant spider",    SHEET_MONSTERS, 8, 6, 12,  10, 12, 8, 3, 1, 120, 20},
    {"kobold",          SHEET_MONSTERS, 0, 9, 6,   6, 12, 6,  1, 0, 120, 35},

    // Mid game
    {"orc warchief",    SHEET_MONSTERS, 4, 0, 30,  16, 10, 14, 6, 2, 85,  10},
    {"skeleton",        SHEET_MONSTERS, 0, 4, 14,  10, 10, 10, 3, 2, 100, 0},
    {"zombie",          SHEET_MONSTERS, 4, 4, 20,  14, 6,  16, 5, 1, 60,  0},
    {"troll",           SHEET_MONSTERS, 2, 1, 35,  18, 8,  18, 7, 2, 80,  10},
    {"warg",            SHEET_MONSTERS, 10, 6, 16, 14, 14, 12, 5, 1, 130, 20},
};

static constexpr int MONSTER_COUNT = sizeof(MONSTER_TABLE) / sizeof(MONSTER_TABLE[0]);

void spawn_monsters(World& world, const TileMap& map,
                     const std::vector<Room>& rooms, RNG& rng) {
    // Skip room 0 (player spawn)
    for (size_t r = 1; r < rooms.size(); r++) {
        auto& room = rooms[r];

        // 1-3 monsters per room
        int count = rng.range(1, 3);
        for (int i = 0; i < count; i++) {
            // Random position in room
            int x = rng.range(room.x + 1, room.x + room.w - 2);
            int y = rng.range(room.y + 1, room.y + room.h - 2);

            if (!map.is_walkable(x, y)) continue;

            // Pick a monster type (weighted toward early game for now)
            int idx = rng.range(0, std::min(MONSTER_COUNT - 1, 5 + static_cast<int>(r) / 3));
            auto& def = MONSTER_TABLE[idx];

            Entity e = world.create();
            world.add<Position>(e, {x, y});
            world.add<Renderable>(e, {def.sheet, def.sprite_x, def.sprite_y,
                                      {255, 255, 255, 255}, 5});

            Stats stats;
            stats.name = def.name;
            stats.hp = def.hp;
            stats.hp_max = def.hp;
            stats.set_attr(Attr::STR, def.str);
            stats.set_attr(Attr::DEX, def.dex);
            stats.set_attr(Attr::CON, def.con);
            stats.base_damage = def.base_damage;
            stats.natural_armor = def.natural_armor;
            stats.base_speed = def.speed;
            world.add<Stats>(e, std::move(stats));

            world.add<AI>(e, {AIState::IDLE, -1, -1, 0, def.flee_threshold});
            world.add<Energy>(e, {0, def.speed});
        }
    }
}

} // namespace populate
