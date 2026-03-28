#include "core/engine.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/player.h"
#include "components/blocker.h"
#include "components/stats.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/corpse.h"
#include "components/item.h"
#include "components/inventory.h"
#include "components/god.h"
#include "components/class_def.h"
#include "components/spellbook.h"
#include "systems/magic.h"
#include "components/background.h"
#include "components/traits.h"
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
    if (font_title_ && font_title_ != font_) TTF_CloseFont(font_title_);
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

    // Load bundled fonts
    font_ = TTF_OpenFont("assets/fonts/PrStart.ttf", 8);
    if (!font_) {
        fprintf(stderr, "Warning: Could not load Press Start font: %s\n", TTF_GetError());
        // Fallback to system font
        font_ = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSansMono.ttf", 12);
    }

    font_title_ = TTF_OpenFont("assets/fonts/Jacquard12-Regular.ttf", 24);
    if (!font_title_) {
        fprintf(stderr, "Warning: Could not load Jacquard font: %s\n", TTF_GetError());
        font_title_ = font_; // fallback to body font
    }

    camera_.viewport_w = SCREEN_W;
    camera_.viewport_h = SCREEN_H - LOG_HEIGHT - HUD_HEIGHT;

    creation_screen_.reset();

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

    // Zone theming based on depth
    struct ZoneTheme {
        TileType wall, floor;
        const char* name;
    };
    static const ZoneTheme ZONES[] = {
        {TileType::WALL_DIRT,        TileType::FLOOR_DIRT,      "The Warrens"},
        {TileType::WALL_STONE_ROUGH, TileType::FLOOR_STONE,     "Stonekeep"},
        {TileType::WALL_STONE_BRICK, TileType::FLOOR_STONE,     "The Deep Halls"},
        {TileType::WALL_CATACOMB,    TileType::FLOOR_BONE,      "The Catacombs"},
        {TileType::WALL_IGNEOUS,     TileType::FLOOR_RED_STONE, "The Molten Depths"},
        {TileType::WALL_LARGE_STONE, TileType::FLOOR_STONE,     "The Sunken Halls"},
    };
    constexpr int ZONE_COUNT = sizeof(ZONES) / sizeof(ZONES[0]);
    int zone_idx = std::min(dungeon_level_ - 1, ZONE_COUNT - 1);
    auto& zone = ZONES[zone_idx];

    DungeonParams params;
    params.width = 80;
    params.height = 50;
    params.max_rooms = 12 + dungeon_level_;
    params.wall_type = zone.wall;
    params.floor_type = zone.floor;

    auto result = dungeon::generate(rng_, params);
    map_ = std::move(result.map);
    rooms_ = std::move(result.rooms);

    // Create or reposition player
    if (player_ == NULL_ENTITY) {
        auto build = creation_screen_.get_build();
        auto& cls = get_class_info(build.class_id);
        auto& god = get_god_info(build.god);

        player_ = world_.create();
        world_.add<Player>(player_);
        world_.add<Position>(player_, {result.start_x, result.start_y});
        world_.add<Renderable>(player_, {SHEET_ROGUES, cls.sprite_x, cls.sprite_y,
                                         {255, 255, 255, 255}, 10});
        world_.add<Energy>(player_, {0, 100});
        world_.add<Inventory>(player_);
        world_.add<GodAlignment>(player_, {build.god, 0});

        auto& bg  = get_background_info(build.background);

        Stats player_stats;
        player_stats.name = "you";
        player_stats.hp = cls.hp + god.bonus_hp + bg.bonus_hp;
        player_stats.hp_max = cls.hp + god.bonus_hp + bg.bonus_hp;
        player_stats.mp = cls.mp + god.bonus_mp;
        player_stats.mp_max = cls.mp + god.bonus_mp;
        player_stats.set_attr(Attr::STR, cls.str   + god.str_bonus + bg.str_bonus);
        player_stats.set_attr(Attr::DEX, cls.dex   + god.dex_bonus + bg.dex_bonus);
        player_stats.set_attr(Attr::CON, cls.con   + god.con_bonus + bg.con_bonus);
        player_stats.set_attr(Attr::INT, cls.intel + god.int_bonus + bg.int_bonus);
        player_stats.set_attr(Attr::WIL, cls.wil   + god.wil_bonus + bg.wil_bonus);
        player_stats.set_attr(Attr::PER, cls.per   + god.per_bonus + bg.per_bonus);
        player_stats.set_attr(Attr::CHA, cls.cha   + god.cha_bonus + bg.cha_bonus);
        player_stats.base_damage = cls.base_damage + bg.bonus_damage;
        player_stats.base_speed = 100;

        // Apply trait stat modifiers
        for (TraitId tid : build.traits) {
            const TraitInfo& tr = get_trait_info(tid);
            player_stats.set_attr(Attr::STR, player_stats.attr(Attr::STR) + tr.str_mod);
            player_stats.set_attr(Attr::DEX, player_stats.attr(Attr::DEX) + tr.dex_mod);
            player_stats.set_attr(Attr::CON, player_stats.attr(Attr::CON) + tr.con_mod);
            player_stats.set_attr(Attr::INT, player_stats.attr(Attr::INT) + tr.int_mod);
            player_stats.set_attr(Attr::WIL, player_stats.attr(Attr::WIL) + tr.wil_mod);
            player_stats.set_attr(Attr::PER, player_stats.attr(Attr::PER) + tr.per_mod);
            player_stats.set_attr(Attr::CHA, player_stats.attr(Attr::CHA) + tr.cha_mod);
        }

        world_.add<Stats>(player_, std::move(player_stats));

        // Starting spells based on class
        Spellbook book;
        switch (build.class_id) {
            case ClassId::WIZARD:
                book.learn(SpellId::SPARK);
                book.learn(SpellId::FORCE_BOLT);
                book.learn(SpellId::IDENTIFY);
                break;
            case ClassId::RANGER:
                book.learn(SpellId::DETECT_MONSTERS);
                break;
            default:
                book.learn(SpellId::MINOR_HEAL); // everyone gets minor heal
                break;
        }
        world_.add<Spellbook>(player_, std::move(book));
    } else {
        world_.get<Position>(player_) = {result.start_x, result.start_y};
    }

    // Spawn monsters and items
    populate::spawn_monsters(world_, map_, rooms_, rng_, dungeon_level_);
    populate::spawn_items(world_, map_, rooms_, rng_, dungeon_level_);

    // Compute initial FOV
    auto& pos = world_.get<Position>(player_);
    auto& stats = world_.get<Stats>(player_);
    fov::compute(map_, pos.x, pos.y, stats.fov_radius());
    camera_.center_on(pos.x, pos.y);

    char buf[128];
    snprintf(buf, sizeof(buf), "%s — Depth %d", zone.name, dungeon_level_);
    log_.add(buf, {180, 170, 160, 255});

    // Atmospheric entry messages per zone
    static const char* ZONE_MESSAGES[] = {
        "Dirt crumbles from the ceiling. Rats scatter at your approach.",
        "Cold stone. The echo of your footsteps returns wrong.",
        "The masonry here is ancient. Someone built this to last.",
        "Bones line the walls. Not decoration — storage.",
        "The heat is oppressive. The stone glows faintly red.",
        "Water drips from every surface. The walls weep.",
    };
    log_.add(ZONE_MESSAGES[zone_idx], {120, 110, 100, 255});
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

