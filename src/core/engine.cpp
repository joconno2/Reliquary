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
    if (font_title_large_ && font_title_large_ != font_title_) TTF_CloseFont(font_title_large_);
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
    if (font_title_large_ && font_title_large_ != font_title_) TTF_CloseFont(font_title_large_);
    if (font_title_ && font_title_ != font_) TTF_CloseFont(font_title_);
    if (font_) TTF_CloseFont(font_);
    font_ = nullptr;
    font_title_ = nullptr;
    font_title_large_ = nullptr;

    int body_size = static_cast<int>(12 * ui_scale_);
    int title_size = static_cast<int>(32 * ui_scale_);
    int title_large_size = static_cast<int>(96 * ui_scale_);

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

    font_title_large_ = TTF_OpenFont("assets/fonts/Jacquard12-Regular.ttf", title_large_size);
    if (!font_title_large_) {
        font_title_large_ = font_title_;
    }
}

void Engine::do_save() {
    SaveData data;
    data.dungeon_level = dungeon_level_;
    data.game_turn = game_turn_;
    data.gold = gold_;
    data.journal = journal_;
    data.rng_seed = rng_.get_seed();

    if (save::save_game(save::default_path(), data, world_, player_, map_)) {
        log_.add("Game saved.", {100, 200, 100, 255});
    } else {
        log_.add("Failed to save.", {200, 100, 100, 255});
    }
}

