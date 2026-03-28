#include "core/engine.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/player.h"
#include "components/blocker.h"
#include "systems/fov.h"
#include "generation/dungeon.h"
#include <SDL2/SDL_image.h>
#include <cstdio>

Engine::Engine() {}

Engine::~Engine() {
    if (font_) TTF_CloseFont(font_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

bool Engine::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return false;
    }

    if (IMG_Init(IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init error: %s\n", IMG_GetError());
        return false;
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
        return false;
    }

    window_ = SDL_CreateWindow(
        "Reliquary",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_SHOWN
    );
    if (!window_) {
        fprintf(stderr, "Window creation error: %s\n", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        fprintf(stderr, "Renderer creation error: %s\n", SDL_GetError());
        return false;
    }

    // Load spritesheets
    if (!sprites_.load_all(renderer_, "assets/32rogues")) {
        fprintf(stderr, "Failed to load spritesheets\n");
        return false;
    }

    // Load a monospace font for the message log
    // Try common system font paths
    const char* font_paths[] = {
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation-mono/LiberationMono-Regular.ttf",
        "/usr/share/fonts/TTF/LiberationMono-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
        nullptr
    };

    for (int i = 0; font_paths[i]; i++) {
        font_ = TTF_OpenFont(font_paths[i], 14);
        if (font_) break;
    }

    if (!font_) {
        fprintf(stderr, "Warning: Could not load any system font for message log\n");
        // Non-fatal — game still runs, just no text
    }

    // Set up camera
    camera_.viewport_w = SCREEN_W;
    camera_.viewport_h = SCREEN_H - LOG_HEIGHT;

    // Generate first level
    generate_level();

    return true;
}

void Engine::generate_level() {
    DungeonParams params;
    params.width = 80;
    params.height = 50;
    params.max_rooms = 15;
    params.wall_type = TileType::WALL_STONE_BRICK;
    params.floor_type = TileType::FLOOR_STONE;

    auto result = dungeon::generate(rng_, params);
    map_ = std::move(result.map);

    // Create or reposition player
    if (player_ == NULL_ENTITY) {
        player_ = world_.create();
        world_.add<Player>(player_);
        world_.add<Position>(player_, {result.start_x, result.start_y});
        // Knight sprite: row 1, col 0 in rogues.png
        world_.add<Renderable>(player_, {SHEET_ROGUES, 0, 1, {255, 255, 255, 255}, 10});
    } else {
        world_.get<Position>(player_) = {result.start_x, result.start_y};
    }

    // Compute initial FOV
    auto& pos = world_.get<Position>(player_);
    fov::compute(map_, pos.x, pos.y, FOV_RADIUS);
    camera_.center_on(pos.x, pos.y);

    game_turn_ = 0;
    log_.add("You descend into the dark.", {180, 170, 160, 255});
    log_.add("The air is thick. Something old lingers here.", {120, 110, 100, 255});
}

void Engine::try_move_player(int dx, int dy) {
    auto& pos = world_.get<Position>(player_);
    int nx = pos.x + dx;
    int ny = pos.y + dy;

    if (!map_.in_bounds(nx, ny)) return;

    auto& tile = map_.at(nx, ny);

    // Door interaction
    if (tile.type == TileType::DOOR_CLOSED) {
        open_door(nx, ny);
        return;
    }

    if (!map_.is_walkable(nx, ny)) return;

    pos.x = nx;
    pos.y = ny;
    game_turn_++;

    // Stairs
    if (tile.type == TileType::STAIRS_DOWN) {
        log_.add("Stairs descend further into the dark.", {150, 140, 130, 255});
    }

    // Recompute FOV
    fov::compute(map_, pos.x, pos.y, FOV_RADIUS);
    camera_.center_on(pos.x, pos.y);
}

void Engine::open_door(int x, int y) {
    map_.at(x, y).type = TileType::DOOR_OPEN;
    game_turn_++;
    log_.add("You push open the heavy door.", {150, 140, 130, 255});

    auto& pos = world_.get<Position>(player_);
    fov::compute(map_, pos.x, pos.y, FOV_RADIUS);
}

void Engine::handle_input() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            state_ = GameState::QUIT;
            return;
        }

        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    state_ = GameState::QUIT;
                    break;

                // Movement — numpad, vi keys, arrows
                // North
                case SDLK_UP:
                case SDLK_k:
                case SDLK_KP_8:
                    try_move_player(0, -1);
                    break;
                // South
                case SDLK_DOWN:
                case SDLK_j:
                case SDLK_KP_2:
                    try_move_player(0, 1);
                    break;
                // West
                case SDLK_LEFT:
                case SDLK_h:
                case SDLK_KP_4:
                    try_move_player(-1, 0);
                    break;
                // East
                case SDLK_RIGHT:
                case SDLK_l:
                case SDLK_KP_6:
                    try_move_player(1, 0);
                    break;
                // NW
                case SDLK_y:
                case SDLK_KP_7:
                    try_move_player(-1, -1);
                    break;
                // NE
                case SDLK_u:
                case SDLK_KP_9:
                    try_move_player(1, -1);
                    break;
                // SW
                case SDLK_b:
                case SDLK_KP_1:
                    try_move_player(-1, 1);
                    break;
                // SE
                case SDLK_n:
                case SDLK_KP_3:
                    try_move_player(1, 1);
                    break;
                // Wait
                case SDLK_PERIOD:
                case SDLK_KP_5:
                    game_turn_++;
                    log_.add("You wait.", {100, 100, 100, 255});
                    break;

                // Descend stairs
                case SDLK_GREATER: {
                    auto& pos = world_.get<Position>(player_);
                    if (map_.at(pos.x, pos.y).type == TileType::STAIRS_DOWN) {
                        log_.add("You descend deeper.", {180, 170, 160, 255});
                        generate_level();
                    } else {
                        log_.add("There are no stairs here.", {150, 100, 100, 255});
                    }
                    break;
                }

                // Screenshot
                case SDLK_F12: {
                    SDL_Surface* sshot = SDL_CreateRGBSurface(0, SCREEN_W, SCREEN_H, 32,
                        0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
                    SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                         sshot->pixels, sshot->pitch);
                    SDL_SaveBMP(sshot, "/tmp/reliquary_ingame.bmp");
                    SDL_FreeSurface(sshot);
                    log_.add("Screenshot saved.", {100, 200, 100, 255});
                    break;
                }

                // Message log scrolling
                case SDLK_PAGEUP:
                    log_.scroll_up();
                    break;
                case SDLK_PAGEDOWN:
                    log_.scroll_down();
                    break;

                default:
                    break;
            }
        }
    }
}

void Engine::update() {
    log_.set_turn(game_turn_);
}

void Engine::render() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    // Draw map
    render::draw_map(renderer_, sprites_, map_, camera_);

    // Draw entities
    render::draw_entities(renderer_, sprites_, world_, map_, camera_);

    // Draw message log
    log_.render(renderer_, font_, 0, SCREEN_H - LOG_HEIGHT, SCREEN_W, LOG_HEIGHT);

    // Draw simple HUD — turn counter
    if (font_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Turn: %d", game_turn_);
        SDL_Color white = {200, 200, 200, 255};
        SDL_Surface* surf = TTF_RenderText_Blended(font_, buf, white);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            SDL_Rect dst = {SCREEN_W - surf->w - 8, 4, surf->w, surf->h};
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }

    SDL_RenderPresent(renderer_);
}

void Engine::run() {
    while (state_ != GameState::QUIT) {
        handle_input();
        update();
        render();
    }
}
