#include "save/save.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/stats.h"
#include "components/player.h"
#include "components/inventory.h"
#include "components/item.h"
#include "components/energy.h"
#include "components/god.h"
#include "components/spellbook.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include <filesystem>

using json = nlohmann::json;

namespace save {

static json stats_to_json(const Stats& s) {
    json j;
    j["name"] = s.name;
    j["hp"] = s.hp; j["hp_max"] = s.hp_max;
    j["mp"] = s.mp; j["mp_max"] = s.mp_max;
    j["base_damage"] = s.base_damage;
    j["natural_armor"] = s.natural_armor;
    j["base_speed"] = s.base_speed;
    j["level"] = s.level; j["xp"] = s.xp; j["xp_next"] = s.xp_next;
    j["xp_value"] = s.xp_value;
    json attrs = json::array();
    for (int i = 0; i < ATTR_COUNT; i++) attrs.push_back(s.attributes[i]);
    j["attributes"] = attrs;
    return j;
}

static Stats json_to_stats(const json& j) {
    Stats s;
    s.name = j.value("name", "Unknown");
    s.hp = j.value("hp", 20); s.hp_max = j.value("hp_max", 20);
    s.mp = j.value("mp", 0); s.mp_max = j.value("mp_max", 0);
    s.base_damage = j.value("base_damage", 1);
    s.natural_armor = j.value("natural_armor", 0);
    s.base_speed = j.value("base_speed", 100);
    s.level = j.value("level", 1); s.xp = j.value("xp", 0);
    s.xp_next = j.value("xp_next", 100);
    s.xp_value = j.value("xp_value", 0);
    if (j.contains("attributes")) {
        auto& a = j["attributes"];
        for (int i = 0; i < ATTR_COUNT && i < static_cast<int>(a.size()); i++)
            s.attributes[i] = a[i].get<int>();
    }
    return s;
}

static json item_to_json(const Item& item) {
    json j;
    j["name"] = item.name; j["description"] = item.description;
    j["type"] = static_cast<int>(item.type);
    j["slot"] = static_cast<int>(item.slot);
    j["damage_bonus"] = item.damage_bonus; j["armor_bonus"] = item.armor_bonus;
    j["attack_bonus"] = item.attack_bonus; j["dodge_bonus"] = item.dodge_bonus;
    j["heal_amount"] = item.heal_amount; j["gold_value"] = item.gold_value;
    j["identified"] = item.identified; j["unid_name"] = item.unid_name;
    j["stack"] = item.stack; j["stackable"] = item.stackable;
    j["quest_id"] = item.quest_id;
    return j;
}

static Item json_to_item(const json& j) {
    Item item;
    item.name = j.value("name", "unknown");
    item.description = j.value("description", "");
    item.type = static_cast<ItemType>(j.value("type", 0));
    item.slot = static_cast<EquipSlot>(j.value("slot", -1));
    item.damage_bonus = j.value("damage_bonus", 0);
    item.armor_bonus = j.value("armor_bonus", 0);
    item.attack_bonus = j.value("attack_bonus", 0);
    item.dodge_bonus = j.value("dodge_bonus", 0);
    item.heal_amount = j.value("heal_amount", 0);
    item.gold_value = j.value("gold_value", 0);
    item.identified = j.value("identified", false);
    item.unid_name = j.value("unid_name", "");
    item.stack = j.value("stack", 1);
    item.stackable = j.value("stackable", false);
    item.quest_id = j.value("quest_id", -1);
    return item;
}

bool save_game(const std::string& path, const SaveData& data,
               World& world, Entity player, const TileMap& map) {
    // Ensure directory exists
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    json root;
    root["version"] = 1;
    root["dungeon_level"] = data.dungeon_level;
    root["game_turn"] = data.game_turn;
    root["gold"] = data.gold;
    root["rng_seed"] = data.rng_seed;
    root["overworld_return_x"] = data.overworld_return_x;
    root["overworld_return_y"] = data.overworld_return_y;

    // Quest journal
    json quests = json::array();
    for (auto& e : data.journal.entries) {
        json qj;
        qj["id"] = static_cast<int>(e.id);
        qj["state"] = static_cast<int>(e.state);
        qj["progress"] = e.progress;
        qj["target"] = e.target;
        quests.push_back(qj);
    }
    root["quests"] = quests;

    // Player
    if (world.has<Position>(player)) {
        auto& p = world.get<Position>(player);
        root["player_x"] = p.x;
        root["player_y"] = p.y;
    }
    if (world.has<Stats>(player)) {
        root["player_stats"] = stats_to_json(world.get<Stats>(player));
    }
    if (world.has<Renderable>(player)) {
        auto& r = world.get<Renderable>(player);
        root["player_sprite"] = {r.sprite_sheet, r.sprite_x, r.sprite_y};
    }
    if (world.has<GodAlignment>(player)) {
        auto& g = world.get<GodAlignment>(player);
        root["god"] = static_cast<int>(g.god);
        root["favor"] = g.favor;
    }
    if (world.has<Energy>(player)) {
        auto& e = world.get<Energy>(player);
        root["energy"] = e.current;
        root["speed"] = e.speed;
    }

    // Spellbook
    if (world.has<Spellbook>(player)) {
        json spells = json::array();
        for (auto s : world.get<Spellbook>(player).known_spells)
            spells.push_back(static_cast<int>(s));
        root["spells"] = spells;
    }

    // Inventory items
    if (world.has<Inventory>(player)) {
        auto& inv = world.get<Inventory>(player);
        json items = json::array();
        for (Entity ie : inv.items) {
            if (!world.has<Item>(ie)) continue;
            json ij = item_to_json(world.get<Item>(ie));
            if (world.has<Renderable>(ie)) {
                auto& r = world.get<Renderable>(ie);
                ij["sprite"] = {r.sprite_sheet, r.sprite_x, r.sprite_y};
            }
            ij["equipped"] = inv.is_equipped(ie);
            if (inv.is_equipped(ie)) {
                ij["equip_slot"] = static_cast<int>(world.get<Item>(ie).slot);
            }
            items.push_back(ij);
        }
        root["inventory"] = items;
    }

    // Map dimensions + tile data (compressed: just type and variant)
    root["map_w"] = map.width();
    root["map_h"] = map.height();
    // Store as a string — one char per tile type, variant in a separate string
    std::string tile_types, tile_variants, tile_explored;
    for (int y = 0; y < map.height(); y++) {
        for (int x = 0; x < map.width(); x++) {
            auto& t = map.at(x, y);
            tile_types += static_cast<char>(static_cast<int>(t.type) + 32);
            tile_variants += static_cast<char>(t.variant + 32);
            tile_explored += t.explored ? '1' : '0';
        }
    }
    root["tile_types"] = tile_types;
    root["tile_variants"] = tile_variants;
    root["tile_explored"] = tile_explored;

    std::ofstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "Failed to save: %s\n", path.c_str());
        return false;
    }
    file << root.dump();
    return true;
}

