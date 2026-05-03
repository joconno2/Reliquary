#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <array>
#include <vector>

// Every rebindable gameplay action.
// Menu navigation (j/k in lists, etc.) stays hardcoded in screen code.
enum class Action {
    MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT,
    MOVE_NW, MOVE_NE, MOVE_SW, MOVE_SE,
    WAIT,
    INTERACT,
    PICKUP,
    STAIRS_DOWN, STAIRS_UP, STAIRS_ENTER,
    FIRE_RANGED,
    CYCLE_TARGET,
    REST,
    PRAY,
    EXAMINE,
    SNEAK_TOGGLE,
    QUICK_CAST,
    INVENTORY, SPELLBOOK, CHARACTER, PASSIVE_TREE,
    QUEST_LOG, WORLD_MAP, BESTIARY, HELP,
    ABILITY_1, ABILITY_2, ABILITY_3, ABILITY_4,
    QUICKSAVE, QUICKLOAD, SCREENSHOT,
    COUNT // sentinel
};

static constexpr int ACTION_COUNT = static_cast<int>(Action::COUNT);

// Human-readable name for each action (for the rebinding UI)
const char* action_name(Action a);

// Whether an action is active and should appear in the keybinds UI
// (removed features like diagonal movement return false)
inline bool action_available(Action a) {
    switch (a) {
        case Action::MOVE_NW: case Action::MOVE_NE:
        case Action::MOVE_SW: case Action::MOVE_SE:
            return false;
        default:
            return true;
    }
}

// Max keys per action (primary + alternates)
static constexpr int MAX_KEYS_PER_ACTION = 4;

struct KeyBinding {
    SDL_Keycode keys[MAX_KEYS_PER_ACTION] = {SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN};
    int count = 0;

    void set(std::initializer_list<SDL_Keycode> codes) {
        count = 0;
        for (auto k : codes) {
            if (count < MAX_KEYS_PER_ACTION) keys[count++] = k;
        }
    }

    bool matches(SDL_Keycode sym) const {
        for (int i = 0; i < count; i++)
            if (keys[i] == sym) return true;
        return false;
    }

    void clear() {
        for (int i = 0; i < MAX_KEYS_PER_ACTION; i++) keys[i] = SDLK_UNKNOWN;
        count = 0;
    }

    void add(SDL_Keycode k) {
        if (count < MAX_KEYS_PER_ACTION) keys[count++] = k;
    }

    void remove(SDL_Keycode k) {
        for (int i = 0; i < count; i++) {
            if (keys[i] == k) {
                for (int j = i; j < count - 1; j++) keys[j] = keys[j + 1];
                keys[--count] = SDLK_UNKNOWN;
                return;
            }
        }
    }
};

class Keybinds {
public:
    Keybinds();

    // Check if a key matches an action
    bool is(SDL_Keycode sym, Action action) const {
        return bindings_[static_cast<int>(action)].matches(sym);
    }

    // Translate a keycode to an action (returns Action::COUNT if no match)
    Action translate(SDL_Keycode sym) const;

    // Translate with modifier awareness (handles sdl2-compat shifted key issues)
    Action translate(SDL_Keycode sym, Uint16 mod) const;

    // Access binding for an action
    const KeyBinding& get(Action a) const { return bindings_[static_cast<int>(a)]; }
    KeyBinding& get(Action a) { return bindings_[static_cast<int>(a)]; }

    // Unbind a key from all actions, then bind to target
    void rebind(Action action, SDL_Keycode key);

    // Reset a single action to defaults
    void reset_action(Action action);

    // Reset all to defaults
    void reset_all();

    // Persistence
    void load(const std::string& path);
    void save(const std::string& path) const;

    // Display name for an SDL keycode
    static const char* key_name(SDL_Keycode k);

    // Build a display string like "W / Up / KP8"
    std::string binding_string(Action a) const;

private:
    std::array<KeyBinding, ACTION_COUNT> bindings_;
    std::array<KeyBinding, ACTION_COUNT> defaults_;

    void set_defaults();
};
