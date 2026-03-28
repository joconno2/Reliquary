#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/spritesheet.h"
#include "core/rng.h"
#include "systems/render.h"
#include "ui/message_log.h"
#include "generation/dungeon.h"
#include "ui/inventory_screen.h"
#include "ui/creation_screen.h"
#include "ui/spell_screen.h"
#include <vector>

enum class GameState {
    CREATING,
    PLAYING,
    DEAD,
    QUIT
};

class Engine {
public:
    Engine();
    ~Engine();

    bool init();
    void run();

private:
    // SDL
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* font_ = nullptr;        // Press Start 2P — body text, UI, messages
    TTF_Font* font_title_ = nullptr;  // Jacquard 12 — titles, god names, decorative

    // Core systems
    World world_;
    TileMap map_;
    SpriteManager sprites_;
    RNG rng_;
    Camera camera_;
    MessageLog log_;
    GameState state_ = GameState::CREATING;

    // Level data
    std::vector<Room> rooms_;
    int dungeon_level_ = 0;

    // Player
    Entity player_ = NULL_ENTITY;
    int game_turn_ = 0;
    int gold_ = 0;
    bool player_acted_ = false;

    // UI
    InventoryScreen inventory_screen_;
    CreationScreen creation_screen_;
    SpellScreen spell_screen_;

    // Window config
    static constexpr int SCREEN_W = 1280;
    static constexpr int SCREEN_H = 800;
    static constexpr int LOG_HEIGHT = 160;
    static constexpr int HUD_HEIGHT = 24;

    // Methods
    void handle_input();
    void try_move_player(int dx, int dy);
    void open_door(int x, int y);
    void process_turn();
    void try_pickup();
    void handle_inventory_action(InvAction action);
    void render();
    void render_hud();
    void generate_level();
    void clear_entities_except_player();
};
