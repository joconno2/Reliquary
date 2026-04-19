#include "core/keybinds.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>

using json = nlohmann::json;

const char* action_name(Action a) {
    switch (a) {
        case Action::MOVE_UP:      return "Move Up";
        case Action::MOVE_DOWN:    return "Move Down";
        case Action::MOVE_LEFT:    return "Move Left";
        case Action::MOVE_RIGHT:   return "Move Right";
        case Action::MOVE_NW:      return "Move NW";
        case Action::MOVE_NE:      return "Move NE";
        case Action::MOVE_SW:      return "Move SW";
        case Action::MOVE_SE:      return "Move SE";
        case Action::WAIT:         return "Wait";
        case Action::INTERACT:     return "Interact";
        case Action::PICKUP:       return "Pick Up";
        case Action::STAIRS_DOWN:  return "Descend";
        case Action::STAIRS_UP:    return "Ascend";
        case Action::STAIRS_ENTER: return "Use Stairs";
        case Action::FIRE_RANGED:  return "Fire Ranged";
        case Action::REST:         return "Rest";
        case Action::PRAY:         return "Pray";
        case Action::EXAMINE:      return "Examine";
        case Action::SNEAK_TOGGLE: return "Sneak";
        case Action::QUICK_CAST:   return "Quick Cast";
        case Action::INVENTORY:    return "Inventory";
        case Action::SPELLBOOK:    return "Spellbook";
        case Action::CHARACTER:    return "Character";
        case Action::PASSIVE_TREE: return "Passive Tree";
        case Action::QUEST_LOG:    return "Quest Log";
        case Action::WORLD_MAP:    return "World Map";
        case Action::BESTIARY:     return "Bestiary";
        case Action::HELP:         return "Help";
        case Action::ABILITY_1:    return "Ability 1";
        case Action::ABILITY_2:    return "Ability 2";
        case Action::ABILITY_3:    return "Ability 3";
        case Action::ABILITY_4:    return "Ability 4";
        case Action::QUICKSAVE:    return "Quick Save";
        case Action::QUICKLOAD:    return "Quick Load";
        case Action::SCREENSHOT:   return "Screenshot";
        default:                   return "???";
    }
}

// ── Defaults ──

void Keybinds::set_defaults() {
    for (auto& b : defaults_) b.clear();

    auto def = [&](Action a, std::initializer_list<SDL_Keycode> keys) {
        defaults_[static_cast<int>(a)].set(keys);
    };

    // Movement (cardinal): arrows + WASD + hjkl + numpad
    def(Action::MOVE_UP,    {SDLK_UP,    SDLK_w, SDLK_k, SDLK_KP_8});
    def(Action::MOVE_DOWN,  {SDLK_DOWN,  SDLK_s, SDLK_j, SDLK_KP_2});
    def(Action::MOVE_LEFT,  {SDLK_LEFT,  SDLK_a, SDLK_h, SDLK_KP_4});
    def(Action::MOVE_RIGHT, {SDLK_RIGHT, SDLK_d, SDLK_l, SDLK_KP_6});

    // Diagonal movement removed (cardinal only)

    // Wait
    def(Action::WAIT, {SDLK_PERIOD, SDLK_KP_5});

    // Actions
    def(Action::INTERACT,     {SDLK_e, SDLK_SPACE});
    def(Action::PICKUP,       {SDLK_g, SDLK_COMMA});
    def(Action::STAIRS_DOWN,  {SDLK_GREATER});
    def(Action::STAIRS_UP,    {SDLK_LESS});
    def(Action::STAIRS_ENTER, {SDLK_RETURN});
    def(Action::FIRE_RANGED,  {SDLK_f});
    def(Action::REST,         {SDLK_r});
    def(Action::PRAY,         {SDLK_p});
    def(Action::EXAMINE,      {SDLK_x});
    def(Action::SNEAK_TOGGLE, {SDLK_o});
    def(Action::QUICK_CAST,   {SDLK_v});

    // Screens
    def(Action::INVENTORY,    {SDLK_i});
    def(Action::SPELLBOOK,    {SDLK_z});
    def(Action::CHARACTER,    {SDLK_c});
    def(Action::PASSIVE_TREE, {SDLK_t});
    def(Action::QUEST_LOG,    {SDLK_q});
    def(Action::WORLD_MAP,    {SDLK_m});
    def(Action::BESTIARY,     {SDLK_TAB});
    def(Action::HELP,         {SDLK_SLASH, SDLK_QUESTION});

    // Abilities (1-4)
    def(Action::ABILITY_1, {SDLK_1});
    def(Action::ABILITY_2, {SDLK_2});
    def(Action::ABILITY_3, {SDLK_3});
    def(Action::ABILITY_4, {SDLK_4});

    // System
    def(Action::QUICKSAVE,  {SDLK_F5});
    def(Action::QUICKLOAD,  {SDLK_F6});
    def(Action::SCREENSHOT, {SDLK_F12});
}