void Engine::try_pickup() {
    auto& pos = world_.get<Position>(player_);
    auto& inv = world_.get<Inventory>(player_);

    // Find items at player position
    auto& positions = world_.pool<Position>();
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (e == player_) continue;
        if (!world_.has<Item>(e)) continue;

        auto& ipos = positions.at_index(i);
        if (ipos.x != pos.x || ipos.y != pos.y) continue;

        auto& item = world_.get<Item>(e);

        // Gold is auto-collected
        if (item.type == ItemType::GOLD) {
            gold_ += item.gold_value;
            char buf[64];
            snprintf(buf, sizeof(buf), "You pick up %d gold.", item.gold_value);
            log_.add(buf, {220, 200, 80, 255});
            world_.destroy(e);
            player_acted_ = true;
            return;
        }

        if (inv.is_full()) {
            log_.add("Your pack is full.", {180, 120, 120, 255});
            return;
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "You pick up the %s.", item.display_name().c_str());
        log_.add(buf, {180, 175, 160, 255});

        // Remove from ground (remove Position), add to inventory
        world_.remove<Position>(e);
        if (world_.has<Renderable>(e)) world_.remove<Renderable>(e);
        inv.add(e);
        player_acted_ = true;
        return;
    }

    log_.add("There is nothing here to pick up.", {120, 110, 100, 255});
}

