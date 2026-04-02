// Data integrity tests for Reliquary
// Validates all game data arrays, sprite bounds, enum consistency, and JSON data.
// Build: cmake --build build --target test_data
// Run: ./build/test_data

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <set>
#include <fstream>
#include <cassert>

// Game data headers (no SDL dependency)
#include "components/class_def.h"
#include "components/traits.h"
#include "components/background.h"
#include "components/spellbook.h"
#include "components/god.h"
#include "components/quest.h"
#include "data/world_data.h"
// Monster data — inline definitions for test access
struct TestMonsterDef {
    const char* name;
    int sheet, sprite_x, sprite_y;
    int hp, str, dex, con, base_damage, natural_armor, speed, flee_threshold, xp_value;
};
static const TestMonsterDef MONSTERS[] = {
    {"giant rat",       1, 11, 6,  6,   6, 14,  6,  2, 0, 130, 40,  10},
    {"bat",             1, 11, 7,  4,   4, 16,  4,  1, 0, 150, 70,   5},
    {"kobold",          1,  0, 9,  6,   6, 12,  6,  1, 0, 120, 35,  10},
    {"slime",           1,  9, 6, 16,   6,  4, 14,  2, 0,  60,  0,  15},
    {"goblin",          1,  2, 0,  8,   8, 12,  8,  2, 0, 110, 30,  15},
    {"giant spider",    1,  8, 6, 12,  10, 12,  8,  3, 1, 120, 20,  20},
    {"goblin archer",   1,  5, 0, 10,   8, 14,  8,  3, 0, 110, 25,  20},
    {"orc",             1,  0, 0, 18,  14,  8, 12,  4, 1,  90, 15,  30},
    {"skeleton",        1,  0, 4, 14,  10, 10, 10,  3, 2, 100,  0,  25},
    {"zombie",          1,  4, 4, 20,  14,  6, 16,  5, 1,  60,  0,  30},
    {"warg",            1, 10, 6, 16,  14, 14, 12,  5, 1, 130, 20,  35},
    {"orc warchief",    1,  4, 0, 30,  16, 10, 14,  6, 2,  85, 10,  50},
    {"troll",           1,  2, 1, 35,  18,  8, 18,  7, 2,  80, 10,  60},
    {"ghoul",           1,  5, 4, 22,  14, 12, 14,  5, 1, 110,  0,  40},
    {"lich",            1,  2, 4, 50,  10, 10, 12, 12, 0, 100,  0,  80},
    {"death knight",    1,  3, 4, 65,  18, 12, 16, 14, 4,  90,  0, 100},
    {"manticore",       1,  3, 6, 35,  16, 14, 14,  7, 2, 110, 10,  70},
    {"minotaur",        1,  7, 7, 45,  20, 10, 18,  9, 3,  85, 10,  90},
    {"naga",            1,  4, 7, 30,  14, 16, 12,  6, 1, 120, 15,  60},
    {"dragon",          1,  2, 8,120,  22, 12, 22, 18, 5,  80,  0, 200},
};
static constexpr int MONSTER_COUNT = sizeof(MONSTERS) / sizeof(MONSTERS[0]);

// nlohmann JSON for dungeons.json validation
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Spritesheet IDs (must match core/spritesheet.h SpriteSheet enum)
static constexpr int SHEET_ROGUES = 0;
static constexpr int SHEET_MONSTERS = 1;
static constexpr int SHEET_ANIMALS = 2;

// Spritesheet dimensions (tiles) — from actual image files
static constexpr int ROGUES_COLS = 7, ROGUES_ROWS = 7;
static constexpr int MONSTERS_COLS = 12, MONSTERS_ROWS = 13;
static constexpr int ANIMALS_COLS = 9, ANIMALS_ROWS = 16;
static constexpr int ITEMS_COLS = 11, ITEMS_ROWS = 26;
static constexpr int TILES_COLS = 17, TILES_ROWS = 26;

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %-50s", name); } while(0)
#define PASS() do { tests_passed++; printf("[PASS]\n"); } while(0)
#define FAIL(msg) do { tests_failed++; printf("[FAIL] %s\n", msg); } while(0)
#define CHECK(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

