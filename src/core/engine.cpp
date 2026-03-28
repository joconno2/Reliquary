#include "core/engine.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/player.h"
#include "components/blocker.h"
#include "components/stats.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/corpse.h"
#include "systems/fov.h"
#include "systems/combat.h"
#include "systems/ai.h"
#include "generation/dungeon.h"
#include "generation/populate.h"
#include <SDL2/SDL_image.h>
#include <cstdio>
#include <algorithm>

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

    if (!sprites_.load_all(renderer_, "assets/32rogues")) {
        fprintf(stderr, "Failed to load spritesheets\n");
        return false;
    }

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
        fprintf(stderr, "Warning: Could not load any system font\n");
    }

    camera_.viewport_w = SCREEN_W;
    camera_.viewport_h = SCREEN_H - LOG_HEIGHT - HUD_HEIGHT;

    generate_level();

    return true;
}

void Engine::clear_entities_except_player() {
    // Collect all entities that aren't the player
    std::vector<Entity> to_destroy;
    auto& positions = world_.pool<Position>();
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (e != player_) {
            to_destroy.push_back(e);
        }
    }
    for (Entity e : to_destroy) {
        world_.destroy(e);
    }
}

void Engine::generate_level() {
    dungeon_level_++;

    // Clear old monsters/items but keep player
    if (player_ != NULL_ENTITY) {
        clear_entities_except_player();
    }

    DungeonParams params;
    params.width = 80;
    params.height = 50;
    params.max_rooms = 15;
    params.wall_type = TileType::WALL_STONE_BRICK;
    params.floor_type = TileType::FLOOR_STONE;

    auto result = dungeon::generate(rng_, params);
    map_ = std::move(result.map);
    rooms_ = std::move(result.rooms);

    // Create or reposition player
    if (player_ == NULL_ENTITY) {
        player_ = world_.create();
        world_.add<Player>(player_);
        world_.add<Position>(player_, {result.start_x, result.start_y});
        world_.add<Renderable>(player_, {SHEET_ROGUES, 0, 1, {255, 255, 255, 255}, 10});
        world_.add<Energy>(player_, {0, 100});

        Stats player_stats;
        player_stats.name = "you";
        player_stats.hp = 30;
        player_stats.hp_max = 30;
        player_stats.mp = 10;
        player_stats.mp_max = 10;
        player_stats.set_attr(Attr::STR, 12);
        player_stats.set_attr(Attr::DEX, 12);
        player_stats.set_attr(Attr::CON, 12);
        player_stats.set_attr(Attr::INT, 10);
        player_stats.set_attr(Attr::WIL, 10);
        player_stats.set_attr(Attr::PER, 12);
        player_stats.set_attr(Attr::CHA, 8);
        player_stats.base_damage = 3;
        player_stats.base_speed = 100;
        world_.add<Stats>(player_, std::move(player_stats));
    } else {
        world_.get<Position>(player_) = {result.start_x, result.start_y};
    }

    // Spawn monsters
    populate::spawn_monsters(world_, map_, rooms_, rng_);

    // Compute initial FOV
    auto& pos = world_.get<Position>(player_);
    auto& stats = world_.get<Stats>(player_);
    fov::compute(map_, pos.x, pos.y, stats.fov_radius());
    camera_.center_on(pos.x, pos.y);

    char buf[64];
    snprintf(buf, sizeof(buf), "Dungeon level %d.", dungeon_level_);
    log_.add(buf, {180, 170, 160, 255});

    if (dungeon_level_ == 1) {
        log_.add("You descend into the dark.", {180, 170, 160, 255});
        log_.add("The air is thick. Something old lingers here.", {120, 110, 100, 255});
    } else {
        log_.add("You descend deeper. The walls are older here.", {120, 110, 100, 255});
    }
}

