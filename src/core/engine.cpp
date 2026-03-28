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
#include "components/npc.h"
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
        width_, height_,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
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

    // Load fonts at current UI scale
    reload_fonts();

    camera_.viewport_w = width_;
    camera_.viewport_h = height_ - LOG_HEIGHT - HUD_HEIGHT;
    camera_.tile_size = 48; // 32 * 1.5 — tiles rendered 50% bigger than native

    creation_screen_.reset();

    return true;
}

void Engine::reload_fonts() {
    // Close existing fonts
    if (font_title_ && font_title_ != font_) TTF_CloseFont(font_title_);
    if (font_) TTF_CloseFont(font_);
    font_ = nullptr;
    font_title_ = nullptr;

    int body_size = static_cast<int>(12 * ui_scale_);
    int title_size = static_cast<int>(32 * ui_scale_);

    font_ = TTF_OpenFont("assets/fonts/PrStart.ttf", body_size);
    if (!font_) {
        fprintf(stderr, "Warning: Could not load Press Start font: %s\n", TTF_GetError());
        font_ = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSansMono.ttf", body_size);
    }

    font_title_ = TTF_OpenFont("assets/fonts/Jacquard12-Regular.ttf", title_size);
    if (!font_title_) {
        fprintf(stderr, "Warning: Could not load Jacquard font: %s\n", TTF_GetError());
        font_title_ = font_;
    }
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

    int start_x, start_y;

    if (dungeon_level_ == 0) {
        // Village — loaded from hand-authored map file
        auto mresult = mapfile::load("data/maps/thornwall.map");
        map_ = std::move(mresult.map);
        rooms_.clear();
        start_x = mresult.start_x;
        start_y = mresult.start_y;

        // Spawn NPCs from map entities
        for (auto& me : mresult.entities) {
            Entity npc = world_.create();
            world_.add<Position>(npc, {me.x, me.y});

            NPC npc_comp;
            int sx = 0, sy = 0; // sprite coords in rogues.png
            switch (me.glyph) {
                case 'S':
                    npc_comp.role = NPCRole::SHOPKEEPER;
                    npc_comp.name = "Shopkeeper";
                    npc_comp.dialogue = "Browse, if you like. I don't haggle.";
                    sx = 2; sy = 6; // shopkeep sprite
                    break;
                case 'B':
                    npc_comp.role = NPCRole::BLACKSMITH;
                    npc_comp.name = "Blacksmith";
                    npc_comp.dialogue = "Iron holds. Steel bites. That's all you need to know.";
                    sx = 4; sy = 6; // blacksmith sprite
                    break;
                case 'P':
                    npc_comp.role = NPCRole::PRIEST;
                    npc_comp.name = "Scholar";
                    npc_comp.dialogue = "The deeper you go, the older things get. Be careful what you read.";
                    sx = 5; sy = 6; // scholar sprite
                    break;
                case 'F':
                    npc_comp.role = NPCRole::FARMER;
                    npc_comp.name = "Farmer";
                    npc_comp.dialogue = "Used to be quiet here. Before they opened the barrow.";
                    sx = 0; sy = 6; // farmer sprite
                    break;
                case 'G':
                    npc_comp.role = NPCRole::GUARD;
                    npc_comp.name = "Guard";
                    npc_comp.dialogue = "Keep your blade sheathed in town.";
                    sx = 0; sy = 1; // knight sprite
                    break;
            }
            world_.add<NPC>(npc, std::move(npc_comp));
            world_.add<Renderable>(npc, {SHEET_ROGUES, sx, sy, {255, 255, 255, 255}, 5});

            // NPCs have stats but aren't killable (no AI component = won't fight)
            Stats npc_stats;
            npc_stats.name = world_.get<NPC>(npc).name;
            npc_stats.hp = 999;
            npc_stats.hp_max = 999;
            world_.add<Stats>(npc, std::move(npc_stats));
        }
    } else {
        // Dungeon zones
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
        start_x = result.start_x;
        start_y = result.start_y;
    }

    // Create or reposition player
    if (player_ == NULL_ENTITY) {
        auto build = creation_screen_.get_build();
        auto& cls = get_class_info(build.class_id);
        auto& god = get_god_info(build.god);

        player_ = world_.create();
        world_.add<Player>(player_);
        world_.add<Position>(player_, {start_x, start_y});
        world_.add<Renderable>(player_, {SHEET_ROGUES, cls.sprite_x, cls.sprite_y,
                                         {255, 255, 255, 255}, 10});
        world_.add<Energy>(player_, {0, 100});
        world_.add<Inventory>(player_);
        world_.add<GodAlignment>(player_, {build.god, 0});

        auto& bg  = get_background_info(build.background);

        Stats player_stats;
        player_stats.name = build.name;
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
        world_.get<Position>(player_) = {start_x, start_y};
    }

    // Spawn monsters and items (not in village)
    if (dungeon_level_ > 0) {
        populate::spawn_monsters(world_, map_, rooms_, rng_, dungeon_level_);
        populate::spawn_items(world_, map_, rooms_, rng_, dungeon_level_);
    }

    // Compute initial FOV
    auto& pos = world_.get<Position>(player_);
    auto& stats = world_.get<Stats>(player_);
    fov::compute(map_, pos.x, pos.y, stats.fov_radius());
    camera_.center_on(pos.x, pos.y);

    if (dungeon_level_ == 0) {
        log_.add("Thornwall.", {180, 170, 160, 255});
        log_.add("A trading post at the edge of the world. Everyone is watching everyone.",
                 {120, 110, 100, 255});
        log_.add("The dungeon entrance lies to the east.", {100, 100, 90, 255});
    } else {
        // Dungeon zone messages
        static const char* ZONE_NAMES[] = {
            "The Warrens", "Stonekeep", "The Deep Halls",
            "The Catacombs", "The Molten Depths", "The Sunken Halls",
        };
        static const char* ZONE_MESSAGES[] = {
            "Dirt crumbles from the ceiling. Rats scatter at your approach.",
            "Cold stone. The echo of your footsteps returns wrong.",
            "The masonry here is ancient. Someone built this to last.",
            "Bones line the walls. Not decoration — storage.",
            "The heat is oppressive. The stone glows faintly red.",
            "Water drips from every surface. The walls weep.",
        };
        constexpr int MSG_COUNT = sizeof(ZONE_NAMES) / sizeof(ZONE_NAMES[0]);
        int idx = std::min(dungeon_level_ - 1, MSG_COUNT - 1);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s — Depth %d", ZONE_NAMES[idx], dungeon_level_);
        log_.add(buf, {180, 170, 160, 255});
        log_.add(ZONE_MESSAGES[idx], {120, 110, 100, 255});
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

    // Check for entity at target tile
    Entity target = combat::entity_at(world_, nx, ny, player_);
    if (target != NULL_ENTITY) {
        // NPC — talk instead of fight
        if (world_.has<NPC>(target)) {
            auto& npc = world_.get<NPC>(target);
            char buf[256];
            snprintf(buf, sizeof(buf), "%s: \"%s\"", npc.name.c_str(), npc.dialogue.c_str());
            log_.add(buf, {180, 180, 140, 255});
            return; // talking doesn't cost a turn
        }
        // Hostile — attack
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

        // Handle window resize
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_RESIZED) {
            width_ = event.window.data1;
            height_ = event.window.data2;
            camera_.viewport_w = width_;
            camera_.viewport_h = height_ - LOG_HEIGHT - HUD_HEIGHT;
            continue;
        }

        // F11 fullscreen toggle — works in any state
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F11) {
            fullscreen_ = !fullscreen_;
            if (fullscreen_) {
                SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
            } else {
                SDL_SetWindowFullscreen(window_, 0);
            }
            // Dimensions update via SDL_WINDOWEVENT_RESIZED
            continue;
        }

        // Main menu state
        if (state_ == GameState::MAIN_MENU) {
            MenuChoice choice = main_menu_.handle_input(event);
            switch (choice) {
                case MenuChoice::NEW_GAME:
                    creation_screen_.reset();
                    state_ = GameState::CREATING;
                    break;
                case MenuChoice::CONTINUE:
                    state_ = GameState::PLAYING;
                    break;
                case MenuChoice::SETTINGS:
                    settings_.reset();
                    return_from_settings_ = GameState::MAIN_MENU;
                    state_ = GameState::SETTINGS;
                    break;
                case MenuChoice::QUIT:
                    state_ = GameState::QUIT;
                    break;
                default: break;
            }
            continue;
        }

        // Settings state
        if (state_ == GameState::SETTINGS) {
            settings_.handle_input(event, window_);
            // Check if UI scale changed
            if (settings_.scale_changed()) {
                ui_scale_ = settings_.get_ui_scale();
                reload_fonts();
                settings_.clear_scale_changed();
            }
            if (settings_.should_close()) {
                state_ = return_from_settings_;
                // If returning to PLAYING, reopen the pause menu
                if (return_from_settings_ == GameState::PLAYING) {
                    pause_menu_.open();
                }
            }
            continue;
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
            // Pause menu intercepts all input when open
            if (pause_menu_.is_open()) {
                PauseChoice choice = pause_menu_.handle_input(event);
                switch (choice) {
                    case PauseChoice::CONTINUE:
                        pause_menu_.close();
                        break;
                    case PauseChoice::SAVE:
                        log_.add("Game saved.", {100, 200, 100, 255});
                        pause_menu_.close();
                        break;
                    case PauseChoice::LOAD:
                        log_.add("Game loaded.", {100, 200, 100, 255});
                        pause_menu_.close();
                        break;
                    case PauseChoice::SETTINGS:
                        pause_menu_.close();
                        settings_.reset();
                        return_from_settings_ = GameState::PLAYING;
                        state_ = GameState::SETTINGS;
                        break;
                    case PauseChoice::EXIT_TO_MENU:
                        pause_menu_.close();
                        main_menu_.set_can_continue(true);
                        state_ = GameState::MAIN_MENU;
                        break;
                    default: break;
                }
                return;
            }

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

            // Esc opens pause menu instead of quitting
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                pause_menu_.open();
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
                    SDL_Surface* sshot = SDL_CreateRGBSurface(0, width_, height_, 32,
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
    SDL_Color white = {200, 200, 200, 255};

    // HUD background
    SDL_Rect hud_bg = {0, 0, width_, HUD_HEIGHT};
    SDL_SetRenderDrawColor(renderer_, 15, 12, 18, 240);
    SDL_RenderFillRect(renderer_, &hud_bg);
    SDL_SetRenderDrawColor(renderer_, 60, 50, 70, 255);
    SDL_RenderDrawLine(renderer_, 0, HUD_HEIGHT, width_, HUD_HEIGHT);

    int bar_h = 14;
    int bar_y = (HUD_HEIGHT - bar_h) / 2;
    int cursor = 8; // running x position — everything flows left to right

    // Helper: draw a stat bar + label, advance cursor
    auto draw_bar = [&](const char* label, int val, int max_val,
                         SDL_Color bg_col, SDL_Color fill_col, int bar_w) {
        // Bar
        SDL_Rect bg = {cursor, bar_y, bar_w, bar_h};
        SDL_SetRenderDrawColor(renderer_, bg_col.r, bg_col.g, bg_col.b, 255);
        SDL_RenderFillRect(renderer_, &bg);
        int fill = (val * bar_w) / std::max(1, max_val);
        SDL_Rect fill_r = {cursor, bar_y, fill, bar_h};
        SDL_SetRenderDrawColor(renderer_, fill_col.r, fill_col.g, fill_col.b, 255);
        SDL_RenderFillRect(renderer_, &fill_r);
        cursor += bar_w + 4;

        // Label
        SDL_Surface* surf = TTF_RenderText_Blended(font_, label, white);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            SDL_Rect dst = {cursor, bar_y, surf->w, surf->h};
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            cursor += surf->w + 12;
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    };

    // HP bar
    char hp_text[32];
    snprintf(hp_text, sizeof(hp_text), "HP:%d/%d", stats.hp, stats.hp_max);
    SDL_Color hp_fill = stats.hp > stats.hp_max / 2 ? SDL_Color{140, 40, 40, 255}
                      : stats.hp > stats.hp_max / 4 ? SDL_Color{160, 100, 30, 255}
                                                      : SDL_Color{200, 50, 50, 255};
    draw_bar(hp_text, stats.hp, stats.hp_max, {40, 10, 10, 255}, hp_fill, 100);

    // MP bar (only if has MP)
    if (stats.mp_max > 0) {
        char mp_text[32];
        snprintf(mp_text, sizeof(mp_text), "MP:%d/%d", stats.mp, stats.mp_max);
        draw_bar(mp_text, stats.mp, stats.mp_max, {10, 10, 40, 255}, {60, 60, 160, 255}, 80);
    }

    // XP bar
    char lvl_text[32];
    snprintf(lvl_text, sizeof(lvl_text), "Lv%d", stats.level);
    draw_bar(lvl_text, stats.xp, stats.xp_next, {15, 15, 40, 255}, {80, 80, 180, 255}, 60);

    // Right side: god + location + gold + turn
    const char* god_name = "";
    if (world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        god_name = get_god_info(ga.god).name;
    }
    char info[128];
    if (dungeon_level_ <= 0) {
        snprintf(info, sizeof(info), "%s  Thornwall  Gold:%d  T:%d",
                 god_name, gold_, game_turn_);
    } else {
        snprintf(info, sizeof(info), "%s  Depth:%d  Gold:%d  T:%d",
                 god_name, dungeon_level_, gold_, game_turn_);
    }
    SDL_Surface* surf = TTF_RenderText_Blended(font_, info, white);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
        SDL_Rect dst = {width_ - surf->w - 8, bar_y - 1, surf->w, surf->h};
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}

void Engine::render() {
    // Main menu screen
    if (state_ == GameState::MAIN_MENU) {
        main_menu_.render(renderer_, font_, font_title_, width_, height_);
        SDL_RenderPresent(renderer_);
        return;
    }

    // Settings screen
    if (state_ == GameState::SETTINGS) {
        settings_.render(renderer_, font_, width_, height_);
        SDL_RenderPresent(renderer_);
        return;
    }

    // Character creation screen
    if (state_ == GameState::CREATING) {
        creation_screen_.render(renderer_, font_, font_title_, sprites_, width_, height_);
        SDL_RenderPresent(renderer_);
        return;
    }

    // Dark slate background — unexplored areas
    SDL_SetRenderDrawColor(renderer_, 18, 20, 28, 255);
    SDL_RenderClear(renderer_);

    Camera render_cam = camera_;

    // Draw map
    render::draw_map(renderer_, sprites_, map_, render_cam, HUD_HEIGHT);

    // Draw entities
    render::draw_entities(renderer_, sprites_, world_, map_, render_cam, HUD_HEIGHT);

    // HUD
    render_hud();

    // Message log
    log_.render(renderer_, font_, 0, height_ - LOG_HEIGHT, width_, LOG_HEIGHT);

    // Overlay screens
    inventory_screen_.render(renderer_, font_, sprites_, world_, width_, height_);
    spell_screen_.render(renderer_, font_, world_, width_, height_);

    // Pause menu overlay
    pause_menu_.render(renderer_, font_, font_title_, width_, height_);

    // Death overlay
    if (state_ == GameState::DEAD && font_) {
        // Darken screen
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_Rect overlay = {0, 0, width_, height_};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 150);
        SDL_RenderFillRect(renderer_, &overlay);

        SDL_Color red = {200, 50, 50, 255};
        TTF_Font* death_font = font_title_ ? font_title_ : font_;
        SDL_Surface* surf = TTF_RenderText_Blended(death_font, "You have died.", red);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            SDL_Rect dst = {width_ / 2 - surf->w / 2, height_ / 2 - surf->h / 2,
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