// ========================================
// CLASS VALIDATION
// ========================================

void test_class_count() {
    TEST("Class count matches enum");
    CHECK(CLASS_COUNT == static_cast<int>(ClassId::COUNT),
          "CLASS_COUNT != ClassId::COUNT");
    CHECK(CLASS_COUNT >= 21, "Expected at least 21 classes");
    PASS();
}

void test_class_sprites() {
    TEST("Class sprites within sheet bounds");
    for (int i = 0; i < CLASS_COUNT; i++) {
        auto& c = get_class_info(static_cast<ClassId>(i));
        int max_col, max_row;
        if (c.sprite_sheet == SHEET_ROGUES) {
            max_col = ROGUES_COLS; max_row = ROGUES_ROWS;
        } else if (c.sprite_sheet == SHEET_MONSTERS) {
            max_col = MONSTERS_COLS; max_row = MONSTERS_ROWS;
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "Class %s has invalid sheet %d", c.name, c.sprite_sheet);
            FAIL(buf);
            return;
        }
        if (c.sprite_x < 0 || c.sprite_x >= max_col ||
            c.sprite_y < 0 || c.sprite_y >= max_row) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Class %s sprite (%d,%d) out of bounds (%dx%d)",
                     c.name, c.sprite_x, c.sprite_y, max_col, max_row);
            FAIL(buf);
            return;
        }
    }
    PASS();
}

void test_class_stats_valid() {
    TEST("Class stats are reasonable");
    for (int i = 0; i < CLASS_COUNT; i++) {
        auto& c = get_class_info(static_cast<ClassId>(i));
        if (c.hp <= 0 || c.hp > 100) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Class %s has unreasonable HP: %d", c.name, c.hp);
            FAIL(buf); return;
        }
        if (c.base_damage <= 0 || c.base_damage > 20) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Class %s has unreasonable base_damage: %d", c.name, c.base_damage);
            FAIL(buf); return;
        }
        // All attributes should be 1-25
        int attrs[] = {c.str, c.dex, c.con, c.intel, c.wil, c.per, c.cha};
        for (int a : attrs) {
            if (a < 1 || a > 25) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Class %s has attribute out of range: %d", c.name, a);
                FAIL(buf); return;
            }
        }
    }
    PASS();
}

void test_class_names_unique() {
    TEST("Class names are unique");
    std::set<std::string> names;
    for (int i = 0; i < CLASS_COUNT; i++) {
        auto& c = get_class_info(static_cast<ClassId>(i));
        if (!names.insert(c.name).second) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Duplicate class name: %s", c.name);
            FAIL(buf); return;
        }
    }
    PASS();
}

// ========================================
// TRAIT VALIDATION
// ========================================

void test_trait_count() {
    TEST("Trait count matches enum");
    CHECK(TRAIT_COUNT == static_cast<int>(TraitId::COUNT), "TRAIT_COUNT != TraitId::COUNT");
    CHECK(POSITIVE_TRAIT_COUNT > 0, "No positive traits");
    CHECK(NEGATIVE_TRAIT_COUNT > 0, "No negative traits");
    CHECK(POSITIVE_TRAIT_COUNT + NEGATIVE_TRAIT_COUNT == TRAIT_COUNT,
          "Positive + negative != total");
    PASS();
}

void test_trait_polarity() {
    TEST("Trait is_positive matches enum ordering");
    for (int i = 0; i < TRAIT_COUNT; i++) {
        auto& t = get_trait_info(static_cast<TraitId>(i));
        bool expected_positive = (i < POSITIVE_TRAIT_COUNT);
        if (t.is_positive != expected_positive) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Trait %s: is_positive=%d but index=%d (cutoff=%d)",
                     t.name, t.is_positive, i, POSITIVE_TRAIT_COUNT);
            FAIL(buf); return;
        }
    }
    PASS();
}

