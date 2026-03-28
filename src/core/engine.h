#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/spritesheet.h"
#include "core/rng.h"
#include "systems/render.h"
#include "ui/message_log.h"

enum class GameState {
    PLAYING,
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
    TTF_Font* font_ = nullptr;

    // Core systems
    World world_;
    TileMap map_;
    SpriteManager sprites_;
    RNG rng_;
    Camera camera_;
    MessageLog log_;
    GameState state_ = GameState::PLAYING;

    // Player
    Entity player_ = NULL_ENTITY;
    int game_turn_ = 0;

    // Window config
    static constexpr int SCREEN_W = 1280;
    static constexpr int SCREEN_H = 800;
    static constexpr int LOG_HEIGHT = 160;
    static constexpr int FOV_RADIUS = 10;

    // Methods
    void handle_input();
    void try_move_player(int dx, int dy);
    void open_door(int x, int y);
    void update();
    void render();
    void generate_level();
};