SaveData load_game(const std::string& path, World& world, TileMap& map) {
    SaveData data;

    std::ifstream file(path);
    if (!file.is_open()) return data;

    json root;
    try { root = json::parse(file); }
    catch (...) { return data; }

    if (root.value("version", 0) != 1) return data;

    data.dungeon_level = root.value("dungeon_level", 0);
    data.game_turn = root.value("game_turn", 0);
    data.gold = root.value("gold", 0);
    data.rng_seed = root.value("rng_seed", static_cast<uint64_t>(0));
    data.overworld_return_x = root.value("overworld_return_x", 0);
    data.overworld_return_y = root.value("overworld_return_y", 0);

    // Quests
    if (root.contains("quests")) {
        for (auto& qj : root["quests"]) {
            QuestJournal::Entry e;
            e.id = static_cast<QuestId>(qj.value("id", 0));
            e.state = static_cast<QuestState>(qj.value("state", 0));
            e.progress = qj.value("progress", 0);
            e.target = qj.value("target", 0);
            data.journal.entries.push_back(e);
        }
    }

    // Map
    int mw = root.value("map_w", 0);
    int mh = root.value("map_h", 0);
    if (mw > 0 && mh > 0) {
        map = TileMap(mw, mh);
        std::string types = root.value("tile_types", "");
        std::string variants = root.value("tile_variants", "");
        std::string explored = root.value("tile_explored", "");
        for (int y = 0; y < mh; y++) {
            for (int x = 0; x < mw; x++) {
                int idx = y * mw + x;
                auto& t = map.at(x, y);
                if (idx < static_cast<int>(types.size()))
                    t.type = static_cast<TileType>(types[idx] - 32);
                if (idx < static_cast<int>(variants.size()))
                    t.variant = static_cast<uint8_t>(variants[idx] - 32);
                if (idx < static_cast<int>(explored.size()))
                    t.explored = explored[idx] == '1';
            }
        }
    }

    // Player
    Entity player = world.create();
    world.add<Player>(player);
    world.add<Position>(player, {root.value("player_x", 0), root.value("player_y", 0)});

    if (root.contains("player_stats")) {
        world.add<Stats>(player, json_to_stats(root["player_stats"]));
    }
    if (root.contains("player_sprite")) {
        auto& s = root["player_sprite"];
        world.add<Renderable>(player, {s[0].get<int>(), s[1].get<int>(), s[2].get<int>(),
                                        {255,255,255,255}, 10});
    }
    if (root.contains("god")) {
        world.add<GodAlignment>(player, {
            static_cast<GodId>(root.value("god", 0)),
            root.value("favor", 0)
        });
    }
    world.add<Energy>(player, {root.value("energy", 0), root.value("speed", 100)});

    // Spellbook
    if (root.contains("spells")) {
        Spellbook book;
        for (auto& s : root["spells"])
            book.learn(static_cast<SpellId>(s.get<int>()));
        world.add<Spellbook>(player, std::move(book));
    }

    // Inventory
    Inventory inv;
    if (root.contains("inventory")) {
        for (auto& ij : root["inventory"]) {
            Entity ie = world.create();
            world.add<Item>(ie, json_to_item(ij));
            if (ij.contains("sprite")) {
                auto& sp = ij["sprite"];
                world.add<Renderable>(ie, {sp[0].get<int>(), sp[1].get<int>(), sp[2].get<int>(),
                                            {255,255,255,255}, 1});
            }
            inv.add(ie);
            if (ij.value("equipped", false)) {
                auto slot = static_cast<EquipSlot>(ij.value("equip_slot", -1));
                if (static_cast<int>(slot) >= 0) inv.equip(slot, ie);
            }
        }
    }
    world.add<Inventory>(player, std::move(inv));

    data.valid = true;
    return data;
}

bool save_exists(const std::string& path) {
    return std::filesystem::exists(path);
}

} // namespace save