void Engine::try_move_player(int dx, int dy) {
    if (state_ != GameState::PLAYING) return;

    auto& pos = world_.get<Position>(player_);
    int nx = pos.x + dx;
    int ny = pos.y + dy;

    if (!map_.in_bounds(nx, ny)) return;

    auto& tile = map_.at(nx, ny);

    // Door interaction
    if (tile.type == TileType::DOOR_CLOSED) {
        open_door(nx, ny);
        player_acted_ = true;
        return;
    }

    // Check for attackable entity
    Entity target = combat::entity_at(world_, nx, ny, player_);
    if (target != NULL_ENTITY) {
        combat::melee_attack(world_, player_, target, rng_, log_);
        player_acted_ = true;
        return;
    }

    if (!map_.is_walkable(nx, ny)) return;

    pos.x = nx;
    pos.y = ny;
    player_acted_ = true;

    // Stairs message
    if (tile.type == TileType::STAIRS_DOWN) {
        log_.add("Stairs descend further into the dark.", {150, 140, 130, 255});
    }
}

void Engine::open_door(int x, int y) {
    map_.at(x, y).type = TileType::DOOR_OPEN;
    log_.add("You push open the heavy door.", {150, 140, 130, 255});
}

void Engine::process_turn() {
    if (!player_acted_) return;
    player_acted_ = false;
    game_turn_++;

    // Check player death
    if (world_.has<Stats>(player_)) {
        auto& stats = world_.get<Stats>(player_);
        if (stats.hp <= 0) {
            state_ = GameState::DEAD;
            return;
        }
    }

    // Give all entities energy
    auto& energy_pool = world_.pool<Energy>();
    for (size_t i = 0; i < energy_pool.size(); i++) {
        auto& e = energy_pool.at_index(i);
        e.gain();
    }

    // Player spends energy
    if (world_.has<Energy>(player_)) {
        world_.get<Energy>(player_).spend();
    }

    // Process AI for all monsters that can act
    // Multiple passes — fast monsters might get multiple actions
    for (int pass = 0; pass < 3; pass++) {
        ai::process(world_, map_, player_, rng_);

        // Check for AI entities adjacent to player — they attack
        auto& ai_pool = world_.pool<AI>();
        for (size_t i = 0; i < ai_pool.size(); i++) {
            Entity e = ai_pool.entity_at(i);
            if (!world_.has<Position>(e) || !world_.has<Stats>(e)) continue;

            auto& mpos = world_.get<Position>(e);
            auto& ppos = world_.get<Position>(player_);

            int dx = std::abs(mpos.x - ppos.x);
            int dy = std::abs(mpos.y - ppos.y);

            if (dx <= 1 && dy <= 1 && (dx + dy > 0)) {
                auto& ai_comp = ai_pool.at_index(i);
                if (ai_comp.state == AIState::HUNTING) {
                    combat::melee_attack(world_, e, player_, rng_, log_);

                    // Check player death after each attack
                    if (world_.has<Stats>(player_) && world_.get<Stats>(player_).hp <= 0) {
                        state_ = GameState::DEAD;
                        return;
                    }
                }
            }
        }
    }

    // Recompute FOV
    if (world_.has<Position>(player_) && world_.has<Stats>(player_)) {
        auto& pos = world_.get<Position>(player_);
        auto& stats = world_.get<Stats>(player_);
        fov::compute(map_, pos.x, pos.y, stats.fov_radius());
        camera_.center_on(pos.x, pos.y);
    }

    log_.set_turn(game_turn_);
}

void Engine::handle_input() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            state_ = GameState::QUIT;
            return;
        }

        if (event.type == SDL_KEYDOWN) {
            // Esc always quits
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                state_ = GameState::QUIT;
                return;
            }

            // Log scrolling always works
            if (event.key.keysym.sym == SDLK_PAGEUP) { log_.scroll_up(); return; }
            if (event.key.keysym.sym == SDLK_PAGEDOWN) { log_.scroll_down(); return; }

            // Dead state — any key restarts? For now just quit
            if (state_ == GameState::DEAD) {
                return;
            }

            switch (event.key.keysym.sym) {
                case SDLK_UP:    case SDLK_k: case SDLK_KP_8: try_move_player(0, -1); break;
                case SDLK_DOWN:  case SDLK_j: case SDLK_KP_2: try_move_player(0, 1);  break;
                case SDLK_LEFT:  case SDLK_h: case SDLK_KP_4: try_move_player(-1, 0); break;
                case SDLK_RIGHT: case SDLK_l: case SDLK_KP_6: try_move_player(1, 0);  break;
                case SDLK_y: case SDLK_KP_7: try_move_player(-1, -1); break;
                case SDLK_u: case SDLK_KP_9: try_move_player(1, -1);  break;
                case SDLK_b: case SDLK_KP_1: try_move_player(-1, 1);  break;
                case SDLK_n: case SDLK_KP_3: try_move_player(1, 1);   break;

                case SDLK_PERIOD: case SDLK_KP_5:
                    player_acted_ = true;
                    break;

                case SDLK_GREATER: {
                    auto& pos = world_.get<Position>(player_);
                    if (map_.at(pos.x, pos.y).type == TileType::STAIRS_DOWN) {
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

                default: break;
            }
        }
    }
}

