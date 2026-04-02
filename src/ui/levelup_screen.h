#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "core/rng.h"
#include "components/class_def.h"
#include <string>
#include <vector>

enum class LevelUpAction {
    NONE,
    CHOSEN, // player picked an option
};

struct LevelUpChoice {
    std::string label;
    std::string description; // shown when highlighted
    // Effects — multiple can be nonzero
    int attr_index = -1;  // Attr enum index, or -1
    int attr_bonus = 0;
    int hp_bonus = 0;
    int mp_bonus = 0;
    int speed_bonus = 0;
    int damage_bonus = 0;
    int armor_bonus = 0;   // natural armor
    ClassId class_req = ClassId::COUNT; // COUNT = any class
};

class LevelUpScreen {
public:
    LevelUpScreen() = default;

    void open(Entity player, RNG& rng, ClassId cls = ClassId::FIGHTER);
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    LevelUpAction handle_input(SDL_Event& event);

    // Apply the chosen bonus to stats. Call after handle_input returns CHOSEN.
    void apply_choice(World& world) const;

    void render(SDL_Renderer* renderer, TTF_Font* font,
                int screen_w, int screen_h) const;

private:
    bool open_ = false;
    int selected_ = 0;
    Entity player_ = 0;
    std::vector<LevelUpChoice> choices_;
};