void test_trait_names_unique() {
    TEST("Trait names are unique");
    std::set<std::string> names;
    for (int i = 0; i < TRAIT_COUNT; i++) {
        auto& t = get_trait_info(static_cast<TraitId>(i));
        if (!names.insert(t.name).second) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Duplicate trait name: %s", t.name);
            FAIL(buf); return;
        }
    }
    PASS();
}

// ========================================
// BACKGROUND VALIDATION
// ========================================

void test_background_count() {
    TEST("Background count matches enum");
    CHECK(BACKGROUND_COUNT == static_cast<int>(BackgroundId::COUNT),
          "BACKGROUND_COUNT != BackgroundId::COUNT");
    PASS();
}

void test_background_names_unique() {
    TEST("Background names are unique");
    std::set<std::string> names;
    for (int i = 0; i < BACKGROUND_COUNT; i++) {
        auto& b = get_background_info(static_cast<BackgroundId>(i));
        if (!names.insert(b.name).second) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Duplicate background name: %s", b.name);
            FAIL(buf); return;
        }
    }
    PASS();
}

void test_background_has_passive() {
    TEST("All backgrounds have passive name and description");
    for (int i = 0; i < BACKGROUND_COUNT; i++) {
        auto& b = get_background_info(static_cast<BackgroundId>(i));
        if (!b.passive_name || strlen(b.passive_name) == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Background %s has no passive_name", b.name);
            FAIL(buf); return;
        }
        if (!b.passive_desc || strlen(b.passive_desc) == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Background %s has no passive_desc", b.name);
            FAIL(buf); return;
        }
    }
    PASS();
}

// ========================================
// SPELL VALIDATION
// ========================================

void test_spell_count() {
    TEST("Spell count matches enum");
    CHECK(SPELL_COUNT == static_cast<int>(SpellId::COUNT), "SPELL_COUNT != SpellId::COUNT");
    PASS();
}

void test_spell_costs() {
    TEST("All spells have non-negative MP cost");
    for (int i = 0; i < SPELL_COUNT; i++) {
        auto& s = get_spell_info(static_cast<SpellId>(i));
        if (s.mp_cost < 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Spell %s has negative MP cost: %d", s.name, s.mp_cost);
            FAIL(buf); return;
        }
    }
    PASS();
}

void test_spell_names_unique() {
    TEST("Spell names are unique");
    std::set<std::string> names;
    for (int i = 0; i < SPELL_COUNT; i++) {
        auto& s = get_spell_info(static_cast<SpellId>(i));
        if (!names.insert(s.name).second) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Duplicate spell name: %s", s.name);
            FAIL(buf); return;
        }
    }
    PASS();
}

// ========================================
// GOD VALIDATION
// ========================================

void test_god_count() {
    TEST("God count is 13");
    CHECK(GOD_COUNT == 13, "Expected 13 gods");
    PASS();
}

void test_god_names_unique() {
    TEST("God names are unique");
    std::set<std::string> names;
    for (int i = 0; i < GOD_COUNT; i++) {
        auto& g = get_god_info(static_cast<GodId>(i));
        if (!names.insert(g.name).second) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Duplicate god name: %s", g.name);
            FAIL(buf); return;
        }
    }
    PASS();
}

void test_god_colors() {
    TEST("All gods have non-zero colors");
    for (int i = 0; i < GOD_COUNT; i++) {
        auto& g = get_god_info(static_cast<GodId>(i));
        if (g.color.r == 0 && g.color.g == 0 && g.color.b == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "God %s has black (0,0,0) color", g.name);
            FAIL(buf); return;
        }
    }
    PASS();
}

// ========================================
// MONSTER VALIDATION
// ========================================