void Engine::handle_inventory_action(InvAction action) {
    if (action == InvAction::CLOSE) {
        inventory_screen_.close();
        return;
    }

    Entity item_e = inventory_screen_.get_selected_item(world_);
    if (item_e == NULL_ENTITY) return;
    if (!world_.has<Item>(item_e)) return;

    auto& inv = world_.get<Inventory>(player_);
    auto& item = world_.get<Item>(item_e);

    switch (action) {
        case InvAction::EQUIP: {
            if (item.slot == EquipSlot::NONE) {
                log_.add("You can't equip that.", {150, 120, 120, 255});
                break;
            }
            if (inv.is_equipped(item_e)) {
                // Unequip
                inv.unequip(item.slot);
                char buf[128];
                snprintf(buf, sizeof(buf), "You remove the %s.", item.display_name().c_str());
                log_.add(buf, {170, 165, 160, 255});
            } else {
                // Unequip existing item in that slot
                Entity prev = inv.get_equipped(item.slot);
                if (prev != NULL_ENTITY) {
                    inv.unequip(item.slot);
                }
                inv.equip(item.slot, item_e);
                char buf[128];
                snprintf(buf, sizeof(buf), "You equip the %s.", item.display_name().c_str());
                log_.add(buf, {170, 180, 160, 255});
                item.identified = true; // equipping identifies
            }
            break;
        }
        case InvAction::USE: {
            if (item.type == ItemType::POTION || item.type == ItemType::FOOD) {
                if (item.heal_amount > 0 && world_.has<Stats>(player_)) {
                    auto& stats = world_.get<Stats>(player_);
                    int healed = std::min(item.heal_amount, stats.hp_max - stats.hp);
                    stats.hp += healed;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "You consume the %s. Healed %d.",
                             item.display_name().c_str(), healed);
                    log_.add(buf, {100, 200, 100, 255});
                    item.identified = true;
                }
                // Remove consumed item
                inv.remove(item_e);
                world_.destroy(item_e);
                player_acted_ = true;
            } else {
                log_.add("You can't use that.", {150, 120, 120, 255});
            }
            break;
        }
        case InvAction::DROP: {
            auto& pos = world_.get<Position>(player_);
            if (inv.is_equipped(item_e)) {
                inv.unequip(item.slot);
            }
            inv.remove(item_e);
            // Put back on ground
            world_.add<Position>(item_e, {pos.x, pos.y});
            if (!world_.has<Renderable>(item_e)) {
                // Re-add renderable (we removed it on pickup)
                world_.add<Renderable>(item_e, {SHEET_ITEMS, 0, 0, {255, 255, 255, 255}, 1});
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "You drop the %s.", item.display_name().c_str());
            log_.add(buf, {160, 155, 150, 255});
            player_acted_ = true;
            break;
        }
        default: break;
    }
}