void Engine::render_hud() {
    if (!font_) return;
    if (!world_.has<Stats>(player_)) return;

    auto& stats = world_.get<Stats>(player_);

    // HUD background
    SDL_Rect hud_bg = {0, 0, SCREEN_W, HUD_HEIGHT};
    SDL_SetRenderDrawColor(renderer_, 15, 12, 18, 240);
    SDL_RenderFillRect(renderer_, &hud_bg);
    SDL_SetRenderDrawColor(renderer_, 60, 50, 70, 255);
    SDL_RenderDrawLine(renderer_, 0, HUD_HEIGHT, SCREEN_W, HUD_HEIGHT);

    // HP bar
    int bar_w = 120;
    int bar_h = 12;
    int bar_x = 8;
    int bar_y = 6;

    // Background
    SDL_Rect bar_bg = {bar_x, bar_y, bar_w, bar_h};
    SDL_SetRenderDrawColor(renderer_, 40, 10, 10, 255);
    SDL_RenderFillRect(renderer_, &bar_bg);

    // Fill
    int fill = (stats.hp * bar_w) / std::max(1, stats.hp_max);
    SDL_Rect bar_fill = {bar_x, bar_y, fill, bar_h};
    if (stats.hp > stats.hp_max / 2) {
        SDL_SetRenderDrawColor(renderer_, 140, 40, 40, 255);
    } else if (stats.hp > stats.hp_max / 4) {
        SDL_SetRenderDrawColor(renderer_, 160, 100, 30, 255);
    } else {
        SDL_SetRenderDrawColor(renderer_, 200, 50, 50, 255);
    }
    SDL_RenderFillRect(renderer_, &bar_fill);

    // HP text
    char hp_text[32];
    snprintf(hp_text, sizeof(hp_text), "HP: %d/%d", stats.hp, stats.hp_max);
    SDL_Color white = {200, 200, 200, 255};
    SDL_Surface* surf = TTF_RenderText_Blended(font_, hp_text, white);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
        SDL_Rect dst = {bar_x + bar_w + 8, bar_y - 1, surf->w, surf->h};
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }

    // Dungeon level + turn
    char info[64];
    snprintf(info, sizeof(info), "Depth: %d  Turn: %d", dungeon_level_, game_turn_);
    surf = TTF_RenderText_Blended(font_, info, white);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
        SDL_Rect dst = {SCREEN_W - surf->w - 8, bar_y - 1, surf->w, surf->h};
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}

void Engine::render() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    // Offset rendering below HUD
    Camera render_cam = camera_;

    // Draw map
    render::draw_map(renderer_, sprites_, map_, render_cam, HUD_HEIGHT);

    // Draw entities
    render::draw_entities(renderer_, sprites_, world_, map_, render_cam, HUD_HEIGHT);

    // HUD
    render_hud();

    // Message log
    log_.render(renderer_, font_, 0, SCREEN_H - LOG_HEIGHT, SCREEN_W, LOG_HEIGHT);

    // Death overlay
    if (state_ == GameState::DEAD && font_) {
        // Darken screen
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_Rect overlay = {0, 0, SCREEN_W, SCREEN_H};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 150);
        SDL_RenderFillRect(renderer_, &overlay);

        SDL_Color red = {200, 50, 50, 255};
        SDL_Surface* surf = TTF_RenderText_Blended(font_, "YOU HAVE DIED", red);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            SDL_Rect dst = {SCREEN_W / 2 - surf->w / 2, SCREEN_H / 2 - surf->h / 2,
                            surf->w, surf->h};
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
        process_turn();
        render();
    }
}