void test_monster_sprites() {
    TEST("Monster sprites within sheet bounds");
    auto* table = MONSTERS;
    int count = MONSTER_COUNT;
    for (int i = 0; i < count; i++) {
        auto& m = table[i];
        int max_col = MONSTERS_COLS, max_row = MONSTERS_ROWS;
        if (m.sheet == SHEET_ANIMALS) { max_col = ANIMALS_COLS; max_row = ANIMALS_ROWS; }
        if (m.sprite_x < 0 || m.sprite_x >= max_col ||
            m.sprite_y < 0 || m.sprite_y >= max_row) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Monster %s sprite (%d,%d) out of bounds (%dx%d)",
                     m.name, m.sprite_x, m.sprite_y, max_col, max_row);
            FAIL(buf); return;
        }
    }
    PASS();
}

void test_monster_stats_valid() {
    TEST("Monster stats are reasonable");
    auto* table = MONSTERS;
    int count = MONSTER_COUNT;
    for (int i = 0; i < count; i++) {
        auto& m = table[i];
        if (m.hp <= 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Monster %s has HP <= 0: %d", m.name, m.hp);
            FAIL(buf); return;
        }
        if (m.speed <= 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Monster %s has speed <= 0: %d", m.name, m.speed);
            FAIL(buf); return;
        }
    }
    PASS();
}

void test_monster_names_unique() {
    TEST("Monster names are unique");
    auto* table = MONSTERS;
    int count = MONSTER_COUNT;
    std::set<std::string> names;
    for (int i = 0; i < count; i++) {
        if (!names.insert(table[i].name).second) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Duplicate monster name: %s", table[i].name);
            FAIL(buf); return;
        }
    }
    PASS();
}

// ========================================
// TOWN VALIDATION
// ========================================

void test_town_count() {
    TEST("Town count is 20");
    CHECK(TOWN_COUNT == 20, "Expected 20 towns");
    PASS();
}

void test_town_positions_valid() {
    TEST("Town positions within map bounds (2000x1500)");
    for (int i = 0; i < TOWN_COUNT; i++) {
        if (ALL_TOWNS[i].x < 0 || ALL_TOWNS[i].x >= 2000 ||
            ALL_TOWNS[i].y < 0 || ALL_TOWNS[i].y >= 1500) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Town %s at (%d,%d) out of map bounds",
                     ALL_TOWNS[i].name, ALL_TOWNS[i].x, ALL_TOWNS[i].y);
            FAIL(buf); return;
        }
    }
    PASS();
}

void test_town_names_unique() {
    TEST("Town names are unique");
    std::set<std::string> names;
    for (int i = 0; i < TOWN_COUNT; i++) {
        if (!names.insert(ALL_TOWNS[i].name).second) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Duplicate town name: %s", ALL_TOWNS[i].name);
            FAIL(buf); return;
        }
    }
    PASS();
}

void test_no_overlapping_towns() {
    TEST("No overlapping town positions (min 50 tiles apart)");
    for (int i = 0; i < TOWN_COUNT; i++) {
        for (int j = i + 1; j < TOWN_COUNT; j++) {
            float d = world_dist(ALL_TOWNS[i].x, ALL_TOWNS[i].y,
                                  ALL_TOWNS[j].x, ALL_TOWNS[j].y);
            if (d < 50) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Towns %s and %s are only %.0f tiles apart",
                         ALL_TOWNS[i].name, ALL_TOWNS[j].name, d);
                FAIL(buf); return;
            }
        }
    }
    PASS();
}

// ========================================
// DUNGEON JSON VALIDATION
// ========================================