void Engine::handle_input() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            state_ = GameState::QUIT;
            return;
        }

        // Character creation state
        if (state_ == GameState::CREATING) {
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE
                && creation_screen_.is_done()) {
                state_ = GameState::QUIT;
                return;
            }
            creation_screen_.handle_input(event);
            if (creation_screen_.is_done()) {
                state_ = GameState::PLAYING;
                generate_level();
            }
            return;
        }

        if (event.type == SDL_KEYDOWN) {
            // Inventory mode intercepts input
            if (inventory_screen_.is_open()) {
                InvAction act = inventory_screen_.handle_input(event);
                if (act != InvAction::NONE) {
                    handle_inventory_action(act);
                }
                return;
            }

            // Spell screen intercepts input
            if (spell_screen_.is_open()) {
                SpellAction act = spell_screen_.handle_input(event);
                if (act == SpellAction::CLOSE) {
                    spell_screen_.close();
                } else if (act == SpellAction::CAST) {
                    SpellId spell = spell_screen_.get_selected_spell(world_);
                    if (spell != SpellId::COUNT) {
                        auto result = magic::cast(world_, player_, spell,
                                                   map_, rng_, log_);
                        if (result.consumed_turn) player_acted_ = true;
                        spell_screen_.close();
                    }
                }
                return;
            }

            // Esc always quits
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                state_ = GameState::QUIT;
                return;
            }

            // Log scrolling always works
            if (event.key.keysym.sym == SDLK_PAGEUP) { log_.scroll_up(); return; }
            if (event.key.keysym.sym == SDLK_PAGEDOWN) { log_.scroll_down(); return; }

            // Dead state
            if (state_ == GameState::DEAD) {
                return;
            }

            switch (event.key.keysym.sym) {
                case SDLK_UP:    case SDLK_KP_8: try_move_player(0, -1); break;
                case SDLK_DOWN:  case SDLK_KP_2: try_move_player(0, 1);  break;
                case SDLK_LEFT:  case SDLK_KP_4: try_move_player(-1, 0); break;
                case SDLK_RIGHT: case SDLK_KP_6: try_move_player(1, 0);  break;
                case SDLK_k: try_move_player(0, -1); break;
                case SDLK_j: try_move_player(0, 1);  break;
                case SDLK_h: try_move_player(-1, 0); break;
                case SDLK_l: try_move_player(1, 0);  break;
                case SDLK_y: case SDLK_KP_7: try_move_player(-1, -1); break;
                case SDLK_u: case SDLK_KP_9: try_move_player(1, -1);  break;
                case SDLK_b: case SDLK_KP_1: try_move_player(-1, 1);  break;
                case SDLK_n: case SDLK_KP_3: try_move_player(1, 1);   break;

                case SDLK_PERIOD: case SDLK_KP_5:
                    player_acted_ = true;
                    break;

                // Pickup
                case SDLK_g:
                case SDLK_COMMA:
                    try_pickup();
                    break;

                // Inventory
                case SDLK_i:
                    inventory_screen_.open(player_);
                    break;

                // Spells
                case SDLK_z:
                    spell_screen_.open(player_);
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

    // MP bar (only if player has MP)
    if (stats.mp_max > 0) {
        int mp_x = bar_x + bar_w + 90;
        SDL_Rect mp_bg = {mp_x, bar_y, 80, bar_h};
        SDL_SetRenderDrawColor(renderer_, 10, 10, 40, 255);
        SDL_RenderFillRect(renderer_, &mp_bg);
        int mp_fill = (stats.mp * 80) / std::max(1, stats.mp_max);
        SDL_Rect mp_fill_r = {mp_x, bar_y, mp_fill, bar_h};
        SDL_SetRenderDrawColor(renderer_, 60, 60, 160, 255);
        SDL_RenderFillRect(renderer_, &mp_fill_r);

        char mp_text[32];
        snprintf(mp_text, sizeof(mp_text), "MP:%d/%d", stats.mp, stats.mp_max);
        surf = TTF_RenderText_Blended(font_, mp_text, white);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            SDL_Rect dst = {mp_x + 84, bar_y - 1, surf->w, surf->h};
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }

    // XP bar
    int xp_bar_x = bar_x + bar_w + 260;
    SDL_Rect xp_bg = {xp_bar_x, bar_y, 80, bar_h};
    SDL_SetRenderDrawColor(renderer_, 15, 15, 40, 255);
    SDL_RenderFillRect(renderer_, &xp_bg);
    int xp_fill = (stats.xp * 80) / std::max(1, stats.xp_next);
    SDL_Rect xp_fill_r = {xp_bar_x, bar_y, xp_fill, bar_h};
    SDL_SetRenderDrawColor(renderer_, 80, 80, 180, 255);
    SDL_RenderFillRect(renderer_, &xp_fill_r);

    char lvl_text[32];
    snprintf(lvl_text, sizeof(lvl_text), "Lv %d", stats.level);
    surf = TTF_RenderText_Blended(font_, lvl_text, white);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
        SDL_Rect dst = {xp_bar_x + 84, bar_y - 1, surf->w, surf->h};
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }

    // God name + depth + gold + turn
    const char* god_name = "";
    if (world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        god_name = get_god_info(ga.god).name;
    }
    char info[128];
    snprintf(info, sizeof(info), "%s  Depth: %d  Gold: %d  Turn: %d",
             god_name, dungeon_level_, gold_, game_turn_);
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
    // Character creation screen
    if (state_ == GameState::CREATING) {
        creation_screen_.render(renderer_, font_, font_title_, sprites_, SCREEN_W, SCREEN_H);
        SDL_RenderPresent(renderer_);
        return;
    }

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    Camera render_cam = camera_;

    // Draw map
    render::draw_map(renderer_, sprites_, map_, render_cam, HUD_HEIGHT);

    // Draw entities
    render::draw_entities(renderer_, sprites_, world_, map_, render_cam, HUD_HEIGHT);

    // HUD
    render_hud();

    // Message log
    log_.render(renderer_, font_, 0, SCREEN_H - LOG_HEIGHT, SCREEN_W, LOG_HEIGHT);

    // Overlay screens
    inventory_screen_.render(renderer_, font_, sprites_, world_, SCREEN_W, SCREEN_H);
    spell_screen_.render(renderer_, font_, world_, SCREEN_W, SCREEN_H);

    // Death overlay
    if (state_ == GameState::DEAD && font_) {
        // Darken screen
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_Rect overlay = {0, 0, SCREEN_W, SCREEN_H};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 150);
        SDL_RenderFillRect(renderer_, &overlay);

        SDL_Color red = {200, 50, 50, 255};
        TTF_Font* death_font = font_title_ ? font_title_ : font_;
        SDL_Surface* surf = TTF_RenderText_Blended(death_font, "You have died.", red);
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