void Engine::do_load() {
    World new_world;
    TileMap new_map;
    auto data = save::load_game(save::default_path(), new_world, new_map);

    if (!data.valid) {
        log_.add("No save file found.", {180, 140, 120, 255});
        return;
    }

    world_ = std::move(new_world);
    map_ = std::move(new_map);
    dungeon_level_ = data.dungeon_level;
    game_turn_ = data.game_turn;
    gold_ = data.gold;
    journal_ = data.journal;
    rng_.reseed(data.rng_seed);

    // Find the player entity
    player_ = NULL_ENTITY;
    auto& players = world_.pool<Player>();
    if (players.size() > 0) {
        player_ = players.entity_at(0);
    }

    if (player_ != NULL_ENTITY && world_.has<Position>(player_) && world_.has<Stats>(player_)) {
        auto& pos = world_.get<Position>(player_);
        auto& stats = world_.get<Stats>(player_);
        fov::compute(map_, pos.x, pos.y, stats.fov_radius());
        camera_.center_on(pos.x, pos.y);
    }

    state_ = GameState::PLAYING;
    pause_menu_.close();
    log_.add("Game loaded.", {100, 200, 100, 255});
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
        // Overworld — loaded from hand-authored map file
        auto mresult = mapfile::load("data/maps/overworld.map");
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
                    sx = 0; sy = 1;
                    break;
                case 'E':
                    npc_comp.role = NPCRole::ELDER;
                    npc_comp.name = "Elder Maren";
                    npc_comp.dialogue = "A wight has risen in the barrow east of here. "
                                        "People have died. Will you put it down?";
                    npc_comp.quest_id = static_cast<int>(QuestId::MQ_BARROW_WIGHT);
                    sx = 4; sy = 6; // elderly sprite
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
        // Dungeon zones — each zone spans 3 depths
        struct ZoneTheme {
            TileType wall, floor;
            const char* name;
            int max_depth; // deepest level in this zone
        };
        static const ZoneTheme ZONES[] = {
            {TileType::WALL_DIRT,        TileType::FLOOR_DIRT,      "The Warrens",       3},
            {TileType::WALL_STONE_ROUGH, TileType::FLOOR_STONE,     "Stonekeep",         6},
            {TileType::WALL_STONE_BRICK, TileType::FLOOR_STONE,     "The Deep Halls",    9},
            {TileType::WALL_CATACOMB,    TileType::FLOOR_BONE,      "The Catacombs",    12},
            {TileType::WALL_IGNEOUS,     TileType::FLOOR_RED_STONE, "The Molten Depths",15},
            {TileType::WALL_LARGE_STONE, TileType::FLOOR_STONE,     "The Sunken Halls", 18},
        };
        constexpr int ZONE_COUNT = sizeof(ZONES) / sizeof(ZONES[0]);
        // Map depth to zone: depths 1-3 = zone 0, 4-6 = zone 1, etc.
        int zone_idx = std::min((dungeon_level_ - 1) / 3, ZONE_COUNT - 1);
        auto& zone = ZONES[zone_idx];

        // Don't place stairs down at the bottom of a zone
        bool at_zone_bottom = (dungeon_level_ >= zone.max_depth);

        DungeonParams params;
        params.width = 80;
        params.height = 50;
        params.max_rooms = 12 + dungeon_level_;
        params.wall_type = zone.wall;
        params.floor_type = zone.floor;

        auto result = dungeon::generate(rng_, params, !at_zone_bottom);
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
        int idx = std::min((dungeon_level_ - 1) / 3, MSG_COUNT - 1);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s — Depth %d", ZONE_NAMES[idx], dungeon_level_);
        log_.add(buf, {180, 170, 160, 255});
        log_.add(ZONE_MESSAGES[idx], {120, 110, 100, 255});

        // Zone depth limit message
        static const int ZONE_MAX_DEPTHS[] = {3, 6, 9, 12, 15, 18};
        if (dungeon_level_ == ZONE_MAX_DEPTHS[idx]) {
            log_.add("You've reached the bottom of this place. There is nothing deeper here.",
                     {180, 160, 120, 255});
        }
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

            // Shopkeeper — open shop screen
            if (npc.role == NPCRole::SHOPKEEPER) {
                shop_screen_.open(player_, world_, rng_, &gold_);
                return;
            }

            char buf[256];
            snprintf(buf, sizeof(buf), "%s: \"%s\"", npc.name.c_str(), npc.dialogue.c_str());
            log_.add(buf, {180, 180, 140, 255});

            // Quest giving
            if (npc.quest_id >= 0) {
                auto qid = static_cast<QuestId>(npc.quest_id);
                if (!journal_.has_quest(qid)) {
                    // Show quest offer modal
                    quest_offer_.show(qid, npc.name);
                    pending_quest_npc_ = target;
                } else if (journal_.get_state(qid) == QuestState::COMPLETE) {
                    journal_.set_state(qid, QuestState::FINISHED);
                    auto& qinfo = get_quest_info(qid);
                    char qbuf[128];
                    snprintf(qbuf, sizeof(qbuf), "Quest complete: %s (+%dxp, +%dgold)",
                             qinfo.name, qinfo.xp_reward, qinfo.gold_reward);
                    log_.add(qbuf, {120, 220, 120, 255});
                    gold_ += qinfo.gold_reward;
                    if (world_.has<Stats>(player_) && qinfo.xp_reward > 0) {
                        if (world_.get<Stats>(player_).grant_xp(qinfo.xp_reward)) {
                            pending_levelup_ = true;
                            levelup_screen_.open(player_, rng_);
                        }
                    }
                    // Chain main quests
                    if (qid == QuestId::MQ_BARROW_WIGHT) {
                        npc.dialogue = "Something stirred when it fell. The scholar may know more.";
                    }
                }
            }
            return;
        }
        // Hostile — attack
        int level_before = world_.has<Stats>(player_) ? world_.get<Stats>(player_).level : 0;
        combat::melee_attack(world_, player_, target, rng_, log_);
        player_acted_ = true;
        // Check for level-up
        if (world_.has<Stats>(player_) && world_.get<Stats>(player_).level > level_before) {
            pending_levelup_ = true;
            levelup_screen_.open(player_, rng_);
        }
        return;
    }

    if (!map_.is_walkable(nx, ny)) return;

    // Mirror sprite when moving left (sprites face right by default)
    if (world_.has<Renderable>(player_)) {
        if (nx > pos.x) world_.get<Renderable>(player_).flip_h = true;
        else if (nx < pos.x) world_.get<Renderable>(player_).flip_h = false;
    }

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

    // Energy system: give energy, then each entity acts once if they can
    auto& energy_pool = world_.pool<Energy>();
    for (size_t i = 0; i < energy_pool.size(); i++) {
        energy_pool.at_index(i).gain();
    }

    // Player spends energy
    if (world_.has<Energy>(player_)) {
        world_.get<Energy>(player_).spend();
    }

    // Process AI — each monster acts at most once per player turn
    ai::process(world_, map_, player_, rng_);

    // Monsters adjacent to player attack (once each)
    auto& ai_pool = world_.pool<AI>();
    for (size_t i = 0; i < ai_pool.size(); i++) {
        Entity e = ai_pool.entity_at(i);
        if (!world_.has<Position>(e) || !world_.has<Stats>(e)) continue;
        if (!world_.has<Energy>(e) || !world_.get<Energy>(e).can_act()) continue;

        auto& mpos = world_.get<Position>(e);
        auto& ppos = world_.get<Position>(player_);

        int dx = std::abs(mpos.x - ppos.x);
        int dy = std::abs(mpos.y - ppos.y);

        if (dx <= 1 && dy <= 1 && (dx + dy > 0)) {
            auto& ai_comp = ai_pool.at_index(i);
            if (ai_comp.state == AIState::HUNTING) {
                combat::melee_attack(world_, e, player_, rng_, log_);

                if (world_.has<Stats>(player_) && world_.get<Stats>(player_).hp <= 0) {
                    state_ = GameState::DEAD;
                    return;
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

void Engine::try_rest() {
    if (!world_.has<Stats>(player_)) return;
    auto& stats = world_.get<Stats>(player_);

    // Can't rest at full HP and MP
    if (stats.hp >= stats.hp_max && stats.mp >= stats.mp_max) {
        log_.add("You don't need to rest.", {150, 140, 130, 255});
        return;
    }

    // Can't rest if enemies are visible (check FOV for any AI entities)
    auto& ai_pool = world_.pool<AI>();
    for (size_t i = 0; i < ai_pool.size(); i++) {
        Entity e = ai_pool.entity_at(i);
        if (!world_.has<Position>(e)) continue;
        auto& mpos = world_.get<Position>(e);
        if (map_.in_bounds(mpos.x, mpos.y) && map_.at(mpos.x, mpos.y).visible) {
            log_.add("You can't rest with enemies nearby.", {180, 120, 120, 255});
            return;
        }
    }

    // In dungeon: 30% chance of interruption
    if (dungeon_level_ > 0 && rng_.chance(30)) {
        // Spawn a wandering monster nearby
        auto& ppos = world_.get<Position>(player_);
        // Try to place monster within 3 tiles
        for (int attempt = 0; attempt < 20; attempt++) {
            int mx = ppos.x + rng_.range(-3, 3);
            int my = ppos.y + rng_.range(-3, 3);
            if (mx == ppos.x && my == ppos.y) continue;
            if (!map_.in_bounds(mx, my) || !map_.is_walkable(mx, my)) continue;
            // Check no entity there
            if (combat::entity_at(world_, mx, my, player_) != NULL_ENTITY) continue;

            // Spawn a simple monster appropriate for depth
            Entity mob = world_.create();
            world_.add<Position>(mob, {mx, my});

            // Pick a random monster from the depth-appropriate range
            int max_idx = std::min(17, 4 + dungeon_level_ * 2);
            int idx = rng_.range(0, max_idx);

            // Simple rat stats as fallback — the real spawn uses the table in populate
            // but we just need one quick monster here
            Stats ms;
            ms.name = "something";
            ms.hp = 8; ms.hp_max = 8;
            ms.base_damage = 2;
            ms.xp_value = 10;

            // Use a generic hostile sprite
            world_.add<Renderable>(mob, {SHEET_MONSTERS, 11, 6, {255, 255, 255, 255}, 5});
            world_.add<AI>(mob, {AIState::HUNTING, ppos.x, ppos.y, 0, 20});
            world_.add<Energy>(mob, {0, 100});

            // Scale with depth
            float hp_scale = 1.0f + dungeon_level_ * 0.15f;
            float dmg_scale = 1.0f + dungeon_level_ * 0.1f;
            ms.hp = static_cast<int>(ms.hp * hp_scale);
            ms.hp_max = ms.hp;
            ms.base_damage = static_cast<int>(ms.base_damage * dmg_scale);

            (void)idx; // we used generic stats; could index table if populate.h exposed it
            world_.add<Stats>(mob, std::move(ms));

            log_.add("Your rest is interrupted!", {255, 120, 80, 255});
            player_acted_ = true;
            return;
        }
    }

    // Rest succeeds — restore 25% of max HP and MP
    int hp_restore = std::max(1, stats.hp_max / 4);
    int mp_restore = std::max(1, stats.mp_max / 4);
    int hp_actual = std::min(hp_restore, stats.hp_max - stats.hp);
    int mp_actual = (stats.mp_max > 0) ? std::min(mp_restore, stats.mp_max - stats.mp) : 0;
    stats.hp += hp_actual;
    stats.mp += mp_actual;

    // Costs 10 turns
    game_turn_ += 10;

    char buf[128];
    snprintf(buf, sizeof(buf), "You rest for a while. (+%d HP, +%d MP)", hp_actual, mp_actual);
    log_.add(buf, {100, 200, 100, 255});
    player_acted_ = true;
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
                case MenuChoice::LOAD:
                    do_load();
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
                        do_save();
                        pause_menu_.close();
                        break;
                    case PauseChoice::LOAD:
                        do_load();
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

            // Level-up screen intercepts all input
            if (levelup_screen_.is_open()) {
                LevelUpAction act = levelup_screen_.handle_input(event);
                if (act == LevelUpAction::CHOSEN) {
                    levelup_screen_.apply_choice(world_);
                    levelup_screen_.close();
                    pending_levelup_ = false;
                    if (world_.has<Stats>(player_)) {
                        auto& stats = world_.get<Stats>(player_);
                        char buf[64];
                        snprintf(buf, sizeof(buf), "You feel stronger. (Level %d)", stats.level);
                        log_.add(buf, {255, 220, 100, 255});
                    }
                }
                return;
            }

            // Shop screen intercepts input
            if (shop_screen_.is_open()) {
                ShopAction act = shop_screen_.handle_input(event);
                if (act == ShopAction::CLOSE) {
                    shop_screen_.close();
                } else if (act == ShopAction::BUY) {
                    if (shop_screen_.execute(world_, &gold_)) {
                        log_.add("Purchased.", {180, 200, 140, 255});
                    } else {
                        log_.add("You can't buy that.", {180, 120, 120, 255});
                    }
                } else if (act == ShopAction::SELL) {
                    if (shop_screen_.execute(world_, &gold_)) {
                        log_.add("Sold.", {180, 200, 140, 255});
                    }
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

            // Quest offer modal intercepts everything
            if (quest_offer_.is_open()) {
                auto choice = quest_offer_.handle_input(event);
                if (choice == QuestOfferChoice::ACCEPT) {
                    auto qid = quest_offer_.get_quest_id();
                    journal_.add_quest(qid);
                    auto& qinfo = get_quest_info(qid);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Quest accepted: %s", qinfo.name);
                    log_.add(buf, {220, 200, 100, 255});
                    quest_offer_.close();
                } else if (choice == QuestOfferChoice::DECLINE) {
                    log_.add("You decline the quest.", {140, 130, 120, 255});
                    quest_offer_.close();
                }
                return;
            }

            // Quest log intercepts input
            if (quest_log_.is_open()) {
                quest_log_.handle_input(event);
                return;
            }

            // Character sheet intercepts input
            if (char_sheet_.is_open()) {
                char_sheet_.handle_input(event);
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

            // Dead state — any key returns to main menu
            if (state_ == GameState::DEAD) {
                // Reset game state for new game
                player_ = NULL_ENTITY;
                dungeon_level_ = -1;
                game_turn_ = 0;
                gold_ = 0;
                journal_ = {};
                // Clear all entities
                world_ = World();
                state_ = GameState::MAIN_MENU;
                main_menu_.set_can_continue(false);
                log_ = MessageLog();
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

                // Character sheet
                case SDLK_c:
                    char_sheet_.open(player_);
                    break;

                // Rest
                case SDLK_r:
                    try_rest();
                    break;

                // Quest log
                case SDLK_q:
                    quest_log_.open();
                    break;

                // Stairs — Enter on any stairs, > to descend, < to ascend
                case SDLK_GREATER:
                case SDLK_LESS:
                case SDLK_RETURN: {
                    auto& pos = world_.get<Position>(player_);
                    auto tile_type = map_.at(pos.x, pos.y).type;
                    if (tile_type == TileType::STAIRS_DOWN &&
                        event.key.keysym.sym != SDLK_LESS) {
                        generate_level();
                    } else if (tile_type == TileType::STAIRS_UP &&
                               event.key.keysym.sym != SDLK_GREATER) {
                        if (dungeon_level_ > 0) {
                            // Go back to overworld (level 0)
                            dungeon_level_ = -1; // will increment to 0
                            generate_level();
                        } else {
                            log_.add("You're already on the surface.", {150, 140, 130, 255});
                        }
                    } else if (event.key.keysym.sym != SDLK_RETURN) {
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
        main_menu_.render(renderer_, font_, font_title_, font_title_large_, sprites_, width_, height_);
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
    char_sheet_.render(renderer_, font_, font_title_, sprites_, world_, width_, height_);
    quest_log_.render(renderer_, font_, font_title_, journal_, width_, height_);
    quest_offer_.render(renderer_, font_, font_title_, width_, height_);
    levelup_screen_.render(renderer_, font_, width_, height_);
    shop_screen_.render(renderer_, font_, sprites_, world_, width_, height_);

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