void test_dungeons_json() {
    TEST("dungeons.json parses and has valid entries");
    std::ifstream f("data/dungeons.json");
    if (!f.is_open()) {
        FAIL("Could not open data/dungeons.json");
        return;
    }
    json j;
    try { j = json::parse(f); } catch (...) {
        FAIL("Failed to parse JSON");
        return;
    }
    CHECK(j.is_array(), "Root is not an array");
    CHECK(j.size() >= 27, "Expected at least 27 dungeons");

    std::set<std::string> valid_zones = {"warrens", "stonekeep", "deep_halls",
                                          "catacombs", "molten", "sunken", "sepulchre"};
    std::set<std::string> names;
    for (auto& entry : j) {
        CHECK(entry.contains("name"), "Entry missing 'name'");
        CHECK(entry.contains("x"), "Entry missing 'x'");
        CHECK(entry.contains("y"), "Entry missing 'y'");
        CHECK(entry.contains("zone"), "Entry missing 'zone'");

        std::string zone = entry["zone"];
        if (valid_zones.find(zone) == valid_zones.end()) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Invalid zone type: %s", zone.c_str());
            FAIL(buf); return;
        }

        std::string name = entry["name"];
        if (!names.insert(name).second) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Duplicate dungeon name: %s", name.c_str());
            FAIL(buf); return;
        }
    }
    PASS();
}

// ========================================
// QUEST VALIDATION
// ========================================

void test_quest_chain() {
    TEST("Main quest chain has 17 entries");
    // QuestId enum starts with MQ_01 = 0 through MQ_17 = 16
    for (int i = 0; i < 17; i++) {
        auto& q = get_quest_info(static_cast<QuestId>(i));
        if (!q.name || strlen(q.name) == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Main quest %d has no name", i + 1);
            FAIL(buf); return;
        }
    }
    PASS();
}

// ========================================
// CROSS-REFERENCE VALIDATION
// ========================================

void test_province_god_mapping() {
    TEST("All towns map to a valid god");
    for (int i = 0; i < TOWN_COUNT; i++) {
        GodId god = get_town_god(ALL_TOWNS[i].x, ALL_TOWNS[i].y);
        if (god == GodId::NONE || static_cast<int>(god) < 0 || static_cast<int>(god) >= GOD_COUNT) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Town %s has invalid god mapping", ALL_TOWNS[i].name);
            FAIL(buf); return;
        }
    }
    PASS();
}

void test_province_name_mapping() {
    TEST("All towns have a province name");
    for (int i = 0; i < TOWN_COUNT; i++) {
        const char* prov = get_province_name(ALL_TOWNS[i].x, ALL_TOWNS[i].y);
        if (!prov || strlen(prov) == 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Town %s has no province name", ALL_TOWNS[i].name);
            FAIL(buf); return;
        }
    }
    PASS();
}

// ========================================
// MAIN
// ========================================

int main() {
    printf("=== Reliquary Data Integrity Tests ===\n\n");

    printf("Classes:\n");
    test_class_count();
    test_class_sprites();
    test_class_stats_valid();
    test_class_names_unique();

    printf("\nTraits:\n");
    test_trait_count();
    test_trait_polarity();
    test_trait_names_unique();

    printf("\nBackgrounds:\n");
    test_background_count();
    test_background_names_unique();
    test_background_has_passive();

    printf("\nSpells:\n");
    test_spell_count();
    test_spell_costs();
    test_spell_names_unique();

    printf("\nGods:\n");
    test_god_count();
    test_god_names_unique();
    test_god_colors();

    printf("\nMonsters:\n");
    test_monster_sprites();
    test_monster_stats_valid();
    test_monster_names_unique();

    printf("\nTowns:\n");
    test_town_count();
    test_town_positions_valid();
    test_town_names_unique();
    test_no_overlapping_towns();

    printf("\nDungeons:\n");
    test_dungeons_json();

    printf("\nQuests:\n");
    test_quest_chain();

    printf("\nCross-references:\n");
    test_province_god_mapping();
    test_province_name_mapping();

    printf("\n=== Results: %d passed, %d failed, %d total ===\n",
           tests_passed, tests_failed, tests_run);

    return tests_failed > 0 ? 1 : 0;
}
