#include "save/meta.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

namespace meta {

MetaSave load(const std::string& path) {
    MetaSave m;

    std::ifstream file(path);
    if (!file.is_open()) return m;

    json root;
    try { root = json::parse(file); }
    catch (...) { return m; }

    m.total_kills = root.value("total_kills", 0);
    m.total_undead_kills = root.value("total_undead_kills", 0);
    m.total_hp_healed = root.value("total_hp_healed", 0);
    m.total_dark_arts_casts = root.value("total_dark_arts_casts", 0);
    m.total_quests_completed = root.value("total_quests_completed", 0);
    m.total_creatures_examined = root.value("total_creatures_examined", 0);
    m.max_dungeon_depth = root.value("max_dungeon_depth", 0);
    m.max_gold_single_run = root.value("max_gold_single_run", 0);

    if (root.contains("gods_completed")) {
        auto& gc = root["gods_completed"];
        for (int i = 0; i < GOD_COUNT && i < static_cast<int>(gc.size()); i++)
            m.gods_completed[i] = gc[i].get<bool>();
    }
    if (root.contains("class_max_level")) {
        auto& cl = root["class_max_level"];
        for (int i = 0; i < MetaSave::MAX_CLASSES && i < static_cast<int>(cl.size()); i++)
            m.class_max_level[i] = cl[i].get<int>();
    }

    m.killed_unarmed = root.value("killed_unarmed", false);
    m.died_deep = root.value("died_deep", false);
    m.killed_paragon = root.value("killed_paragon", false);

    // Persistent bestiary
    if (root.contains("bestiary")) {
        for (auto& [name, entry] : root["bestiary"].items()) {
            MetaBestiaryEntry be;
            be.hp = entry.value("hp", 0);
            be.damage = entry.value("damage", 0);
            be.armor = entry.value("armor", 0);
            be.speed = entry.value("speed", 0);
            be.total_kills = entry.value("kills", 0);
            m.bestiary[name] = be;
        }
    }

    // Identified potions
    if (root.contains("identified_potions")) {
        for (auto& p : root["identified_potions"])
            m.identified_potions.insert(p.get<std::string>());
    }

    return m;
}

bool save(const MetaSave& m, const std::string& path) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    json root;
    root["total_kills"] = m.total_kills;
    root["total_undead_kills"] = m.total_undead_kills;
    root["total_hp_healed"] = m.total_hp_healed;
    root["total_dark_arts_casts"] = m.total_dark_arts_casts;
    root["total_quests_completed"] = m.total_quests_completed;
    root["total_creatures_examined"] = m.total_creatures_examined;
    root["max_dungeon_depth"] = m.max_dungeon_depth;
    root["max_gold_single_run"] = m.max_gold_single_run;

    json gc = json::array();
    for (int i = 0; i < GOD_COUNT; i++) gc.push_back(m.gods_completed[i]);
    root["gods_completed"] = gc;

    json cl = json::array();
    for (int i = 0; i < MetaSave::MAX_CLASSES; i++) cl.push_back(m.class_max_level[i]);
    root["class_max_level"] = cl;

    root["killed_unarmed"] = m.killed_unarmed;
    root["died_deep"] = m.died_deep;
    root["killed_paragon"] = m.killed_paragon;

    // Bestiary
    json bestiary_j;
    for (auto& [name, entry] : m.bestiary) {
        json ej;
        ej["hp"] = entry.hp;
        ej["damage"] = entry.damage;
        ej["armor"] = entry.armor;
        ej["speed"] = entry.speed;
        ej["kills"] = entry.total_kills;
        bestiary_j[name] = ej;
    }
    root["bestiary"] = bestiary_j;

    // Identified potions
    json potions_j = json::array();
    for (auto& p : m.identified_potions) potions_j.push_back(p);
    root["identified_potions"] = potions_j;

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << root.dump(2);
    return true;
}

} // namespace meta
