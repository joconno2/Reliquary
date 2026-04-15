#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "components/church.h"

enum class ChurchAction {
    NONE,
    CLOSE,
    REST,          // free full heal
    IDENTIFY,      // ID all items
    ENCHANT,       // enchant weapon
    LEARN_SPELL,   // learn exclusive spell
    CLAIM_ITEM,    // get exclusive equipment
    CLAIM_BLESSING,// get champion blessing
};

class ChurchScreen {
public:
    ChurchScreen() = default;

    void open(Entity player, World* world, GodId god, int favor);
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    ChurchAction handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                int screen_w, int screen_h) const;

    GodId get_god() const { return god_; }
    ChurchRank get_rank() const { return rank_; }

private:
    bool open_ = false;
    Entity player_ = 0;
    World* world_ = nullptr;
    GodId god_ = GodId::NONE;
    int favor_ = 0;
    ChurchRank rank_ = ChurchRank::OUTSIDER;
    int selected_ = 0;
    int max_options_ = 0;
};