Keybinds::Keybinds() {
    set_defaults();
    reset_all();
}

void Keybinds::reset_all() {
    bindings_ = defaults_;
}

void Keybinds::reset_action(Action action) {
    bindings_[static_cast<int>(action)] = defaults_[static_cast<int>(action)];
}

Action Keybinds::translate(SDL_Keycode sym) const {
    for (int i = 0; i < ACTION_COUNT; i++) {
        if (bindings_[i].matches(sym))
            return static_cast<Action>(i);
    }
    return Action::COUNT;
}

void Keybinds::rebind(Action action, SDL_Keycode key) {
    // Remove this key from any other action first
    for (int i = 0; i < ACTION_COUNT; i++) {
        bindings_[i].remove(key);
    }
    // Add to target action
    auto& b = bindings_[static_cast<int>(action)];
    // If already at max, replace the last one
    if (b.count >= MAX_KEYS_PER_ACTION)
        b.keys[b.count - 1] = key;
    else
        b.add(key);
}

// ── Key name display ──

const char* Keybinds::key_name(SDL_Keycode k) {
    // SDL_GetKeyName returns something usable for most keys.
    // Override a few for shorter/cleaner display.
    switch (k) {
        case SDLK_UP:     return "Up";
        case SDLK_DOWN:   return "Down";
        case SDLK_LEFT:   return "Left";
        case SDLK_RIGHT:  return "Right";
        case SDLK_KP_1:   return "KP1";
        case SDLK_KP_2:   return "KP2";
        case SDLK_KP_3:   return "KP3";
        case SDLK_KP_4:   return "KP4";
        case SDLK_KP_5:   return "KP5";
        case SDLK_KP_6:   return "KP6";
        case SDLK_KP_7:   return "KP7";
        case SDLK_KP_8:   return "KP8";
        case SDLK_KP_9:   return "KP9";
        case SDLK_RETURN:  return "Enter";
        case SDLK_ESCAPE:  return "Esc";
        case SDLK_TAB:     return "Tab";
        case SDLK_PERIOD:  return ".";
        case SDLK_COMMA:   return ",";
        case SDLK_GREATER: return ">";
        case SDLK_LESS:    return "<";
        case SDLK_QUESTION: return "?";
        case SDLK_SLASH:   return "/";
        case SDLK_UNKNOWN: return "";
        default:           return SDL_GetKeyName(k);
    }
}

std::string Keybinds::binding_string(Action a) const {
    auto& b = bindings_[static_cast<int>(a)];
    if (b.count == 0) return "(unbound)";
    std::string result;
    for (int i = 0; i < b.count; i++) {
        if (i > 0) result += " / ";
        result += key_name(b.keys[i]);
    }
    return result;
}

// ── Persistence ──

// Store keycodes as their SDL name strings for human-readable JSON
void Keybinds::save(const std::string& path) const {
    json root = json::object();
    for (int i = 0; i < ACTION_COUNT; i++) {
        auto& b = bindings_[i];
        if (b.count == 0) continue;
        json keys = json::array();
        for (int j = 0; j < b.count; j++)
            keys.push_back(static_cast<int>(b.keys[j]));
        root[action_name(static_cast<Action>(i))] = keys;
    }

    // Atomic write: tmp then rename
    std::string tmp = path + ".tmp";
    std::ofstream f(tmp);
    if (!f.is_open()) return;
    f << root.dump(2) << "\n";
    f.close();
    std::rename(tmp.c_str(), path.c_str());
}

void Keybinds::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return; // keep defaults

    json root;
    try { root = json::parse(f); }
    catch (...) { return; } // keep defaults on parse error

    // Build a name-to-action lookup
    for (int i = 0; i < ACTION_COUNT; i++) {
        const char* name = action_name(static_cast<Action>(i));
        if (!root.contains(name)) continue;
        auto& arr = root[name];
        if (!arr.is_array()) continue;
        auto& b = bindings_[i];
        b.clear();
        for (auto& val : arr) {
            if (val.is_number_integer() && b.count < MAX_KEYS_PER_ACTION)
                b.keys[b.count++] = static_cast<SDL_Keycode>(val.get<int>());
        }
    }
}
