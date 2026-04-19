#include "core/engine.h"
#include "ui/ui_draw.h"
#include "systems/god_system.h"
#include "systems/npc_interaction.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/player.h"
#include "components/blocker.h"
#include "components/stats.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/corpse.h"
#include "data/world_data.h"
#include "components/item.h"
#include "components/inventory.h"
#include "components/god.h"
#include "components/class_def.h"
#include "components/spellbook.h"
#include "components/npc.h"
#include "components/sign.h"
#include "components/prayer.h"
#include "components/status_effect.h"
#include "components/container.h"
#include "components/buff.h"
#include "components/disease.h"
#include "components/pet.h"
#include "components/quest_target.h"
#include "components/death_anim.h"
#include "components/dynamic_quest.h"
#include "ui/ui_draw.h"
#include "ui/death_screen.h"
#include "systems/magic.h"
#include "systems/status.h"
#include "generation/quest_gen.h"
#include "components/background.h"
#include "components/traits.h"
#include "components/passive_tree.h"
#include "components/trap.h"
#include "components/skills.h"
#include "systems/fov.h"
#include "systems/combat.h"
#include "systems/ai.h"
#include "generation/dungeon.h"
#include "generation/populate.h"
#include "generation/overworld.h"
#include "generation/player_setup.h"
#include <SDL2/SDL_image.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cmath>
#include <algorithm>

Engine::Engine() {}

Engine::~Engine() {
    audio_.shutdown();
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
    fprintf(stderr, "[init] SDL_Init...\n"); fflush(stderr);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        fflush(stderr);
        return false;
    }

    fprintf(stderr, "[init] IMG_Init...\n"); fflush(stderr);
    if (IMG_Init(IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init error: %s\n", IMG_GetError());
        fflush(stderr);
        return false;
    }

    fprintf(stderr, "[init] TTF_Init...\n"); fflush(stderr);
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
        fflush(stderr);
        return false;
    }

    // Get display resolution for fullscreen default
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        width_ = dm.w;
        height_ = dm.h;
        fprintf(stderr, "[init] Display: %dx%d\n", width_, height_); fflush(stderr);
    }

    fprintf(stderr, "[init] Creating window...\n"); fflush(stderr);
    window_ = SDL_CreateWindow(
        "Reliquary",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width_, height_,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN_DESKTOP
    );
    fullscreen_ = true;
    if (!window_) {
        fprintf(stderr, "Window creation error: %s\n", SDL_GetError());
        fflush(stderr);
        return false;
    }

    fprintf(stderr, "[init] Creating renderer...\n"); fflush(stderr);
    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        fprintf(stderr, "Renderer creation error: %s\n", SDL_GetError());
        fflush(stderr);
        return false;
    }

    fprintf(stderr, "[init] Loading spritesheets...\n"); fflush(stderr);
    if (!sprites_.load_all(renderer_, "assets/32rogues")) {
        fprintf(stderr, "Failed to load spritesheets from assets/32rogues\n");
        fflush(stderr);
        return false;
    }
    fprintf(stderr, "[init] Spritesheets loaded.\n"); fflush(stderr);

    // Compute UI scale from resolution — base reference is 1080p
    // At 1440p this gives ~1.33, at 2160p ~2.0, at 800p ~0.74
    ui_scale_ = static_cast<float>(height_) / 1080.0f;
    if (ui_scale_ < 0.75f) ui_scale_ = 0.75f;
    if (ui_scale_ > 3.0f) ui_scale_ = 3.0f;

    // Scale UI element sizes
    LOG_HEIGHT = static_cast<int>(180 * ui_scale_);
    HUD_HEIGHT = static_cast<int>(32 * ui_scale_);

    // Load fonts at computed UI scale
    reload_fonts();

    camera_.viewport_w = width_;
    camera_.viewport_h = height_ - LOG_HEIGHT - HUD_HEIGHT;
    camera_.tile_size = static_cast<int>(60 * ui_scale_); // scale tiles with resolution (4x base)

    creation_screen_.reset();

    // Load dungeon registry
    {
        std::ifstream f("data/dungeons.json");
        if (f.is_open()) {
            auto j = nlohmann::json::parse(f, nullptr, false);
            if (!j.is_discarded() && j.is_array()) {
                for (auto& entry : j) {
                    DungeonEntry de;
                    de.name = entry.value("name", "");
                    de.x = entry.value("x", 0);
                    de.y = entry.value("y", 0);
                    de.zone = entry.value("zone", "warrens");
                    if (entry.contains("quest") && !entry["quest"].is_null())
                        de.quest = entry["quest"].get<std::string>();
                    de.patron_god_idx = entry.value("patron_god_idx", -1);
                    dungeon_registry_.push_back(std::move(de));
                }
            }
        }
        // Calculate zone difficulty based on distance from Thornwall (1000, 750)
        constexpr int START_X = 1000, START_Y = 750;
        float max_dist = 0;
        for (auto& de : dungeon_registry_) {
            float d = std::sqrt(static_cast<float>((de.x - START_X) * (de.x - START_X) +
                                                     (de.y - START_Y) * (de.y - START_Y)));
            if (d > max_dist) max_dist = d;
        }
        if (max_dist > 0) {
            for (auto& de : dungeon_registry_) {
                float d = std::sqrt(static_cast<float>((de.x - START_X) * (de.x - START_X) +
                                                         (de.y - START_Y) * (de.y - START_Y)));
                de.zone_difficulty = static_cast<int>(d / max_dist * 8.0f);
                if (de.zone_difficulty > 8) de.zone_difficulty = 8;
                // Dungeon depth scales with difficulty: 3 floors near start, up to 10 far out
                de.max_depth = 3 + de.zone_difficulty;
                // Named quest dungeons get a minimum depth based on zone
                if (de.zone == "sepulchre") { de.max_depth = 13; de.zone_difficulty = 8; }
                else if (!de.quest.empty() && de.max_depth < 4) de.max_depth = 4;
            }
        }
    }

    audio_.init();
    keybinds_.load("save/keybinds.json");
    settings_.set_audio(&audio_);
    settings_.set_keybinds(&keybinds_);
    meta_ = meta::load();

    // Title screen music + ambients — slow fade in
    audio_.play_music(MusicId::TITLE, 3000);
    audio_.play_ambient(AmbientId::FIRE_CRACKLE, 4000);
    audio_.play_ambient(AmbientId::FOREST_NIGHT_RAIN, 5000);
    return true;
}

void Engine::reload_fonts() {
    if (font_title_large_ && font_title_large_ != font_title_) TTF_CloseFont(font_title_large_);
    if (font_title_ && font_title_ != font_) TTF_CloseFont(font_title_);
    if (font_) TTF_CloseFont(font_);
    font_ = nullptr;
    font_title_ = nullptr;
    font_title_large_ = nullptr;

    int body_size = static_cast<int>(15 * ui_scale_);
    int title_size = static_cast<int>(38 * ui_scale_);
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
    // Cache current floor before saving so it's in the floor_cache_
    if (dungeon_level_ > 0) cache_current_floor();

    SaveData data;
    data.dungeon_level = dungeon_level_;
    data.game_turn = game_turn_;
    data.gold = gold_;
    data.journal = journal_;
    data.rng_seed = rng_.get_seed();
    data.overworld_return_x = overworld_return_x_;
    data.overworld_return_y = overworld_return_y_;
    data.hardcore = hardcore_;
    data.traits = build_traits_;
    data.current_dungeon_idx = current_dungeon_idx_;
    data.visited_towns = visited_towns_;
    data.background_id = static_cast<int>(background_);
    data.run_kills = run_kills_;
    data.run_gold_earned = run_gold_earned_;
    data.run_deepest = run_deepest_;

    if (save::save_game(save::default_path(), data, world_, player_, map_)) {
        // Save floor cache as separate file
        save_floor_cache("save/floors.dat");
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
    overworld_return_x_ = data.overworld_return_x;
    overworld_return_y_ = data.overworld_return_y;
    hardcore_ = data.hardcore;
    build_traits_ = data.traits;
    current_dungeon_idx_ = data.current_dungeon_idx;
    visited_towns_ = data.visited_towns;
    background_ = static_cast<BackgroundId>(data.background_id);
    run_kills_ = data.run_kills;
    run_gold_earned_ = data.run_gold_earned;
    run_deepest_ = data.run_deepest;
    floor_cache_.clear();
    load_floor_cache("save/floors.dat"); // restore all cached dungeon floors

    // Hardcore: one-shot load — delete save after loading
    if (hardcore_) {
        std::filesystem::remove(save::default_path());
    }

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

    // Re-create pet visual if a pet item is equipped
    pet_entity_ = NULL_ENTITY;
    if (player_ != NULL_ENTITY && world_.has<Inventory>(player_)) {
        auto& inv = world_.get<Inventory>(player_);
        Entity pet_item = inv.get_equipped(EquipSlot::PET);
        if (pet_item != NULL_ENTITY && world_.has<Item>(pet_item)) {
            int pid = world_.get<Item>(pet_item).pet_id;
            if (pid >= 0) spawn_pet_visual(pid);
        }
    }

    state_ = GameState::PLAYING;
    pause_menu_.close();
    log_.add("Game loaded.", {100, 200, 100, 255});
}

void Engine::cache_current_floor() {
    if (dungeon_level_ < 1) return; // don't cache overworld
    FloorState& fs = floor_cache_[dungeon_level_];
    fs.map = map_; // copy tilemap (preserves explored state)
    fs.rooms = rooms_;

    // Save player position on this floor
    if (world_.has<Position>(player_)) {
        auto& pp = world_.get<Position>(player_);
        fs.player_x = pp.x;
        fs.player_y = pp.y;
    }

    // Serialize all non-player entities with Position
    fs.entities.clear();
    auto& positions = world_.pool<Position>();
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (e == player_ || e == pet_entity_) continue;
        // Skip friendly summons (they travel with the player)
        if (world_.has<AI>(e) && world_.get<AI>(e).friendly) continue;
        if (!world_.has<Renderable>(e)) continue;

        CachedEntity ce;
        auto& pos = positions.at_index(i);
        ce.x = pos.x; ce.y = pos.y;
        auto& rend = world_.get<Renderable>(e);
        ce.sheet = rend.sprite_sheet; ce.sprite_x = rend.sprite_x; ce.sprite_y = rend.sprite_y;
        ce.tint_r = rend.tint.r; ce.tint_g = rend.tint.g; ce.tint_b = rend.tint.b; ce.tint_a = rend.tint.a;
        ce.z_order = rend.z_order; ce.flip_h = rend.flip_h;

        if (world_.has<Stats>(e)) {
            ce.has_stats = true;
            ce.stats = world_.get<Stats>(e);
        }
        if (world_.has<AI>(e)) {
            ce.has_ai = true;
            ce.ai = world_.get<AI>(e);
        }
        if (world_.has<Energy>(e)) {
            auto& en = world_.get<Energy>(e);
            ce.energy_current = en.current; ce.energy_speed = en.speed;
        }
        if (world_.has<StatusEffects>(e)) {
            ce.has_status = true;
            ce.status_fx = world_.get<StatusEffects>(e);
        }
        if (world_.has<GodAlignment>(e)) {
            ce.has_god = true;
            ce.god_align = world_.get<GodAlignment>(e);
        }
        if (world_.has<Item>(e)) {
            ce.has_item = true;
            ce.item = world_.get<Item>(e);
        }
        if (world_.has<Container>(e)) {
            ce.has_container = true;
            ce.container = world_.get<Container>(e);
        }
        if (world_.has<Trap>(e)) {
            ce.has_trap = true;
            ce.trap = world_.get<Trap>(e);
        }
        fs.entities.push_back(std::move(ce));
    }

    // Also cache trap entities that don't have Renderable (unrevealed traps)
    auto& trap_pool = world_.pool<Trap>();
    for (size_t ti = 0; ti < trap_pool.size(); ti++) {
        Entity te = trap_pool.entity_at(ti);
        if (!world_.has<Position>(te)) continue;
        if (world_.has<Renderable>(te)) continue; // already cached above
        auto& tpos = world_.get<Position>(te);
        CachedEntity ce;
        ce.x = tpos.x; ce.y = tpos.y;
        ce.sheet = 0; ce.sprite_x = 0; ce.sprite_y = 0;
        ce.tint_r = 0; ce.tint_g = 0; ce.tint_b = 0; ce.tint_a = 0;
        ce.z_order = -10;
        ce.flip_h = false;
        ce.has_trap = true;
        ce.trap = trap_pool.at_index(ti);
        fs.entities.push_back(std::move(ce));
    }
}

bool Engine::restore_floor(int level, bool ascending) {
    auto it = floor_cache_.find(level);
    if (it == floor_cache_.end()) return false;

    auto& fs = it->second;
    map_ = fs.map; // restore map with explored tiles preserved
    rooms_ = fs.rooms;

    // Recreate all cached entities
    for (auto& ce : fs.entities) {
        Entity e = world_.create();
        world_.add<Position>(e, {ce.x, ce.y});
        world_.add<Renderable>(e, {ce.sheet, ce.sprite_x, ce.sprite_y,
                                    {ce.tint_r, ce.tint_g, ce.tint_b, ce.tint_a},
                                    ce.z_order, ce.flip_h});
        if (ce.has_stats) world_.add<Stats>(e, ce.stats);
        if (ce.has_ai) world_.add<AI>(e, ce.ai);
        if (ce.has_stats || ce.has_ai) // entities with stats get energy
            world_.add<Energy>(e, {ce.energy_current, ce.energy_speed});
        if (ce.has_status) world_.add<StatusEffects>(e, ce.status_fx);
        if (ce.has_god) world_.add<GodAlignment>(e, ce.god_align);
        if (ce.has_item) world_.add<Item>(e, ce.item);
        if (ce.has_container) world_.add<Container>(e, ce.container);
        if (ce.has_trap) {
            world_.add<Trap>(e, ce.trap);
            // Unrevealed traps shouldn't have a Renderable; remove the dummy one
            if (!ce.trap.revealed && world_.has<Renderable>(e)) {
                world_.remove<Renderable>(e);
            }
        }
    }

    // Place player at appropriate stairs
    if (world_.has<Position>(player_)) {
        auto& pp = world_.get<Position>(player_);
        TileType target_tile = ascending ? TileType::STAIRS_DOWN : TileType::STAIRS_UP;
        bool found_stairs = false;
        // Search the map for the target stair tile
        for (int sy = 0; sy < map_.height() && !found_stairs; sy++) {
            for (int sx = 0; sx < map_.width() && !found_stairs; sx++) {
                if (map_.at(sx, sy).type == target_tile) {
                    pp.x = sx;
                    pp.y = sy;
                    found_stairs = true;
                }
            }
        }
        // Fallback to cached position if no stairs found
        if (!found_stairs) {
            pp.x = fs.player_x;
            pp.y = fs.player_y;
        }
    }

    return true;
}

void Engine::save_floor_cache(const std::string& path) {
    nlohmann::json root = nlohmann::json::array();
    for (auto& [level, fs] : floor_cache_) {
        nlohmann::json fj;
        fj["level"] = level;
        fj["player_x"] = fs.player_x;
        fj["player_y"] = fs.player_y;
        // Map tiles
        int w = fs.map.width(), h = fs.map.height();
        fj["map_w"] = w; fj["map_h"] = h;
        std::string types, variants, explored;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                auto& t = fs.map.at(x, y);
                types += static_cast<char>(static_cast<int>(t.type) + 32);
                variants += static_cast<char>(t.variant + 32);
                explored += t.explored ? '1' : '0';
            }
        }
        fj["types"] = types; fj["variants"] = variants; fj["explored"] = explored;
        // Entities
        nlohmann::json ents = nlohmann::json::array();
        for (auto& ce : fs.entities) {
            nlohmann::json ej;
            ej["x"] = ce.x; ej["y"] = ce.y;
            ej["sheet"] = ce.sheet; ej["sx"] = ce.sprite_x; ej["sy"] = ce.sprite_y;
            ej["tint"] = {ce.tint_r, ce.tint_g, ce.tint_b, ce.tint_a};
            ej["z"] = ce.z_order; ej["flip"] = ce.flip_h;
            if (ce.has_stats) {
                ej["sname"] = ce.stats.name; ej["shp"] = ce.stats.hp; ej["shpm"] = ce.stats.hp_max;
                ej["sdmg"] = ce.stats.base_damage; ej["sarm"] = ce.stats.natural_armor;
                ej["sspd"] = ce.stats.base_speed; ej["sxp"] = ce.stats.xp_value;
                nlohmann::json attrs = nlohmann::json::array();
                for (int a = 0; a < ATTR_COUNT; a++) attrs.push_back(ce.stats.attributes[a]);
                ej["sattr"] = attrs;
            }
            if (ce.has_ai) {
                ej["ai"] = static_cast<int>(ce.ai.state);
                ej["ai_rr"] = ce.ai.ranged_range; ej["ai_rd"] = ce.ai.ranged_damage;
                ej["ai_ft"] = ce.ai.flee_threshold; ej["ai_fg"] = ce.ai.forget_player;
            }
            if (ce.has_stats || ce.has_ai) { ej["ec"] = ce.energy_current; ej["es"] = ce.energy_speed; }
            if (ce.has_item) {
                ej["iname"] = ce.item.name; ej["itype"] = static_cast<int>(ce.item.type);
                ej["islot"] = static_cast<int>(ce.item.slot);
                ej["idmg"] = ce.item.damage_bonus; ej["iarm"] = ce.item.armor_bonus;
                ej["iatk"] = ce.item.attack_bonus; ej["idodge"] = ce.item.dodge_bonus;
                ej["iheal"] = ce.item.heal_amount; ej["igold"] = ce.item.gold_value;
                ej["imat"] = static_cast<int>(ce.item.material); ej["itags"] = ce.item.tags;
                ej["iid"] = ce.item.identified; ej["icurse"] = ce.item.curse_state;
            }
            if (ce.has_god) { ej["god"] = static_cast<int>(ce.god_align.god); ej["gfav"] = ce.god_align.favor; }
            if (ce.has_container) {
                ej["cont_open"] = ce.container.opened;
                ej["cont_osx"] = ce.container.open_sprite_x;
                ej["cont_osy"] = ce.container.open_sprite_y;
            }
            ents.push_back(ej);
        }
        fj["entities"] = ents;
        root.push_back(fj);
    }
    std::ofstream f(path);
    if (f.is_open()) f << root.dump();
}

void Engine::load_floor_cache(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    nlohmann::json root;
    try { root = nlohmann::json::parse(f); } catch (...) { return; }
    if (!root.is_array()) return;

    floor_cache_.clear();
    for (auto& fj : root) {
        int level = fj.value("level", -1);
        if (level < 0) continue;
        FloorState& fs = floor_cache_[level];
        fs.player_x = fj.value("player_x", 0);
        fs.player_y = fj.value("player_y", 0);
        // Map
        int w = fj.value("map_w", 0), h = fj.value("map_h", 0);
        if (w > 0 && h > 0) {
            fs.map = TileMap(w, h);
            std::string types = fj.value("types", "");
            std::string variants = fj.value("variants", "");
            std::string explored = fj.value("explored", "");
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int idx = y * w + x;
                    if (idx < static_cast<int>(types.size())) {
                        auto& t = fs.map.at(x, y);
                        t.type = static_cast<TileType>(static_cast<int>(types[idx]) - 32);
                        if (idx < static_cast<int>(variants.size())) t.variant = static_cast<uint8_t>(variants[idx] - 32);
                        if (idx < static_cast<int>(explored.size())) t.explored = (explored[idx] == '1');
                    }
                }
            }
        }
        // Entities
        if (fj.contains("entities")) {
            for (auto& ej : fj["entities"]) {
                CachedEntity ce;
                ce.x = ej.value("x", 0); ce.y = ej.value("y", 0);
                ce.sheet = ej.value("sheet", 0); ce.sprite_x = ej.value("sx", 0); ce.sprite_y = ej.value("sy", 0);
                if (ej.contains("tint")) {
                    auto& t = ej["tint"];
                    ce.tint_r = t[0].get<uint8_t>(); ce.tint_g = t[1].get<uint8_t>();
                    ce.tint_b = t[2].get<uint8_t>(); ce.tint_a = t[3].get<uint8_t>();
                } else { ce.tint_r = ce.tint_g = ce.tint_b = ce.tint_a = 255; }
                ce.z_order = ej.value("z", 0); ce.flip_h = ej.value("flip", false);
                if (ej.contains("sname")) {
                    ce.has_stats = true;
                    ce.stats.name = ej.value("sname", ""); ce.stats.hp = ej.value("shp", 1);
                    ce.stats.hp_max = ej.value("shpm", 1); ce.stats.base_damage = ej.value("sdmg", 1);
                    ce.stats.natural_armor = ej.value("sarm", 0); ce.stats.base_speed = ej.value("sspd", 100);
                    ce.stats.xp_value = ej.value("sxp", 0);
                    if (ej.contains("sattr")) {
                        int ai = 0;
                        for (auto& a : ej["sattr"]) { if (ai < ATTR_COUNT) ce.stats.attributes[ai++] = a.get<int>(); }
                    }
                }
                if (ej.contains("ai")) {
                    ce.has_ai = true;
                    ce.ai.state = static_cast<AIState>(ej.value("ai", 0));
                    ce.ai.ranged_range = ej.value("ai_rr", 0); ce.ai.ranged_damage = ej.value("ai_rd", 0);
                    ce.ai.flee_threshold = ej.value("ai_ft", 20); ce.ai.forget_player = ej.value("ai_fg", false);
                }
                ce.energy_current = ej.value("ec", 0); ce.energy_speed = ej.value("es", 100);
                if (ej.contains("iname")) {
                    ce.has_item = true;
                    ce.item.name = ej.value("iname", ""); ce.item.type = static_cast<ItemType>(ej.value("itype", 0));
                    ce.item.slot = static_cast<EquipSlot>(ej.value("islot", -1));
                    ce.item.damage_bonus = ej.value("idmg", 0); ce.item.armor_bonus = ej.value("iarm", 0);
                    ce.item.attack_bonus = ej.value("iatk", 0); ce.item.dodge_bonus = ej.value("idodge", 0);
                    ce.item.heal_amount = ej.value("iheal", 0); ce.item.gold_value = ej.value("igold", 0);
                    ce.item.material = static_cast<MaterialType>(ej.value("imat", 0));
                    ce.item.tags = ej.value("itags", (uint32_t)0);
                    ce.item.identified = ej.value("iid", false); ce.item.curse_state = ej.value("icurse", 0);
                }
                if (ej.contains("god")) {
                    ce.has_god = true;
                    ce.god_align.god = static_cast<GodId>(ej.value("god", 0));
                    ce.god_align.favor = ej.value("gfav", 0);
                }
                if (ej.contains("cont_open")) {
                    ce.has_container = true;
                    ce.container.opened = ej.value("cont_open", false);
                    ce.container.open_sprite_x = ej.value("cont_osx", 0);
                    ce.container.open_sprite_y = ej.value("cont_osy", 0);
                }
                fs.entities.push_back(std::move(ce));
            }
        }
    }
}

void Engine::clear_entities_except_player() {
    // Collect all entities that aren't the player or pet
    std::vector<Entity> to_destroy;
    auto& positions = world_.pool<Position>();
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (e != player_ && e != pet_entity_) {
            to_destroy.push_back(e);
        }
    }
    for (Entity e : to_destroy) {
        world_.destroy(e);
    }
}

void Engine::generate_level() {
    dungeon_level_++;
    if (dungeon_level_ > run_deepest_) run_deepest_ = dungeon_level_;

    // Mark dynamic quests that require dungeon visits
    if (dungeon_level_ > 0) {
        auto& dq_pool = world_.pool<DynamicQuest>();
        for (size_t i = 0; i < dq_pool.size(); i++) {
            auto& dq = dq_pool.at_index(i);
            if (dq.accepted && dq.requires_dungeon) dq.visited_dungeon = true;
        }
    }

    // God effects on level change
    if (dungeon_level_ > 1 && player_ != NULL_ENTITY && world_.has<GodAlignment>(player_)) {
        auto& align = world_.get<GodAlignment>(player_);
        // Thessarka gains favor from exploration (descending deeper)
        if (align.god == GodId::THESSARKA) {
            god_system::adjust_favor(world_, player_, log_, 2);
            align.items_identified_floor = 0; // reset auto-ID for new floor
        }
        // Gathruun gains favor from depth
        if (align.god == GodId::GATHRUUN) {
            god_system::adjust_favor(world_, player_, log_, 1);
        }
        // Lethis: reset lethal save per floor
        if (align.god == GodId::LETHIS) {
            align.lethal_save_used = false;
        }
        // Gathruun: reset dig per floor
        if (align.god == GodId::GATHRUUN) {
            align.dig_used_floor = false;
        }
        // Lethis tenet: check if rested last floor (violation if not)
        if (align.god == GodId::LETHIS && dungeon_level_ > 2 && !rested_this_floor_) {
            god_system::adjust_favor(world_, player_, log_, -2);
            log_.add("Lethis frowns. You did not rest.", {160, 120, 200, 255});
        }
        rested_this_floor_ = false;
        rest_count_this_floor_ = 0;

        // Thessarka: auto-identify one unidentified item per floor
        if (align.god == GodId::THESSARKA && world_.has<Inventory>(player_)) {
            auto& inv = world_.get<Inventory>(player_);
            for (size_t s = 0; s < inv.items.size(); s++) {
                Entity ie = inv.items[s];
                if (ie != NULL_ENTITY && world_.has<Item>(ie)) {
                    auto& item = world_.get<Item>(ie);
                    if (!item.identified && !item.unid_name.empty()) {
                        item.identified = true;
                        char idbuf[128];
                        snprintf(idbuf, sizeof(idbuf),
                            "Thessarka whispers: the %s is %s.", item.unid_name.c_str(), item.name.c_str());
                        log_.add(idbuf, {140, 140, 220, 255});
                        break; // only one per floor
                    }
                }
            }
        }

        // Ixuul: slime/aberration neutrality (set on floor entry)
        // handled in AI code
    }

    // Clear old monsters/items but keep player
    // Note: cache_current_floor() is called from stair handlers BEFORE generate_level()
    if (player_ != NULL_ENTITY) {
        clear_entities_except_player();
    }

    // Try to restore cached floor
    if (dungeon_level_ > 0 && restore_floor(dungeon_level_, ascending_)) {
        // Floor restored from cache — skip generation
        if (world_.has<Position>(player_) && world_.has<Stats>(player_)) {
            auto& pos = world_.get<Position>(player_);
            fov::compute(map_, pos.x, pos.y, world_.get<Stats>(player_).fov_radius());
            camera_.center_on(pos.x, pos.y);
        }
        return;
    }

    int start_x, start_y;

    if (dungeon_level_ == 0) {
        // Overworld — load from file once, then reuse cached copy
        if (!overworld_loaded_) {
            // Try relative path first, then from executable directory
            overworld_cache_ = mapfile::load("data/maps/overworld.map");
            if (overworld_cache_.map.width() == 0) {
                // Try path relative to executable (for running from build/ dir)
                overworld_cache_ = mapfile::load("../data/maps/overworld.map");
            }
            if (overworld_cache_.map.width() == 0) {
                fprintf(stderr, "FATAL: Could not load overworld.map from data/maps/ or ../data/maps/\n");
            }
            overworld_loaded_ = true;
        }
        auto& mresult = overworld_cache_;
        if (mresult.map.width() == 0 || mresult.map.height() == 0) {
            fprintf(stderr, "FATAL: Overworld map is empty. Cannot proceed.\n");
            state_ = GameState::MAIN_MENU;
            return;
        }
        map_ = mresult.map; // copy (preserves cache, gets fresh explored state)

        // Province-specific building materials: restyle walls near each town
        for (int ti = 0; ti < TOWN_COUNT; ti++) {
            GodId god = get_town_god(ALL_TOWNS[ti].x, ALL_TOWNS[ti].y);
            TileType wall_type = TileType::WALL_STONE_BRICK; // default
            TileType floor_type = TileType::FLOOR_STONE;
            switch (god) {
                case GodId::KHAEL:    wall_type = TileType::WALL_WOOD; break;
                case GodId::GATHRUUN: wall_type = TileType::WALL_STONE_ROUGH; break;
                case GodId::SYTHARA:  wall_type = TileType::WALL_SANDSTONE; break;
                case GodId::OSSREN:   wall_type = TileType::WALL_STONE_ROUGH; break;
                case GodId::SOLETH:   wall_type = TileType::WALL_STONE_BRICK; break;
                default: break;
            }
            if (wall_type != TileType::WALL_STONE_BRICK) {
                int cx = ALL_TOWNS[ti].x, cy = ALL_TOWNS[ti].y;
                int r = 25; // town radius
                for (int dy = -r; dy <= r; dy++) {
                    for (int dx = -r; dx <= r; dx++) {
                        int tx = cx + dx, ty = cy + dy;
                        if (!map_.in_bounds(tx, ty)) continue;
                        auto& t = map_.at(tx, ty);
                        if (t.type == TileType::WALL_STONE_BRICK)
                            t.type = wall_type;
                    }
                }
            }
            // Frozen Marches: snow ground around town
            if (god == GodId::GATHRUUN) {
                int cx = ALL_TOWNS[ti].x, cy = ALL_TOWNS[ti].y;
                for (int dy = -20; dy <= 20; dy++) {
                    for (int dx = -20; dx <= 20; dx++) {
                        int tx = cx + dx, ty = cy + dy;
                        if (!map_.in_bounds(tx, ty)) continue;
                        auto& t = map_.at(tx, ty);
                        if (t.type == TileType::FLOOR_GRASS)
                            t.type = TileType::FLOOR_SNOW;
                    }
                }
            }
            // Dust Provinces: sand ground
            if (god == GodId::SYTHARA) {
                int cx = ALL_TOWNS[ti].x, cy = ALL_TOWNS[ti].y;
                for (int dy = -20; dy <= 20; dy++) {
                    for (int dx = -20; dx <= 20; dx++) {
                        int tx = cx + dx, ty = cy + dy;
                        if (!map_.in_bounds(tx, ty)) continue;
                        auto& t = map_.at(tx, ty);
                        if (t.type == TileType::FLOOR_GRASS)
                            t.type = TileType::FLOOR_SAND;
                    }
                }
            }
        }

        // Province road restyle: upgrade dirt roads by region (outside towns only)
        for (int y = 0; y < map_.height(); y++) {
            for (int x = 0; x < map_.width(); x++) {
                auto& t = map_.at(x, y);
                if (t.type != TileType::FLOOR_DIRT) continue;
                if (near_town(x, y, 25) >= 0) continue; // skip inside towns
                GodId region = get_town_god(x, y);
                switch (region) {
                    case GodId::MORRETH: // Heartlands: cobblestone trade roads
                        t.type = TileType::FLOOR_COBBLE;
                        break;
                    case GodId::SYTHARA: // Dust Provinces: sand tracks
                        t.type = TileType::FLOOR_SAND;
                        break;
                    case GodId::GATHRUUN: // Frozen Marches: dirt stays (visible against snow)
                        break;
                    default: break; // other regions keep dirt
                }
            }
        }

        // Province ground and vegetation pass: restyle terrain by region
        for (int y = 0; y < map_.height(); y++) {
            for (int x = 0; x < map_.width(); x++) {
                auto& t = map_.at(x, y);
                if (t.type != TileType::FLOOR_GRASS) continue;
                GodId region = get_town_god(x, y);
                // Position hash for deterministic variation
                unsigned h = static_cast<unsigned>(x * 7919 + y * 1301);
                switch (region) {
                    case GodId::GATHRUUN: // Frozen Marches: snow, sparse trees
                        if ((h % 100) < 70) t.type = TileType::FLOOR_SNOW;
                        else if ((h % 100) < 80) t.type = TileType::FLOOR_ICE;
                        break;
                    case GodId::SYTHARA: // Dust Provinces: sand, dry dirt
                        if ((h % 100) < 60) t.type = TileType::FLOOR_SAND;
                        else if ((h % 100) < 80) t.type = TileType::FLOOR_DIRT;
                        break;
                    case GodId::KHAEL: // Greenwood: dense vegetation, extra trees
                        if (near_town(x, y, 30) >= 0) break; // don't plant trees in towns
                        if ((h % 100) < 8) t.type = TileType::TREE;
                        else if ((h % 100) < 12) t.type = TileType::BRUSH;
                        break;
                    case GodId::OSSREN: // Iron Coast: rocky dirt, some cobble roads
                        if ((h % 100) < 20) t.type = TileType::FLOOR_DIRT;
                        break;
                    case GodId::SOLETH: // Pale Reach: mix of grass and dirt, sparse
                        if ((h % 100) < 15) t.type = TileType::FLOOR_DIRT;
                        break;
                    default: break; // Heartlands stays green
                }
            }
        }

        start_x = mresult.start_x;
        start_y = mresult.start_y;
        rooms_.clear();

        // Dialogue pools — deterministic selection via position hash
        static const char* FARMER_DIALOGUE[] = {
            "Used to be quiet here. Before they opened the barrow.",
            "My grandfather said there were older things than gods buried in these hills.",
            "The scholar knows more than he lets on. Always reading those old texts.",
        };
        static const char* FARMER_IDLE[] = {
            "Harvest is thin this year.", "The well water tastes different lately.",
            "My neighbor left for the city. Smart man.", "The children are afraid to play outside.",
            "I've lived here all my life. Never seen times like these.",
            "Prices at the shops keep climbing.", "We used to trade with the southern towns. Not anymore.",
        };
        static const char* GUARD_DIALOGUE[] = {
            "Keep your blade sheathed in town.",
            "Something's been killing livestock east of here. Stay sharp.",
            "The barrow's been sealed for generations. Now it's open.",
        };
        static const char* GUARD_IDLE[] = {
            "Move along.", "I've been on watch since dawn.",
            "The garrison's stretched thin.", "We could use more swords.",
            "Report anything suspicious.", "Stay out of trouble.",
            "The roads aren't as safe as they used to be.",
        };
        static const char* SCHOLAR_DIALOGUE[] = {
            "The Reliquary predates the gods we know. What made it? I don't think we want to know.",
            "Each god claims the Reliquary is theirs by right. They're all wrong.",
            "The inscriptions in the deep places are in no language I recognize.",
        };
        static const char* SCHOLAR_IDLE[] = {
            "I've been cross-referencing the old texts. The dates don't add up.",
            "There are gaps in the histories. Deliberate ones.",
            "The gods arrived. That's what the oldest records say. Arrived, not arose.",
            "Have you read the standing stone inscriptions? They predate everything.",
            "Somewhere in those dungeons is the truth. I just can't go get it myself.",
        };
        static const char* BLACKSMITH_DIALOGUE[] = {
            "Iron holds. Steel bites. That's all you need to know.",
            "I can repair anything made by human hands. What's down there... I'm not sure.",
            "The ore from the deep mines has a strange color. I don't like working with it.",
        };
        static const char* BLACKSMITH_IDLE[] = {
            "The forge runs hot today.", "Silver's good against the dead. Remember that.",
            "Mithril's rare. If you find any, bring it here.",
            "A good blade is the difference between coming home and not.",
            "I don't ask where the ore comes from anymore.",
        };
        static const char* SHOPKEEPER_IDLE[] = {
            "Take your time. I'm not going anywhere.", "Good stock today. Fresh from the road.",
            "You look like you could use supplies.", "Best prices this side of Thornwall.",
            "Business has been slow. Too many dungeons, not enough customers.",
            "I trade in what the road provides.",
        };
        static const char* HERBALIST_IDLE[] = {
            "The forest provides, if you know where to look.",
            "Antidotes don't grow on trees. Well, some do.",
            "These poultices take days to prepare.", "Mind the red mushrooms. Those aren't for eating.",
        };
        static const char* INNKEEPER_IDLE[] = {
            "10 gold for a room. Best deal you'll find.",
            "The ale's fresh. The beds are... acceptable.",
            "Travelers bring news. Mostly bad news, these days.",
            "Rest up. The road doesn't get easier from here.",
        };

        // Helper: deterministic dialogue pick from position hash
        auto pick_dialogue = [](const char* pool[], int pool_size, int x, int y) -> const char* {
            unsigned h = static_cast<unsigned>(x * 31 + y * 17 + x * y * 7);
            return pool[h % pool_size];
        };

        // Helper: assign idle lines from a pool to an NPC
        auto set_idle = [](NPC& npc, const char* pool[], int pool_size) {
            for (int i = 0; i < pool_size; i++)
                npc.idle_lines.push_back(pool[i]);
        };

        // Append town-specific rumor lines based on NPC position
        auto add_town_rumors = [](NPC& npc, int x, int y) {
            int ti = near_town(x, y);
            if (ti < 0 || ti >= TOWN_COUNT) return;
            auto& td = ALL_TOWNS[ti];
            npc.idle_lines.push_back(td.rumor1);
            npc.idle_lines.push_back(td.rumor2);
            npc.idle_lines.push_back(td.rumor3);
            npc.idle_lines.push_back(td.nearby_warning);
        };

        // NPC name generator: deterministic from position hash
        static const char* FIRST_NAMES[] = {
            "Arden", "Bram", "Cora", "Dael", "Edrin", "Fenn", "Greta", "Holt",
            "Iona", "Jace", "Kael", "Lira", "Maren", "Nils", "Orin", "Petra",
            "Quinn", "Ren", "Sera", "Thom", "Una", "Voss", "Wynn", "Xara",
            "Yara", "Zev", "Asta", "Brin", "Cael", "Dara", "Elka", "Falk",
            "Gale", "Hanna", "Idra", "Jorr", "Kira", "Leif", "Mira", "Nara",
            "Osmund", "Pell", "Rook", "Sable", "Tarn", "Ulric", "Vela", "Wren",
        };
        static constexpr int NAME_COUNT = sizeof(FIRST_NAMES) / sizeof(FIRST_NAMES[0]);

        auto gen_npc_name = [](const char* role, int x, int y) -> std::string {
            unsigned h = static_cast<unsigned>(x * 7919 + y * 1301 + x * y * 31);
            std::string result = role;
            result += " ";
            result += FIRST_NAMES[h % NAME_COUNT];
            return result;
        };

        // Track which main quest slots have been assigned so each is assigned once
        bool mq_assigned[17] = {};
        // Side quest assignment tracking (for Thornwall side quests)
        bool sq_farmer_assigned = false;
        bool sq_guard_assigned = false;
        bool sq_blacksmith_assigned = false;
        bool sq_ratcellar_assigned = false;
        bool sq_amulet_assigned = false;
        bool sq_undead_assigned = false;

        // Spawn NPCs from map entities
        for (auto& me : mresult.entities) {
            Entity npc = world_.create();
            world_.add<Position>(npc, {me.x, me.y});

            NPC npc_comp;
            int sx = 0, sy = 0; // sprite coords in rogues.png
            int town_idx = near_quest_town(me.x, me.y);

            switch (me.glyph) {
                case 'S':
                    npc_comp.role = NPCRole::SHOPKEEPER;
                    npc_comp.name = gen_npc_name("Shopkeeper", me.x, me.y);
                    npc_comp.dialogue = "Browse, if you like. I don't haggle.";
                    set_idle(npc_comp, SHOPKEEPER_IDLE, 6);
                    { int sv = (me.x * 11 + me.y * 7) % 2;
                      if (sv == 0) { sx = 2; sy = 6; }      // shopkeep
                      else { sx = 3; sy = 6; } }             // elderly woman (merchant)
                    // Side quest: Ashford shopkeeper — rats in the cellar
                    if (town_idx == 1 && !sq_ratcellar_assigned) {
                        npc_comp.quest_id = static_cast<int>(QuestId::SQ_RAT_CELLAR);
                        npc_comp.dialogue = "Rats in the cellar. Every night, more of them. I'll pay you to clear them out.";
                        sq_ratcellar_assigned = true;
                    }
                    break;
                case 'B':
                    npc_comp.role = NPCRole::BLACKSMITH;
                    npc_comp.name = gen_npc_name("Blacksmith", me.x, me.y);
                    npc_comp.dialogue = pick_dialogue(BLACKSMITH_DIALOGUE, 3, me.x, me.y);
                    set_idle(npc_comp, BLACKSMITH_IDLE, 5);
                    sx = 4; sy = 5; // blacksmith sprite (row 6)
                    // Side quest: Thornwall blacksmith
                    if (town_idx == 0 && !sq_blacksmith_assigned) {
                        npc_comp.quest_id = static_cast<int>(QuestId::SQ_DELIVER_WEAPON);
                        sq_blacksmith_assigned = true;
                    }
                    // MQ_10: Ironhearth blacksmith
                    if (town_idx == 4 && !mq_assigned[9]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_10_IRONHEARTH_FORGE);
                        npc_comp.name = "Master Smith Brynn";
                        npc_comp.dialogue = "Bring me something worth studying and I'll tell you what it is.";
                        mq_assigned[9] = true;
                    }
                    break;
                case 'P':
                    npc_comp.role = NPCRole::PRIEST;
                    npc_comp.name = gen_npc_name("Scholar", me.x, me.y);
                    npc_comp.dialogue = pick_dialogue(SCHOLAR_DIALOGUE, 3, me.x, me.y);
                    set_idle(npc_comp, SCHOLAR_IDLE, 5);
                    { int pv = (me.x * 17 + me.y * 5) % 3;
                      if (pv == 0) { sx = 5; sy = 5; }      // scholar
                      else if (pv == 1) { sx = 3; sy = 4; }  // desert sage
                      else { sx = 4; sy = 4; } }             // dwarf mage
                    // MQ_02: Thornwall scholar
                    if (town_idx == 0 && !mq_assigned[1]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_02_SCHOLAR_CLUE);
                        npc_comp.name = "Scholar Aldric";
                        npc_comp.dialogue = "That brand... I've seen drawings of it. "
                                            "You're not the first. The others didn't survive.";
                        mq_assigned[1] = true;
                    }
                    // MQ_05: Greywatch scholar
                    else if (town_idx == 2 && !mq_assigned[4]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_05_STONEKEEP_DEPTHS);
                        npc_comp.name = "Scholar Erynn";
                        npc_comp.dialogue = "Stonekeep holds inscriptions no one alive can read.";
                        mq_assigned[4] = true;
                    }
                    // MQ_06: Frostmere scholar (ice sage)
                    else if (town_idx == 3 && !mq_assigned[5]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_06_FROSTMERE_SAGE);
                        npc_comp.name = "Sage Yeva";
                        npc_comp.dialogue = "Some names should stay frozen.";
                        mq_assigned[5] = true;
                    }
                    // MQ_08+MQ_09: Millhaven scholar (Catacombs area)
                    else if (town_idx == 7 && !mq_assigned[7]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_08_CATACOMBS_GATE);
                        npc_comp.name = "Scholar Maren";
                        npc_comp.dialogue = "The Catacombs gate has stood sealed since before this town was built.";
                        mq_assigned[7] = true;
                    }
                    // MQ_12: Candlemere scholar (binding ritual)
                    else if (town_idx == 5 && !mq_assigned[11]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_12_CANDLEMERE_RITUAL);
                        npc_comp.name = "Priest Solara";
                        npc_comp.dialogue = "The old rituals are preserved here. The gods tried to make everyone forget.";
                        mq_assigned[11] = true;
                    }
                    // MQ_14: Hollowgate scholar (break the seal)
                    else if (town_idx == 6 && !mq_assigned[13]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_14_HOLLOWGATE_SEAL);
                        npc_comp.name = "Scholar Daven";
                        npc_comp.dialogue = "The seal here is the last one. Beyond it lies the oldest place in the world.";
                        mq_assigned[13] = true;
                    }
                    break;
                case 'F':
                    npc_comp.role = NPCRole::FARMER;
                    npc_comp.name = gen_npc_name("Farmer", me.x, me.y);
                    npc_comp.dialogue = pick_dialogue(FARMER_DIALOGUE, 3, me.x, me.y);
                    set_idle(npc_comp, FARMER_IDLE, 7);
                    { int fv = (me.x * 13 + me.y * 3) % 3;
                      if (fv == 0) { sx = 0; sy = 5; }      // farmer (wheat)
                      else if (fv == 1) { sx = 1; sy = 5; }  // farmer (scythe)
                      else { sx = 2; sy = 5; } }             // farmer (pitchfork)
                    // Side quest: Thornwall farmer
                    if (town_idx == 0 && !sq_farmer_assigned) {
                        npc_comp.quest_id = static_cast<int>(QuestId::SQ_MISSING_PERSON);
                        sq_farmer_assigned = true;
                    }
                    // Side quest: Millhaven farmer — lost amulet
                    if (town_idx == 7 && !sq_amulet_assigned) {
                        npc_comp.quest_id = static_cast<int>(QuestId::SQ_LOST_AMULET);
                        npc_comp.dialogue = "My grandmother's amulet — I lost it in the dungeon nearby. Please, it's all I have of her.";
                        sq_amulet_assigned = true;
                    }
                    // MQ_03: Ashford farmer
                    if (town_idx == 1 && !mq_assigned[2]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_03_ASHFORD_TABLET);
                        npc_comp.name = "Farmer Galen";
                        npc_comp.dialogue = "There's a stone tablet in the ruins nearby. The dead don't want it found.";
                        mq_assigned[2] = true;
                    }
                    break;
                case 'G':
                    npc_comp.role = NPCRole::GUARD;
                    npc_comp.name = gen_npc_name("Guard", me.x, me.y);
                    npc_comp.dialogue = pick_dialogue(GUARD_DIALOGUE, 3, me.x, me.y);
                    set_idle(npc_comp, GUARD_IDLE, 7);
                    { // vary guard sprites: knight, female knight, female knight helmetless
                        int guard_var = (me.x * 7 + me.y * 13) % 3;
                        if (guard_var == 0) { sx = 0; sy = 1; }      // knight
                        else if (guard_var == 1) { sx = 2; sy = 1; }  // female knight
                        else { sx = 3; sy = 1; }                      // female knight helmetless
                    }
                    // Side quest: Thornwall guard
                    if (town_idx == 0 && !sq_guard_assigned) {
                        npc_comp.quest_id = static_cast<int>(QuestId::SQ_KILL_BEAR);
                        sq_guard_assigned = true;
                    }
                    // Side quest: Greywatch guard — undead patrol
                    else if (town_idx == 2 && !sq_undead_assigned) {
                        npc_comp.quest_id = static_cast<int>(QuestId::SQ_UNDEAD_PATROL);
                        npc_comp.name = "Sergeant Breck";
                        npc_comp.dialogue = "The dead walk in the tunnels south of here. Thin their numbers.";
                        sx = 2; sy = 1; // female knight
                        sq_undead_assigned = true;
                    }
                    // MQ_04: Greywatch guard (receives tablet)
                    if (town_idx == 2 && !mq_assigned[3]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_04_GREYWATCH_WARNING);
                        npc_comp.name = "Captain Voss";
                        npc_comp.dialogue = "I command the largest garrison in the region. Speak plainly.";
                        sx = 0; sy = 1; // knight
                        mq_assigned[3] = true;
                    }
                    // MQ_07: Frostmere guard (frozen key location)
                    else if (town_idx == 3 && !mq_assigned[6]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_07_FROZEN_KEY);
                        npc_comp.name = "Guard Osric";
                        npc_comp.dialogue = "The ice dungeon north of here holds things that stopped being human long ago.";
                        mq_assigned[6] = true;
                    }
                    // MQ_11: Ironhearth guard (Molten Depths)
                    else if (town_idx == 4 && !mq_assigned[10]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_11_MOLTEN_TRIAL);
                        npc_comp.name = "Guard Holt";
                        npc_comp.dialogue = "The volcanic tunnels beneath us run deep. The heat kills anything that isn't already dead.";
                        mq_assigned[10] = true;
                    }
                    // MQ_13: Candlemere guard (Sunken Halls)
                    else if (town_idx == 5 && !mq_assigned[12]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_13_SUNKEN_FRAGMENT);
                        npc_comp.name = "Guard Thane";
                        npc_comp.dialogue = "The Sunken Halls flood more each year. The water there remembers.";
                        mq_assigned[12] = true;
                    }
                    break;
                case 'W':
                    npc_comp.role = NPCRole::FARMER;
                    npc_comp.name = gen_npc_name("Villager", me.x, me.y);
                    npc_comp.dialogue = pick_dialogue(FARMER_DIALOGUE, 3, me.x, me.y);
                    set_idle(npc_comp, FARMER_IDLE, 7);
                    { int wv = (me.x * 9 + me.y * 19) % 5;
                      if (wv == 0) { sx = 0; sy = 6; }      // peasant/coalburner
                      else if (wv == 1) { sx = 3; sy = 6; }  // elderly woman
                      else if (wv == 2) { sx = 4; sy = 6; }  // elderly man
                      else if (wv == 3) { sx = 3; sy = 5; }  // baker
                      else { sx = 0; sy = 5; } }             // farmer (wheat)
                    break;
                case 'H':
                    npc_comp.role = NPCRole::PRIEST; // herbalist uses priest role
                    npc_comp.name = gen_npc_name("Herbalist", me.x, me.y);
                    npc_comp.dialogue = "The wilds hold remedies for every ill, if you know where to look.";
                    set_idle(npc_comp, HERBALIST_IDLE, 4);
                    sx = 3; sy = 6;
                    break;
                case 'M':
                    npc_comp.role = NPCRole::SHOPKEEPER;
                    npc_comp.name = gen_npc_name("Merchant", me.x, me.y);
                    npc_comp.dialogue = "I trade in what the road provides. Take a look.";
                    set_idle(npc_comp, SHOPKEEPER_IDLE, 6);
                    sx = 2; sy = 6;
                    break;
                case 'E':
                    npc_comp.role = NPCRole::ELDER;
                    npc_comp.name = "Elder Maren";
                    npc_comp.dialogue = "That mark on your face. I've read about it. "
                                        "The Barrow woke the same night you appeared. "
                                        "Whatever you are, you're connected to what's down there.";
                    npc_comp.quest_id = static_cast<int>(QuestId::MQ_01_BARROW_WIGHT);
                    mq_assigned[0] = true;
                    sx = 4; sy = 6; // elderly man sprite (row 7)
                    break;
            }
            npc_comp.home_x = me.x;
            npc_comp.home_y = me.y;
            npc_comp.god_affiliation = get_town_god(me.x, me.y);
            add_town_rumors(npc_comp, me.x, me.y);
            world_.add<NPC>(npc, std::move(npc_comp));
            // Province tint: subtle color shift per region
            SDL_Color npc_tint = {255, 255, 255, 255};
            GodId npc_god = get_town_god(me.x, me.y);
            switch (npc_god) {
                case GodId::GATHRUUN: npc_tint = {210, 220, 240, 255}; break; // cold blue
                case GodId::SYTHARA:  npc_tint = {230, 210, 180, 255}; break; // dusty yellow
                case GodId::KHAEL:    npc_tint = {210, 230, 200, 255}; break; // forest green
                case GodId::OSSREN:   npc_tint = {220, 210, 200, 255}; break; // soot grey
                case GodId::SOLETH:   npc_tint = {240, 230, 210, 255}; break; // warm gold
                default: break;
            }
            world_.add<Renderable>(npc, {SHEET_ROGUES, sx, sy, npc_tint, 5});

            // NPCs have stats but aren't killable (no AI component = won't fight)
            Stats npc_stats;
            npc_stats.name = world_.get<NPC>(npc).name;
            npc_stats.hp = 999;
            npc_stats.hp_max = 999;
            world_.add<Stats>(npc, std::move(npc_stats));

            // Energy for NPC wandering (slow — acts every ~3 turns)
            world_.add<Energy>(npc, {0, 35});
        }

        // Spawn Herbalist and Merchant NPCs at each town (not in map file)
        auto spawn_extra_npc = [&](int cx, int cy, const char* name, NPCRole role,
                                    const char* dialogue, int spr_x, int spr_y) {
            // Find a walkable tile near town center
            for (int attempt = 0; attempt < 40; attempt++) {
                int tx = cx + rng_.range(-8, 8);
                int ty = cy + rng_.range(-8, 8);
                if (!map_.in_bounds(tx, ty) || !map_.is_walkable(tx, ty)) continue;
                // Check no entity already there
                if (combat::entity_at(world_, tx, ty, player_) != NULL_ENTITY) continue;

                Entity e = world_.create();
                world_.add<Position>(e, {tx, ty});
                NPC nc;
                nc.role = role;
                nc.name = name;
                nc.dialogue = dialogue;
                nc.home_x = tx;
                nc.home_y = ty;
                nc.god_affiliation = get_town_god(cx, cy);
                // Add idle lines based on role
                if (role == NPCRole::PRIEST)
                    set_idle(nc, HERBALIST_IDLE, 4);
                else if (role == NPCRole::SHOPKEEPER)
                    set_idle(nc, SHOPKEEPER_IDLE, 6);
                else if (role == NPCRole::INNKEEPER)
                    set_idle(nc, INNKEEPER_IDLE, 4);
                add_town_rumors(nc, cx, cy);
                world_.add<NPC>(e, std::move(nc));
                world_.add<Renderable>(e, {SHEET_ROGUES, spr_x, spr_y, {255, 255, 255, 255}, 5});
                Stats ns;
                ns.name = name;
                ns.hp = 999; ns.hp_max = 999;
                world_.add<Stats>(e, std::move(ns));
                world_.add<Energy>(e, {0, 35});
                return;
            }
        };

        static const char* HERBALIST_LINES[] = {
            "The wilds hold remedies for every ill, if you know where to look.",
            "Moonpetal grows only where the dead have lain. Think about that.",
            "Most poisons come from the same plants as their cures.",
        };
        static const char* MERCHANT_LINES[] = {
            "I trade in what the road provides. Take a look.",
            "Every town needs something only another town has.",
            "The roads are getting worse. Good for business, bad for living.",
        };
        for (int i = 0; i < TOWN_COUNT; i++) {
            auto herb_name = gen_npc_name("Herbalist", ALL_TOWNS[i].x + 1, ALL_TOWNS[i].y);
            auto merch_name = gen_npc_name("Merchant", ALL_TOWNS[i].x, ALL_TOWNS[i].y + 1);
            auto inn_name = gen_npc_name("Innkeeper", ALL_TOWNS[i].x + 2, ALL_TOWNS[i].y + 2);
            spawn_extra_npc(ALL_TOWNS[i].x, ALL_TOWNS[i].y, herb_name.c_str(), NPCRole::PRIEST,
                            HERBALIST_LINES[rng_.range(0, 2)], 3, 6);
            spawn_extra_npc(ALL_TOWNS[i].x, ALL_TOWNS[i].y, merch_name.c_str(), NPCRole::SHOPKEEPER,
                            MERCHANT_LINES[rng_.range(0, 2)], 2, 6);
            spawn_extra_npc(ALL_TOWNS[i].x, ALL_TOWNS[i].y, inn_name.c_str(), NPCRole::INNKEEPER,
                            INNKEEPER_IDLE[rng_.range(0, 3)], 1, 6);
        }

        // Populate overworld with wilderness content
        populate_overworld();

        // Generate dynamic side quests for each town's NPCs
        for (int i = 0; i < TOWN_COUNT; i++) {
            quest_gen::generate_town_quests(world_, map_, rng_,
                ALL_TOWNS[i].x, ALL_TOWNS[i].y, ALL_TOWNS[i].name);
        }

        // Spawn church high priests at province capitals
        for (int ci = 0; ci < CHURCH_COUNT; ci++) {
            auto& cl = CHURCH_LOCATIONS[ci];
            auto& town = ALL_TOWNS[cl.town_idx];
            auto& ginfo = get_god_info(cl.god);

            // Place church priest near the town center
            int cx = town.x + rng_.range(-5, 5);
            int cy = town.y + rng_.range(-5, 5);
            // Find walkable spot
            for (int a = 0; a < 30; a++) {
                int tx = town.x + rng_.range(-8, 8);
                int ty = town.y + rng_.range(-8, 8);
                if (map_.is_walkable(tx, ty)) {
                    cx = tx; cy = ty; break;
                }
            }

            Entity priest = world_.create();
            world_.add<Position>(priest, {cx, cy});
            world_.add<Renderable>(priest, {SHEET_ROGUES, 1, 4,
                {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255}, 8});

            NPC npc_comp;
            npc_comp.role = NPCRole::PRIEST;
            char name_buf[64];
            snprintf(name_buf, sizeof(name_buf), "High Priest of %s", ginfo.name);
            npc_comp.name = name_buf;
            char dial_buf[128];
            snprintf(dial_buf, sizeof(dial_buf),
                "Welcome to the Church of %s. %s", ginfo.name, ginfo.description);
            npc_comp.dialogue = dial_buf;
            npc_comp.god_affiliation = cl.god;
            npc_comp.quest_id = -1;
            world_.add<NPC>(priest, npc_comp);

            { Stats ps; ps.name = name_buf; ps.hp = 999; ps.hp_max = 999;
              world_.add<Stats>(priest, std::move(ps)); }
            world_.add<Energy>(priest, {0, 35});

            world_.add<Church>(priest, {cl.god, false});
        }
    } else {
        // Dungeon zone themes — keyed by name for registry lookup
        struct ZoneTheme {
            TileType wall, floor;
            const char* name;
            int max_depth; // deepest level in this zone
        };
        // Default depth-based zones
        static const ZoneTheme DEPTH_ZONES[] = {
            {TileType::WALL_DIRT,        TileType::FLOOR_DIRT,      "The Warrens",       3},
            {TileType::WALL_STONE_ROUGH, TileType::FLOOR_STONE,     "Stonekeep",         6},
            {TileType::WALL_STONE_BRICK, TileType::FLOOR_STONE,     "The Deep Halls",    9},
            {TileType::WALL_CATACOMB,    TileType::FLOOR_BONE,      "The Catacombs",    12},
            {TileType::WALL_IGNEOUS,     TileType::FLOOR_RED_STONE, "The Molten Depths",15},
            {TileType::WALL_LARGE_STONE, TileType::FLOOR_STONE,     "The Sunken Halls", 18},
        };
        constexpr int DEPTH_ZONE_COUNT = sizeof(DEPTH_ZONES) / sizeof(DEPTH_ZONES[0]);

        // Named zone string -> theme mapping
        struct NamedZone { const char* key; int theme_idx; };
        static const NamedZone ZONE_MAP[] = {
            {"warrens",    0},
            {"stonekeep",  1},
            {"deep_halls", 2},
            {"catacombs",  3},
            {"molten",     4},
            {"sunken",     5},
            {"sepulchre",  2}, // The Sepulchre uses Deep Halls theme (ancient stone)
        };
        constexpr int ZONE_MAP_COUNT = sizeof(ZONE_MAP) / sizeof(ZONE_MAP[0]);

        // Determine zone from dungeon registry or fall back to depth
        int zone_idx = std::min((dungeon_level_ - 1) / 3, DEPTH_ZONE_COUNT - 1);
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            auto& dentry = dungeon_registry_[current_dungeon_idx_];
            for (int zi = 0; zi < ZONE_MAP_COUNT; zi++) {
                if (dentry.zone == ZONE_MAP[zi].key) {
                    zone_idx = ZONE_MAP[zi].theme_idx;
                    break;
                }
            }
        }
        auto& zone = DEPTH_ZONES[zone_idx];

        // Don't place stairs down at the bottom — use per-dungeon max_depth
        int max_depth = zone.max_depth; // fallback
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            max_depth = dungeon_registry_[current_dungeon_idx_].max_depth;
        }
        bool at_zone_bottom = (dungeon_level_ >= max_depth);

        DungeonParams params;
        params.width = 80;
        params.height = 50;
        params.wall_type = zone.wall;
        params.floor_type = zone.floor;

        // Province-specific wall override (Frozen Marches dungeons get ice walls)
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            auto& de = dungeon_registry_[current_dungeon_idx_];
            GodId dgod = (de.patron_god_idx >= 0) ? static_cast<GodId>(de.patron_god_idx) : GodId::NONE;
            if (dgod == GodId::GATHRUUN) {
                params.wall_type = TileType::WALL_ICE;
                params.floor_type = TileType::FLOOR_SNOW;
            }
        }

        // Zone-specific generation parameters
        std::string zone_key;
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            zone_key = dungeon_registry_[current_dungeon_idx_].zone;
        }

        if (zone_key == "warrens") {
            params.room_min_w = 4; params.room_max_w = 8;
            params.room_min_h = 4; params.room_max_h = 8;
            params.max_rooms = 15 + dungeon_level_;
            params.corridor_width = 1;
        } else if (zone_key == "stonekeep") {
            params.room_min_w = 5; params.room_max_w = 10;
            params.room_min_h = 5; params.room_max_h = 10;
            params.max_rooms = 10 + dungeon_level_;
            params.corridor_width = rng_.range(1, 2);
        } else if (zone_key == "deep_halls") {
            params.room_min_w = 8; params.room_max_w = 16;
            params.room_min_h = 8; params.room_max_h = 16;
            params.max_rooms = 6 + dungeon_level_;
            params.corridor_width = rng_.range(2, 3);
        } else if (zone_key == "catacombs") {
            params.room_min_w = 4; params.room_max_w = 7;
            params.room_min_h = 4; params.room_max_h = 7;
            params.max_rooms = 14 + dungeon_level_;
            params.corridor_width = 1;
        } else if (zone_key == "molten") {
            params.room_min_w = 6; params.room_max_w = 12;
            params.room_min_h = 6; params.room_max_h = 12;
            params.max_rooms = 8 + dungeon_level_;
            params.corridor_width = 2;
        } else if (zone_key == "sunken") {
            params.room_min_w = 6; params.room_max_w = 14;
            params.room_min_h = 6; params.room_max_h = 14;
            params.max_rooms = 7 + dungeon_level_;
            params.corridor_width = rng_.range(2, 3);
        } else if (zone_key == "sepulchre") {
            params.room_min_w = 7; params.room_max_w = 14;
            params.room_min_h = 7; params.room_max_h = 14;
            params.max_rooms = 8 + dungeon_level_;
            params.corridor_width = 2;
        } else {
            // Fallback: depth-based defaults
            params.room_min_w = 5; params.room_max_w = 12;
            params.room_min_h = 5; params.room_max_h = 12;
            params.max_rooms = 12 + dungeon_level_;
            params.corridor_width = 1;
        }

        auto result = dungeon::generate(rng_, params, !at_zone_bottom);
        map_ = std::move(result.map);
        rooms_ = std::move(result.rooms);
        start_x = result.start_x;
        start_y = result.start_y;

        // Zone-specific terrain features (post-carving)
        if (zone_key == "sunken") {
            // Shallow water pools along room edges
            for (size_t ri = 1; ri < rooms_.size(); ri++) {
                if (!rng_.chance(45)) continue; // ~45% of rooms get water
                auto& rm = rooms_[ri];
                // Fill edge strip with water (1-2 tiles from walls)
                int side = rng_.range(0, 3);
                for (int s = 0; s < rm.w && s < rm.h; s++) {
                    int wx, wy;
                    switch (side) {
                        case 0: wx = rm.x + s; wy = rm.y; break;
                        case 1: wx = rm.x + s; wy = rm.y + rm.h - 1; break;
                        case 2: wx = rm.x; wy = rm.y + s; break;
                        default: wx = rm.x + rm.w - 1; wy = rm.y + s; break;
                    }
                    if (map_.in_bounds(wx, wy) &&
                        map_.at(wx, wy).type == params.floor_type) {
                        map_.at(wx, wy).type = TileType::WATER;
                    }
                }
            }
        } else if (zone_key == "catacombs") {
            // Scattered bone floor patches
            for (size_t ri = 1; ri < rooms_.size(); ri++) {
                if (!rng_.chance(35)) continue;
                auto& rm = rooms_[ri];
                int count = rng_.range(3, 8);
                for (int c = 0; c < count; c++) {
                    int bx = rng_.range(rm.x, rm.x + rm.w - 1);
                    int by = rng_.range(rm.y, rm.y + rm.h - 1);
                    if (map_.in_bounds(bx, by) &&
                        map_.at(bx, by).type == params.floor_type) {
                        map_.at(bx, by).type = TileType::FLOOR_BONE;
                    }
                }
            }
        } else if (zone_key == "deep_halls") {
            // Scattered rock pillars in large rooms
            for (size_t ri = 1; ri < rooms_.size(); ri++) {
                auto& rm = rooms_[ri];
                if (rm.w < 10 || rm.h < 10) continue;
                if (!rng_.chance(40)) continue;
                int pillars = rng_.range(2, 4);
                for (int p = 0; p < pillars; p++) {
                    int px = rng_.range(rm.x + 2, rm.x + rm.w - 3);
                    int py = rng_.range(rm.y + 2, rm.y + rm.h - 3);
                    if (map_.in_bounds(px, py) &&
                        map_.at(px, py).type == params.floor_type) {
                        map_.at(px, py).type = TileType::ROCK;
                    }
                }
            }
        }

        // ─── Room shape modifications (zone-specific) ───
        if (zone_key == "warrens") {
            // Nibble corners: remove 1-2 tiles from room corners to make irregular shapes
            for (size_t ri = 1; ri < rooms_.size(); ri++) {
                if (!rng_.chance(60)) continue;
                auto& rm = rooms_[ri];
                // Each corner: randomly fill 1-3 tiles back to wall
                for (int corner = 0; corner < 4; corner++) {
                    int nibble = rng_.range(1, 3);
                    for (int n = 0; n < nibble; n++) {
                        int nx, ny;
                        switch (corner) {
                            case 0: nx = rm.x + n;          ny = rm.y;             break;
                            case 1: nx = rm.x + rm.w-1 - n; ny = rm.y;             break;
                            case 2: nx = rm.x;               ny = rm.y + rm.h-1 - n; break;
                            default: nx = rm.x + rm.w-1;     ny = rm.y + rm.h-1 - n; break;
                        }
                        if (map_.in_bounds(nx, ny) && map_.at(nx, ny).type == params.floor_type)
                            map_.at(nx, ny).type = params.wall_type;
                    }
                }
            }
        } else if (zone_key == "sunken") {
            // Round rooms: carve off corners to make oval-ish shapes
            for (size_t ri = 0; ri < rooms_.size(); ri++) {
                if (!rng_.chance(50)) continue;
                auto& rm = rooms_[ri];
                int hw = rm.w / 2, hh = rm.h / 2;
                int ccx = rm.x + hw, ccy = rm.y + hh;
                float rx = static_cast<float>(hw), ry = static_cast<float>(hh);
                for (int dy = -hh; dy <= hh; dy++) {
                    for (int dx = -hw; dx <= hw; dx++) {
                        float d = (dx*dx)/(rx*rx) + (dy*dy)/(ry*ry);
                        if (d > 1.0f) {
                            int tx = ccx + dx, ty = ccy + dy;
                            if (map_.in_bounds(tx, ty) && map_.at(tx, ty).type == params.floor_type)
                                map_.at(tx, ty).type = params.wall_type;
                        }
                    }
                }
            }
        } else if (zone_key == "catacombs") {
            // Alcoves: small 2x1 extensions from corridors into walls
            for (int y = 2; y < params.height - 2; y++) {
                for (int x = 2; x < params.width - 2; x++) {
                    if (map_.at(x, y).type != params.floor_type) continue;
                    if (!rng_.chance(3)) continue; // sparse
                    // Check if this is a corridor tile (exactly 2 floor neighbors on one axis)
                    bool wall_n = map_.is_opaque(x, y-1);
                    bool wall_s = map_.is_opaque(x, y+1);
                    bool wall_e = map_.is_opaque(x+1, y);
                    bool wall_w = map_.is_opaque(x-1, y);
                    // Horizontal corridor: walls north and south
                    if (wall_n && wall_s && !wall_e && !wall_w) {
                        // Carve alcove north or south
                        int ay = wall_n ? y-1 : y+1;
                        if (map_.in_bounds(x, ay) && map_.is_opaque(x, ay))
                            map_.at(x, ay).type = params.floor_type;
                    }
                    // Vertical corridor: walls east and west
                    if (wall_e && wall_w && !wall_n && !wall_s) {
                        int ax = wall_e ? x+1 : x-1;
                        if (map_.in_bounds(ax, y) && map_.is_opaque(ax, y))
                            map_.at(ax, y).type = params.floor_type;
                    }
                }
            }
        } else if (zone_key == "molten") {
            // Jagged walls: randomly carve 1-tile protrusions into rooms
            for (size_t ri = 1; ri < rooms_.size(); ri++) {
                auto& rm = rooms_[ri];
                for (int side = 0; side < 4; side++) {
                    int len = (side < 2) ? rm.w : rm.h;
                    for (int i = 0; i < len; i++) {
                        if (!rng_.chance(25)) continue;
                        int jx, jy;
                        switch (side) {
                            case 0: jx = rm.x + i; jy = rm.y - 1; break;
                            case 1: jx = rm.x + i; jy = rm.y + rm.h; break;
                            case 2: jx = rm.x - 1; jy = rm.y + i; break;
                            default: jx = rm.x + rm.w; jy = rm.y + i; break;
                        }
                        if (map_.in_bounds(jx, jy) && map_.is_opaque(jx, jy))
                            map_.at(jx, jy).type = params.floor_type;
                    }
                }
            }
        } else if (zone_key == "deep_halls") {
            // Pillar grids in the largest rooms
            for (size_t ri = 1; ri < rooms_.size(); ri++) {
                auto& rm = rooms_[ri];
                if (rm.w < 12 || rm.h < 12) continue;
                // Regular 3x3 pillar grid
                for (int py = rm.y + 3; py < rm.y + rm.h - 2; py += 3) {
                    for (int px = rm.x + 3; px < rm.x + rm.w - 2; px += 3) {
                        if (map_.in_bounds(px, py) && map_.at(px, py).type == params.floor_type)
                            map_.at(px, py).type = TileType::ROCK;
                    }
                }
            }
        }

        // Special rooms — pick one mid-room (not first/last) for a special purpose
        if (rooms_.size() >= 5 && dungeon_level_ >= 2) {
            int special_idx = rng_.range(2, static_cast<int>(rooms_.size()) - 2);
            auto& sr = rooms_[special_idx];
            int roll = rng_.range(1, 100);

            if (roll <= 20) {
                // Flooded chamber — fill most of the room with water
                for (int sy = sr.y; sy < sr.y + sr.h; sy++) {
                    for (int sx = sr.x; sx < sr.x + sr.w; sx++) {
                        if (!map_.in_bounds(sx, sy)) continue;
                        // Leave a 1-tile dry border for walkability
                        bool edge = (sx == sr.x || sx == sr.x + sr.w - 1 ||
                                     sy == sr.y || sy == sr.y + sr.h - 1);
                        if (!edge && map_.at(sx, sy).type == params.floor_type) {
                            map_.at(sx, sy).type = TileType::WATER;
                        }
                    }
                }
            } else if (roll <= 40 && dungeon_level_ >= 3) {
                // Treasure vault — extra chests spawned by populate, mark floor as distinct
                for (int sy = sr.y; sy < sr.y + sr.h; sy++) {
                    for (int sx = sr.x; sx < sr.x + sr.w; sx++) {
                        if (!map_.in_bounds(sx, sy)) continue;
                        if (map_.at(sx, sy).type == params.floor_type) {
                            map_.at(sx, sy).type = TileType::FLOOR_RED_STONE;
                        }
                    }
                }
                // Spawn 2-4 extra chests in the vault
                for (int ci = 0; ci < rng_.range(2, 4); ci++) {
                    int cx = rng_.range(sr.x + 1, sr.x + sr.w - 2);
                    int cy = rng_.range(sr.y + 1, sr.y + sr.h - 2);
                    if (!map_.in_bounds(cx, cy) || !map_.is_walkable(cx, cy)) continue;
                    Entity chest = world_.create();
                    world_.add<Position>(chest, {cx, cy});
                    world_.add<Renderable>(chest, {SHEET_TILES, 0, 17, {255,255,255,255}, 1});
                    Container cont;
                    cont.open_sprite_x = 1; cont.open_sprite_y = 17;
                    cont.contents.name = "gold coins"; cont.contents.type = ItemType::GOLD;
                    cont.contents.gold_value = rng_.range(20, 50 + dungeon_level_ * 10);
                    cont.contents.stack = cont.contents.gold_value;
                    cont.contents.stackable = true; cont.contents.identified = true;
                    world_.add<Container>(chest, std::move(cont));
                }
            } else if (roll <= 55) {
                // Bone crypt — floor littered with remains
                for (int sy = sr.y; sy < sr.y + sr.h; sy++) {
                    for (int sx = sr.x; sx < sr.x + sr.w; sx++) {
                        if (!map_.in_bounds(sx, sy)) continue;
                        if (map_.at(sx, sy).type == params.floor_type && rng_.chance(50)) {
                            map_.at(sx, sy).type = TileType::FLOOR_BONE;
                        }
                    }
                }
            } else if (roll <= 70 && dungeon_level_ >= 3) {
                // Library — bookshelves (rock pillars in grid) + tome drops
                // Place "shelves" as rock pillars along walls
                for (int sy = sr.y + 1; sy < sr.y + sr.h - 1; sy += 2) {
                    for (int sx = sr.x + 1; sx < sr.x + sr.w - 1; sx++) {
                        if ((sy == sr.y + 1 || sy >= sr.y + sr.h - 2) &&
                            map_.in_bounds(sx, sy) && map_.at(sx, sy).type == params.floor_type) {
                            if (rng_.chance(60))
                                map_.at(sx, sy).type = TileType::ROCK;
                        }
                    }
                }
                // Drop 1-3 spellbooks in the room
                static const SpellId LIBRARY_SPELLS[] = {
                    SpellId::IDENTIFY, SpellId::DETECT_MONSTERS, SpellId::REVEAL_MAP,
                    SpellId::FORESIGHT, SpellId::SCRY, SpellId::TRUESIGHT, SpellId::CLAIRVOYANCE
                };
                int tomes = rng_.range(1, 3);
                for (int ti = 0; ti < tomes; ti++) {
                    int tx = rng_.range(sr.x + 1, sr.x + sr.w - 2);
                    int ty = rng_.range(sr.y + 1, sr.y + sr.h - 2);
                    if (!map_.in_bounds(tx, ty) || !map_.is_walkable(tx, ty)) continue;
                    auto spell = LIBRARY_SPELLS[rng_.range(0, 6)];
                    auto& sinfo = get_spell_info(spell);
                    Entity tome = world_.create();
                    world_.add<Position>(tome, {tx, ty});
                    world_.add<Renderable>(tome, {SHEET_ITEMS, 1, 21, {255,255,255,255}, 1});
                    Item book; book.name = std::string("Tome of ") + sinfo.name;
                    book.description = sinfo.description; book.type = ItemType::SCROLL;
                    book.gold_value = 30 + sinfo.mp_cost * 5;
                    book.identified = true; book.teaches_spell = static_cast<int>(spell);
                    book.tags |= TAG_BOOK;
                    world_.add<Item>(tome, std::move(book));
                }
                // Lore item
                int lx = sr.cx(), ly = sr.cy();
                if (map_.in_bounds(lx, ly) && map_.is_walkable(lx, ly)) {
                    Entity lore = world_.create();
                    world_.add<Position>(lore, {lx, ly});
                    world_.add<Renderable>(lore, {SHEET_ITEMS, 0, 21, {255,255,255,255}, 1});
                    Item note; note.name = "scholar's journal";
                    note.description = "The shelves held thousands of volumes once. Most are dust now. What remains speaks of things the gods would rather forget.";
                    note.type = ItemType::SCROLL; note.gold_value = 5; note.identified = true;
                    world_.add<Item>(lore, std::move(note));
                }
            } else if (roll <= 82 && sr.w >= 7 && sr.h >= 7) {
                // Arena — open room with pillar ring and tough enemies
                // Place pillars in a ring pattern
                int midx = sr.cx(), midy = sr.cy();
                int ring_r = std::min(sr.w, sr.h) / 2 - 1;
                for (int angle = 0; angle < 8; angle++) {
                    float a = angle * 3.14159f / 4.0f;
                    int px = midx + static_cast<int>(ring_r * std::cos(a));
                    int py = midy + static_cast<int>(ring_r * std::sin(a));
                    if (map_.in_bounds(px, py) && map_.at(px, py).type == params.floor_type)
                        map_.at(px, py).type = TileType::ROCK;
                }
                // Spawn 2-3 tough monsters in the center
                const auto* mtable = populate::get_monster_table();
                int mcount = populate::get_monster_count();
                int max_idx = std::min(mcount - 1, 8 + dungeon_level_ * 2);
                for (int mi = 0; mi < rng_.range(2, 3); mi++) {
                    int mx = midx + rng_.range(-2, 2);
                    int my = midy + rng_.range(-2, 2);
                    if (!map_.in_bounds(mx, my) || !map_.is_walkable(mx, my)) continue;
                    if (combat::entity_at(world_, mx, my, player_) != NULL_ENTITY) continue;
                    int idx = rng_.range(max_idx / 2, max_idx); // tougher half of pool
                    auto& mdef = mtable[idx];
                    Entity mob = world_.create();
                    world_.add<Position>(mob, {mx, my});
                    world_.add<Renderable>(mob, {SHEET_MONSTERS, mdef.sprite_x, mdef.sprite_y, {255,255,255,255}, 5});
                    Stats ms; ms.name = mdef.name;
                    float scale = 1.2f + dungeon_level_ * 0.2f; // tougher than normal
                    ms.hp = static_cast<int>(mdef.hp * scale); ms.hp_max = ms.hp;
                    ms.base_damage = static_cast<int>(mdef.base_damage * scale);
                    ms.natural_armor = mdef.natural_armor; ms.base_speed = mdef.speed;
                    ms.xp_value = static_cast<int>(mdef.xp_value * 1.5f);
                    ms.set_attr(Attr::STR, mdef.str); ms.set_attr(Attr::DEX, mdef.dex);
                    ms.set_attr(Attr::CON, mdef.con);
                    world_.add<Stats>(mob, std::move(ms));
                    AI ai; ai.state = AIState::IDLE; ai.flee_threshold = 5;
                    world_.add<AI>(mob, ai);
                    world_.add<Energy>(mob, {0, mdef.speed});
                    world_.add<StatusEffects>(mob);
                }
                // Gold reward in center
                Entity gold = world_.create();
                world_.add<Position>(gold, {midx, midy});
                world_.add<Renderable>(gold, {SHEET_ITEMS, 1, 24, {255,255,255,255}, 1});
                Item gi; gi.name = "gold coins"; gi.type = ItemType::GOLD;
                gi.gold_value = rng_.range(30, 80 + dungeon_level_ * 15);
                gi.stack = gi.gold_value; gi.stackable = true; gi.identified = true;
                world_.add<Item>(gold, std::move(gi));
            } else if (roll <= 92 && dungeon_level_ >= 4) {
                // Shrine room — god shrine + guardian + blessing
                // Special floor
                for (int sy = sr.y; sy < sr.y + sr.h; sy++) {
                    for (int sx = sr.x; sx < sr.x + sr.w; sx++) {
                        if (!map_.in_bounds(sx, sy)) continue;
                        if (map_.at(sx, sy).type == params.floor_type)
                            map_.at(sx, sy).type = TileType::FLOOR_RED_STONE;
                    }
                }
                // Place shrine in center
                int scx = sr.cx(), scy = sr.cy();
                if (map_.in_bounds(scx, scy))
                    map_.at(scx, scy).type = TileType::SHRINE;
                // 4 braziers at corners
                int boff = std::min(sr.w, sr.h) / 2 - 1;
                int corners[][2] = {{-boff,-boff},{boff,-boff},{-boff,boff},{boff,boff}};
                for (auto& c : corners) {
                    int bx = scx + c[0], by = scy + c[1];
                    if (map_.in_bounds(bx, by) && map_.is_walkable(bx, by)) {
                        Entity braz = world_.create();
                        world_.add<Position>(braz, {bx, by});
                        world_.add<Renderable>(braz, {SHEET_ANIMATED, 0, 1, {255,255,255,255}, 0});
                    }
                }
            }
        }
    }

    // Create or reposition player
    if (player_ == NULL_ENTITY) {
        auto build = creation_screen_.get_build();
        auto result = player_setup::create_player(world_, build, start_x, start_y);
        player_ = result.entity;
        build_traits_ = std::move(result.traits);
    } else {
        world_.get<Position>(player_) = {start_x, start_y};
    }

    // Re-position pet to player after level transition
    if (pet_entity_ != NULL_ENTITY && world_.has<Position>(pet_entity_)) {
        world_.get<Position>(pet_entity_) = {start_x, start_y};
    }

    // Spawn monsters and items (not in village)
    if (dungeon_level_ > 0) {
        // Effective level = dungeon depth + zone difficulty (distance from start)
        int zone_diff = 0;
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()))
            zone_diff = dungeon_registry_[current_dungeon_idx_].zone_difficulty;
        int effective_level = dungeon_level_ + zone_diff;

        populate::spawn_monsters(world_, map_, rooms_, rng_, effective_level);
        populate::spawn_items(world_, map_, rooms_, rng_, effective_level);
        populate::spawn_traps(world_, map_, rooms_, rng_, effective_level);

        // Dungeon doodads (chests, jars, mushrooms, coffins, god shrines, etc.)
        {
            std::string zone_name;
            int patron_god = -1;
            if (current_dungeon_idx_ >= 0 &&
                current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
                zone_name = dungeon_registry_[current_dungeon_idx_].zone;
                patron_god = dungeon_registry_[current_dungeon_idx_].patron_god_idx;
            }
            populate::spawn_doodads(world_, map_, rooms_, rng_, effective_level, zone_name, patron_god);
        }

        // Rival paragons — depth 4+ effective in named dungeons
        if (effective_level >= 4 && current_dungeon_idx_ >= 0 && rng_.chance(15)) {
            GodId pgod = GodId::NONE;
            if (world_.has<GodAlignment>(player_))
                pgod = world_.get<GodAlignment>(player_).god;
            Entity paragon = populate::spawn_paragon(world_, map_, rooms_, rng_,
                                                      effective_level, pgod);
            if (paragon != NULL_ENTITY) {
                auto& pgalign = world_.get<GodAlignment>(paragon);
                auto& ginfo = get_god_info(pgalign.god);
                auto& pstats = world_.get<Stats>(paragon);
                // Dramatic multi-line intro
                log_.add("The air grows heavy.", {180, 140, 180, 255});
                char pbuf[128];
                snprintf(pbuf, sizeof(pbuf),
                    "%s, Paragon of %s, bars your way.", pstats.name.c_str(), ginfo.name);
                log_.add(pbuf, {220, 170, 220, 255});
                // Taunt based on god
                static const char* TAUNTS[] = {
                    "\"The dead will claim you.\"",           // Vethrik
                    "\"Your ignorance ends here.\"",          // Thessarka
                    "\"Stand and face me.\"",                 // Morreth
                    "\"I smell your blood already.\"",        // Yashkhet
                    "\"Nature turns against you.\"",          // Khael
                    "\"Burn in the pale flame.\"",            // Soleth
                    "\"Chaos takes what order builds.\"",     // Ixuul
                    "\"You won't see the blade.\"",           // Zhavek
                    "\"The tide answers to me.\"",            // Thalara
                    "\"My steel is harder than yours.\"",     // Ossren
                    "\"Sleep now. Forever.\"",                // Lethis
                    "\"The mountain will not move for you.\"", // Gathruun
                    "\"Breathe deep. It will be your last.\"", // Sythara
                };
                int gi = static_cast<int>(pgalign.god);
                if (gi >= 0 && gi < 13)
                    log_.add(TAUNTS[gi], {200, 180, 220, 255});
                audio_.play(SfxId::SPELL_IMPACT);
                trigger_screen_shake(3.0f);
            }
        }

        // Legendary items — only in high-difficulty dungeons (zone_difficulty >= 5), bottom floor
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            auto& dentry = dungeon_registry_[current_dungeon_idx_];
            if (dungeon_level_ >= dentry.max_depth && dentry.zone_difficulty >= 5) {
                Entity leg = populate::spawn_legendary(world_, rooms_, rng_, dentry.name);
                if (leg != NULL_ENTITY) {
                    log_.add("Something valuable gleams in the deepest chamber.", {255, 240, 140, 255});
                }
            }
            // God relic — bottom floor of late-game dungeons with a patron god, ~30% chance
            if (dungeon_level_ >= dentry.max_depth && dentry.zone_difficulty >= 6
                && dentry.patron_god_idx >= 0 && rng_.chance(30)) {
                Entity relic = populate::spawn_relic(world_, rooms_, rng_, dentry.patron_god_idx);
                if (relic != NULL_ENTITY) {
                    auto& ginfo = get_god_info(static_cast<GodId>(dentry.patron_god_idx));
                    char rbuf[128];
                    snprintf(rbuf, sizeof(rbuf), "A divine presence radiates from the depths — something of %s.", ginfo.name);
                    log_.add(rbuf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                }
            }
        }

        // Unique items — 12% chance per floor, zone-aware
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()) &&
            rng_.chance(12)) {
            auto& dentry = dungeon_registry_[current_dungeon_idx_];
            int effective_depth = dungeon_level_ + dentry.zone_difficulty;
            populate::spawn_unique(world_, rooms_, rng_, effective_depth, dentry.zone);
        }

        // Spawn quest bosses, quest items, and depth-triggered quest auto-starts
        {
            const quest_gen::DungeonContext* ctx = nullptr;
            quest_gen::DungeonContext dctx;
            if (current_dungeon_idx_ >= 0 &&
                current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
                auto& dentry = dungeon_registry_[current_dungeon_idx_];
                dctx.zone = dentry.zone;
                dctx.quest = dentry.quest;
                dctx.max_depth = dentry.max_depth;
                ctx = &dctx;
            }
            quest_gen::spawn_quest_content(world_, map_, rooms_,
                dungeon_level_, ctx, journal_, log_);
        }
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

        // Use registry dungeon name if available, otherwise zone name
        const char* dungeon_name = ZONE_NAMES[idx];
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            dungeon_name = dungeon_registry_[current_dungeon_idx_].name.c_str();
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "%s — Depth %d", dungeon_name, dungeon_level_);
        log_.add(buf, {180, 170, 160, 255});
        log_.add(ZONE_MESSAGES[idx], {120, 110, 100, 255});

        // Zone depth limit message
        static const int ZONE_MAX_DEPTHS[] = {3, 6, 9, 12, 15, 18};
        if (dungeon_level_ == ZONE_MAX_DEPTHS[idx]) {
            log_.add("You've reached the bottom of this place. There is nothing deeper here.",
                     {180, 160, 120, 255});
        }
    }

    // Update music/ambient for new location
    update_music_for_location();

    // Reposition friendly summons near player after floor change
    if (world_.has<Position>(player_)) {
        auto& pp = world_.get<Position>(player_);
        auto& all_ai = world_.pool<AI>();
        for (size_t si = 0; si < all_ai.size(); si++) {
            Entity se = all_ai.entity_at(si);
            if (!all_ai.at_index(si).friendly) continue;
            if (!world_.has<Position>(se)) continue;
            for (int a = 0; a < 30; a++) {
                int sx = pp.x + rng_.range(-2, 2);
                int sy = pp.y + rng_.range(-2, 2);
                if (sx == pp.x && sy == pp.y) continue;
                if (map_.in_bounds(sx, sy) && map_.is_walkable(sx, sy) &&
                    combat::entity_at(world_, sx, sy, player_) == NULL_ENTITY) {
                    world_.get<Position>(se).x = sx;
                    world_.get<Position>(se).y = sy;
                    break;
                }
            }
        }
    }
}

void Engine::grant_skill_xp(SkillId skill, int amount) {
    if (!world_.has<Skills>(player_)) return;
    auto& skills = world_.get<Skills>(player_);
    int old_lv = skills.get_level(skill);
    bool leveled = skills.grant_xp(skill, amount);
    if (leveled) {
        int new_lv = skills.get_level(skill);
        char buf[96];
        snprintf(buf, sizeof(buf), "%s increased to %d.", skill_name(skill), new_lv);
        log_.add(buf, {140, 200, 160, 255});
        if (!tips_shown_.first_skill_levelup) {
            tips_shown_.first_skill_levelup = true;
            tutorial_popup_.show("Skill Up",
                "Skills improve through use and unlock\n"
                "bonuses at levels 25, 50, and 75.\n\n"
                "C - View character sheet and skills");
        }
        // Milestone notifications
        if (new_lv == 25 || new_lv == 50 || new_lv == 75) {
            const char* unlock = nullptr;
            switch (skill) {
                case SkillId::STEALTH:
                    if (new_lv == 25) unlock = "Stealth 25: sleeping monsters won't wake near you.";
                    if (new_lv == 50) unlock = "Stealth 50: detection range reduced to 1 tile.";
                    break;
                case SkillId::BLADES:
                    if (new_lv == 25) unlock = "Blades 25: +3% crit chance.";
                    if (new_lv == 50) unlock = "Blades 50: +5% crit, can intimidate humanoids.";
                    break;
                case SkillId::DIVINATION:
                    if (new_lv == 25) unlock = "Divination 25: auto-identify potions on pickup.";
                    break;
                case SkillId::NATURE_MAGIC:
                    if (new_lv == 25) unlock = "Nature 25: forage herbs in overworld grass.";
                    break;
                case SkillId::DARK_ARTS:
                    if (new_lv == 25) unlock = "Dark Arts 25: examine corpses for hints.";
                    break;
                case SkillId::PRAYER:
                    if (new_lv == 50) unlock = "Prayer 50: your god hints at quest direction.";
                    break;
                case SkillId::HEAVY_ARMOR:
                    if (new_lv == 50) unlock = "Heavy Armor 50: reduced spell failure penalty.";
                    break;
                default: break;
            }
            if (unlock) {
                log_.add(unlock, {220, 220, 100, 255});
                audio_.play(SfxId::LEVELUP);
            }
        }
    }
}

void Engine::try_move_player(int dx, int dy) {
    if (state_ != GameState::PLAYING) return;

    // Status effect checks — frozen/stunned skip turn, confused randomizes direction
    if (world_.has<StatusEffects>(player_)) {
        auto& fx = world_.get<StatusEffects>(player_);
        if (fx.has(StatusType::FROZEN) || fx.has(StatusType::STUNNED)) {
            const char* msg = fx.has(StatusType::FROZEN) ? "You are frozen solid." : "You are stunned.";
            log_.add(msg, {180, 180, 220, 255});
            player_acted_ = true;
            return;
        }
        if (fx.has(StatusType::CONFUSED) && rng_.chance(50)) {
            // 50% chance movement goes in a random direction
            dx = rng_.range(-1, 1);
            dy = rng_.range(-1, 1);
            if (dx == 0 && dy == 0) dx = 1;
        }
        if (fx.has(StatusType::FEARED)) {
            // Reverse direction — flee from where you were trying to go
            dx = -dx;
            dy = -dy;
        }
    }

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

    // Check for sign at target tile (signs don't have Stats, so entity_at won't find them)
    {
        auto& positions = world_.pool<Position>();
        for (size_t i = 0; i < positions.size(); i++) {
            Entity se = positions.entity_at(i);
            auto& sp = positions.at_index(i);
            if (sp.x == nx && sp.y == ny && world_.has<Sign>(se)) {
                auto& sign = world_.get<Sign>(se);
                log_.add(sign.text, {200, 190, 150, 255});
                player_acted_ = true;
                return;
            }
        }
    }

    // Check for entity at target tile
    Entity target = combat::entity_at(world_, nx, ny, player_);
    if (target != NULL_ENTITY) {
        // NPC — talk, pickpocket, or push past
        if (world_.has<NPC>(target) && !world_.has<AI>(target)) {
            // Track consecutive bumps: push past after 3 bumps on the same NPC
            if (target == last_bumped_npc_) {
                npc_bump_count_++;
                if (npc_bump_count_ >= 3) {
                    auto& tpos = world_.get<Position>(target);
                    int old_px = pos.x, old_py = pos.y;
                    pos.x = nx; pos.y = ny;
                    tpos.x = old_px; tpos.y = old_py;
                    log_.add("You push past.", {180, 180, 160, 255});
                    npc_bump_count_ = 0;
                    last_bumped_npc_ = NULL_ENTITY;
                    player_acted_ = true;
                    return;
                }
            } else {
                last_bumped_npc_ = target;
                npc_bump_count_ = 1;
            }

            // Pickpocket while sneaking
            if (sneaking_) {
                int stealth_lv = 0;
                if (world_.has<Skills>(player_))
                    stealth_lv = world_.get<Skills>(player_).get_level(SkillId::STEALTH);
                int pickpocket_chance = 20 + stealth_lv;
                if (pickpocket_chance > 85) pickpocket_chance = 85;

                auto& npc = world_.get<NPC>(target);
                if (rng_.chance(pickpocket_chance)) {
                    // Success: steal gold
                    int stolen = rng_.range(5, 15 + dungeon_level_ * 5);
                    gold_ += stolen;
                    char sbuf[64];
                    snprintf(sbuf, sizeof(sbuf), "You lift %d gold from %s.", stolen, npc.name.c_str());
                    log_.add(sbuf, {200, 200, 100, 255});
                    audio_.play(SfxId::GOLD);
                    if (world_.has<Skills>(player_))
                        grant_skill_xp(SkillId::STEALTH, 8);
                } else {
                    // Failure: NPC becomes hostile or calls guards
                    char fbuf[96];
                    snprintf(fbuf, sizeof(fbuf), "%s catches you stealing! \"Thief!\"", npc.name.c_str());
                    log_.add(fbuf, {255, 100, 80, 255});
                    audio_.play(SfxId::CURSE);
                    // Spawn a guard that attacks the player
                    for (int gi = 0; gi < 20; gi++) {
                        int gx = nx + rng_.range(-2, 2);
                        int gy = ny + rng_.range(-2, 2);
                        if (gx == pos.x && gy == pos.y) continue;
                        if (!map_.in_bounds(gx, gy) || !map_.is_walkable(gx, gy)) continue;
                        if (combat::entity_at(world_, gx, gy, player_) != NULL_ENTITY) continue;
                        Entity guard = world_.create();
                        world_.add<Position>(guard, {gx, gy});
                        world_.add<Renderable>(guard, {SHEET_ROGUES, 0, 1, {255, 255, 255, 255}, 5}); // knight sprite
                        Stats gs; gs.name = "town guard";
                        gs.hp = 80 + dungeon_level_ * 15; gs.hp_max = gs.hp;
                        gs.base_damage = 12 + dungeon_level_ * 3;
                        gs.natural_armor = 6; gs.base_speed = 110; gs.xp_value = 0;
                        for (int a = 0; a < ATTR_COUNT; a++) gs.attributes[a] = 16;
                        world_.add<Stats>(guard, std::move(gs));
                        AI gai; gai.state = AIState::HUNTING;
                        gai.last_seen_x = pos.x; gai.last_seen_y = pos.y;
                        gai.flee_threshold = 0;
                        world_.add<AI>(guard, gai);
                        world_.add<Energy>(guard, {100, 110}); // starts with energy to act immediately
                        world_.add<StatusEffects>(guard);
                        break;
                    }
                }
                sneaking_ = false;
                player_acted_ = true;
                return;
            }
            // Normal talk
            npc_interaction::Context npc_ctx {
                world_, player_, log_, audio_, rng_, particles_,
                shop_screen_, quest_offer_, levelup_screen_,
                journal_, meta_, gold_, game_turn_, dungeon_level_,
                pending_levelup_, pending_quest_npc_
            };
            if (!tips_shown_.first_npc) {
                tips_shown_.first_npc = true;
                tutorial_popup_.show("NPCs",
                    "NPCs offer quests, shops, and healing.\n\n"
                    "E - Interact with a nearby NPC\n"
                    "Bump - Walk into an NPC to talk\n"
                    "Sneak + Bump - Pickpocket");
            }
            // Church high priest: open church screen
            if (world_.has<Church>(target) && world_.has<GodAlignment>(player_)) {
                auto& church = world_.get<Church>(target);
                auto& ga = world_.get<GodAlignment>(player_);
                if (ga.god == church.god || ga.god == GodId::NONE) {
                    church_screen_.open(player_, &world_, church.god,
                                         ga.god == church.god ? ga.favor : 0);
                    return;
                } else {
                    auto& ginfo = get_god_info(church.god);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "This is the Church of %s. You serve another god.", ginfo.name);
                    log_.add(buf, {180, 140, 120, 255});
                    return;
                }
            }
            if (npc_interaction::interact(npc_ctx, target, nx, ny))
                return;
        }
        // Intimidate: Blades/Blunt 50+ can scare humanoids into fleeing (20% chance)
        if (!sneaking_ && world_.has<Skills>(player_) && world_.has<AI>(target) && world_.has<Stats>(target)) {
            auto& skills = world_.get<Skills>(player_);
            auto& tai = world_.get<AI>(target);
            auto& tname = world_.get<Stats>(target).name;
            bool is_humanoid = (tname == "goblin" || tname == "orc" || tname == "kobold" ||
                                tname == "goblin archer" || tname == "goblin shaman" ||
                                tname == "orc warchief" || tname == "bandit" ||
                                tname == "skeleton" || tname == "zombie");
            int best_weapon_skill = std::max(skills.get_level(SkillId::BLADES),
                                   std::max(skills.get_level(SkillId::BLUNT),
                                            skills.get_level(SkillId::AXES)));
            if (is_humanoid && best_weapon_skill >= 50 && tai.state == AIState::HUNTING && rng_.chance(20)) {
                tai.state = AIState::FLEEING;
                char ibuf[64];
                snprintf(ibuf, sizeof(ibuf), "The %s cowers before you!", tname.c_str());
                log_.add(ibuf, {220, 200, 100, 255});
                player_acted_ = true;
                return;
            }
        }

        // Hostile — attack
        int level_before = world_.has<Stats>(player_) ? world_.get<Stats>(player_).level : 0;
        // Capture defender stats before kill removes them
        std::string victim_name;
        int victim_hp = 0, victim_dmg = 0, victim_arm = 0, victim_spd = 0;
        GodId victim_god = GodId::NONE;
        if (world_.has<Stats>(target)) {
            auto& vs = world_.get<Stats>(target);
            victim_name = vs.name;
            victim_hp = vs.hp_max;
            victim_dmg = vs.base_damage;
            victim_arm = vs.natural_armor;
            victim_spd = vs.base_speed;
        }
        if (world_.has<GodAlignment>(target) && world_.has<AI>(target))
            victim_god = world_.get<GodAlignment>(target).god;
        auto atk_result = combat::melee_attack(world_, player_, target, rng_, log_);
        player_acted_ = true;

        // Tutorial: first combat
        if (!tips_shown_.first_combat) {
            tips_shown_.first_combat = true;
            tutorial_popup_.show("Combat",
                "Weapon skills improve through use.\n\n"
                "Z - Cast spells\n"
                "F - Fire ranged weapon\n"
                "O - Toggle sneak (backstab for 2-4x damage)");
        }

        // Backstab: bonus damage from sneak attack
        if (sneaking_ && atk_result.hit && !atk_result.killed && world_.has<Stats>(target)) {
            int stealth_lv = 0;
            if (world_.has<Skills>(player_))
                stealth_lv = world_.get<Skills>(player_).get_level(SkillId::STEALTH);
            int backstab_mult = skill_bonus::stealth_backstab(stealth_lv); // 2x/3x/4x
            int bonus_dmg = atk_result.damage * (backstab_mult - 1);
            world_.get<Stats>(target).hp -= bonus_dmg;
            char bbuf[64];
            snprintf(bbuf, sizeof(bbuf), "Backstab! (%d bonus damage)", bonus_dmg);
            log_.add(bbuf, {200, 180, 255, 255});
            audio_.play(SfxId::CRIT);
            // Stealth skill XP from backstab
            if (world_.has<Skills>(player_))
                grant_skill_xp(SkillId::STEALTH, 5);
            // Check if killed
            if (world_.has<Stats>(target) && world_.get<Stats>(target).hp <= 0)
                atk_result.killed = true;
            // Attacking breaks sneak
            sneaking_ = false;
            turn_actions_.used_stealth_attack = true;
        } else if (sneaking_) {
            // Attacking breaks sneak even on miss
            sneaking_ = false;
        }

        // Skill XP from melee hit
        if (atk_result.hit && world_.has<Skills>(player_)) {
            auto& skills = world_.get<Skills>(player_);
            // Determine weapon skill from equipped weapon tags
            uint32_t wtags = 0;
            if (world_.has<Inventory>(player_)) {
                Entity wpn = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn != NULL_ENTITY && world_.has<Item>(wpn))
                    wtags = world_.get<Item>(wpn).tags;
            }
            if (wtags == 0) grant_skill_xp(SkillId::UNARMED, 2);
            else if (wtags & TAG_AXE) grant_skill_xp(SkillId::AXES, 2);
            else if (wtags & TAG_BLUNT) grant_skill_xp(SkillId::BLUNT, 2);
            else if (wtags & TAG_DAGGER) grant_skill_xp(SkillId::BLADES, 2);
            else grant_skill_xp(SkillId::BLADES, 2);
        }

        // Clumsy: 10% chance to fumble (turn hit into miss)
        if (atk_result.hit) {
            for (auto tid : build_traits_) {
                if (tid == TraitId::CLUMSY && rng_.chance(10)) {
                    // Refund damage
                    if (world_.has<Stats>(target))
                        world_.get<Stats>(target).hp += atk_result.damage;
                    atk_result.hit = false;
                    atk_result.damage = 0;
                    log_.add("You fumble the attack!", {200, 180, 100, 255});
                    break;
                }
            }
        }

        // Sure-Handed: 10% chance to strike twice
        if (atk_result.hit && !atk_result.killed && world_.has<Stats>(target)) {
            for (auto tid : build_traits_) {
                if (tid == TraitId::SURE_HANDED && rng_.chance(10)) {
                    auto bonus_hit = combat::melee_attack(world_, player_, target, rng_, log_);
                    if (bonus_hit.hit) {
                        log_.add("Double strike!", {220, 220, 140, 255});
                        if (bonus_hit.killed) atk_result.killed = true;
                    }
                    break;
                }
            }
        }

        // God passive combat bonuses (applied as extra damage after hit)
        if (atk_result.hit && world_.has<GodAlignment>(player_) && world_.has<Stats>(target)) {
            auto& ga = world_.get<GodAlignment>(player_);
            auto& tgt_stats = world_.get<Stats>(target);
            int bonus = 0;

            // Check weapon tags and material for bonuses
            uint32_t weapon_tags = 0;
            MaterialType weapon_mat = MaterialType::NONE;
            if (world_.has<Inventory>(player_)) {
                Entity wpn = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn != NULL_ENTITY && world_.has<Item>(wpn)) {
                    weapon_tags = world_.get<Item>(wpn).tags;
                    weapon_mat = world_.get<Item>(wpn).material;
                }
            }

            // Silver weapons: +50% damage vs undead
            if (weapon_mat == MaterialType::SILVER && is_undead(tgt_stats.name.c_str()))
                bonus += std::max(1, atk_result.damage / 2);

            // Favor milestones: 25/50/75 thresholds enhance god passives
            int fav = ga.favor;
            switch (ga.god) {
                case GodId::VETHRIK: {
                    // Undead damage: 15% base, 25% at favor 25, 35% at favor 50
                    int pct = (fav >= 50) ? 35 : (fav >= 25) ? 25 : 15;
                    if (is_undead(tgt_stats.name.c_str())) bonus = std::max(1, atk_result.damage * pct / 100);
                    int bone_dmg = (fav >= 75) ? 4 : 2;
                    if (weapon_tags & TAG_BONE_ITEM) bonus += bone_dmg;
                    break;
                }
                case GodId::MORRETH: {
                    // Blunt/axe: +2 base, +3 at favor 25, +4 at favor 50
                    int wpn_bonus = (fav >= 50) ? 4 : (fav >= 25) ? 3 : 2;
                    if (weapon_tags & (TAG_BLUNT | TAG_AXE)) bonus = wpn_bonus;
                    // Favor 75: +1 natural armor (applied once, tracked elsewhere)
                    break;
                }
                case GodId::YASHKHET: {
                    auto& ps = world_.get<Stats>(player_);
                    // Low-HP bonus: 15% base, 25% at favor 25; threshold 50% base, 75% at favor 50
                    int threshold_pct = (fav >= 50) ? 75 : 50;
                    int dmg_pct = (fav >= 25) ? 25 : 15;
                    if (ps.hp * 100 < ps.hp_max * threshold_pct)
                        bonus = std::max(1, atk_result.damage * dmg_pct / 100);
                    int dagger_bonus = (fav >= 75) ? 3 : 1;
                    if (weapon_tags & TAG_DAGGER) bonus += dagger_bonus;
                    break;
                }
                case GodId::SOLETH: {
                    // Holy smite: +2 base, +4 at favor 25, +6 at favor 50
                    int smite = (fav >= 50) ? 6 : (fav >= 25) ? 4 : 2;
                    if (is_undead(tgt_stats.name.c_str())) bonus = smite;
                    // Favor 75: all hits vs undead also burn
                    if (fav >= 75 && is_undead(tgt_stats.name.c_str()) && !atk_result.killed) {
                        if (!world_.has<StatusEffects>(target))
                            world_.add<StatusEffects>(target, {});
                        world_.get<StatusEffects>(target).add(StatusType::BURN, 2, 4);
                    }
                    break;
                }
                case GodId::ZHAVEK: {
                    // Stealth multiplier: 2x base, 3x at favor 50
                    if (world_.get<Stats>(player_).invisible_turns > 0) {
                        int mult = (fav >= 50) ? 2 : 1; // 2x or 3x total
                        bonus = atk_result.damage * mult;
                        world_.get<Stats>(player_).invisible_turns = 0;
                    }
                    break;
                }
                case GodId::OSSREN: {
                    // Craftsmanship: +1 base, +2 at favor 25, +3 at favor 75
                    int craft = (fav >= 75) ? 3 : (fav >= 25) ? 2 : 1;
                    if (weapon_tags != 0) bonus = craft;
                    break;
                }
                case GodId::GATHRUUN: {
                    // Underground strength: +2 base, +3 at favor 25, +5 at favor 50
                    int ground = (fav >= 50) ? 5 : (fav >= 25) ? 3 : 2;
                    if (dungeon_level_ > 0) bonus = ground;
                    break;
                }
                case GodId::SYTHARA: {
                    // Poison chance: 15% base, 25% at favor 25, 35% at favor 50
                    int pois_chance = (fav >= 50) ? 35 : (fav >= 25) ? 25 : 15;
                    int pois_sev = (fav >= 75) ? 3 : 2;
                    if (rng_.chance(pois_chance) && !atk_result.killed) {
                        if (!world_.has<StatusEffects>(target))
                            world_.add<StatusEffects>(target, {});
                        world_.get<StatusEffects>(target).add(StatusType::POISON, pois_sev, 8);
                        log_.add("Your touch carries Sythara's gift.", {120, 180, 60, 255});
                    }
                    break;
                }
                case GodId::KHAEL:
                    // Favor 50: melee hits have 10% chance to entangle (slow)
                    if (fav >= 50 && rng_.chance(10) && !atk_result.killed) {
                        if (!world_.has<StatusEffects>(target))
                            world_.add<StatusEffects>(target, {});
                        world_.get<StatusEffects>(target).add(StatusType::FROZEN, 1, 3);
                        log_.add("Roots grip your enemy.", {60, 160, 60, 255});
                    }
                    break;
                case GodId::THALARA:
                    // Favor 25: melee hits have 10% chance to bleed
                    if (fav >= 25 && rng_.chance(10) && !atk_result.killed) {
                        if (!world_.has<StatusEffects>(target))
                            world_.add<StatusEffects>(target, {});
                        world_.get<StatusEffects>(target).add(StatusType::BLEED, 1, 5);
                    }
                    break;
                default: break;
            }
            if (bonus > 0 && !atk_result.killed) {
                tgt_stats.hp -= bonus;
                atk_result.damage += bonus;
                if (tgt_stats.hp <= 0) {
                    combat::kill(world_, target, log_);
                    atk_result.killed = true;
                }
            }
        }

        if (atk_result.hit && atk_result.critical) {
            audio_.play(SfxId::CRIT); particles_.crit_flash((float)nx, (float)ny); trigger_screen_shake(4.0f);
            char cbuf[16]; snprintf(cbuf, sizeof(cbuf), "%d", atk_result.damage);
            floating_text_.spawn((float)nx, (float)ny, cbuf, {255, 200, 80, 255}, true);
        } else if (atk_result.hit) {
            audio_.play_hit();
            // Weapon-typed hit particles
            uint32_t wtags = 0;
            if (world_.has<Inventory>(player_)) {
                Entity wpn = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn != NULL_ENTITY && world_.has<Item>(wpn)) wtags = world_.get<Item>(wpn).tags;
            }
            particles_.hit_spark_weapon((float)nx, (float)ny, wtags);
            char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%d", atk_result.damage);
            floating_text_.spawn((float)nx, (float)ny, dbuf, {255, 255, 255, 255});
        }
        else audio_.play_miss();
        if (atk_result.killed) {
            audio_.play(SfxId::DEATH);
            // Creature-typed death particles
            bool is_un = world_.has<Stats>(target) && is_undead(world_.get<Stats>(target).name.c_str());
            bool is_an = world_.has<Stats>(target) && is_animal(world_.get<Stats>(target).name.c_str());
            particles_.death_burst_typed((float)nx, (float)ny, is_un, is_an);
            trigger_screen_shake(3.0f);
        }

        // Tenet action tracking for kills
        if (atk_result.killed) {
            turn_actions_.killed_anything = true;
            if (is_undead(victim_name.c_str())) turn_actions_.killed_undead = true;
            if (is_animal(victim_name.c_str())) turn_actions_.killed_animal = true;
            if (world_.has<Stats>(target) && world_.get<Stats>(target).sleep_turns > 0)
                turn_actions_.killed_sleeping = true;
            if (world_.has<Stats>(player_) && world_.get<Stats>(player_).invisible_turns > 0)
                turn_actions_.used_stealth_attack = true;
        }

        // Bestiary entry
        if (atk_result.killed && !victim_name.empty()) {
            auto& entry = bestiary_[victim_name];
            if (entry.kills == 0) {
                entry.name = victim_name;
                entry.hp = victim_hp; entry.damage = victim_dmg;
                entry.armor = victim_arm; entry.speed = victim_spd;
            }
            entry.kills++;
            // Meta tracking
            meta_.total_kills++;
            run_kills_++;
            if (victim_name == "dragon") meta_.killed_dragon = true;
            if (is_undead(victim_name.c_str())) {
                meta_.total_undead_kills++;
                journal_.add_progress(QuestId::SQ_UNDEAD_PATROL);
            }
            if (victim_name == "giant rat")
                journal_.add_progress(QuestId::SQ_RAT_CELLAR);
            // Unarmed kill check
            if (world_.has<Inventory>(player_)) {
                Entity wpn = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn == NULL_ENTITY) meta_.killed_unarmed = true;
            }
        }

        // God favor on kill
        if (atk_result.killed && world_.has<GodAlignment>(player_)) {
            auto& align = world_.get<GodAlignment>(player_);
            if (align.god != GodId::NONE) {
                int gain = 1; // base favor for any kill
                switch (align.god) {
                    case GodId::VETHRIK:
                    case GodId::SOLETH:
                        if (is_undead(victim_name.c_str())) gain += 2;
                        break;
                    case GodId::MORRETH:
                        gain += 1; // war god loves all combat
                        break;
                    case GodId::IXUUL:
                        gain += rng_.range(0, 2); // chaos
                        break;
                    case GodId::KHAEL:
                        if (is_animal(victim_name.c_str())) gain = -2; // nature god hates animal kills
                        break;
                    case GodId::ZHAVEK:
                        // Bonus favor for kills from stealth/invisible
                        if (world_.has<Stats>(player_) && world_.get<Stats>(player_).invisible_turns > 0) gain += 3;
                        break;
                    case GodId::THALARA:
                        // Bonus favor for kills near water (drowning theme)
                        gain += 1;
                        break;
                    case GodId::OSSREN:
                        // No special kill favor — Ossren cares about craft, not death
                        break;
                    case GodId::LETHIS:
                        // Favor from killing sleeping enemies
                        if (world_.has<Stats>(target) && world_.get<Stats>(target).sleep_turns > 0)
                            gain = -3; // Lethis forbids killing sleepers!
                        break;
                    case GodId::GATHRUUN:
                        // More favor for kills underground
                        if (dungeon_level_ > 0) gain += 1;
                        break;
                    case GodId::SYTHARA:
                        // Favor when enemy was poisoned/diseased
                        if (world_.has<StatusEffects>(target) && world_.get<StatusEffects>(target).has(StatusType::POISON))
                            gain += 2;
                        break;
                    default: break;
                }
                if (gain != 0) god_system::adjust_favor(world_, player_, log_, gain);
            }
        }

        // Rival paragon killed — bonus favor + special message
        if (atk_result.killed && victim_god != GodId::NONE) {
            auto& ginfo = get_god_info(victim_god);
            char rbuf[128];
            snprintf(rbuf, sizeof(rbuf),
                "A rival paragon falls. The servant of %s is no more.", ginfo.name);
            log_.add(rbuf, {220, 200, 100, 255});
            if (world_.has<GodAlignment>(player_)) {
                god_system::adjust_favor(world_, player_, log_, 10); // large favor bonus
            }
        }

        // Vampirism heal on kill
        if (atk_result.killed && world_.has<Diseases>(player_)
            && world_.get<Diseases>(player_).has(DiseaseId::VAMPIRISM)
            && world_.has<Stats>(player_)) {
            auto& ps = world_.get<Stats>(player_);
            int heal = std::min(3 + ps.level, ps.hp_max - ps.hp);
            if (heal > 0) {
                ps.hp += heal;
                char hbuf[64];
                snprintf(hbuf, sizeof(hbuf), "You drink in the kill. (+%d HP)", heal);
                log_.add(hbuf, {180, 80, 80, 255});
            }
        }

        // Trait: Venomous — 10% chance to poison on hit
        if (atk_result.hit && !atk_result.killed) {
            for (auto tid : build_traits_) {
                if (tid == TraitId::VENOMOUS && rng_.chance(10) && world_.has<Stats>(target)) {
                    if (!world_.has<StatusEffects>(target))
                        world_.add<StatusEffects>(target, {});
                    world_.get<StatusEffects>(target).add(StatusType::POISON, 2, 6);
                    log_.add("Your venomous strike poisons the enemy.", {100, 200, 80, 255});
                    break;
                }
            }
        }

        // Trait: Bloodlust — heal on kill
        if (atk_result.killed && world_.has<Stats>(player_)) {
            for (TraitId tid : build_traits_) {
                auto& tr = get_trait_info(tid);
                if (tr.hp_on_kill > 0) {
                    auto& ps = world_.get<Stats>(player_);
                    int heal = std::min(tr.hp_on_kill, ps.hp_max - ps.hp);
                    if (heal > 0) {
                        ps.hp += heal;
                        char hbuf[64];
                        snprintf(hbuf, sizeof(hbuf), "Bloodlust! (+%d HP)", heal);
                        log_.add(hbuf, {200, 100, 100, 255});
                    }
                }
            }
        }

        // Background passives on kill
        if (atk_result.killed) {
            if (background_ == BackgroundId::NOBLE_EXILE) {
                // Silver Tongue: +2-5 bonus gold on kill
                int bonus_gold = rng_.range(2, 5);
                gold_ += bonus_gold; run_gold_earned_ += bonus_gold;
            }
            if (background_ == BackgroundId::EXECUTIONER && world_.has<Stats>(player_)) {
                // Clean Strike: first kill per floor restores 5 HP
                auto& ps = world_.get<Stats>(player_);
                int heal = std::min(5, ps.hp_max - ps.hp);
                if (heal > 0) {
                    ps.hp += heal;
                    char hbuf[64];
                    snprintf(hbuf, sizeof(hbuf), "Clean strike. (+%d HP)", heal);
                    log_.add(hbuf, {200, 180, 140, 255});
                }
            }
        }

        // Crow scavenges gold from kills
        if (atk_result.killed && world_.has<Inventory>(player_)) {
            Entity pet_item = world_.get<Inventory>(player_).get_equipped(EquipSlot::PET);
            if (pet_item != NULL_ENTITY && world_.has<Item>(pet_item)
                && world_.get<Item>(pet_item).pet_id == static_cast<int>(PetId::CROW)) {
                int scav = rng_.range(1, 5 + dungeon_level_);
                gold_ += scav; run_gold_earned_ += scav;
                char sbuf[64];
                snprintf(sbuf, sizeof(sbuf), "Your crow scavenges %d gold.", scav);
                log_.add(sbuf, {200, 190, 100, 255});
            }
        }

        // Check for level-up
        if (world_.has<Stats>(player_) && world_.get<Stats>(player_).level > level_before) {
            pending_levelup_ = false;
            // Grant a passive tree point on level-up (player opens tree with T)
            if (world_.has<PassiveTreeState>(player_)) {
                world_.get<PassiveTreeState>(player_).grant_point();
            }
            audio_.play(SfxId::LEVELUP);
            start_transition(TransitionType::FLASH, 250, {255, 255, 200, 255});
            if (!tips_shown_.first_levelup) {
                tips_shown_.first_levelup = true;
                tutorial_popup_.show("Level Up",
                    "You earned a passive tree point!\n\n"
                    "T - Open the passive tree to spend it\n"
                    "I - Check inventory and equipment\n"
                    "C - View character sheet");
            }
        }
        return;
    }

    if (!map_.is_walkable(nx, ny)) return;

    // Mirror sprite when moving left (sprites face right by default)
    if (world_.has<Renderable>(player_)) {
        if (nx > pos.x) world_.get<Renderable>(player_).flip_h = true;
        else if (nx < pos.x) world_.get<Renderable>(player_).flip_h = false;
    }

    // Pet follows player — move to player's old position
    if (pet_entity_ != NULL_ENTITY && world_.has<Position>(pet_entity_)) {
        auto& pet_pos = world_.get<Position>(pet_entity_);
        pet_pos.x = pos.x;
        pet_pos.y = pos.y;
        // Pet faces same direction as player
        if (world_.has<Renderable>(pet_entity_)) {
            auto& pet_rend = world_.get<Renderable>(pet_entity_);
            if (nx > pos.x) pet_rend.flip_h = true;
            else if (nx < pos.x) pet_rend.flip_h = false;
        }
    }

    // Check fled combat: was there an adjacent hostile before we moved?
    {
        auto& ai_pool_fc = world_.pool<AI>();
        for (size_t fi = 0; fi < ai_pool_fc.size(); fi++) {
            Entity fe = ai_pool_fc.entity_at(fi);
            if (!world_.has<Position>(fe)) continue;
            auto& fep = world_.get<Position>(fe);
            if (std::abs(fep.x - pos.x) <= 1 && std::abs(fep.y - pos.y) <= 1
                && !(fep.x == pos.x && fep.y == pos.y)) {
                auto& fai = ai_pool_fc.at_index(fi);
                if (fai.state == AIState::HUNTING) {
                    turn_actions_.fled_combat = true;
                    break;
                }
            }
        }
    }

    pos.x = nx;
    pos.y = ny;
    player_acted_ = true;
    last_bumped_npc_ = NULL_ENTITY;
    npc_bump_count_ = 0;

    // Sneaking: costs extra turn time, grants stealth XP
    if (sneaking_) {
        game_turn_ += 1; // extra turn cost (effectively half speed)
        if (world_.has<Skills>(player_))
            grant_skill_xp(SkillId::STEALTH, 1);
    }

    // Check for traps at new position
    if (dungeon_level_ > 0 && world_.has<Stats>(player_)) {
        auto& trap_pool = world_.pool<Trap>();
        for (size_t ti = 0; ti < trap_pool.size(); ti++) {
            Entity te = trap_pool.entity_at(ti);
            if (!world_.has<Position>(te)) continue;
            auto& tpos = world_.get<Position>(te);
            if (tpos.x != nx || tpos.y != ny) continue;
            auto& trap = trap_pool.at_index(ti);
            if (trap.triggered) continue;

            // PER check to detect before triggering
            auto& pstats = world_.get<Stats>(player_);
            int per_roll = rng_.range(1, 20) + pstats.attr(Attr::PER) / 2;
            // Tree trap detection bonus
            if (world_.has<PassiveTreeState>(player_)) {
                auto tb = passive_tree::compute_bonuses(world_.get<PassiveTreeState>(player_));
                per_roll += tb.trap_detection;
            }

            if (!trap.revealed && per_roll >= trap.difficulty) {
                trap.revealed = true;
                // Add visible sprite for detected trap
                if (!world_.has<Renderable>(te)) {
                    world_.add<Renderable>(te, {SHEET_TILES, trap.sprite_x, trap.sprite_y,
                                                 {255, 200, 100, 200}, -2});
                }
                log_.add("You spot a trap!", {255, 220, 100, 255});
                audio_.play(SfxId::SELECT);
                // Don't trigger, player can step around it now
                break;
            }

            // Unique effect: TRAP_IMMUNITY
            {
                bool trap_immune = false;
                if (world_.has<Inventory>(player_)) {
                    auto& tinv = world_.get<Inventory>(player_);
                    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
                        Entity eq = tinv.equipped[s];
                        if (eq != NULL_ENTITY && world_.has<Item>(eq) &&
                            world_.get<Item>(eq).unique_effect == UniqueEffect::TRAP_IMMUNITY) {
                            trap_immune = true; break;
                        }
                    }
                }
                if (trap_immune) {
                    trap.triggered = true; trap.revealed = true;
                    log_.add("You step over the trap unharmed.", {180, 200, 140, 255});
                    break;
                }
            }
            // Trigger the trap
            trap.triggered = true;
            trap.revealed = true;
            trap_sprite(trap.type, true, trap.sprite_x, trap.sprite_y);
            if (!tips_shown_.first_trap) {
                tips_shown_.first_trap = true;
                tutorial_popup_.show("Trap!",
                    "Traps are hidden on dungeon floors.\n"
                    "High PER reveals them before you step on them.\n\n"
                    "Revealed traps show as floor markings\n"
                    "you can walk around.");
            }
            // Show triggered sprite
            if (!world_.has<Renderable>(te)) {
                world_.add<Renderable>(te, {SHEET_TILES, trap.sprite_x, trap.sprite_y,
                                             {200, 160, 140, 200}, -2});
            } else {
                auto& tr = world_.get<Renderable>(te);
                tr.sprite_x = trap.sprite_x;
                tr.sprite_y = trap.sprite_y;
                tr.tint = {200, 160, 140, 200};
            }

            switch (trap.type) {
                case TrapType::SPIKE:
                    pstats.hp -= trap.damage;
                    log_.add("Spikes pierce your feet!", {255, 100, 80, 255});
                    audio_.play(SfxId::CRIT);
                    break;
                case TrapType::PIT:
                    pstats.hp -= trap.damage;
                    log_.add("You fall into a pit!", {255, 100, 80, 255});
                    audio_.play(SfxId::CRIT);
                    break;
                case TrapType::DART:
                    pstats.hp -= trap.damage;
                    log_.add("A dart shoots from the wall!", {255, 120, 80, 255});
                    audio_.play(SfxId::HIT1);
                    break;
                case TrapType::ALARM: {
                    log_.add("An alarm sounds! Monsters stir.", {255, 180, 80, 255});
                    audio_.play(SfxId::SPELL_IMPACT);
                    // Summon 2-3 monsters at nearby empty tiles
                    for (int si = 0; si < rng_.range(2, 3); si++) {
                        for (int tries = 0; tries < 10; tries++) {
                            int sx = nx + rng_.range(-3, 3);
                            int sy = ny + rng_.range(-3, 3);
                            if (map_.is_walkable(sx, sy) &&
                                combat::entity_at(world_, sx, sy, player_) == NULL_ENTITY) {
                                Entity mob = world_.create();
                                world_.add<Position>(mob, {sx, sy});
                                world_.add<Renderable>(mob, {1, 0, 4, {255, 255, 255, 255}, 5});
                                Stats ms; ms.name = "skeleton"; ms.hp = 12; ms.hp_max = 12;
                                ms.base_damage = 3; ms.xp_value = 15;
                                world_.add<Stats>(mob, std::move(ms));
                                AI mob_ai; mob_ai.state = AIState::HUNTING;
                                mob_ai.last_seen_x = nx; mob_ai.last_seen_y = ny;
                                world_.add<AI>(mob, mob_ai);
                                world_.add<Energy>(mob, {0, 100});
                                world_.add<StatusEffects>(mob);
                                break;
                            }
                        }
                    }
                    break;
                }
                case TrapType::BEAR_TRAP:
                    if (world_.has<StatusEffects>(player_))
                        world_.get<StatusEffects>(player_).add(StatusType::STUNNED, 0, 3);
                    log_.add("A bear trap snaps shut on your leg!", {255, 120, 80, 255});
                    audio_.play(SfxId::CRIT);
                    break;
                case TrapType::POISON_GAS:
                    if (world_.has<StatusEffects>(player_))
                        world_.get<StatusEffects>(player_).add(StatusType::POISON, 3, 5);
                    log_.add("Poison gas erupts from the floor!", {120, 200, 80, 255});
                    audio_.play(SfxId::POISON);
                    break;
            }

            // Floating damage + hit flash for trap damage
            if (trap.type == TrapType::SPIKE || trap.type == TrapType::PIT || trap.type == TrapType::DART) {
                char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%d", trap.damage);
                floating_text_.spawn((float)nx, (float)ny, dbuf, {255, 100, 80, 255});
            }
            // Death check
            if (pstats.hp <= 0) {
                log_.add("You die.", {255, 50, 50, 255});
            }
            break;
        }
    }

    // Update cached location for HUD (avoids per-frame near_town calls)
    if (dungeon_level_ == 0) {
        cached_near_town_ = near_town(nx, ny, 25);
        cached_location_ = (cached_near_town_ >= 0) ? ALL_TOWNS[cached_near_town_].name
                                                      : get_province_name(nx, ny);
    }

    // Town arrival text (first visit only)
    if (dungeon_level_ == 0) {
        int ti = near_town(nx, ny, 20);
        if (ti >= 0 && visited_towns_.find(ti) == visited_towns_.end()) {
            visited_towns_.insert(ti);
            char tbuf[128];
            snprintf(tbuf, sizeof(tbuf), "You arrive at %s.", ALL_TOWNS[ti].name);
            log_.add(tbuf, {200, 190, 160, 255});
            log_.add(ALL_TOWNS[ti].description, {160, 155, 140, 255});
        }
    }

    // Overworld travel events — rare roadside discoveries
    if (dungeon_level_ == 0 && near_town(nx, ny, 40) < 0 && rng_.chance(2)) {
        int ev = rng_.range(1, 100);
        if (ev <= 20) {
            // Find a small pouch of gold
            int amount = rng_.range(5, 20);
            gold_ += amount; run_gold_earned_ += amount;
            char buf[96];
            snprintf(buf, sizeof(buf), "You find a dropped coin purse. (%d gold)", amount);
            log_.add(buf, {255, 220, 80, 255});
            audio_.play(SfxId::GOLD);
        } else if (ev <= 35) {
            // Abandoned campsite with supplies
            static const char* FINDS[] = {
                "An abandoned campsite. Bread left behind.",
                "A traveler's pack, discarded. A potion inside.",
                "An old camp. Dried meat, still edible.",
            };
            log_.add(FINDS[rng_.range(0, 2)], {180, 170, 140, 255});
            // Spawn a food/potion item at player position
            Entity loot = world_.create();
            world_.add<Position>(loot, {nx, ny});
            Item item;
            if (ev <= 25) {
                item.name = "bread"; item.description = "Restores 8 HP.";
                item.type = ItemType::FOOD; item.heal_amount = 8;
                item.gold_value = 5; item.identified = true;
            } else {
                item.name = "healing potion"; item.description = "Restores 15 HP.";
                item.type = ItemType::POTION; item.heal_amount = 15;
                item.gold_value = 25; item.unid_name = "red potion";
            }
            world_.add<Renderable>(loot, {SHEET_ITEMS, 1, item.type == ItemType::FOOD ? 25 : 19,
                                          {255,255,255,255}, 1});
            world_.add<Item>(loot, std::move(item));
        } else if (ev <= 55) {
            // Atmospheric flavor — no reward, just world-building
            static const char* FLAVOR[] = {
                "Old wheel ruts in the dirt. A cart passed recently.",
                "A faded trail marker, half-buried.",
                "Boot prints in the mud. Someone was running.",
                "A crow watches you from a dead tree.",
                "Wildflowers grow over an old grave.",
                "Broken arrows scattered on the ground. A fight happened here.",
                "A stone boundary marker. The inscription is worn smooth.",
                "Wind carries the smell of smoke from somewhere distant.",
            };
            log_.add(FLAVOR[rng_.range(0, 7)], {140, 135, 120, 255});
        } else if (ev <= 70) {
            // Province-specific flavor
            const char* prov = get_province_name(nx, ny);
            if (std::string(prov) == "Frozen Marches")
                log_.add("Frost clings to the rocks here. Even the air bites.", {160, 180, 200, 255});
            else if (std::string(prov) == "Greenwood")
                log_.add("The canopy thickens. Birdsong echoes between the trunks.", {100, 160, 100, 255});
            else if (std::string(prov) == "Dust Provinces")
                log_.add("Dust devils spin in the distance. The land is parched.", {180, 160, 120, 255});
            else if (std::string(prov) == "Iron Coast")
                log_.add("Salt air and the sound of distant hammers.", {150, 160, 170, 255});
            else if (std::string(prov) == "Pale Reach")
                log_.add("The wind carries ash. Braziers burn on a distant hill.", {180, 160, 140, 255});
            else
                log_.add("Rolling fields stretch to the horizon. Heartland country.", {150, 160, 130, 255});
        }
    }

    // Terrain-aware footsteps (not every step — ~40% chance)
    if (rng_.chance(40)) {
        auto dest_type = map_.at(nx, ny).type;
        SfxId step;
        switch (dest_type) {
            case TileType::WATER:
                step = static_cast<SfxId>(static_cast<int>(SfxId::STEP_WATER1) + rng_.range(0, 2));
                break;
            case TileType::FLOOR_GRASS:
            case TileType::FLOOR_DIRT:
            case TileType::FLOOR_SAND:
            case TileType::BRUSH:
                step = static_cast<SfxId>(static_cast<int>(SfxId::STEP_DIRT1) + rng_.range(0, 2));
                break;
            case TileType::FLOOR_STONE:
            case TileType::FLOOR_RED_STONE:
            case TileType::FLOOR_ICE:
            case TileType::FLOOR_SNOW:
            case TileType::FLOOR_COBBLE:
            case TileType::FLOOR_BONE:
                step = static_cast<SfxId>(static_cast<int>(SfxId::STEP_STONE1) + rng_.range(0, 2));
                break;
            default:
                step = static_cast<SfxId>(static_cast<int>(SfxId::STEP_STONE1) + rng_.range(0, 2));
                break;
        }
        audio_.play(step);
    }

    // Stairs message
    if (tile.type == TileType::STAIRS_DOWN) {
        log_.add("Stairs descend further into the dark.", {150, 140, 130, 255});
    }

    // God shrine interaction
    if (map_.at(nx, ny).type == TileType::SHRINE && world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        GodId shrine_god = static_cast<GodId>(map_.at(nx, ny).variant % GOD_COUNT);
        auto& sginfo = get_god_info(shrine_god);
        auto& pginfo = get_god_info(ga.god);

        if (shrine_god == ga.god) {
            // Same god shrine: +5 favor, small heal, identify curse/bless
            god_system::adjust_favor(world_, player_, log_, 5);
            auto& ps = world_.get<Stats>(player_);
            int heal = std::min(5, ps.hp_max - ps.hp);
            ps.hp += heal;
            char sbuf[128];
            snprintf(sbuf, sizeof(sbuf), "A shrine of %s. You feel your god's presence. (+5 favor, +%d HP)", sginfo.name, heal);
            if (!tips_shown_.first_shrine) {
                tips_shown_.first_shrine = true;
                tutorial_popup_.show("God Shrine",
                    "Shrines of your god heal you, identify\n"
                    "items, and let you respec passive tree\n"
                    "points (costs 10 favor).\n\n"
                    "Other gods' shrines may help or harm.");
            }
            log_.add(sbuf, {sginfo.color.r, sginfo.color.g, sginfo.color.b, 255});
            // Identify curse/bless on all equipped items
            if (world_.has<Inventory>(player_)) {
                auto& inv = world_.get<Inventory>(player_);
                for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
                    Entity eq = inv.equipped[s];
                    if (eq != NULL_ENTITY && world_.has<Item>(eq))
                        world_.get<Item>(eq).identified = true;
                }
            }
            audio_.play(SfxId::PRAYER);
            // Respec: refund last 3 passive tree points (costs 10 favor)
            if (world_.has<PassiveTreeState>(player_) && ga.favor >= 10) {
                auto& tree = world_.get<PassiveTreeState>(player_);
                if (tree.points_spent > 1) { // keep at least start node
                    int refund = std::min(3, tree.points_spent - 1);
                    // Find and deallocate the last N allocated nodes
                    // Walk nodes in reverse ID order, deallocate if allocated and not start
                    const auto* nodes = passive_tree::nodes();
                    int count = passive_tree::node_count();
                    int refunded = 0;
                    for (int ni = count - 1; ni >= 0 && refunded < refund; ni--) {
                        uint16_t nid = nodes[ni].id;
                        if (nid == tree.start_node) continue;
                        if (tree.is_allocated(nid)) {
                            // Reverse stat effects
                            auto& rs = world_.get<Stats>(player_);
                            for (int ei = 0; ei < 4; ei++) {
                                auto& eff = nodes[ni].effects[ei];
                                switch (eff.type) {
                                    case EffectType::BONUS_STR: rs.set_attr(Attr::STR, rs.attr(Attr::STR) - eff.value); break;
                                    case EffectType::BONUS_DEX: rs.set_attr(Attr::DEX, rs.attr(Attr::DEX) - eff.value); break;
                                    case EffectType::BONUS_CON: rs.set_attr(Attr::CON, rs.attr(Attr::CON) - eff.value); break;
                                    case EffectType::BONUS_INT: rs.set_attr(Attr::INT, rs.attr(Attr::INT) - eff.value); break;
                                    case EffectType::BONUS_WIL: rs.set_attr(Attr::WIL, rs.attr(Attr::WIL) - eff.value); break;
                                    case EffectType::BONUS_PER: rs.set_attr(Attr::PER, rs.attr(Attr::PER) - eff.value); break;
                                    case EffectType::BONUS_HP: rs.hp_max -= eff.value; rs.hp = std::min(rs.hp, rs.hp_max); break;
                                    case EffectType::BONUS_MP: rs.mp_max -= eff.value; rs.mp = std::min(rs.mp, rs.mp_max); break;
                                    case EffectType::BONUS_SPEED: rs.base_speed -= eff.value; break;
                                    case EffectType::BONUS_ARMOR: rs.natural_armor -= eff.value; break;
                                    default: break;
                                }
                            }
                            tree.deallocate(nid);
                            refunded++;
                        }
                    }
                    if (refunded > 0) {
                        ga.favor -= 10;
                        char rbuf[64];
                        snprintf(rbuf, sizeof(rbuf), "The shrine purges %d passive nodes. (-10 favor)", refunded);
                        log_.add(rbuf, {sginfo.color.r, sginfo.color.g, sginfo.color.b, 255});
                    }
                }
            }
        } else if (ga.god == GodId::NONE) {
            // Godless — shrines are just stone to you
            char sbuf[128];
            snprintf(sbuf, sizeof(sbuf), "A shrine of %s. It means nothing to you.", sginfo.name);
            log_.add(sbuf, {128, 128, 128, 255});
        } else if (ga.favor <= -100) {
            // Excommunicated — can convert at a rival god's shrine
            ga.god = shrine_god;
            ga.favor = 0;
            // Permanent penalty: -2 to all attributes
            auto& conv_stats = world_.get<Stats>(player_);
            for (int ai = 0; ai < ATTR_COUNT; ai++)
                conv_stats.attributes[ai] = std::max(1, conv_stats.attributes[ai] - 2);
            // Update sprite tint
            if (world_.has<Renderable>(player_)) {
                auto& rend = world_.get<Renderable>(player_);
                rend.tint.r = static_cast<Uint8>(255 - (255 - sginfo.color.r) / 5);
                rend.tint.g = static_cast<Uint8>(255 - (255 - sginfo.color.g) / 5);
                rend.tint.b = static_cast<Uint8>(255 - (255 - sginfo.color.b) / 5);
            }
            char sbuf[128];
            snprintf(sbuf, sizeof(sbuf), "You renounce your old faith. %s accepts you. (-2 all attributes)", sginfo.name);
            log_.add(sbuf, {sginfo.color.r, sginfo.color.g, sginfo.color.b, 255});
        } else {
            // Different god shrine — slight favor from your own god
            god_system::adjust_favor(world_, player_, log_, 2);
            char sbuf[128];
            snprintf(sbuf, sizeof(sbuf), "A shrine of %s. %s watches.", sginfo.name, pginfo.name);
            log_.add(sbuf, {sginfo.color.r, sginfo.color.g, sginfo.color.b, 255});
        }
        particles_.prayer_effect(nx, ny, sginfo.color.r, sginfo.color.g, sginfo.color.b);
    }
}

void Engine::open_door(int x, int y) {
    map_.at(x, y).type = TileType::DOOR_OPEN;
    log_.add("You push open the heavy door.", {150, 140, 130, 255});
    audio_.play(SfxId::DOOR);
}

void Engine::process_turn() {
    if (!player_acted_) return;
    player_acted_ = false;
    game_turn_++;

    // Drain pending quest kills (from combat::kill, covers melee/ranged/spell/prayer)
    for (int qid_raw : world_.pending_quest_kills) {
        auto qid = static_cast<QuestId>(qid_raw);
        if (journal_.has_quest(qid) && journal_.get_state(qid) == QuestState::ACTIVE) {
            journal_.set_state(qid, QuestState::COMPLETE);
            auto& qinfo = get_quest_info(qid);
            bool is_main = (static_cast<int>(qid) < 20);
            if (is_main) {
                log_.add("The threat is ended. The path forward opens.", {140, 220, 140, 255});
            } else {
                static const char* DONE[] = {
                    "It's done. Time to collect your reward.",
                    "Another problem solved. Head back to town.",
                    "The deed is done.",
                };
                log_.add(DONE[rng_.range(0, 2)], {120, 200, 120, 255});
            }
            char qbuf[128];
            snprintf(qbuf, sizeof(qbuf), "Quest complete: %s", qinfo.name);
            log_.add(qbuf, {120, 220, 120, 255});
            audio_.play(SfxId::QUEST);
        }
    }
    world_.pending_quest_kills.clear();

    // Recalculate equipment-derived stats (unique effects)
    if (world_.has<Stats>(player_) && world_.has<Inventory>(player_)) {
        auto& pstats = world_.get<Stats>(player_);
        auto& pinv = world_.get<Inventory>(player_);
        pstats.fov_bonus = 0;
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            Entity eq = pinv.equipped[s];
            if (eq == NULL_ENTITY || !world_.has<Item>(eq)) continue;
            if (world_.get<Item>(eq).unique_effect == UniqueEffect::LIGHT_RADIUS)
                pstats.fov_bonus += 2;
        }
    }

    // Check tenet violations for this turn's actions
    check_tenets();
    turn_actions_.clear();

    // Tick capstone cooldowns and handle duration-based effects
    if (world_.has<PassiveTreeState>(player_) && world_.has<Stats>(player_)) {
        auto& tree = world_.get<PassiveTreeState>(player_);
        auto& pstats = world_.get<Stats>(player_);
        for (int i = 0; i < PassiveTreeState::MAX_CAPSTONES; i++) {
            if (tree.capstone_cooldowns[i] > 0)
                tree.capstone_cooldowns[i]--;
            // Aspect of Beast (index 5): negative value = active duration
            if (i == 5 && tree.capstone_cooldowns[i] < 0) {
                tree.capstone_cooldowns[i]++;
                if (tree.capstone_cooldowns[i] == 0) {
                    // Duration expired: remove the stat bonuses
                    for (int a = 0; a < ATTR_COUNT; a++)
                        pstats.attributes[a] -= 5;
                    pstats.base_damage -= 3;
                    pstats.hp_max -= 15;
                    if (pstats.hp > pstats.hp_max) pstats.hp = pstats.hp_max;
                    tree.capstone_cooldowns[i] = 50; // start real cooldown
                    log_.add("The beast within subsides.", {80, 160, 80, 255});
                }
            }
        }
    }

    // Excommunication punishments — periodic divine wrath when favor <= -100
    if (world_.has<GodAlignment>(player_) && world_.has<Stats>(player_)) {
        auto& align = world_.get<GodAlignment>(player_);
        if (align.god != GodId::NONE && align.favor <= -100) {
            // Random divine damage every ~15 turns
            if (game_turn_ % 15 == 0 && rng_.chance(60)) {
                auto& ps = world_.get<Stats>(player_);
                int dmg = rng_.range(2, 6);
                ps.hp -= dmg;
                auto& ginfo = get_god_info(align.god);
                char wbuf[128];
                snprintf(wbuf, sizeof(wbuf), "%s's wrath strikes you! (%d damage)", ginfo.name, dmg);
                log_.add(wbuf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                auto& pp = world_.get<Position>(player_);
                particles_.crit_flash(pp.x, pp.y);
                audio_.play(SfxId::CRIT);
            }
            // Random stat drain every ~40 turns
            if (game_turn_ % 40 == 0 && rng_.chance(40)) {
                auto& ps = world_.get<Stats>(player_);
                int attr = rng_.range(0, ATTR_COUNT - 1);
                if (ps.attributes[attr] > 1) {
                    ps.attributes[attr]--;
                    auto& ginfo = get_god_info(align.god);
                    static const char* ATTR_NAMES[] = {"STR", "DEX", "CON", "INT", "WIL", "PER", "CHA"};
                    char dbuf[128];
                    snprintf(dbuf, sizeof(dbuf), "You feel %s's curse draining your %s. (-1 %s)",
                             ginfo.name, ATTR_NAMES[attr], ATTR_NAMES[attr]);
                    log_.add(dbuf, {180, 80, 80, 255});
                }
            }
        }
    }

    // Check player death
    if (world_.has<Stats>(player_)) {
        auto& stats = world_.get<Stats>(player_);
        if (stats.hp <= 0) {
            state_ = GameState::DEAD;
            end_screen_time_ = SDL_GetTicks();
            audio_.play(SfxId::DEATH);
            audio_.stop_all_ambient(500);
            audio_.play_music(MusicId::DEATH, 1500);
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
    ai::process(world_, map_, player_, rng_, sneaking_);

    // NPC wandering (overworld only, every 3 turns to reduce CPU)
    if (dungeon_level_ == 0 && game_turn_ % 3 == 0) {
        process_npc_wander();
        // Check for town proximity music change every 10 turns
        if (game_turn_ % 10 == 0) update_music_for_location();
    }

    // Ambient NPC reactions to the brand
    if (game_turn_ % 15 == 0 && world_.has<Position>(player_) && world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        if (ga.god != GodId::NONE) {
            auto& pp = world_.get<Position>(player_);
            auto& npc_pool = world_.pool<NPC>();
            for (size_t i = 0; i < npc_pool.size(); i++) {
                Entity ne = npc_pool.entity_at(i);
                if (!world_.has<Position>(ne)) continue;
                auto& npos = world_.get<Position>(ne);
                int nd = std::max(std::abs(npos.x - pp.x), std::abs(npos.y - pp.y));
                if (nd > 3 || nd == 0) continue;
                if (!map_.in_bounds(npos.x, npos.y) || !map_.at(npos.x, npos.y).visible) continue;
                // Skip quest NPCs and church priests
                auto& npc = npc_pool.at_index(i);
                if (npc.quest_id >= 0 || world_.has<Church>(ne)) continue;
                // Only trigger occasionally per NPC
                if (!rng_.chance(8)) continue;

                static const char* REACTIONS[] = {
                    "\"That mark...\"",
                    "\"Stay away from me, branded one.\"",
                    "\"The priests say you're a sign of what's coming.\"",
                    "\"I've heard about people like you. None of them lasted long.\"",
                    "\"Your face. It's glowing.\"",
                    "\"My grandmother told stories about the branded. I thought they were myths.\"",
                    "\"Don't bring that curse near my family.\"",
                    "\"The ground shook the night you came. Everyone knows.\"",
                    "\"You're the one from the road, aren't you? The one they found.\"",
                    "\"That light on your face. It's getting brighter.\"",
                    "\"Some say the branded are chosen. Others say they're bait.\"",
                    "\"I saw another like you once, years ago. She walked into the hills and never came back.\"",
                };
                int ri = rng_.range(0, 11);
                log_.add(REACTIONS[ri], {160, 150, 130, 255});
                break; // only one reaction per turn
            }
        }
    }

    // Overworld enemy spawning
    if (dungeon_level_ == 0 && game_turn_ % 12 == 0) {
        try_spawn_overworld_enemy();
    }

    // Overworld random encounters (every ~40 turns, 30% chance)
    if (dungeon_level_ == 0 && game_turn_ % 40 == 0 && rng_.chance(30)
        && world_.has<Position>(player_) && world_.has<Stats>(player_)) {
        auto& pp = world_.get<Position>(player_);
        auto& ps = world_.get<Stats>(player_);
        int roll = rng_.range(1, 100);

        if (roll <= 25) {
            // Merchant caravan: sell a rare item
            static const char* WARES[] = {
                "a silver dagger", "a mithril ring", "a Tome of Phase",
                "a strong healing potion", "a blessed amulet", "an antidote bundle"
            };
            int ware = rng_.range(0, 5);
            char ebuf[128];
            snprintf(ebuf, sizeof(ebuf), "A merchant caravan passes. They offer %s for %d gold.",
                     WARES[ware], 30 + ps.level * 10);
            log_.add(ebuf, {200, 190, 140, 255});
            log_.add("(They vanish before you can respond.)", {140, 135, 120, 255});
            // TODO: actual trade interaction
        } else if (roll <= 45) {
            // Wounded traveler
            bool can_heal = false;
            if (world_.has<Skills>(player_)) {
                int heal_lv = world_.get<Skills>(player_).get_level(SkillId::HEALING);
                int nat_lv = world_.get<Skills>(player_).get_level(SkillId::NATURE_MAGIC);
                if (heal_lv >= 10 || nat_lv >= 10) can_heal = true;
            }
            if (can_heal) {
                int reward = rng_.range(15, 40);
                gold_ += reward;
                log_.add("A wounded traveler lies by the road. You tend their injuries.", {140, 200, 140, 255});
                char rbuf[64];
                snprintf(rbuf, sizeof(rbuf), "\"Thank you, friend.\" (+%d gold)", reward);
                log_.add(rbuf, {200, 200, 100, 255});
                if (world_.has<Skills>(player_))
                    grant_skill_xp(SkillId::HEALING, 5);
            } else {
                log_.add("A wounded traveler lies by the road. You lack the skill to help.", {160, 140, 120, 255});
            }
        } else if (roll <= 60) {
            // Hermit offers to teach a spell (requires INT 12+)
            if (ps.attr(Attr::INT) >= 12 && world_.has<Spellbook>(player_)) {
                static const SpellId TEACH[] = {
                    SpellId::MINOR_HEAL, SpellId::SPARK, SpellId::DETECT_MONSTERS,
                    SpellId::IDENTIFY, SpellId::HARDEN_SKIN, SpellId::CURE_POISON,
                };
                auto spell = TEACH[rng_.range(0, 5)];
                auto& sinfo = get_spell_info(spell);
                auto& book = world_.get<Spellbook>(player_);
                if (!book.knows(spell)) {
                    book.learn(spell);
                    char tbuf[128];
                    snprintf(tbuf, sizeof(tbuf), "A wandering hermit teaches you %s.", sinfo.name);
                    log_.add(tbuf, {160, 180, 200, 255});
                } else {
                    log_.add("A hermit offers wisdom, but you already know what they teach.", {140, 140, 130, 255});
                }
            } else {
                log_.add("A hermit mutters strange words and wanders off.", {140, 130, 120, 255});
            }
        } else if (roll <= 75) {
            // Roadside shrine: temporary buff
            if (world_.has<GodAlignment>(player_)) {
                auto& ga = world_.get<GodAlignment>(player_);
                if (ga.god != GodId::NONE) {
                    auto& ginfo = get_god_info(ga.god);
                    god_system::adjust_favor(world_, player_, log_, 3);
                    ps.hp = std::min(ps.hp + 10, ps.hp_max);
                    char sbuf[96];
                    snprintf(sbuf, sizeof(sbuf), "You find a roadside shrine to %s. (+3 favor, +10 HP)", ginfo.name);
                    log_.add(sbuf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                } else {
                    log_.add("You pass an old shrine. It means nothing to you.", {130, 130, 120, 255});
                }
            }
        } else if (roll <= 85) {
            // Lost supply cache: free consumables
            int heal = rng_.range(10, 25);
            ps.hp = std::min(ps.hp + heal, ps.hp_max);
            int gfind = rng_.range(5, 20);
            gold_ += gfind;
            char cbuf[96];
            snprintf(cbuf, sizeof(cbuf), "You stumble on an abandoned pack. (+%d HP, +%d gold)", heal, gfind);
            log_.add(cbuf, {180, 200, 140, 255});
        } else if (roll <= 92) {
            // Strange weather event (province-flavored)
            GodId region = get_town_god(pp.x, pp.y);
            switch (region) {
                case GodId::GATHRUUN:
                    log_.add("The ground trembles underfoot. Rocks shift in the distance.", {160, 160, 180, 255});
                    break;
                case GodId::SOLETH:
                    log_.add("A column of fire flickers on the horizon and dies.", {255, 180, 80, 255});
                    break;
                case GodId::KHAEL:
                    log_.add("The trees sway without wind. Something moves through the canopy.", {120, 180, 120, 255});
                    break;
                case GodId::SYTHARA:
                    log_.add("A foul mist drifts across the road and dissipates.", {160, 200, 140, 255});
                    break;
                case GodId::OSSREN:
                    log_.add("You hear hammering from deep underground. It stops when you listen.", {180, 180, 200, 255});
                    break;
                default:
                    log_.add("The wind shifts direction three times in as many breaths.", {160, 160, 160, 255});
                    break;
            }
        } else {
            // Ambush: tough wandering monster spawns close
            log_.add("Something stirs in the brush nearby.", {200, 160, 100, 255});
            // Spawn a stronger-than-normal enemy at close range
            for (int a = 0; a < 20; a++) {
                int sx = pp.x + rng_.range(-4, 4);
                int sy = pp.y + rng_.range(-4, 4);
                if (sx == pp.x && sy == pp.y) continue;
                if (!map_.in_bounds(sx, sy) || !map_.is_walkable(sx, sy)) continue;
                if (combat::entity_at(world_, sx, sy, player_) != NULL_ENTITY) continue;
                // Spawn a bandit or highwayman ambush
                Entity e = world_.create();
                world_.add<Position>(e, {sx, sy});
                world_.add<Renderable>(e, {SHEET_ROGUES, 4, 0, {255,255,255,255}, 5});
                Stats ms; ms.name = "ambusher";
                ms.hp = 18 + ps.level * 2; ms.hp_max = ms.hp;
                ms.set_attr(Attr::STR, 12); ms.set_attr(Attr::DEX, 14); ms.set_attr(Attr::CON, 10);
                ms.base_damage = 4 + ps.level / 2; ms.natural_armor = 1;
                ms.base_speed = 110; ms.xp_value = 25 + ps.level * 3;
                world_.add<Stats>(e, std::move(ms));
                AI ai; ai.state = AIState::HUNTING; ai.flee_threshold = 25;
                world_.add<AI>(e, ai);
                world_.add<Energy>(e, {0, 110});
                break;
            }
        }
    }

    // Monsters attack player — melee if adjacent, ranged if in range
    auto& ai_pool = world_.pool<AI>();
    for (size_t i = 0; i < ai_pool.size(); i++) {
        Entity e = ai_pool.entity_at(i);
        if (!world_.has<Position>(e) || !world_.has<Stats>(e)) continue;
        if (!world_.has<Energy>(e) || !world_.get<Energy>(e).can_act()) continue;

        auto& ai_comp = ai_pool.at_index(i);
        if (ai_comp.state != AIState::HUNTING) continue;
        if (ai_comp.friendly) continue; // summons don't attack player

        auto& mpos = world_.get<Position>(e);
        auto& ppos = world_.get<Position>(player_);

        int dx = std::abs(mpos.x - ppos.x);
        int dy = std::abs(mpos.y - ppos.y);
        int dist = std::max(dx, dy);

        if (dist <= 1 && dist > 0) {
            // Melee attack
            auto mresult = combat::melee_attack(world_, e, player_, rng_, log_);
            // Riposte killed the attacker
            if (mresult.attacker_killed) {
                audio_.play(SfxId::DEATH);
                particles_.death_burst(mpos.x, mpos.y);
                continue; // monster is dead, skip rest of its turn
            }
            // Audio feedback for enemy attacks
            if (mresult.critical) audio_.play(SfxId::CRIT);
            else if (mresult.hit) audio_.play_hit();
            else audio_.play_miss();
            if (mresult.hit) {
                auto& pp = world_.get<Position>(player_);
                // Quick-Footed: 15% dodge (negate the hit)
                if (mresult.hit && !mresult.critical) {
                    for (auto tid : build_traits_) {
                        if (tid == TraitId::QUICK_FOOTED && rng_.chance(15)) {
                            // Refund the damage
                            if (world_.has<Stats>(player_)) {
                                world_.get<Stats>(player_).hp += mresult.damage;
                                log_.add("You dodge the attack!", {140, 220, 140, 255});
                            }
                            mresult.hit = false;
                            break;
                        }
                    }
                }
                // Frail: crits deal double damage
                if (mresult.hit && mresult.critical && world_.has<Stats>(player_)) {
                    for (auto tid : build_traits_) {
                        if (tid == TraitId::FRAIL) {
                            world_.get<Stats>(player_).hp -= mresult.damage; // extra damage = double
                            break;
                        }
                    }
                }
                // Trait: Second Wind — 10% chance to heal 3 HP when hit
                if (mresult.hit && world_.has<Stats>(player_)) {
                    for (auto tid : build_traits_) {
                        if (tid == TraitId::SECOND_WIND && rng_.chance(10)) {
                            auto& ps = world_.get<Stats>(player_);
                            int heal = std::min(3, ps.hp_max - ps.hp);
                            if (heal > 0) { ps.hp += heal; }
                            break;
                        }
                    }
                }
                // Background damage reduction
                if (mresult.hit && mresult.damage > 0) {
                    auto& mname2 = world_.get<Stats>(e).name;
                    // Ratcatcher: vermin deal 50% damage
                    if (background_ == BackgroundId::RATCATCHER &&
                        (mname2 == "giant rat" || mname2 == "bat" || mname2 == "giant spider" || mname2 == "kobold")) {
                        int reduce = mresult.damage / 2;
                        if (reduce > 0 && world_.has<Stats>(player_)) {
                            world_.get<Stats>(player_).hp += reduce; // refund half
                        }
                    }
                    // Shipwreck Survivor: +2 effective armor when below 25% HP
                    if (background_ == BackgroundId::SHIPWRECK_SURVIVOR && world_.has<Stats>(player_)) {
                        auto& ps = world_.get<Stats>(player_);
                        if (ps.hp * 4 < ps.hp_max) {
                            int reduce = std::min(2, mresult.damage);
                            ps.hp += reduce;
                        }
                    }
                    // Pit Fighter: -1 damage from natural/unarmed attacks
                    if (background_ == BackgroundId::PIT_FIGHTER && world_.has<Stats>(player_)) {
                        world_.get<Stats>(player_).hp += 1; // refund 1 damage
                    }
                }
                // Floating damage number + hit flash
                {
                    char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%d", mresult.damage);
                    floating_text_.spawn(pp.x, pp.y, dbuf, {255, 80, 80, 255}, mresult.critical);

                }
                if (mresult.critical) {
                    particles_.crit_flash(pp.x, pp.y); trigger_screen_shake(5.0f);
                    // Cowardly trait: crits cause fear
                    if (world_.has<StatusEffects>(player_)) {
                        for (auto tid : build_traits_) {
                            if (tid == TraitId::COWARDLY) {
                                world_.get<StatusEffects>(player_).add(StatusType::FEARED, 0, 2);
                                log_.add("Fear grips you!", {255, 200, 200, 255});
                                break;
                            }
                        }
                    }
                }
                else particles_.blood(pp.x, pp.y);
                // Track what hit us for death screen
                if (world_.has<Stats>(e))
                    death_cause_ = world_.get<Stats>(e).name;
            } else {
                // Monster missed: grant dodge skill XP
                if (world_.has<Skills>(player_))
                    grant_skill_xp(SkillId::DODGE, 2);
                // Dodge counter (unique ring): 30% chance to counter-attack
                if (combat::has_unique_effect(world_, player_, UniqueEffect::DODGE_COUNTER) &&
                    rng_.chance(30) && world_.has<Stats>(e) && world_.get<Stats>(e).hp > 0) {
                    int counter_dmg = 3 + (world_.has<Stats>(player_) ? world_.get<Stats>(player_).attr(Attr::DEX) / 2 : 0);
                    world_.get<Stats>(e).hp -= counter_dmg;
                    char cbuf[64];
                    snprintf(cbuf, sizeof(cbuf), "You counter-attack! (%d)", counter_dmg);
                    log_.add(cbuf, {220, 200, 140, 255});
                    audio_.play_hit();
                    if (world_.get<Stats>(e).hp <= 0)
                        combat::kill(world_, e, log_);
                }
            }

            // Heavy armor XP: gain when hit while wearing heavy armor
            if (mresult.hit && world_.has<Skills>(player_) && world_.has<Inventory>(player_)) {
                auto& inv = world_.get<Inventory>(player_);
                Entity chest = inv.get_equipped(EquipSlot::CHEST);
                if (chest != NULL_ENTITY && world_.has<Item>(chest) && world_.get<Item>(chest).armor_bonus >= 4)
                    grant_skill_xp(SkillId::HEAVY_ARMOR, 2);
            }

            // Status effects from monster hits
            if (mresult.hit && world_.has<Stats>(e) && world_.has<StatusEffects>(player_)) {
                auto& mname = world_.get<Stats>(e).name;
                auto& fx = world_.get<StatusEffects>(player_);
                bool inflicted = false;
                SfxId inflict_sfx = SfxId::POISON;
                bool poison_immune = combat::has_unique_effect(world_, player_, UniqueEffect::POISON_IMMUNE);
                bool fire_immune = combat::has_unique_effect(world_, player_, UniqueEffect::FIRE_IMMUNE);
                // Poison: spiders, naga, snakes
                if (!poison_immune && (mname == "giant spider" || mname == "naga" || mname == "snake") && rng_.chance(25)) {
                    fx.add(StatusType::POISON, 2, 5); inflicted = true; inflict_sfx = SfxId::POISON;
                }
                // Bleed: ghouls, wolves, bears
                else if ((mname == "ghoul" || mname == "wolf" || mname == "dire wolf" || mname == "bear") && rng_.chance(20)) {
                    fx.add(StatusType::BLEED, 1, 8); inflicted = true; inflict_sfx = SfxId::CRIT;
                }
                // Burn: dragons
                else if (!fire_immune && mname == "dragon" && rng_.chance(30)) {
                    fx.add(StatusType::BURN, 3, 4); inflicted = true; inflict_sfx = SfxId::BURN;
                }
                // Stun: trolls, orc warchief (heavy hit)
                else if ((mname == "troll" || mname == "orc warchief") && rng_.chance(20)) {
                    fx.add(StatusType::STUNNED, 0, 1); inflicted = true; inflict_sfx = SfxId::SPELL_IMPACT;
                }
                // Freeze: ice creatures
                else if ((mname == "ice elemental" || mname == "frost drake") && rng_.chance(25)) {
                    fx.add(StatusType::FROZEN, 0, 1); inflicted = true; inflict_sfx = SfxId::SPELL_FREEZE;
                }
                // Confuse: wraiths, banshees
                else if ((mname == "wraith" || mname == "banshee") && rng_.chance(20)) {
                    fx.add(StatusType::CONFUSED, 0, 3); inflicted = true; inflict_sfx = SfxId::CURSE;
                }
                // Fear: death knight on crit
                else if (mname == "death knight" && mresult.critical) {
                    fx.add(StatusType::FEARED, 0, 2); inflicted = true; inflict_sfx = SfxId::CURSE;
                }
                // Blind: basilisk
                else if (mname == "basilisk" && rng_.chance(15)) {
                    fx.add(StatusType::BLIND, 0, 3); inflicted = true; inflict_sfx = SfxId::CURSE;
                }
                if (inflicted) audio_.play(inflict_sfx);
            }
            // Troll/slime regeneration — heals HP per turn if alive
            if (world_.has<Stats>(e)) {
                auto& ms = world_.get<Stats>(e);
                if (ms.name == "troll" && ms.hp > 0 && ms.hp < ms.hp_max)
                    ms.hp = std::min(ms.hp_max, ms.hp + 2);
                if (ms.name == "slime" && ms.hp > 0 && ms.hp < ms.hp_max)
                    ms.hp = std::min(ms.hp_max, ms.hp + 1);
            }
            // Trollblood player class: regenerate 1 HP every 3 turns
            if (e == player_ && creation_screen_.get_build().class_id == ClassId::TROLLBLOOD
                && world_.has<Stats>(player_) && game_turn_ % 3 == 0) {
                auto& ps = world_.get<Stats>(player_);
                if (ps.hp > 0 && ps.hp < ps.hp_max)
                    ps.hp = std::min(ps.hp_max, ps.hp + 1);
            }
            // Unique item regen: 1 HP every 5 turns
            if (e == player_ && game_turn_ % 5 == 0 &&
                combat::has_unique_effect(world_, player_, UniqueEffect::REGEN) &&
                world_.has<Stats>(player_)) {
                auto& ps = world_.get<Stats>(player_);
                if (ps.hp > 0 && ps.hp < ps.hp_max)
                    ps.hp = std::min(ps.hp_max, ps.hp + 1);
            }
            // Stealth regen (unique amulet): 2 HP/turn while sneaking
            if (e == player_ && sneaking_ &&
                combat::has_unique_effect(world_, player_, UniqueEffect::STEALTH_REGEN) &&
                world_.has<Stats>(player_)) {
                auto& ps = world_.get<Stats>(player_);
                if (ps.hp > 0 && ps.hp < ps.hp_max)
                    ps.hp = std::min(ps.hp_max, ps.hp + 2);
            }
            // Fear aura (unique amulet): 10% chance nearby enemies flee
            if (e == player_ && game_turn_ % 3 == 0 &&
                combat::has_unique_effect(world_, player_, UniqueEffect::FEAR_AURA) &&
                world_.has<Position>(player_)) {
                auto& pp = world_.get<Position>(player_);
                auto& ai_fear = world_.pool<AI>();
                for (size_t fi = 0; fi < ai_fear.size(); fi++) {
                    Entity fe = ai_fear.entity_at(fi);
                    if (!world_.has<Position>(fe)) continue;
                    auto& fp = world_.get<Position>(fe);
                    if (std::abs(fp.x - pp.x) <= 2 && std::abs(fp.y - pp.y) <= 2 &&
                        ai_fear.at_index(fi).state == AIState::HUNTING && rng_.chance(10)) {
                        ai_fear.at_index(fi).state = AIState::FLEEING;
                    }
                }
            }
            // Haste turns: tick down
            if (e == player_ && world_.has<Stats>(player_)) {
                auto& ps = world_.get<Stats>(player_);
                if (ps.haste_turns > 0) ps.haste_turns--;
            }
            // Skeleton shield — 25% chance to block melee attacks entirely
            // (already handled implicitly by high natural_armor, but add message)
            // Orc warchief — buff adjacent orcs (+2 damage)
            if (world_.has<Stats>(e) && world_.get<Stats>(e).name == "orc warchief" && game_turn_ % 5 == 0) {
                auto& ai_pool_b = world_.pool<AI>();
                for (size_t bi = 0; bi < ai_pool_b.size(); bi++) {
                    Entity be = ai_pool_b.entity_at(bi);
                    if (be == e || !world_.has<Stats>(be) || !world_.has<Position>(be)) continue;
                    auto& bs = world_.get<Stats>(be);
                    auto& bp = world_.get<Position>(be);
                    if (bs.name.find("orc") != std::string::npos &&
                        std::abs(bp.x - mpos.x) <= 2 && std::abs(bp.y - mpos.y) <= 2) {
                        // Temporary damage boost (stacks slowly)
                        if (bs.base_damage < bs.hp_max / 2)
                            bs.base_damage++;
                    }
                }
            }
            // Permanent disease contraction from monster hits
            if (mresult.hit && world_.has<Stats>(e) && world_.has<Diseases>(player_)
                && world_.has<Stats>(player_)) {
                auto& mname = world_.get<Stats>(e).name;
                auto& diseases = world_.get<Diseases>(player_);
                auto& pstats = world_.get<Stats>(player_);
                // CON resist check: d20 + CON/3 >= 15
                auto con_resist = [&]() {
                    return rng_.range(1, 20) + pstats.attr(Attr::CON) / 3 >= 15;
                };
                DiseaseId candidate = DiseaseId::DISEASE_COUNT;
                if (mname == "warg" && rng_.chance(3))
                    candidate = DiseaseId::LYCANTHROPY;
                else if (mname == "lich" && rng_.chance(5))
                    candidate = DiseaseId::VAMPIRISM;
                else if (mname == "dragon" && rng_.chance(3))
                    candidate = DiseaseId::STONESCALE;
                else if (mname == "death knight" && rng_.chance(5))
                    candidate = DiseaseId::MINDFIRE;
                else if (mname == "giant spider" && rng_.chance(5))
                    candidate = DiseaseId::SPOREBLOOM;
                else if (mname == "skeleton" && rng_.chance(5))
                    candidate = DiseaseId::HOLLOW_BONES;
                else if (mname == "naga" && rng_.chance(5))
                    candidate = DiseaseId::BLACKBLOOD;

                if (candidate != DiseaseId::DISEASE_COUNT && !diseases.has(candidate)) {
                    // Chaos Inoculation keystone: immune to disease
                    bool chaos_immune = false;
                    if (world_.has<PassiveTreeState>(player_)) {
                        auto cb = passive_tree::compute_bonuses(world_.get<PassiveTreeState>(player_));
                        chaos_immune = cb.chaos_inoculation;
                    }
                    // Plague Doctor: immune to all diseases
                    if (chaos_immune) {
                        log_.add("Your inoculated body rejects the infection.", {120, 200, 60, 255});
                    } else if (background_ == BackgroundId::PLAGUE_DOCTOR) {
                        log_.add("Your medical training protects you from infection.", {140, 200, 160, 255});
                    } else if (!con_resist()) {
                        if (diseases.contract(candidate)) {
                            auto& info = get_disease_info(candidate);
                            log_.add(info.contraction_msg, {200, 160, 200, 255});
                            // Apply permanent stat modifiers
                            switch (candidate) {
                                case DiseaseId::LYCANTHROPY:
                                    pstats.set_attr(Attr::STR, pstats.attr(Attr::STR) + 3);
                                    pstats.set_attr(Attr::CON, pstats.attr(Attr::CON) + 2);
                                    pstats.set_attr(Attr::CHA, pstats.attr(Attr::CHA) - 3);
                                    pstats.set_attr(Attr::INT, pstats.attr(Attr::INT) - 2);
                                    break;
                                case DiseaseId::VAMPIRISM:
                                    pstats.set_attr(Attr::STR, pstats.attr(Attr::STR) + 2);
                                    pstats.set_attr(Attr::DEX, pstats.attr(Attr::DEX) + 2);
                                    pstats.set_attr(Attr::CON, pstats.attr(Attr::CON) - 3);
                                    pstats.hp_max = std::max(1, pstats.hp_max - 3);
                                    if (pstats.hp > pstats.hp_max) pstats.hp = pstats.hp_max;
                                    break;
                                case DiseaseId::STONESCALE:
                                    pstats.natural_armor += 4;
                                    pstats.set_attr(Attr::DEX, pstats.attr(Attr::DEX) - 3);
                                    pstats.base_speed = std::max(50, pstats.base_speed - 15);
                                    if (world_.has<Energy>(player_))
                                        world_.get<Energy>(player_).speed = pstats.base_speed;
                                    break;
                                case DiseaseId::MINDFIRE:
                                    pstats.set_attr(Attr::INT, pstats.attr(Attr::INT) + 3);
                                    pstats.set_attr(Attr::PER, pstats.attr(Attr::PER) + 2);
                                    pstats.set_attr(Attr::WIL, pstats.attr(Attr::WIL) - 2);
                                    pstats.set_attr(Attr::CON, pstats.attr(Attr::CON) - 2);
                                    break;
                                case DiseaseId::SPOREBLOOM:
                                    pstats.set_attr(Attr::CON, pstats.attr(Attr::CON) + 2);
                                    pstats.set_attr(Attr::CHA, pstats.attr(Attr::CHA) - 2);
                                    pstats.set_attr(Attr::STR, pstats.attr(Attr::STR) - 1);
                                    break;
                                case DiseaseId::HOLLOW_BONES:
                                    pstats.set_attr(Attr::CON, pstats.attr(Attr::CON) - 3);
                                    pstats.set_attr(Attr::DEX, pstats.attr(Attr::DEX) + 3);
                                    pstats.base_speed = std::min(200, pstats.base_speed + 15);
                                    if (world_.has<Energy>(player_))
                                        world_.get<Energy>(player_).speed = pstats.base_speed;
                                    break;
                                case DiseaseId::BLACKBLOOD:
                                    pstats.set_attr(Attr::CHA, pstats.attr(Attr::CHA) - 2);
                                    break;
                                default: break;
                            }
                            audio_.play(SfxId::CURSE);
                        }
                    }
                }
            }
            // Blackblood retaliation — biters take damage
            if (mresult.hit && world_.has<Diseases>(player_)
                && world_.get<Diseases>(player_).has(DiseaseId::BLACKBLOOD)
                && world_.has<Stats>(e)) {
                int retdmg = rng_.range(2, 5);
                world_.get<Stats>(e).hp -= retdmg;
                char rbuf[128];
                snprintf(rbuf, sizeof(rbuf), "Your toxic blood burns the %s. (%d)",
                         world_.get<Stats>(e).name.c_str(), retdmg);
                log_.add(rbuf, {80, 40, 80, 255});
                if (world_.get<Stats>(e).hp <= 0) {
                    combat::kill(world_, e, log_);
                }
            }
        } else if (ai_comp.ranged_range > 0 && dist <= ai_comp.ranged_range && dist > 1) {
            // Ranged attack — check LOS
            if (map_.in_bounds(mpos.x, mpos.y) && map_.at(mpos.x, mpos.y).visible) {
                auto& pp = world_.get<Position>(player_);
                particles_.arrow_trail(mpos.x, mpos.y, pp.x, pp.y);
                audio_.play_bow_fire();
                auto rresult = combat::ranged_attack(world_, e, player_, ai_comp.ranged_damage, rng_, log_);
                if (rresult.hit) {
                    audio_.play_bow_hit(); particles_.hit_spark(pp.x, pp.y);
                    char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%d", rresult.damage);
                    floating_text_.spawn(pp.x, pp.y, dbuf, {255, 80, 80, 255});

                }
                if (rresult.killed) { audio_.play(SfxId::DEATH); }
            }
        } else if (dist <= 6 && dist > 1 && world_.has<Stats>(e) &&
                   map_.in_bounds(mpos.x, mpos.y) && map_.at(mpos.x, mpos.y).visible) {
            // Monster special abilities at range
            auto& mstats = world_.get<Stats>(e);
            death_cause_ = mstats.name; // track for death screen
            if (mstats.name == "lich" && rng_.chance(40)) {
                // Lich casts drain life
                int dmg = 8 + rng_.range(0, 6);
                auto& pstats = world_.get<Stats>(player_);
                pstats.hp -= dmg;
                int healed = std::min(dmg / 2, mstats.hp_max - mstats.hp);
                mstats.hp += healed;
                char mbuf[128];
                snprintf(mbuf, sizeof(mbuf),
                    "The lich drains your life force. (%d)", dmg);
                log_.add(mbuf, {180, 80, 200, 255});
                { auto& lp = world_.get<Position>(player_); particles_.spell_effect(lp.x, lp.y, 140, 60, 180); }
            } else if (mstats.name == "death knight" && dist <= 3 && rng_.chance(25)) {
                // Death knight fear aura — applies FEARED (blocked by Iron Willed)
                if (world_.has<StatusEffects>(player_)) {
                    bool immune = false;
                    for (auto tid : build_traits_) if (get_trait_info(tid).immune_fear) immune = true;
                    if (!immune) world_.get<StatusEffects>(player_).add(StatusType::FEARED, 0, 3);
                    else log_.add("Your iron will resists the fear.", {200, 200, 140, 255});
                }
                log_.add("The death knight's presence freezes your courage.", {160, 100, 160, 255});
            } else if (mstats.name == "naga" && dist <= 4 && rng_.chance(30)) {
                // Naga gaze — applies STUNNED (Glass Jaw extends by 2)
                if (world_.has<StatusEffects>(player_)) {
                    int dur = 2;
                    for (auto tid : build_traits_) if (tid == TraitId::MARKED) dur += 2; // Glass Jaw
                    world_.get<StatusEffects>(player_).add(StatusType::STUNNED, 0, dur);
                }
                log_.add("The naga's gaze locks your muscles.", {255, 255, 100, 255});
            } else if (mstats.name == "wraith" && dist <= 3 && rng_.chance(30)) {
                // Wraith wail — applies CONFUSED (blocked by Iron Willed)
                if (world_.has<StatusEffects>(player_)) {
                    bool immune = false;
                    for (auto tid : build_traits_) if (get_trait_info(tid).immune_confuse) immune = true;
                    if (!immune) {
                        int dur = 4;
                        for (auto tid : build_traits_) if (tid == TraitId::SLOW_WITTED) dur += 2;
                        world_.get<StatusEffects>(player_).add(StatusType::CONFUSED, 0, dur);
                    }
                    else log_.add("Your mind holds firm against the wail.", {200, 200, 140, 255});
                }
                log_.add("The wraith screams. Your thoughts scatter.", {200, 140, 255, 255});
            } else if ((mstats.name == "ice elemental" || mstats.name == "frost drake") && dist <= 3 && rng_.chance(30)) {
                // Ice breath — applies FROZEN
                int dmg = 5 + rng_.range(0, 5);
                world_.get<Stats>(player_).hp -= dmg;
                if (world_.has<StatusEffects>(player_))
                    world_.get<StatusEffects>(player_).add(StatusType::FROZEN, 0, 2);
                char mbuf[128];
                snprintf(mbuf, sizeof(mbuf), "A wave of cold hits you. (%d)", dmg);
                log_.add(mbuf, {140, 200, 255, 255});
            } else if (mstats.name == "basilisk" && dist <= 4 && rng_.chance(20)) {
                // Basilisk gaze — BLIND
                if (world_.has<StatusEffects>(player_))
                    world_.get<StatusEffects>(player_).add(StatusType::BLIND, 0, 5);
                log_.add("The basilisk's gaze sears your vision.", {120, 120, 120, 255});
            } else if (mstats.name == "dragon" && dist <= 3 && rng_.chance(35)) {
                // Dragon breathes fire
                int dmg = 10 + rng_.range(0, 10);
                world_.get<Stats>(player_).hp -= dmg;
                char mbuf[128];
                snprintf(mbuf, sizeof(mbuf),
                    "The dragon breathes fire! (%d)", dmg);
                log_.add(mbuf, {255, 140, 40, 255});
                { auto& fp = world_.get<Position>(player_); particles_.burn_effect(fp.x, fp.y); }
                if (world_.has<StatusEffects>(player_))
                    world_.get<StatusEffects>(player_).add(StatusType::BURN, 3, 3);
            } else {
                continue;
            }
        } else {
            continue;
        }

        if (world_.has<Stats>(player_) && world_.get<Stats>(player_).hp <= 0) {
            state_ = GameState::DEAD;
            end_screen_time_ = SDL_GetTicks();
            audio_.stop_all_ambient(500);
            audio_.play_music(MusicId::DEATH, 1500);
            return;
        }
    }

    // Recompute FOV (blind reduces to 1)
    if (world_.has<Position>(player_) && world_.has<Stats>(player_)) {
        auto& pos = world_.get<Position>(player_);
        auto& stats = world_.get<Stats>(player_);
        int fov_r = stats.fov_radius();
        if (world_.has<StatusEffects>(player_) && world_.get<StatusEffects>(player_).has(StatusType::BLIND))
            fov_r = 1;
        fov::compute(map_, pos.x, pos.y, fov_r);
        camera_.center_on(pos.x, pos.y);
    }

    // Status effects tick
    {
        std::string zone;
        if (current_dungeon_idx_ >= 0 && current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()))
            zone = dungeon_registry_[current_dungeon_idx_].zone;
        auto fx_result = status::process(world_, player_, map_, rng_, log_, audio_, particles_,
                                         game_turn_, dungeon_level_, zone);
        if (fx_result.player_died) {
            death_cause_ = fx_result.death_cause;
            state_ = GameState::DEAD;
            end_screen_time_ = SDL_GetTicks();
            audio_.stop_all_ambient(500);
            audio_.play_music(MusicId::DEATH, 1500);
        }
    }

    // Sepulchre ambient messages
    sepulchre_ambient();

    particles_.update();
    log_.set_turn(game_turn_);
}

void Engine::process_npc_wander() {
    overworld::process_npc_wander(world_, map_, rng_);
}

void Engine::try_spawn_overworld_enemy() {
    overworld::try_spawn_overworld_enemy(world_, map_, rng_, player_);
}

void Engine::adjust_favor(int amount) {
    // Heretic Priest: favor changes are 50% stronger (both gains and losses)
    if (background_ == BackgroundId::HERETIC_PRIEST) {
        amount = amount + (amount > 0 ? amount / 2 : amount / 2);
        if (amount == 0 && background_ == BackgroundId::HERETIC_PRIEST) amount = 1; // minimum ±1
    }
    god_system::adjust_favor(world_, player_, log_, amount);
}

void Engine::check_tenets() {
    god_system::check_tenets(world_, player_, turn_actions_, game_turn_, log_);
}

void Engine::execute_prayer(int prayer_idx) {
    player_acted_ = god_system::execute_prayer(world_, player_, map_, rng_,
                                                log_, audio_, particles_,
                                                camera_, prayer_idx);
}

void Engine::fire_ranged() {
    if (!world_.has<Inventory>(player_) || !world_.has<Position>(player_)) return;

    auto& inv = world_.get<Inventory>(player_);
    Entity weapon_e = inv.get_equipped(EquipSlot::MAIN_HAND);
    if (weapon_e == NULL_ENTITY || !world_.has<Item>(weapon_e)) {
        log_.add("You have no weapon equipped.", {150, 140, 130, 255});
        return;
    }
    auto& weapon = world_.get<Item>(weapon_e);
    if (weapon.range <= 0) {
        log_.add("Your weapon can't be fired.", {150, 140, 130, 255});
        return;
    }

    // Find nearest visible enemy in range
    Entity target = magic::nearest_enemy(world_, player_, map_, weapon.range);
    if (target == NULL_ENTITY) {
        log_.add("No target in range.", {150, 140, 130, 255});
        return;
    }

    int level_before = world_.has<Stats>(player_) ? world_.get<Stats>(player_).level : 0;
    // Capture victim stats before kill
    std::string victim_name;
    int victim_hp = 0, victim_dmg = 0, victim_arm = 0, victim_spd = 0;
    if (world_.has<Stats>(target)) {
        auto& vs = world_.get<Stats>(target);
        victim_name = vs.name;
        victim_hp = vs.hp_max; victim_dmg = vs.base_damage;
        victim_arm = vs.natural_armor; victim_spd = vs.base_speed;
    }

    // Capture target position for particles before potential kill
    int tgt_x = 0, tgt_y = 0;
    if (world_.has<Position>(target)) {
        auto& tp = world_.get<Position>(target);
        tgt_x = tp.x; tgt_y = tp.y;
    }
    auto& shooter = world_.get<Position>(player_);
    auto result = combat::ranged_attack(world_, player_, target, weapon.damage_bonus, rng_, log_);
    player_acted_ = true;
    audio_.play_bow_fire();
    particles_.arrow_trail(shooter.x, shooter.y, tgt_x, tgt_y);
    if (result.hit) {
        audio_.play_bow_hit(); particles_.hit_spark(tgt_x, tgt_y);
        char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%d", result.damage);
        floating_text_.spawn((float)tgt_x, (float)tgt_y, dbuf,
                              result.critical ? SDL_Color{255,200,80,255} : SDL_Color{255,255,255,255},
                              result.critical);
        // Archery skill XP
        if (world_.has<Skills>(player_))
            grant_skill_xp(SkillId::ARCHERY, 2);
    }
    if (result.critical) { trigger_screen_shake(4.0f); }
    if (result.killed) { audio_.play(SfxId::DEATH); particles_.death_burst(tgt_x, tgt_y); trigger_screen_shake(3.0f); }

    // Bestiary entry
    if (result.killed && !victim_name.empty()) {
        auto& entry = bestiary_[victim_name];
        if (entry.kills == 0) {
            entry.name = victim_name;
            entry.hp = victim_hp; entry.damage = victim_dmg;
            entry.armor = victim_arm; entry.speed = victim_spd;
        }
        entry.kills++;
        // Meta tracking
        meta_.total_kills++;
        run_kills_++;
        if (is_undead(victim_name.c_str())) {
            meta_.total_undead_kills++;
            journal_.add_progress(QuestId::SQ_UNDEAD_PATROL);
        }
        if (victim_name == "giant rat")
            journal_.add_progress(QuestId::SQ_RAT_CELLAR);
    }

    // God favor on ranged kill
    if (result.killed && world_.has<GodAlignment>(player_)) {
        auto& align = world_.get<GodAlignment>(player_);
        if (align.god != GodId::NONE) {
            int gain = 1;
            switch (align.god) {
                case GodId::VETHRIK:
                case GodId::SOLETH:
                    if (is_undead(victim_name.c_str())) gain += 2;
                    break;
                case GodId::MORRETH: gain += 1; break;
                case GodId::IXUUL: gain += rng_.range(0, 2); break;
                case GodId::KHAEL:
                    if (is_animal(victim_name.c_str())) gain = -2;
                    break;
                default: break;
            }
            if (gain != 0) god_system::adjust_favor(world_, player_, log_, gain);
        }
    }

    // Level-up check
    if (world_.has<Stats>(player_) && world_.get<Stats>(player_).level > level_before) {
        pending_levelup_ = false;
        if (world_.has<PassiveTreeState>(player_))
            world_.get<PassiveTreeState>(player_).grant_point();
        audio_.play(SfxId::LEVELUP);
    }
}

void Engine::describe_tile(int x, int y) {
    if (!map_.in_bounds(x, y)) {
        log_.add("You see nothing.", {140, 140, 140, 255});
        return;
    }
    auto& tile = map_.at(x, y);
    if (!tile.explored) {
        log_.add("Unexplored.", {100, 100, 100, 255});
        return;
    }

    // Tile description
    const char* desc = "Nothing.";
    switch (tile.type) {
        case TileType::FLOOR_GRASS:    desc = "Tall grass."; break;
        case TileType::FLOOR_DIRT:     desc = "Packed dirt."; break;
        case TileType::FLOOR_STONE:    desc = "Stone floor."; break;
        case TileType::FLOOR_BONE:     desc = "A floor of old bones."; break;
        case TileType::FLOOR_RED_STONE:desc = "Scorched red stone."; break;
        case TileType::FLOOR_SAND:     desc = "Desert sand."; break;
        case TileType::FLOOR_ICE:      desc = "Frozen ground."; break;
        case TileType::BRUSH:          desc = "Dense brush."; break;
        case TileType::WATER:          desc = "Dark water."; break;
        case TileType::TREE:           desc = "A gnarled tree."; break;
        case TileType::ROCK:           desc = "A rocky outcrop."; break;
        case TileType::WALL_DIRT:      desc = "An earthen wall."; break;
        case TileType::WALL_STONE_ROUGH: desc = "Rough stone wall."; break;
        case TileType::WALL_STONE_BRICK: desc = "Mortared stone wall."; break;
        case TileType::WALL_IGNEOUS:   desc = "Volcanic rock."; break;
        case TileType::WALL_LARGE_STONE: desc = "Massive stone blocks."; break;
        case TileType::WALL_CATACOMB:  desc = "Carved catacomb wall."; break;
        case TileType::WALL_WOOD:      desc = "A wooden wall. Rough-hewn planks."; break;
        case TileType::DOOR_CLOSED:    desc = "A closed door."; break;
        case TileType::DOOR_OPEN:      desc = "An open doorway."; break;
        case TileType::STAIRS_DOWN:    desc = "Stairs leading down."; break;
        case TileType::STAIRS_UP:      desc = "Stairs leading up."; break;
        case TileType::SHRINE: {
            GodId sg = static_cast<GodId>(tile.variant % GOD_COUNT);
            auto& sgi = get_god_info(sg);
            static char shrine_buf[128];
            snprintf(shrine_buf, sizeof(shrine_buf), "A shrine of %s. Step on it to interact.", sgi.name);
            desc = shrine_buf;
            break;
        }
        default: break;
    }

    // Check for entities at this position
    auto& positions = world_.pool<Position>();
    bool found_entity = false;
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        auto& p = positions.at_index(i);
        if (p.x != x || p.y != y) continue;

        if (e == player_) {
            log_.add("You.", {200, 200, 200, 255});
            found_entity = true;
        } else if (e == pet_entity_) {
            // Describe the pet
            if (world_.has<Inventory>(player_)) {
                Entity pet_item = world_.get<Inventory>(player_).get_equipped(EquipSlot::PET);
                if (pet_item != NULL_ENTITY && world_.has<Item>(pet_item)) {
                    auto& pi = world_.get<Item>(pet_item);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Your %s.", pi.name.c_str());
                    log_.add(buf, {160, 180, 140, 255});
                } else {
                    log_.add("Your pet.", {160, 180, 140, 255});
                }
            }
            found_entity = true;
        } else if (world_.has<NPC>(e)) {
            auto& npc = world_.get<NPC>(e);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s.", npc.name.c_str());
            log_.add(buf, {180, 180, 140, 255});
            found_entity = true;
        } else if (world_.has<Sign>(e)) {
            auto& sign = world_.get<Sign>(e);
            log_.add(sign.text, {200, 190, 150, 255});
            found_entity = true;
        } else if (world_.has<Stats>(e) && world_.has<AI>(e)) {
            auto& st = world_.get<Stats>(e);
            if (tile.visible) {
                char buf[256];
                if (world_.has<GodAlignment>(e)) {
                    auto& ga = world_.get<GodAlignment>(e);
                    auto& gi = get_god_info(ga.god);
                    snprintf(buf, sizeof(buf), "%s — Paragon of %s. HP %d/%d, Dmg %d, Arm %d.",
                             st.name.c_str(), gi.name, st.hp, st.hp_max, st.melee_damage(), st.protection());
                    log_.add(buf, {200, 160, 200, 255});
                } else {
                    // Monster description with stats + abilities
                    const char* note = "";
                    if (st.name == "troll") note = " Regenerates.";
                    else if (st.name == "giant spider") note = " Poisonous bite.";
                    else if (st.name == "naga") note = " Paralyzing gaze. Poison.";
                    else if (st.name == "lich") note = " Drains life at range.";
                    else if (st.name == "dragon") note = " Breathes fire.";
                    else if (st.name == "death knight") note = " Fear aura.";
                    else if (st.name == "wraith") note = " Confusing wail.";
                    else if (st.name == "basilisk") note = " Blinding gaze.";
                    else if (st.name == "orc warchief") note = " Buffs nearby orcs.";
                    else if (st.name == "goblin archer") note = " Ranged attack.";
                    else if (st.name == "ghoul") note = " Causes bleeding.";
                    else if (st.name == "bat") note = " Fast. Flees when hurt.";
                    else if (st.name == "slime") note = " Slow. Regenerates.";
                    else if (is_undead(st.name.c_str())) note = " Undead.";
                    else if (is_animal(st.name.c_str())) note = " Beast.";
                    snprintf(buf, sizeof(buf), "%s. HP %d/%d, Dmg %d, Arm %d.%s",
                             st.name.c_str(), st.hp, st.hp_max, st.melee_damage(), st.protection(), note);
                    log_.add(buf, {220, 140, 140, 255});
                }
                meta_.total_creatures_examined++;
            } else {
                log_.add("Something was here.", {160, 140, 140, 255});
            }
            found_entity = true;
        } else if (world_.has<Item>(e)) {
            auto& item = world_.get<Item>(e);
            char buf[256];
            if (item.type == ItemType::WEAPON || item.type == ItemType::ARMOR_CHEST ||
                item.type == ItemType::ARMOR_HEAD || item.type == ItemType::ARMOR_FEET ||
                item.type == ItemType::ARMOR_HANDS || item.type == ItemType::SHIELD ||
                item.type == ItemType::AMULET || item.type == ItemType::RING) {
                snprintf(buf, sizeof(buf), "%s. %s", item.display_name().c_str(), item.description.c_str());
            } else {
                snprintf(buf, sizeof(buf), "%s.", item.display_name().c_str());
            }
            SDL_Color examine_col = (item.rarity != Rarity::COMMON)
                ? rarity_color(item.rarity) : SDL_Color{180, 200, 160, 255};
            log_.add(buf, examine_col);
            found_entity = true;
        } else if (world_.has<Container>(e)) {
            auto& cont = world_.get<Container>(e);
            if (cont.opened) {
                log_.add("An opened container.", {140, 130, 120, 255});
            } else {
                log_.add("A closed container. Press g to open.", {180, 170, 140, 255});
            }
            found_entity = true;
        } else if (world_.has<Corpse>(e)) {
            auto& c = world_.get<Corpse>(e);
            char buf[128];
            snprintf(buf, sizeof(buf), "Remains of %s.", c.name.c_str());
            log_.add(buf, {140, 130, 120, 255});
            // Dark Arts 25: speak with dead
            if (world_.has<Skills>(player_)) {
                int da_lv = world_.get<Skills>(player_).get_level(SkillId::DARK_ARTS);
                if (da_lv >= 25) {
                    static const char* WHISPERS[] = {
                        "The dead whispers: \"Deeper... the treasure lies deeper.\"",
                        "A fading voice: \"Beware the one that hides in the walls.\"",
                        "The spirit murmurs: \"I died facing east. The stairs are that way.\"",
                        "A ghostly voice: \"The strong ones gather near the bottom.\"",
                        "The corpse sighs: \"I should have brought more potions.\"",
                        "A whisper: \"The shrines will guide you, if you have faith.\"",
                        "The dead speaks: \"Something powerful guards the lowest floor.\"",
                        "A hollow voice: \"Silver. You need silver for the wraiths.\"",
                    };
                    int widx = rng_.range(0, 7);
                    log_.add(WHISPERS[widx], {160, 120, 200, 255});
                    grant_skill_xp(SkillId::DARK_ARTS, 3);
                }
            }
            found_entity = true;
        }
    }

    if (!found_entity) {
        log_.add(desc, {160, 155, 150, 255});
    }
}

void Engine::update_music_for_location() {
    if (state_ != GameState::PLAYING) return;

    if (dungeon_level_ <= 0) {
        // Overworld — check if near a town
        // Use cached near_town result (updated on player move)
        bool in_town = (cached_near_town_ >= 0);

        // Day/night cycle: 100-turn period (day for first 50, night for last 50)
        bool is_night = ((game_turn_ / 50) % 2) == 1;

        if (in_town) {
            // Only switch if not already playing town music
            if (audio_.current_music() != MusicId::TOWN1 &&
                audio_.current_music() != MusicId::TOWN2) {
                audio_.stop_all_ambient(800);
                MusicId town[] = {MusicId::TOWN1, MusicId::TOWN2};
                audio_.play_music(town[rng_.range(0, 1)], 2000);
                audio_.play_ambient(is_night ? AmbientId::INTERIOR_NIGHT : AmbientId::INTERIOR_DAY, 1500);
            }
        } else {
            // Only switch if not already playing overworld music
            if (audio_.current_music() != MusicId::OVERWORLD1 &&
                audio_.current_music() != MusicId::OVERWORLD2 &&
                audio_.current_music() != MusicId::OVERWORLD3) {
                audio_.stop_all_ambient(800);
                MusicId ow_tracks[] = {MusicId::OVERWORLD1, MusicId::OVERWORLD2, MusicId::OVERWORLD3};
                audio_.play_music(ow_tracks[rng_.range(0, 2)], 2000);
                // Weather-aware ambient: rain in Greenwood, default forest elsewhere
                bool in_rain = false;
                if (world_.has<Position>(player_)) {
                    auto& wp = world_.get<Position>(player_);
                    in_rain = (wp.x < 700 && wp.y > 500 && wp.y < 1100);
                }
                if (in_rain) {
                    audio_.play_ambient(is_night ? AmbientId::FOREST_NIGHT_RAIN : AmbientId::FOREST_DAY_RAIN, 1500);
                } else {
                    audio_.play_ambient(is_night ? AmbientId::FOREST_NIGHT : AmbientId::FOREST_DAY, 1500);
                }
            }
        }
    } else {
        // Dungeon — check for Sepulchre, boss presence
        bool in_sepulchre = false;
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            in_sepulchre = (dungeon_registry_[current_dungeon_idx_].zone == "sepulchre");
        }

        // Check for boss/paragon on this level (has GodAlignment + AI = paragon, or QuestTarget = boss)
        // Boss detection — only recheck on floor change (not every 10 turns)
        static bool has_boss = false;
        static int boss_check_floor = -999;
        if (boss_check_floor != dungeon_level_) {
            has_boss = false;
            boss_check_floor = dungeon_level_;
            auto& ai_pool = world_.pool<AI>();
            for (size_t i = 0; i < ai_pool.size(); i++) {
                Entity e = ai_pool.entity_at(i);
                if (world_.has<GodAlignment>(e) || world_.has<QuestTarget>(e)) {
                    has_boss = true;
                    break;
                }
            }
        }

        if (in_sepulchre) {
            audio_.play_music(MusicId::SEPULCHRE, 2000);
        } else if (has_boss) {
            MusicId boss[] = {MusicId::BOSS, MusicId::BOSS2};
            if (audio_.current_music() != MusicId::BOSS &&
                audio_.current_music() != MusicId::BOSS2) {
                audio_.play_music(boss[rng_.range(0, 1)], 1000);
            }
        } else if (dungeon_level_ >= 4) {
            MusicId deep[] = {MusicId::DUNGEON_DEEP1, MusicId::DUNGEON_DEEP2, MusicId::DUNGEON_DEEP3};
            audio_.play_music(deep[rng_.range(0, 2)], 1500);
        } else {
            MusicId dun[] = {MusicId::DUNGEON1, MusicId::DUNGEON2, MusicId::DUNGEON3};
            audio_.play_music(dun[rng_.range(0, 2)], 1500);
        }
        // Clear previous ambients before setting zone-specific ones
        audio_.stop_all_ambient(800);

        // Zone-specific ambient sounds
        std::string zone_str;
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            zone_str = dungeon_registry_[current_dungeon_idx_].zone;
        }

        if (zone_str == "molten") {
            audio_.play_ambient(AmbientId::FIRE_CRACKLE, 1500);
            audio_.play_ambient(AmbientId::CAVE, 1500);
        } else if (zone_str == "sunken") {
            audio_.play_ambient(AmbientId::CAVE_RAIN, 1500);
            audio_.play_ambient(AmbientId::RIVER, 1500);
        } else if (zone_str == "deep_halls") {
            audio_.play_ambient(AmbientId::CAVE, 1500);
            audio_.play_ambient(AmbientId::CAVE_RAIN, 1500);
        } else if (zone_str == "sepulchre") {
            audio_.play_ambient(AmbientId::CAVE, 1500);
            audio_.play_ambient(AmbientId::INTERIOR_NIGHT, 1500);
        } else {
            // warrens, stonekeep, catacombs, fallback
            audio_.play_ambient(AmbientId::CAVE, 1500);
        }
    }
}

bool Engine::is_class_unlocked(ClassId id) const {
    int idx = static_cast<int>(id);
    if (idx < BASE_CLASS_COUNT) return true; // base classes always available
    switch (id) {
        case ClassId::BARBARIAN:    return meta_.total_kills >= 50;
        case ClassId::KNIGHT:       return meta_.max_dungeon_depth >= 5;
        case ClassId::MONK:         return meta_.killed_unarmed;
        case ClassId::TEMPLAR:      return meta_.total_undead_kills >= 30;
        case ClassId::DRUID:        return meta_.total_quests_completed >= 10;
        case ClassId::WAR_CLERIC:   return meta_.total_hp_healed >= 300;
        case ClassId::WARLOCK:      return meta_.died_deep;
        case ClassId::DWARF:        return meta_.max_dungeon_depth >= 6;
        case ClassId::ELF:          return meta_.total_creatures_examined >= 50;
        case ClassId::BANDIT:       return meta_.max_gold_single_run >= 500;
        case ClassId::NECROMANCER:  return meta_.total_dark_arts_casts >= 30;
        case ClassId::SCHEMA_MONK:  return meta_.class_max_level[static_cast<int>(ClassId::MONK)] >= 12;
        case ClassId::HERETIC:      return meta_.gods_completed_count() >= GOD_COUNT;
        case ClassId::WYRMKIN:      return meta_.killed_dragon;
        case ClassId::REVENANT:     return meta_.total_deaths >= 10;
        case ClassId::SERPENTINE:   return meta_.max_diseases >= 3;
        case ClassId::TROLLBLOOD:   return meta_.max_dungeon_depth >= 8;
        default: return false;
    }
}

void Engine::update_meta_on_end() {
    // Snapshot old unlock state
    bool was_unlocked[CLASS_COUNT];
    for (int i = 0; i < CLASS_COUNT; i++)
        was_unlocked[i] = is_class_unlocked(static_cast<ClassId>(i));

    // Update max depth
    if (run_deepest_ > meta_.max_dungeon_depth)
        meta_.max_dungeon_depth = run_deepest_;

    // Track deaths (for Revenant unlock)
    if (state_ == GameState::DEAD) meta_.total_deaths++;

    // Track max simultaneous diseases (for Serpentine unlock)
    if (world_.has<Diseases>(player_)) {
        int dcount = world_.get<Diseases>(player_).count();
        if (dcount > meta_.max_diseases) meta_.max_diseases = dcount;
    }

    // Update max gold
    if (gold_ > meta_.max_gold_single_run)
        meta_.max_gold_single_run = gold_;

    // Update class level record
    if (player_ != NULL_ENTITY && world_.has<Stats>(player_)) {
        int cls = static_cast<int>(creation_screen_.get_build().class_id);
        int lvl = world_.get<Stats>(player_).level;
        if (cls >= 0 && cls < MetaSave::MAX_CLASSES && lvl > meta_.class_max_level[cls])
            meta_.class_max_level[cls] = lvl;
    }

    // Merge run bestiary into persistent bestiary
    for (auto& [name, entry] : bestiary_) {
        auto& me = meta_.bestiary[name];
        me.hp = entry.hp;
        me.damage = entry.damage;
        me.armor = entry.armor;
        me.speed = entry.speed;
        me.total_kills += entry.kills;
    }

    // Merge identified potions — check inventory for identified potions
    if (player_ != NULL_ENTITY && world_.has<Inventory>(player_)) {
        auto& inv = world_.get<Inventory>(player_);
        for (Entity ie : inv.items) {
            if (!world_.has<Item>(ie)) continue;
            auto& item = world_.get<Item>(ie);
            if (item.type == ItemType::POTION && item.identified && !item.name.empty())
                meta_.identified_potions.insert(item.name);
        }
    }

    meta::save(meta_);

    // Detect newly unlocked classes
    newly_unlocked_.clear();
    for (int i = BASE_CLASS_COUNT; i < CLASS_COUNT; i++) {
        if (!was_unlocked[i] && is_class_unlocked(static_cast<ClassId>(i))) {
            newly_unlocked_.push_back(get_class_info(static_cast<ClassId>(i)).name);
        }
    }
}

void Engine::populate_overworld() {
    overworld::populate(world_, map_, rng_, dungeon_registry_);
}

void Engine::spawn_pet_visual(int pet_id) {
    despawn_pet_visual(); // remove old pet if any
    if (pet_id < 0 || pet_id >= PET_TYPE_COUNT) return;

    auto& info = get_pet_info(static_cast<PetId>(pet_id));
    auto& ppos = world_.get<Position>(player_);

    pet_entity_ = world_.create();
    world_.add<Position>(pet_entity_, {ppos.x, ppos.y});
    world_.add<Renderable>(pet_entity_, {
        info.sprite_sheet, info.sprite_x, info.sprite_y,
        {static_cast<Uint8>(info.tint_r), static_cast<Uint8>(info.tint_g),
         static_cast<Uint8>(info.tint_b), 255},
        9, // z_order — just below player (10)
        false
    });
    // No Stats, AI, Energy, or Blocker — pet is invincible and non-targetable
}

void Engine::despawn_pet_visual() {
    if (pet_entity_ != NULL_ENTITY) {
        if (world_.has<Position>(pet_entity_)) world_.remove<Position>(pet_entity_);
        if (world_.has<Renderable>(pet_entity_)) world_.remove<Renderable>(pet_entity_);
        world_.destroy(pet_entity_);
        pet_entity_ = NULL_ENTITY;
    }
}

void Engine::sepulchre_ambient() {
    if (dungeon_level_ <= 0) return; // overworld — no ambient
    if (current_dungeon_idx_ < 0 ||
        current_dungeon_idx_ >= static_cast<int>(dungeon_registry_.size())) {
        // Generic dungeon ambient (every ~30 turns)
        if (game_turn_ % 30 != 0) return;
        static const char* GENERIC[] = {
            "Water drips somewhere in the dark.",
            "Your footsteps echo.",
            "The air is stale down here.",
            "Something rustles in the shadows.",
            "A distant grinding of stone.",
            "Dust motes drift through your torchlight.",
            "The passage narrows ahead.",
            "An old cobweb brushes your face.",
            "You hear your own breathing, and nothing else.",
            "A pebble shifts under your boot.",
        };
        log_.add(GENERIC[rng_.range(0, 9)], {120, 115, 110, 255});
        return;
    }

    auto& zone = dungeon_registry_[current_dungeon_idx_].zone;

    if (zone == "sepulchre") {
        // Sepulchre — frequent, unsettling
        if (game_turn_ % 18 != 0) return;
        static const char* SEPULCHRE[] = {
            "A cold draft from nowhere.",
            "The shadows move when you aren't looking.",
            "Something scratches behind the walls.",
            "The floor feels wrong beneath your feet.",
            "You smell old blood.",
            "A whisper in a language you almost understand.",
            "Your god stirs uneasily.",
            "The silence presses against your ears.",
            "Stone groans overhead.",
            "You feel watched.",
        };
        log_.add(SEPULCHRE[rng_.range(0, 9)], {130, 100, 130, 255});
    } else {
        // Zone-specific ambient (every ~25 turns)
        if (game_turn_ % 25 != 0) return;
        if (zone == "warrens") {
            static const char* W[] = {
                "Rats skitter in the walls.", "The dirt ceiling sags.", "A damp, earthy smell.",
                "Roots hang from the ceiling like fingers.", "Something wet drips on your neck.",
                "The tunnel narrows to a crawl ahead.", "Insect legs brush the back of your hand.",
                "A nest of something. Recently abandoned.", "The walls are scored with claw marks.",
                "Fungus grows thick on the ceiling. Some of it pulses faintly.",
            };
            log_.add(W[rng_.range(0, 9)], {140, 130, 100, 255});
        } else if (zone == "stonekeep") {
            static const char* S[] = {
                "Ancient mortar crumbles at your touch.", "The stonework here is older than the towns above.",
                "A cold wind blows through cracks in the wall.", "Iron sconces, long since empty.",
                "Your torch light catches old scratches on the walls.",
                "A collapsed archway blocks a side passage.", "Someone carved a warning here. The words are worn away.",
                "The flagstones are cracked from something heavy.", "An old iron chain hangs from the ceiling.",
                "These halls were built to last. They did.",
            };
            log_.add(S[rng_.range(0, 9)], {130, 130, 140, 255});
        } else if (zone == "catacombs") {
            static const char* C[] = {
                "Bones are stacked floor to ceiling.", "The dead are everywhere, but not all of them stay still.",
                "A faint moan from deeper in.", "The air tastes like dust and copper.",
                "Names are carved into every surface. Thousands of them.",
                "A jaw has fallen from its skull. It looks like it was screaming.",
                "Candle stubs litter the floor. Someone was here recently.",
                "The dead were laid to rest with care. That care has faded.",
                "Grave offerings have been picked clean.", "The walls are lined with alcoves. Not all of them are empty.",
            };
            log_.add(C[rng_.range(0, 9)], {140, 120, 130, 255});
        } else if (zone == "molten") {
            static const char* M[] = {
                "The heat is almost unbearable.", "Lava glows in the cracks between stones.",
                "The rock walls radiate warmth.", "Sulphur stings your nostrils.",
                "The ground trembles slightly.",
                "Smoke curls from vents in the floor.", "The metal of your gear is warm to the touch.",
                "A distant rumble shakes loose dust from the ceiling.", "The air shimmers with heat haze.",
                "Something molten drips from above. You step aside.",
            };
            log_.add(M[rng_.range(0, 9)], {160, 120, 80, 255});
        } else if (zone == "sunken") {
            static const char* SU[] = {
                "Water seeps through every crack.", "The walls are slick with moisture.",
                "Your boots splash in shallow water.", "The water here is unnaturally still.",
                "Something moves beneath the water.",
                "The ceiling drips steadily. It sounds like a heartbeat.", "Waterlogged wood floats past.",
                "The water level has risen since you entered this room.", "Pale fish dart away from your torchlight.",
                "Salt crusts the walls at knee height. The water was deeper once.",
            };
            log_.add(SU[rng_.range(0, 9)], {100, 130, 150, 255});
        } else if (zone == "deep_halls") {
            static const char* D[] = {
                "The architecture here predates anything on the surface.",
                "The ceiling is so high your light doesn't reach it.",
                "Old banners hang in tatters from the walls.",
                "The stonework is precise beyond anything you've seen.",
                "Something about the proportions is wrong. Built for something larger.",
                "Pillars thick as ancient trees line the hall.", "Your footsteps echo for a long time.",
                "A draft from below. There are deeper levels than this.",
                "Carvings on the wall depict a civilization you don't recognize.",
                "The silence here has weight.",
            };
            log_.add(D[rng_.range(0, 9)], {130, 125, 140, 255});
        }
    }
}

RunSummary Engine::build_run_summary() const {
    RunSummary s;
    s.turns = game_turn_;
    s.kills = run_kills_;
    s.deepest_floor = run_deepest_;
    s.gold_earned = run_gold_earned_;
    s.quests_completed = journal_.count_completed();
    auto build = creation_screen_.get_build();
    s.class_name = get_class_info(build.class_id).name;
    if (world_.has<Stats>(player_))
        s.level = world_.get<Stats>(player_).level;
    if (world_.has<GodAlignment>(player_)) {
        auto god = world_.get<GodAlignment>(player_).god;
        if (god != GodId::NONE) s.god_name = get_god_info(god).name;
        else s.god_name = "Godless";
    }
    if (world_.has<Inventory>(player_))
        s.items_carried = static_cast<int>(world_.get<Inventory>(player_).items.size());
    if (dungeon_level_ > 0 && current_dungeon_idx_ >= 0 &&
        current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()))
        s.death_location = dungeon_registry_[current_dungeon_idx_].name + " depth " + std::to_string(dungeon_level_);
    else if (dungeon_level_ == 0)
        s.death_location = "the overworld";
    return s;
}

void Engine::render_victory() {
    int god_id = static_cast<int>(GodId::NONE);
    if (world_.has<GodAlignment>(player_))
        god_id = static_cast<int>(world_.get<GodAlignment>(player_).god);
    render_victory_screen(renderer_, font_, font_title_, width_, height_,
                          god_id, newly_unlocked_, build_run_summary());
}

void Engine::reset_to_main_menu() {
    player_ = NULL_ENTITY;
    pet_entity_ = NULL_ENTITY;
    run_kills_ = 0; run_gold_earned_ = 0; run_deepest_ = 0;
    hardcore_ = false;
    dungeon_level_ = -1;
    game_turn_ = 0;
    gold_ = 0;
    journal_ = {};
    overworld_return_x_ = 0;
    overworld_return_y_ = 0;
    floor_cache_.clear();
    overworld_loaded_ = false;
    current_dungeon_idx_ = -1;
    build_traits_.clear();
    visited_towns_.clear();
    world_ = World();
    state_ = GameState::MAIN_MENU;
    main_menu_.set_can_continue(false);
    log_ = MessageLog();
    audio_.stop_all_ambient(800);
    audio_.play_music(MusicId::TITLE, 3000);
    audio_.play_ambient(AmbientId::FIRE_CRACKLE, 4000);
    audio_.play_ambient(AmbientId::FOREST_NIGHT_RAIN, 5000);
}

void Engine::try_interact() {
    auto& pos = world_.get<Position>(player_);

    // 1. Items or containers on the ground -> pickup
    {
        auto& positions = world_.pool<Position>();
        for (size_t i = 0; i < positions.size(); i++) {
            Entity e = positions.entity_at(i);
            if (e == player_) continue;
            auto& ipos = positions.at_index(i);
            if (ipos.x != pos.x || ipos.y != pos.y) continue;
            if (world_.has<Item>(e) || world_.has<Container>(e)) {
                try_pickup();
                return;
            }
        }
    }

    // 2. Adjacent friendly NPC -> talk
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = pos.x + dx, ny = pos.y + dy;
            if (!map_.in_bounds(nx, ny)) continue;
            Entity target = combat::entity_at(world_, nx, ny, player_);
            if (target != NULL_ENTITY && world_.has<NPC>(target) && !world_.has<AI>(target)) {
                npc_interaction::Context npc_ctx {
                    world_, player_, log_, audio_, rng_, particles_,
                    shop_screen_, quest_offer_, levelup_screen_,
                    journal_, meta_, gold_, game_turn_, dungeon_level_,
                    pending_levelup_, pending_quest_npc_
                };
                // Church priest
                if (world_.has<Church>(target) && world_.has<GodAlignment>(player_)) {
                    auto& church = world_.get<Church>(target);
                    auto& ga = world_.get<GodAlignment>(player_);
                    if (ga.god == church.god || ga.god == GodId::NONE) {
                        church_screen_.open(player_, &world_, church.god,
                                             ga.god == church.god ? ga.favor : 0);
                        return;
                    }
                }
                if (npc_interaction::interact(npc_ctx, target, nx, ny)) {
                    player_acted_ = true;
                    return;
                }
            }
        }
    }

    // 3. Stairs underfoot -> hint
    auto tile_type = map_.at(pos.x, pos.y).type;
    if (tile_type == TileType::STAIRS_DOWN) {
        log_.add("Press Enter or > to descend.", {150, 140, 130, 255});
        return;
    }
    if (tile_type == TileType::STAIRS_UP) {
        log_.add("Press Enter or < to ascend.", {150, 140, 130, 255});
        return;
    }

    log_.add("Nothing to interact with.", {140, 130, 120, 255});
}

void Engine::try_pickup() {
    auto& pos = world_.get<Position>(player_);
    auto& inv = world_.get<Inventory>(player_);

    // Check for containers first (chests, jars) — open them, don't pick up
    auto& positions = world_.pool<Position>();
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (e == player_) continue;
        if (!world_.has<Container>(e)) continue;

        auto& ipos = positions.at_index(i);
        if (ipos.x != pos.x || ipos.y != pos.y) continue;

        auto& cont = world_.get<Container>(e);
        if (cont.opened) continue; // already opened

        // Open the container — change sprite, spawn loot item
        cont.opened = true;
        audio_.play_chest_open();
        if (world_.has<Renderable>(e)) {
            auto& rend = world_.get<Renderable>(e);
            rend.sprite_x = cont.open_sprite_x;
            rend.sprite_y = cont.open_sprite_y;
        }

        // Spawn the contents as a separate item entity on top
        Entity loot = world_.create();
        world_.add<Position>(loot, {ipos.x, ipos.y});

        // Determine sprite for the loot
        int lsx = 1, lsy = 24; // default: coins
        if (cont.contents.type == ItemType::POTION) { lsx = 1; lsy = 19; }
        else if (cont.contents.type == ItemType::FOOD) { lsx = 1; lsy = 25; }
        world_.add<Renderable>(loot, {SHEET_ITEMS, lsx, lsy, {255,255,255,255}, 1});
        world_.add<Item>(loot, cont.contents);

        char buf[128];
        snprintf(buf, sizeof(buf), "You open it. Found: %s.", cont.contents.display_name().c_str());
        log_.add(buf, {200, 190, 140, 255});
        audio_.play(SfxId::PICKUP);
        player_acted_ = true;
        return;
    }

    // Find items at player position (regular pickup)
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (e == player_) continue;
        if (!world_.has<Item>(e)) continue;

        auto& ipos = positions.at_index(i);
        if (ipos.x != pos.x || ipos.y != pos.y) continue;

        auto& item = world_.get<Item>(e);

        // Gold is auto-collected
        if (item.type == ItemType::GOLD) {
            int gv = item.gold_value;
            // Unique effect: GOLD_FIND (+50%)
            for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
                Entity eq = inv.equipped[s];
                if (eq != NULL_ENTITY && world_.has<Item>(eq) &&
                    world_.get<Item>(eq).unique_effect == UniqueEffect::GOLD_FIND) {
                    gv = gv * 150 / 100;
                    break;
                }
            }
            gold_ += gv; run_gold_earned_ += gv;
            char buf[64];
            snprintf(buf, sizeof(buf), "You pick up %d gold.", gv);
            log_.add(buf, {220, 200, 80, 255});
            audio_.play(SfxId::GOLD);
            { auto& gp = world_.get<Position>(player_); particles_.gold_sparkle(gp.x, gp.y); }
            world_.destroy(e);
            player_acted_ = true;
            return;
        }

        if (inv.is_full()) {
            log_.add("Your pack is full.", {180, 120, 120, 255});
            return;
        }

        // Unique effect: auto-identify on pickup
        if (!item.identified) {
            for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
                Entity eq = inv.equipped[s];
                if (eq != NULL_ENTITY && world_.has<Item>(eq) &&
                    world_.get<Item>(eq).unique_effect == UniqueEffect::IDENTIFY_ON_PICKUP) {
                    item.identified = true;
                    break;
                }
            }
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "You pick up the %s.", item.display_name().c_str());
        SDL_Color pickup_col = (item.rarity != Rarity::COMMON)
            ? rarity_color(item.rarity) : SDL_Color{180, 175, 160, 255};
        log_.add(buf, pickup_col);
        audio_.play(SfxId::PICKUP);

        // Sacred/profane check on pickup
        if (world_.has<GodAlignment>(player_) && item.tags != 0) {
            auto& ga = world_.get<GodAlignment>(player_);
            auto sp = get_sacred_profane(ga.god);
            if (sp.sacred && (item.tags & sp.sacred)) {
                auto& ginfo = get_god_info(ga.god);
                god_system::adjust_favor(world_, player_, log_, 1);
                char sbuf[128];
                snprintf(sbuf, sizeof(sbuf), "A sacred item. %s approves.", ginfo.name);
                log_.add(sbuf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                turn_actions_.picked_up_sacred = true;
            }
        }

        // Remove from ground (remove Position only — keep Renderable for paper doll)
        world_.remove<Position>(e);
        inv.add(e);

        // Potions must be identified each run (no meta-save carry-over)
        // Alchemist background still auto-IDs below

        // Auto-identify potions: Alchemist background OR Divination 25+
        if (item.type == ItemType::POTION && !item.identified) {
            bool can_id = (background_ == BackgroundId::ALCHEMISTS_APPRENTICE);
            if (!can_id && world_.has<Skills>(player_)) {
                int div_lv = world_.get<Skills>(player_).get_level(SkillId::DIVINATION);
                if (div_lv >= 25) can_id = true;
            }
            // Tree identify-on-pickup bonus
            if (!can_id && world_.has<PassiveTreeState>(player_)) {
                auto tb = passive_tree::compute_bonuses(world_.get<PassiveTreeState>(player_));
                if (tb.identify_on_pickup > 0 && rng_.chance(tb.identify_on_pickup))
                    can_id = true;
            }
            if (can_id) {
                item.identified = true;
                log_.add("You recognize this potion.", {140, 200, 160, 255});
            }
        }

        // Pet pickup — prompt for naming
        if (item.type == ItemType::PET && item.pet_id >= 0) {
            auto& pinfo = get_pet_info(static_cast<PetId>(item.pet_id));
            log_.add("What will you name it?", {200, 190, 140, 255});
            pet_naming_ = true;
            pet_name_buf_ = pinfo.name; // default to species name
            pet_naming_item_ = e;
        }

        // Quest item pickup — mark quest complete
        if (item.quest_id >= 0) {
            auto qid = static_cast<QuestId>(item.quest_id);
            auto& qinfo = get_quest_info(qid);
            if (journal_.has_quest(qid) && journal_.get_state(qid) == QuestState::ACTIVE) {
                journal_.set_state(qid, QuestState::COMPLETE);
                char qbuf[128];
                snprintf(qbuf, sizeof(qbuf), "Quest complete: %s", qinfo.name);
                log_.add(qbuf, {220, 200, 100, 255});
            } else if (!journal_.has_quest(qid)) {
                journal_.add_quest(qid);
                journal_.set_state(qid, QuestState::COMPLETE);
                char qbuf[128];
                snprintf(qbuf, sizeof(qbuf), "Quest complete: %s", qinfo.name);
                log_.add(qbuf, {220, 200, 100, 255});
            }
            // Victory — claiming The Reliquary ends the game
            if (qid == QuestId::MQ_17_CLAIM_RELIQUARY) {
                state_ = GameState::VICTORY;
                end_screen_time_ = SDL_GetTicks();
                audio_.stop_all_ambient(500);
                audio_.play_music(MusicId::VICTORY, 1500);
            }
        }

        player_acted_ = true;
        return;
    }

    // Nature 25: forage herbs in overworld wilderness
    if (dungeon_level_ == 0 && world_.has<Skills>(player_) && world_.has<Position>(player_)) {
        int nat_lv = world_.get<Skills>(player_).get_level(SkillId::NATURE_MAGIC);
        auto& pp = world_.get<Position>(player_);
        if (nat_lv >= 25 && map_.in_bounds(pp.x, pp.y)) {
            auto tt = map_.at(pp.x, pp.y).type;
            if (tt == TileType::FLOOR_GRASS || tt == TileType::BRUSH) {
                if (rng_.chance(40)) { // 40% success
                    // Create a healing herb (acts like a weak potion)
                    Entity herb = world_.create();
                    world_.add<Position>(herb, {pp.x, pp.y});
                    world_.add<Renderable>(herb, {SHEET_ITEMS, 4, 19, {120, 200, 80, 255}, 1});
                    Item hi;
                    hi.name = "foraged herb";
                    hi.description = "A medicinal plant. Restores 8 HP.";
                    hi.type = ItemType::POTION;
                    hi.heal_amount = 8;
                    hi.gold_value = 5;
                    hi.identified = true;
                    world_.add<Item>(herb, std::move(hi));
                    log_.add("You find a useful herb.", {80, 180, 80, 255});
                    grant_skill_xp(SkillId::NATURE_MAGIC, 3);
                    player_acted_ = true;
                    return;
                } else {
                    log_.add("You search but find nothing useful.", {120, 140, 100, 255});
                    player_acted_ = true;
                    return;
                }
            }
        }
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

    // Can't rest if enemies are visible
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

    // Dungeon: limited rests per floor (2 base, Lethis/Monk get 3)
    int max_rests = 2;
    if (world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        if (ga.god == GodId::LETHIS) max_rests = 3;
    }
    if (background_ == BackgroundId::MONK_OF_ORDER) max_rests = 3;

    if (dungeon_level_ > 0 && rest_count_this_floor_ >= max_rests) {
        log_.add("You have no rests remaining on this floor.", {200, 120, 100, 255});
        log_.add("Descend to the next floor to rest again.", {160, 140, 120, 255});
        return;
    }

    // Track rest for tenets
    turn_actions_.rested = true;
    rested_this_floor_ = true;
    if (dungeon_level_ <= 0) turn_actions_.rested_on_surface = true;

    // (Rest-until-morning handled by innkeeper interaction)

    // Spend the rest charge
    rest_count_this_floor_++;

    // Full heal: restore all HP and MP
    bool is_vampire = world_.has<Diseases>(player_) &&
                      world_.get<Diseases>(player_).has(DiseaseId::VAMPIRISM);

    int hp_actual = 0;
    int mp_actual = 0;

    if (!is_vampire) {
        hp_actual = stats.hp_max - stats.hp;
        stats.hp = stats.hp_max;
    }
    if (stats.mp_max > 0) {
        mp_actual = stats.mp_max - stats.mp;
        stats.mp = stats.mp_max;
    }
    if (hp_actual > 0) meta_.total_hp_healed += hp_actual;

    // Yashkhet tenet: healed above 75%
    if (stats.hp * 4 > stats.hp_max * 3)
        turn_actions_.healed_above_75pct = true;

    // Costs 10 turns
    game_turn_ += 10;

    // Clear non-permanent status effects on rest
    if (world_.has<StatusEffects>(player_)) {
        auto& fx = world_.get<StatusEffects>(player_);
        fx.effects.clear();
    }

    // Rest message with clear resource feedback
    int rests_left = (dungeon_level_ > 0) ? (max_rests - rest_count_this_floor_) : -1;

    char buf[128];
    if (is_vampire && hp_actual == 0 && mp_actual > 0)
        snprintf(buf, sizeof(buf), "You rest, but your dead flesh does not mend. (+%d MP)", mp_actual);
    else if (is_vampire && hp_actual == 0 && mp_actual == 0)
        snprintf(buf, sizeof(buf), "You rest, but nothing heals. The hunger gnaws.");
    else
        snprintf(buf, sizeof(buf), "You rest. Fully restored. (+%d HP, +%d MP)", hp_actual, mp_actual);
    log_.add(buf, {100, 220, 100, 255});

    // Prominent rest counter feedback
    if (rests_left > 0) {
        char rbuf[64];
        snprintf(rbuf, sizeof(rbuf), "%d rest%s remaining this floor.",
                 rests_left, rests_left == 1 ? "" : "s");
        log_.add(rbuf, {220, 200, 100, 255});
    } else if (rests_left == 0) {
        log_.add("No rests remaining. You must descend to rest again.", {220, 140, 80, 255});
    }
    audio_.play(SfxId::REST);

    // Dismiss all summons on rest
    {
        auto& ai_pool = world_.pool<AI>();
        std::vector<Entity> to_dismiss;
        for (size_t si = 0; si < ai_pool.size(); si++) {
            Entity se = ai_pool.entity_at(si);
            if (ai_pool.at_index(si).friendly && world_.has<Stats>(se))
                to_dismiss.push_back(se);
        }
        for (Entity se : to_dismiss) world_.destroy(se);
        if (!to_dismiss.empty())
            log_.add("Your summons dissipate.", {140, 140, 120, 255});
    }

    // Yashkhet disapproves of rest
    if (world_.has<GodAlignment>(player_)) {
        auto& align = world_.get<GodAlignment>(player_);
        if (align.god == GodId::YASHKHET) {
            god_system::adjust_favor(world_, player_, log_, -2);
        }
    }

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
                // Relics and cursed items can't be unequipped
                if (item.curse_state == 1 && item.relic_god >= 0) {
                    log_.add("The relic is bound to you. It cannot be removed.", {255, 200, 100, 255});
                    break;
                }
                if (item.curse_state == 1) {
                    log_.add("The item is cursed! It won't come off.", {200, 80, 80, 255});
                    audio_.play(SfxId::CURSE);
                    item.identified = true;
                    break;
                }
                inv.unequip(item.slot);
                char buf[128];
                snprintf(buf, sizeof(buf), "You remove the %s.", item.display_name().c_str());
                log_.add(buf, {170, 165, 160, 255});
                // Pet unequipped — despawn visual
                if (item.slot == EquipSlot::PET) {
                    despawn_pet_visual();
                }
            } else {
                // Unequip existing item in that slot (check curse/relic)
                Entity prev = inv.get_equipped(item.slot);
                if (prev != NULL_ENTITY && world_.has<Item>(prev) &&
                    world_.get<Item>(prev).curse_state == 1) {
                    auto& prev_item = world_.get<Item>(prev);
                    if (prev_item.relic_god >= 0)
                        log_.add("The relic is bound to you. It cannot be replaced.", {255, 200, 100, 255});
                    else {
                        log_.add("You can't remove what's already equipped — it's cursed.", {200, 80, 80, 255});
                        prev_item.identified = true;
                    }
                    break;
                }
                // Capture old stats for comparison
                int old_dmg = 0, old_arm = 0, old_atk = 0, old_dodge = 0;
                if (prev != NULL_ENTITY && world_.has<Item>(prev)) {
                    auto& pi = world_.get<Item>(prev);
                    old_dmg = pi.damage_bonus; old_arm = pi.armor_bonus;
                    old_atk = pi.attack_bonus; old_dodge = pi.dodge_bonus;
                }

                if (prev != NULL_ENTITY) {
                    inv.unequip(item.slot);
                    // Removing old pet
                    if (item.slot == EquipSlot::PET) despawn_pet_visual();
                }
                inv.equip(item.slot, item_e);
                char buf[128];
                snprintf(buf, sizeof(buf), "You equip the %s.", item.display_name().c_str());
                log_.add(buf, {170, 180, 160, 255});
                audio_.play(SfxId::EQUIP);

                // Brief sparkle on player
                if (world_.has<Position>(player_)) {
                    auto& pp = world_.get<Position>(player_);
                    particles_.equip_flash(static_cast<float>(pp.x), static_cast<float>(pp.y));
                }

                // Show stat changes vs previous item
                {
                    auto show_diff = [&](const char* label, int new_val, int old_val) {
                        if (new_val == old_val) return;
                        int diff = new_val - old_val;
                        char dbuf[64];
                        snprintf(dbuf, sizeof(dbuf), "  %s %+d", label, diff);
                        SDL_Color dc = diff > 0 ? SDL_Color{100, 200, 100, 255}
                                                 : SDL_Color{200, 100, 100, 255};
                        log_.add(dbuf, dc);
                    };
                    if (item.type == ItemType::WEAPON) {
                        show_diff("Damage", item.damage_bonus, old_dmg);
                        show_diff("Attack", item.attack_bonus, old_atk);
                    } else {
                        show_diff("Armor", item.armor_bonus, old_arm);
                        show_diff("Dodge", item.dodge_bonus, old_dodge);
                    }
                }
                item.identified = true;
                // Reveal curse/binding on equip
                if (item.curse_state == 1 && item.relic_god >= 0) {
                    log_.add("The relic binds to you. It cannot be removed.", {255, 200, 100, 255});
                    audio_.play(SfxId::PRAYER);
                } else if (item.curse_state == 1) {
                    log_.add("A dark chill runs through you. The item is cursed!", {200, 80, 80, 255});
                    audio_.play(SfxId::CURSE);
                }
                // Profane item check on equip
                if (world_.has<GodAlignment>(player_) && item.tags != 0) {
                    auto& ga = world_.get<GodAlignment>(player_);
                    auto sp = get_sacred_profane(ga.god);
                    if (sp.profane && (item.tags & sp.profane)) {
                        auto& ginfo = get_god_info(ga.god);
                        god_system::adjust_favor(world_, player_, log_, -2);
                        char pbuf[128];
                        snprintf(pbuf, sizeof(pbuf), "%s recoils. This item is profane.", ginfo.name);
                        log_.add(pbuf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        turn_actions_.equipped_profane = true;
                    }
                }
                // God relic — equipping another god's relic causes massive favor loss
                if (item.relic_god >= 0 && world_.has<GodAlignment>(player_)) {
                    auto& ga = world_.get<GodAlignment>(player_);
                    if (ga.god != GodId::NONE && item.relic_god != static_cast<int>(ga.god)) {
                        auto& ginfo = get_god_info(ga.god);
                        auto& rinfo = get_god_info(static_cast<GodId>(item.relic_god));
                        god_system::adjust_favor(world_, player_, log_, -50);
                        char rbuf[128];
                        snprintf(rbuf, sizeof(rbuf),
                            "%s is ENRAGED! You wield a relic of %s! (-50 favor)", ginfo.name, rinfo.name);
                        log_.add(rbuf, {220, 40, 40, 255});
                    } else if (ga.god != GodId::NONE) {
                        auto& ginfo = get_god_info(ga.god);
                        god_system::adjust_favor(world_, player_, log_, 20);
                        char rbuf[128];
                        snprintf(rbuf, sizeof(rbuf),
                            "%s rejoices! You carry a divine relic! (+20 favor)", ginfo.name);
                        log_.add(rbuf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                    }
                }
                // Pet equipped — spawn visual following entity
                if (item.slot == EquipSlot::PET && item.pet_id >= 0) {
                    spawn_pet_visual(item.pet_id);
                    auto& pinfo = get_pet_info(static_cast<PetId>(item.pet_id));
                    char pbuf[128];
                    snprintf(pbuf, sizeof(pbuf), "The %s follows at your heels.", pinfo.name);
                    log_.add(pbuf, {160, 180, 140, 255});
                }
            }
            break;
        }
        case InvAction::USE: {
            if (item.type == ItemType::POTION || item.type == ItemType::FOOD) {
                bool consumed = false;
                char use_buf[128];

                if (item.heal_amount > 0 && world_.has<Stats>(player_)) {
                    auto& stats = world_.get<Stats>(player_);
                    int heal_amt = item.heal_amount;
                    // Yashkhet: healing -50%, Sythara: healing -30%
                    if (world_.has<GodAlignment>(player_)) {
                        auto& ga = world_.get<GodAlignment>(player_);
                        if (ga.god == GodId::YASHKHET) heal_amt = heal_amt / 2;
                        else if (ga.god == GodId::SYTHARA) heal_amt = heal_amt * 7 / 10;
                    }
                    // Cursed Blood trait: healing potions -50%
                    for (auto tid : build_traits_)
                        if (tid == TraitId::CURSED_BLOOD) { heal_amt = heal_amt / 2; break; }
                    int healed = std::min(heal_amt, stats.hp_max - stats.hp);
                    stats.hp += healed;
                    if (healed > 0) meta_.total_hp_healed += healed;
                    // Yashkhet tenet: healed above 75%
                    if (stats.hp * 4 > stats.hp_max * 3)
                        turn_actions_.healed_above_75pct = true;
                    snprintf(use_buf, sizeof(use_buf), "You consume the %s. Healed %d.",
                             item.display_name().c_str(), healed);
                    log_.add(use_buf, {100, 200, 100, 255});
                    if (healed > 0) {
                        auto& pp = world_.get<Position>(player_);
                        floating_text_.spawn(pp.x, pp.y, healed, {80, 220, 80, 255});
                    }
                    consumed = true;
                } else if (item.name == "mana potion" && world_.has<Stats>(player_)) {
                    auto& stats = world_.get<Stats>(player_);
                    int restored = std::min(15, stats.mp_max - stats.mp);
                    stats.mp += restored;
                    snprintf(use_buf, sizeof(use_buf), "You drink the %s. Restored %d MP.",
                             item.display_name().c_str(), restored);
                    log_.add(use_buf, {100, 120, 220, 255});
                    consumed = true;
                } else if (item.name == "antidote" && world_.has<StatusEffects>(player_)) {
                    auto& fx = world_.get<StatusEffects>(player_);
                    bool had_poison = fx.has(StatusType::POISON);
                    // Clear all poison
                    fx.effects.erase(
                        std::remove_if(fx.effects.begin(), fx.effects.end(),
                            [](const StatusEffect& e) { return e.type == StatusType::POISON; }),
                        fx.effects.end());
                    snprintf(use_buf, sizeof(use_buf), "You drink the %s.%s",
                             item.display_name().c_str(),
                             had_poison ? " The poison fades." : " Nothing happens.");
                    log_.add(use_buf, {100, 200, 100, 255});
                    consumed = true;
                } else if (item.name == "speed draught" && world_.has<Energy>(player_)) {
                    // Temporary speed boost — increase energy speed for a while
                    // Simple: grant 300 bonus energy (3 free actions)
                    world_.get<Energy>(player_).current += 300;
                    snprintf(use_buf, sizeof(use_buf), "You drink the %s. (+3 actions)",
                             item.display_name().c_str());
                    log_.add(use_buf, {220, 220, 100, 255});
                    consumed = true;
                } else if (item.name == "strength elixir" && world_.has<Stats>(player_)) {
                    // Temporary STR boost — +4 STR (permanent for simplicity, like a minor buff)
                    auto& stats = world_.get<Stats>(player_);
                    stats.set_attr(Attr::STR, stats.attr(Attr::STR) + 4);
                    snprintf(use_buf, sizeof(use_buf), "You drink the %s. (+4 STR)",
                             item.display_name().c_str());
                    log_.add(use_buf, {220, 160, 100, 255});
                    consumed = true;
                } else if (item.type == ItemType::FOOD) {
                    // Food with 0 heal — just consume for flavor
                    snprintf(use_buf, sizeof(use_buf), "You eat the %s.", item.display_name().c_str());
                    log_.add(use_buf, {180, 175, 160, 255});
                    consumed = true;
                }

                if (consumed) {
                    audio_.play(SfxId::POTION);
                    item.identified = true;
                    if (item.type == ItemType::POTION)
                        meta_.identified_potions.insert(item.name);
                }
                inv.remove(item_e);
                world_.destroy(item_e);
                player_acted_ = true;
            } else if (item.type == ItemType::SCROLL && item.teaches_spell < 0) {
                // Lore item — read it
                audio_.play(SfxId::SELECT);
                if (!item.description.empty()) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "You read the %s:", item.display_name().c_str());
                    log_.add(buf, {180, 175, 160, 255});
                    log_.add(item.description.c_str(), {200, 195, 175, 255});
                } else {
                    log_.add("The text is illegible.", {150, 140, 130, 255});
                }
            } else if (item.teaches_spell >= 0 && item.teaches_spell < SPELL_COUNT) {
                // Spellbook — learn the spell
                auto spell = static_cast<SpellId>(item.teaches_spell);
                if (world_.has<Spellbook>(player_)) {
                    auto& book = world_.get<Spellbook>(player_);
                    if (book.knows(spell)) {
                        log_.add("You already know this spell.", {150, 140, 130, 255});
                    } else {
                        // INT-based study check: fail chance = 60 - INT*2 (min 5%)
                        int fail_chance = std::max(5, 60 - world_.get<Stats>(player_).attr(Attr::INT) * 2);
                        if (rng_.chance(fail_chance)) {
                            // Failed — book destroyed
                            char buf[128];
                            snprintf(buf, sizeof(buf), "The %s crumbles as you study it. The spell is lost.", item.display_name().c_str());
                            log_.add(buf, {200, 120, 120, 255});
                            audio_.play(SfxId::CURSE);
                            turn_actions_.destroyed_book = true;
                            inv.remove(item_e);
                            world_.destroy(item_e);
                            player_acted_ = true;
                        } else {
                            book.learn(spell);
                            auto& sinfo = get_spell_info(spell);
                            char buf[128];
                            snprintf(buf, sizeof(buf), "You learn %s.", sinfo.name);
                            log_.add(buf, {160, 140, 220, 255});
                            audio_.play(SfxId::SPELL);
                            inv.remove(item_e);
                            world_.destroy(item_e);
                            player_acted_ = true;
                        }
                    }
                }
            } else {
                log_.add("You can't use that.", {150, 120, 120, 255});
            }
            break;
        }
        case InvAction::DROP: {
            auto& pos = world_.get<Position>(player_);
            if (inv.is_equipped(item_e) && item.curse_state == 1) {
                log_.add("The cursed item clings to you.", {200, 80, 80, 255});
                break;
            }
            if (inv.is_equipped(item_e)) {
                if (item.slot == EquipSlot::PET) despawn_pet_visual();
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
            // Recompute scale for new size
            ui_scale_ = static_cast<float>(height_) / 1080.0f;
            if (ui_scale_ < 0.75f) ui_scale_ = 0.75f;
            if (ui_scale_ > 3.0f) ui_scale_ = 3.0f;
            LOG_HEIGHT = static_cast<int>(180 * ui_scale_);
            HUD_HEIGHT = static_cast<int>(32 * ui_scale_);
            camera_.tile_size = static_cast<int>(60 * ui_scale_);
            camera_.viewport_w = width_;
            camera_.viewport_h = height_ - LOG_HEIGHT - HUD_HEIGHT;
            reload_fonts();
            continue;
        }

        // Mouse wheel: scroll message log (when no screen is open)
        if (event.type == SDL_MOUSEWHEEL && state_ == GameState::PLAYING &&
            !inventory_screen_.is_open() && !char_sheet_.is_open() &&
            !spell_screen_.is_open() && !quest_log_.is_open() &&
            !passive_tree_screen_.is_open() && !help_screen_.is_open()) {
            if (event.wheel.y > 0) log_.scroll_up(3);
            else if (event.wheel.y < 0) log_.scroll_down(3);
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
                case MenuChoice::NEW_GAME: {
                    creation_screen_.reset();
                    bool unlocks[CLASS_COUNT];
                    for (int i = 0; i < CLASS_COUNT; i++)
                        unlocks[i] = is_class_unlocked(static_cast<ClassId>(i));
                    creation_screen_.set_unlocked(unlocks, CLASS_COUNT);

                    // Generate progress strings for locked classes
                    char pbuf[64];
                    auto prog = [&](int cur, int needed, const char* unit) {
                        snprintf(pbuf, sizeof(pbuf), "Progress: %d / %d %s", std::min(cur, needed), needed, unit);
                        return pbuf;
                    };
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::BARBARIAN),
                        prog(meta_.total_kills, 50, "kills"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::KNIGHT),
                        prog(meta_.max_dungeon_depth, 5, "depth"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::MONK),
                        meta_.killed_unarmed ? "Complete!" : "Not yet achieved");
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::TEMPLAR),
                        prog(meta_.total_undead_kills, 30, "undead"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::DRUID),
                        prog(meta_.total_quests_completed, 10, "quests"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::WAR_CLERIC),
                        prog(meta_.total_hp_healed, 300, "HP healed"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::WARLOCK),
                        meta_.died_deep ? "Complete!" : "Not yet achieved");
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::DWARF),
                        prog(meta_.max_dungeon_depth, 6, "depth"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::ELF),
                        prog(meta_.total_creatures_examined, 50, "examined"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::BANDIT),
                        prog(meta_.max_gold_single_run, 500, "gold"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::NECROMANCER),
                        prog(meta_.total_dark_arts_casts, 30, "casts"));
                    { int monk_lvl = meta_.class_max_level[static_cast<int>(ClassId::MONK)];
                      creation_screen_.set_unlock_progress(static_cast<int>(ClassId::SCHEMA_MONK),
                        prog(monk_lvl, 12, "Monk level")); }
                    { int gc = meta_.gods_completed_count();
                      creation_screen_.set_unlock_progress(static_cast<int>(ClassId::HERETIC),
                        prog(gc, GOD_COUNT, "gods")); }

                    state_ = GameState::CREATING;
                    audio_.stop_all_ambient(500);
                    audio_.stop_music(1500);
                    break;
                }
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
            // Recompute layout after any settings change — resolution or scale
            {
                int new_w, new_h;
                SDL_GetWindowSize(window_, &new_w, &new_h);
                if (new_w != width_ || new_h != height_ || settings_.scale_changed()) {
                    width_ = new_w;
                    height_ = new_h;
                    if (settings_.scale_changed()) {
                        ui_scale_ = settings_.get_ui_scale();
                        settings_.clear_scale_changed();
                    } else {
                        ui_scale_ = static_cast<float>(height_) / 1080.0f;
                        if (ui_scale_ < 0.75f) ui_scale_ = 0.75f;
                        if (ui_scale_ > 3.0f) ui_scale_ = 3.0f;
                    }
                    LOG_HEIGHT = static_cast<int>(180 * ui_scale_);
                    HUD_HEIGHT = static_cast<int>(32 * ui_scale_);
                    camera_.tile_size = static_cast<int>(60 * ui_scale_);
                    camera_.viewport_w = width_;
                    camera_.viewport_h = height_ - LOG_HEIGHT - HUD_HEIGHT;
                    reload_fonts();
                }
            }
            if (settings_.should_close()) {
                state_ = return_from_settings_;
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
            bool consumed = creation_screen_.handle_input(event);
            if (consumed && event.type == SDL_KEYDOWN)
                audio_.play(SfxId::SELECT);
            if (creation_screen_.is_cancelled()) {
                // Return to main menu
                state_ = GameState::MAIN_MENU;
                main_menu_.set_can_continue(false);
                audio_.play_music(MusicId::TITLE, 2000);
                audio_.play_ambient(AmbientId::FIRE_CRACKLE, 1000);
                continue;
            }
            if (creation_screen_.is_done()) {
                // Full reset for new game — clear any leftover state from previous run
                player_ = NULL_ENTITY;
                pet_entity_ = NULL_ENTITY;
                world_ = World();
                dungeon_level_ = -1;
                game_turn_ = 0;
                gold_ = 0;
                run_kills_ = 0; run_gold_earned_ = 0; run_deepest_ = 0;
                journal_ = {};
                overworld_return_x_ = 0;
                overworld_return_y_ = 0;
                floor_cache_.clear();
                std::filesystem::remove("save/floors.dat");
                overworld_loaded_ = false;
                current_dungeon_idx_ = -1;
                build_traits_.clear();
                visited_towns_.clear();
                log_ = MessageLog();
                bestiary_.clear();

                hardcore_ = creation_screen_.get_build().hardcore;
                background_ = creation_screen_.get_build().background;
                // Pre-populate bestiary from meta-progression
                for (auto& [name, me] : meta_.bestiary) {
                    auto& entry = bestiary_[name];
                    entry.name = name;
                    entry.hp = me.hp;
                    entry.damage = me.damage;
                    entry.armor = me.armor;
                    entry.speed = me.speed;
                    entry.kills = 0;
                }
                generate_level();

                // Launch cinematic intro instead of dumping text into the log
                intro_screen_.start(creation_screen_.get_build().god);
                state_ = GameState::INTRO;

                // Seed the message log with a reminder (visible after intro)
                log_.add("? for help  |  q quests  |  t passive tree  |  c character  |  o sneak", {100, 180, 140, 255});
                tips_shown_ = {}; // reset tips for new run
            }
            return;
        }

        // Intro cinematic
        if (state_ == GameState::INTRO) {
            intro_screen_.handle_input(event);
            if (intro_screen_.is_done()) {
                state_ = GameState::PLAYING;
            }
            continue;
        }

        // Tutorial popup (blocks all game input until dismissed)
        if (tutorial_popup_.is_open()) {
            tutorial_popup_.handle_input(event);
            continue;
        }

        // Church screen
        if (church_screen_.is_open()) {
            ChurchAction cact = church_screen_.handle_input(event);
            if (cact == ChurchAction::CLOSE) {
                church_screen_.close();
            } else if (cact == ChurchAction::REST) {
                // Full heal, no exhaustion
                if (world_.has<Stats>(player_)) {
                    auto& ps = world_.get<Stats>(player_);
                    ps.hp = ps.hp_max;
                    ps.mp = ps.mp_max;
                    log_.add("You rest in the church. Fully restored.", {100, 200, 140, 255});
                    audio_.play(SfxId::HEAL);
                }
                church_screen_.close();
            } else if (cact == ChurchAction::IDENTIFY) {
                // Identify all items
                if (world_.has<Inventory>(player_)) {
                    auto& inv = world_.get<Inventory>(player_);
                    int id_count = 0;
                    for (Entity ie : inv.items) {
                        if (world_.has<Item>(ie) && !world_.get<Item>(ie).identified) {
                            world_.get<Item>(ie).identified = true;
                            id_count++;
                        }
                    }
                    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
                        Entity eq = inv.equipped[s];
                        if (eq != NULL_ENTITY && world_.has<Item>(eq) && !world_.get<Item>(eq).identified) {
                            world_.get<Item>(eq).identified = true;
                            id_count++;
                        }
                    }
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%d items identified.", id_count);
                    log_.add(buf, {140, 200, 200, 255});
                }
                church_screen_.close();
            } else if (cact == ChurchAction::ENCHANT) {
                // Enchant main-hand weapon
                auto& rewards = get_church_rewards(church_screen_.get_god());
                if (world_.has<Inventory>(player_)) {
                    Entity wpn = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
                    if (wpn != NULL_ENTITY && world_.has<Item>(wpn)) {
                        auto& item = world_.get<Item>(wpn);
                        item.damage_bonus += rewards.enchant_bonus;
                        char buf[96];
                        snprintf(buf, sizeof(buf), "%s enchanted with %s (+%d damage, 50 turns).",
                                 item.name.c_str(), rewards.enchant_name, rewards.enchant_bonus);
                        log_.add(buf, {200, 200, 140, 255});
                        audio_.play(SfxId::SPELL_BUFF);
                    } else {
                        log_.add("You have no weapon equipped.", {180, 140, 120, 255});
                    }
                }
                church_screen_.close();
            } else if (cact == ChurchAction::LEARN_SPELL) {
                auto& rewards = get_church_rewards(church_screen_.get_god());
                if (world_.has<Spellbook>(player_)) {
                    auto& book = world_.get<Spellbook>(player_);
                    if (book.knows(rewards.exclusive_spell)) {
                        log_.add("You already know this spell.", {140, 130, 120, 255});
                    } else {
                        book.learn(rewards.exclusive_spell);
                        auto& sinfo = get_spell_info(rewards.exclusive_spell);
                        char buf[64];
                        snprintf(buf, sizeof(buf), "Learned %s.", sinfo.name);
                        log_.add(buf, {160, 200, 220, 255});
                        audio_.play(SfxId::SPELL);
                    }
                }
                church_screen_.close();
            } else if (cact == ChurchAction::CLAIM_ITEM) {
                auto& rewards = get_church_rewards(church_screen_.get_god());
                // Create and give the exclusive item
                Entity item_e = world_.create();
                Item ci;
                ci.name = rewards.exclusive_item_name;
                ci.description = rewards.exclusive_item_desc;
                ci.damage_bonus = rewards.exclusive_item_damage;
                ci.armor_bonus = rewards.exclusive_item_armor;
                ci.identified = true;
                ci.gold_value = 200;
                if (ci.damage_bonus > 0) {
                    ci.type = ItemType::WEAPON;
                    ci.slot = EquipSlot::MAIN_HAND;
                } else {
                    ci.type = ItemType::ARMOR_CHEST;
                    ci.slot = EquipSlot::CHEST;
                }
                world_.add<Item>(item_e, std::move(ci));
                if (world_.has<Inventory>(player_))
                    world_.get<Inventory>(player_).add(item_e);
                char buf[96];
                snprintf(buf, sizeof(buf), "Received: %s.", rewards.exclusive_item_name);
                log_.add(buf, {220, 200, 100, 255});
                audio_.play(SfxId::PICKUP);
                church_screen_.close();
            } else if (cact == ChurchAction::CLAIM_BLESSING) {
                auto& rewards = get_church_rewards(church_screen_.get_god());
                if (world_.has<Stats>(player_)) {
                    auto& ps = world_.get<Stats>(player_);
                    ps.set_attr(Attr::STR, ps.attr(Attr::STR) + rewards.blessing_str);
                    ps.set_attr(Attr::DEX, ps.attr(Attr::DEX) + rewards.blessing_dex);
                    ps.set_attr(Attr::CON, ps.attr(Attr::CON) + rewards.blessing_con);
                    ps.set_attr(Attr::INT, ps.attr(Attr::INT) + rewards.blessing_int);
                    ps.set_attr(Attr::WIL, ps.attr(Attr::WIL) + rewards.blessing_wil);
                    ps.set_attr(Attr::PER, ps.attr(Attr::PER) + rewards.blessing_per);
                    ps.hp_max += rewards.blessing_hp; ps.hp += rewards.blessing_hp;
                    ps.mp_max += rewards.blessing_mp; ps.mp += rewards.blessing_mp;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "You receive %s: %s",
                             rewards.blessing_name, rewards.blessing_desc);
                    log_.add(buf, {255, 220, 100, 255});
                    audio_.play(SfxId::LEVELUP);
                    auto& gi = get_god_info(church_screen_.get_god());
                    screen_flash(gi.color.r, gi.color.g, gi.color.b, 80);
                }
                church_screen_.close();
            }
            continue;
        }

        // Passive tree screen — handles all event types (mouse, keyboard, scroll)
        if (passive_tree_screen_.is_open()) {
            // Snapshot allocated bitfield before input
            PassiveTreeState snap{};
            if (world_.has<PassiveTreeState>(player_))
                snap = world_.get<PassiveTreeState>(player_);

            passive_tree_screen_.handle_input(event);

            // If a node was allocated, apply its stat effects to player
            if (world_.has<PassiveTreeState>(player_) && world_.has<Stats>(player_)) {
                auto& tree = world_.get<PassiveTreeState>(player_);
                if (tree.points_spent > snap.points_spent) {
                    auto& stats = world_.get<Stats>(player_);
                    const auto* nodes = passive_tree::nodes();
                    int count = passive_tree::node_count();
                    for (int i = 0; i < count; i++) {
                        uint16_t nid = nodes[i].id;
                        if (tree.is_allocated(nid) && !snap.is_allocated(nid)) {
                            for (int e = 0; e < 4; e++) {
                                auto& eff = nodes[i].effects[e];
                                switch (eff.type) {
                                    case EffectType::BONUS_STR: stats.set_attr(Attr::STR, stats.attr(Attr::STR) + eff.value); break;
                                    case EffectType::BONUS_DEX: stats.set_attr(Attr::DEX, stats.attr(Attr::DEX) + eff.value); break;
                                    case EffectType::BONUS_CON: stats.set_attr(Attr::CON, stats.attr(Attr::CON) + eff.value); break;
                                    case EffectType::BONUS_INT: stats.set_attr(Attr::INT, stats.attr(Attr::INT) + eff.value); break;
                                    case EffectType::BONUS_WIL: stats.set_attr(Attr::WIL, stats.attr(Attr::WIL) + eff.value); break;
                                    case EffectType::BONUS_PER: stats.set_attr(Attr::PER, stats.attr(Attr::PER) + eff.value); break;
                                    case EffectType::BONUS_CHA: stats.set_attr(Attr::CHA, stats.attr(Attr::CHA) + eff.value); break;
                                    case EffectType::BONUS_HP: stats.hp_max += eff.value; stats.hp += eff.value; break;
                                    case EffectType::BONUS_MP: stats.mp_max += eff.value; stats.mp += eff.value; break;
                                    case EffectType::BONUS_SPEED: stats.base_speed += eff.value; break;
                                    case EffectType::BONUS_ARMOR: stats.natural_armor += eff.value; break;
                                    case EffectType::XP_GAIN_BONUS: stats.xp_bonus_pct += eff.value; break;
                                    case EffectType::KS_CHAOS_INOCULATION: {
                                        stats.hp_max = stats.hp_max / 2;
                                        if (stats.hp > stats.hp_max) stats.hp = stats.hp_max;
                                        stats.poison_resist = 100;
                                        log_.add("Chaos Inoculation. Your body hardens against corruption.", {120, 200, 60, 255});
                                        break;
                                    }
                                    default: break;
                                }
                            }
                            break;
                        }
                    }
                }
            }
            continue;
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
                        floor_cache_.clear();
                        main_menu_.set_can_continue(true);
                        state_ = GameState::MAIN_MENU;
                        audio_.stop_all_ambient(500);
                        audio_.play_music(MusicId::TITLE, 1500);
                        audio_.play_ambient(AmbientId::FIRE_CRACKLE, 1000);
                        audio_.play_ambient(AmbientId::FOREST_NIGHT_RAIN, 1000);
                        break;
                    default: break;
                }
                return;
            }

            // Level-up screen intercepts all input
            // Look mode — move cursor, describe tiles
            if (look_mode_) {
                if (event.type == SDL_KEYDOWN) {
                    int dx = 0, dy = 0;
                    auto lsym = event.key.keysym.sym;
                    auto lact = keybinds_.translate(lsym);
                    switch (lact) {
                        case Action::MOVE_LEFT:  dx = -1; break;
                        case Action::MOVE_RIGHT: dx =  1; break;
                        case Action::MOVE_UP:    dy = -1; break;
                        case Action::MOVE_DOWN:  dy =  1; break;
                        case Action::MOVE_NW: dx = -1; dy = -1; break;
                        case Action::MOVE_NE: dx =  1; dy = -1; break;
                        case Action::MOVE_SW: dx = -1; dy =  1; break;
                        case Action::MOVE_SE: dx =  1; dy =  1; break;
                        default:
                            if (lsym == SDLK_ESCAPE || lact == Action::EXAMINE) {
                                look_mode_ = false;
                                log_.add("Look mode off.", {140, 140, 140, 255});
                            }
                            break;
                    }
                    if (dx != 0 || dy != 0) {
                        int nx = look_x_ + dx;
                        int ny = look_y_ + dy;
                        if (map_.in_bounds(nx, ny)) {
                            look_x_ = nx;
                            look_y_ = ny;
                            describe_tile(look_x_, look_y_);
                        }
                    }
                }
                return;
            }

            // Prayer mode — pick 1 or 2 or Esc
            if (prayer_mode_) {
                if (event.type == SDL_KEYDOWN) {
                    auto sym = event.key.keysym.sym;
                    if (sym == SDLK_1) { execute_prayer(0); prayer_mode_ = false; }
                    else if (sym == SDLK_2) { execute_prayer(1); prayer_mode_ = false; }
                    else if (sym == SDLK_3) {
                        // God mastery ability (favor 75+)
                        prayer_mode_ = false;
                        if (!world_.has<GodAlignment>(player_) || !world_.has<Stats>(player_)) break;
                        auto& ga = world_.get<GodAlignment>(player_);
                        auto& ps = world_.get<Stats>(player_);
                        if (ga.favor < 75) {
                            log_.add("Not enough favor.", {180, 120, 120, 255});
                            break;
                        }
                        if (ga.favor < 15) {
                            log_.add("Not enough favor.", {180, 120, 120, 255});
                            break;
                        }
                        ga.favor -= 15;
                        auto& pp = world_.get<Position>(player_);
                        bool acted = true;

                        switch (ga.god) {
                            case GodId::YASHKHET: {
                                // Sacrifice nearest corpse for +1 max HP
                                auto& corpse_pool = world_.pool<Corpse>();
                                bool found = false;
                                for (size_t ci = 0; ci < corpse_pool.size(); ci++) {
                                    Entity ce = corpse_pool.entity_at(ci);
                                    if (!world_.has<Position>(ce)) continue;
                                    auto& cp = world_.get<Position>(ce);
                                    int cd = std::max(std::abs(cp.x - pp.x), std::abs(cp.y - pp.y));
                                    if (cd <= 2) {
                                        world_.destroy(ce);
                                        ps.hp_max += 1;
                                        ps.hp += 1;
                                        log_.add("You consume the corpse. Your body strengthens.", {200, 60, 60, 255});
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    log_.add("No corpse nearby.", {150, 130, 130, 255});
                                    ga.favor += 15; // refund
                                    acted = false;
                                }
                                break;
                            }
                            case GodId::ZHAVEK: {
                                // Shadow Step: teleport behind nearest enemy + free attack
                                Entity nearest = magic::nearest_enemy(world_, player_, map_, 6);
                                if (nearest != NULL_ENTITY && world_.has<Position>(nearest)) {
                                    auto& np = world_.get<Position>(nearest);
                                    // Find tile behind enemy relative to player
                                    int dx = np.x - pp.x, dy = np.y - pp.y;
                                    int bx = np.x + (dx > 0 ? 1 : dx < 0 ? -1 : 0);
                                    int by = np.y + (dy > 0 ? 1 : dy < 0 ? -1 : 0);
                                    if (map_.is_walkable(bx, by) &&
                                        combat::entity_at(world_, bx, by, player_) == NULL_ENTITY) {
                                        pp.x = bx; pp.y = by;
                                        combat::melee_attack(world_, player_, nearest, rng_, log_);
                                        log_.add("You step through shadow.", {100, 80, 160, 255});
                                    } else {
                                        // Can't get behind, just teleport adjacent
                                        pp.x = np.x + (dx > 0 ? -1 : 1);
                                        pp.y = np.y;
                                        log_.add("Shadow carries you forward.", {100, 80, 160, 255});
                                    }
                                } else {
                                    log_.add("No target nearby.", {150, 130, 130, 255});
                                    ga.favor += 15; acted = false;
                                }
                                break;
                            }
                            case GodId::KHAEL: {
                                // Tame nearest animal
                                Entity nearest = magic::nearest_enemy(world_, player_, map_, 5);
                                if (nearest != NULL_ENTITY && world_.has<Stats>(nearest) && world_.has<AI>(nearest)) {
                                    auto& ns = world_.get<Stats>(nearest);
                                    if (is_animal(ns.name.c_str())) {
                                        world_.get<AI>(nearest).forget_player = true;
                                        world_.get<AI>(nearest).state = AIState::IDLE;
                                        char buf[64];
                                        snprintf(buf, sizeof(buf), "The %s becomes calm.", ns.name.c_str());
                                        log_.add(buf, {80, 180, 80, 255});
                                    } else {
                                        log_.add("Only beasts can be tamed.", {150, 130, 130, 255});
                                        ga.favor += 15; acted = false;
                                    }
                                } else {
                                    log_.add("No beast nearby.", {150, 130, 130, 255});
                                    ga.favor += 15; acted = false;
                                }
                                break;
                            }
                            case GodId::MORRETH: {
                                // War Cry: stun adjacent + buff damage
                                static const int DX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                                static const int DY[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                                for (int d = 0; d < 8; d++) {
                                    Entity adj = combat::entity_at(world_, pp.x + DX[d], pp.y + DY[d], player_);
                                    if (adj != NULL_ENTITY && world_.has<StatusEffects>(adj))
                                        world_.get<StatusEffects>(adj).add(StatusType::STUNNED, 0, 2);
                                }
                                ps.base_damage += 3;
                                log_.add("WAR CRY! Enemies stagger. You feel battle fury.", {220, 80, 60, 255});
                                break;
                            }
                            case GodId::SOLETH: {
                                // Consecrate: damage undead in 5x5 area
                                int kills = 0;
                                auto& ai_pool = world_.pool<AI>();
                                for (size_t ai = 0; ai < ai_pool.size(); ai++) {
                                    Entity ae = ai_pool.entity_at(ai);
                                    if (!world_.has<Position>(ae) || !world_.has<Stats>(ae)) continue;
                                    auto& apos = world_.get<Position>(ae);
                                    auto& as = world_.get<Stats>(ae);
                                    if (std::abs(apos.x - pp.x) <= 2 && std::abs(apos.y - pp.y) <= 2
                                        && is_undead(as.name.c_str())) {
                                        as.hp -= 5;
                                        if (as.hp <= 0) kills++;
                                    }
                                }
                                log_.add("Holy light sears the undead!", {255, 240, 160, 255});
                                break;
                            }
                            case GodId::GATHRUUN: {
                                // Stone Wall: create 3 wall tiles in front of player
                                // Direction: based on last movement or facing
                                int dx = 1, dy = 0; // default east
                                for (int step = 1; step <= 3; step++) {
                                    int wx = pp.x + dx * step, wy = pp.y;
                                    if (map_.in_bounds(wx, wy) && map_.is_walkable(wx, wy)
                                        && combat::entity_at(world_, wx, wy, player_) == NULL_ENTITY) {
                                        map_.at(wx, wy).type = TileType::WALL_STONE_ROUGH;
                                    }
                                }
                                log_.add("Stone erupts from the ground!", {180, 140, 100, 255});
                                break;
                            }
                            default: {
                                // Generic: +10 favor, full MP
                                ga.favor += 10;
                                ps.mp = ps.mp_max;
                                log_.add("Divine energy fills you.", {200, 200, 140, 255});
                                break;
                            }
                        }
                        if (acted) {
                            audio_.play(SfxId::PRAYER);
                            player_acted_ = true;
                        }
                    }
                    else if (sym == SDLK_ESCAPE) { prayer_mode_ = false; }
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
                        audio_.play(SfxId::GOLD);
                    } else {
                        log_.add("You can't buy that.", {180, 120, 120, 255});
                    }
                } else if (act == ShopAction::SELL) {
                    if (shop_screen_.execute(world_, &gold_)) {
                        log_.add("Sold.", {180, 200, 140, 255});
                        audio_.play(SfxId::GOLD);
                    }
                }
                return;
            }

            // World map intercepts input
            if (world_map_.is_open()) {
                world_map_.handle_input(event);
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

            // Help screen intercepts everything
            if (help_screen_.is_open()) {
                help_screen_.handle_input(event);
                return;
            }

            // Pet naming mode — text input
            if (pet_naming_ && event.type == SDL_KEYDOWN) {
                auto sym = event.key.keysym.sym;
                if (sym == SDLK_RETURN) {
                    // Confirm name
                    if (!pet_name_buf_.empty() && pet_naming_item_ != NULL_ENTITY
                        && world_.has<Item>(pet_naming_item_)) {
                        world_.get<Item>(pet_naming_item_).name = pet_name_buf_;
                        char buf[128];
                        snprintf(buf, sizeof(buf), "You name it %s.", pet_name_buf_.c_str());
                        log_.add(buf, {160, 200, 140, 255});
                    }
                    pet_naming_ = false;
                    pet_naming_item_ = NULL_ENTITY;
                } else if (sym == SDLK_ESCAPE) {
                    // Keep default name
                    pet_naming_ = false;
                    pet_naming_item_ = NULL_ENTITY;
                } else if (sym == SDLK_BACKSPACE && !pet_name_buf_.empty()) {
                    pet_name_buf_.pop_back();
                } else if (pet_name_buf_.size() < 20) {
                    char c = 0;
                    if (sym >= SDLK_a && sym <= SDLK_z) {
                        c = 'a' + (sym - SDLK_a);
                        if (event.key.keysym.mod & KMOD_SHIFT) c -= 32;
                        if (pet_name_buf_.empty()) c = static_cast<char>(toupper(c));
                    } else if (sym == SDLK_SPACE) c = ' ';
                    else if (sym == SDLK_MINUS) c = '-';
                    if (c) pet_name_buf_ += c;
                }
                return;
            }

            // Quest offer modal intercepts everything
            if (quest_offer_.is_open()) {
                auto choice = quest_offer_.handle_input(event);
                if (choice == QuestOfferChoice::ACCEPT) {
                    auto qid = quest_offer_.get_quest_id();
                    auto& qinfo = get_quest_info(qid);
                    journal_.add_quest(qid);

                    // Set kill targets for count-based quests
                    if (qid == QuestId::SQ_RAT_CELLAR) {
                        for (auto& e : journal_.entries)
                            if (e.id == qid) { e.target = 5; break; }
                    } else if (qid == QuestId::SQ_UNDEAD_PATROL) {
                        for (auto& e : journal_.entries)
                            if (e.id == qid) { e.target = 10; break; }
                    }

                    if (is_auto_complete_quest(qid)) {
                        // "Talk to" quests — mark COMPLETE, player talks again to finish
                        journal_.set_state(qid, QuestState::COMPLETE);
                        log_.add(qinfo.complete_text, {180, 170, 140, 255});
                        log_.add("Speak to them again.", {160, 155, 140, 255});
                        audio_.play(SfxId::QUEST);
                    } else {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "Quest accepted: %s", qinfo.name);
                        log_.add(buf, {220, 200, 100, 255});
                    }
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
                } else if (act == SpellAction::QUICKCAST) {
                    SpellId qs = spell_screen_.get_selected_spell(world_);
                    if (qs != SpellId::COUNT) {
                        quick_cast_ = qs;
                        auto& qsi = get_spell_info(qs);
                        char qbuf[128];
                        snprintf(qbuf, sizeof(qbuf), "Quick-cast set: %s", qsi.name);
                        log_.add(qbuf, {160, 180, 220, 255});
                        spell_screen_.close();
                    }
                } else if (act == SpellAction::CAST) {
                    SpellId spell = spell_screen_.get_selected_spell(world_);
                    if (spell != SpellId::COUNT) {
                        auto& sinfo = get_spell_info(spell);
                        // Get target position before cast (for particles)
                        int tx = 0, ty = 0;
                        bool has_target = false;
                        if (sinfo.hostile && sinfo.range > 0) {
                            Entity tgt = magic::nearest_enemy(world_, player_, map_, sinfo.range);
                            if (tgt != NULL_ENTITY && world_.has<Position>(tgt)) {
                                auto& tp = world_.get<Position>(tgt);
                                tx = tp.x; ty = tp.y; has_target = true;
                            }
                        }
                        auto result = magic::cast(world_, player_, spell,
                                                   map_, rng_, log_);
                        if (result.consumed_turn) player_acted_ = true;
                        if (result.success) {
                            // Per-school spell sound
                            switch (sinfo.school) {
                                case SpellSchool::CONJURATION: audio_.play(SfxId::SPELL_FIRE); break;
                                case SpellSchool::TRANSMUTATION: audio_.play(SfxId::SPELL_BUFF); break;
                                case SpellSchool::DIVINATION: audio_.play(SfxId::SPELL); break;
                                case SpellSchool::HEALING: audio_.play(SfxId::HEAL); break;
                                case SpellSchool::NATURE: audio_.play(SfxId::SPELL_EARTH); break;
                                case SpellSchool::DARK_ARTS: audio_.play(SfxId::SPELL_IMPACT); break;
                                default: audio_.play(SfxId::SPELL); break;
                            }
                            // Override for specific spells
                            if (spell == SpellId::FROST_NOVA || spell == SpellId::ICE_SHARD)
                                audio_.play(SfxId::SPELL_ICE);
                            else if (spell == SpellId::EARTHQUAKE || spell == SpellId::STONE_FIST)
                                audio_.play(SfxId::SPELL_EARTH);
                            else if (spell == SpellId::POISON_CLOUD)
                                audio_.play(SfxId::SPELL_WATER);
                            else if (spell == SpellId::SHIELD_OF_FAITH || spell == SpellId::IRON_BODY ||
                                     spell == SpellId::BARKSKIN || spell == SpellId::HASTEN)
                                audio_.play(SfxId::SPELL_BUFF);
                            else if (spell == SpellId::HEX || spell == SpellId::DOOM || spell == SpellId::WITHER)
                                audio_.play(SfxId::SPELL_FREEZE);
                            if (sinfo.school == SpellSchool::DARK_ARTS) {
                                meta_.total_dark_arts_casts++;
                                turn_actions_.used_dark_arts = true;
                            }
                            if (spell == SpellId::FIREBALL)
                                turn_actions_.used_fire_magic = true;
                            // Healing magic tenet flag
                            if (sinfo.school == SpellSchool::HEALING)
                                turn_actions_.used_healing_magic = true;
                            auto& sp = world_.get<Position>(player_);
                            switch (spell) {
                                case SpellId::SPARK:
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 8, 255, 255, 100, 0.35f, 4);
                                        particles_.burst(tx, ty, 10, 255, 255, 140, 0.1f, 0.3f, 3);
                                    }
                                    break;
                                case SpellId::FORCE_BOLT:
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 12, 140, 160, 255, 0.3f, 5);
                                        particles_.burst(tx, ty, 15, 160, 180, 255, 0.1f, 0.5f, 6);
                                    }
                                    break;
                                case SpellId::FIREBALL:
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 15, 255, 140, 40, 0.25f, 6);
                                        particles_.burst(tx, ty, 25, 255, 120, 30, 0.15f, 0.6f, 8);
                                        particles_.burst(tx, ty, 15, 255, 200, 60, 0.1f, 0.4f, 6);
                                        trigger_screen_shake(3.0f);
                                        screen_flash(255, 120, 30, 50);
                                    }
                                    break;
                                case SpellId::DRAIN_LIFE:
                                    if (has_target) {
                                        particles_.trail(tx, ty, sp.x, sp.y, 12, 140, 60, 180, 4);
                                        particles_.burst(tx, ty, 10, 160, 80, 200, 0.08f, 0.5f, 6);
                                    }
                                    break;
                                case SpellId::FEAR:
                                    particles_.burst(sp.x, sp.y, 18, 100, 60, 140, 0.12f, 0.6f, 6);
                                    break;
                                case SpellId::HARDEN_SKIN:
                                    particles_.burst(sp.x, sp.y, 14, 160, 150, 120, 0.05f, 0.6f, 6);
                                    break;
                                case SpellId::REVEAL_MAP:
                                case SpellId::DETECT_MONSTERS:
                                    particles_.burst(sp.x, sp.y, 20, 120, 140, 220, 0.18f, 0.7f, 5);
                                    break;
                                case SpellId::IDENTIFY:
                                    particles_.burst(sp.x, sp.y, 12, 220, 220, 255, 0.06f, 0.4f, 5);
                                    break;
                                case SpellId::MINOR_HEAL:
                                    particles_.heal_effect(sp.x, sp.y);
                                    break;
                                case SpellId::MAJOR_HEAL:
                                    particles_.rise(sp.x, sp.y, 25, 80, 240, 80, 1.2f, 7);
                                    particles_.rise(sp.x, sp.y, 12, 180, 255, 180, 0.9f, 5);
                                    break;
                                case SpellId::CURE_POISON:
                                    particles_.rise(sp.x, sp.y, 15, 200, 255, 200, 0.7f, 6);
                                    break;
                                case SpellId::ENTANGLE:
                                    particles_.burst(sp.x, sp.y, 20, 60, 160, 40, 0.12f, 0.7f, 6);
                                    break;
                                // --- Conjuration: fire/ice/lightning ---
                                case SpellId::ICE_SHARD:
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 10, 140, 200, 255, 0.35f, 4);
                                        particles_.drift(tx, ty, 12, 180, 220, 255, 0.6f, 4);
                                    }
                                    break;
                                case SpellId::LIGHTNING:
                                case SpellId::CHAIN_LIGHTNING:
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 18, 255, 255, 180, 0.5f, 2);
                                        particles_.burst(tx, ty, 20, 255, 255, 140, 0.2f, 0.3f, 3);
                                        trigger_screen_shake(2.0f);
                                        screen_flash(255, 255, 220, 70);
                                    }
                                    break;
                                case SpellId::FROST_NOVA:
                                    particles_.burst(sp.x, sp.y, 45, 160, 220, 255, 0.2f, 1.0f, 6);
                                    particles_.drift(sp.x, sp.y, 30, 200, 240, 255, 1.5f, 4);
                                    particles_.burst(sp.x, sp.y, 15, 255, 255, 255, 0.25f, 0.4f, 2);
                                    trigger_screen_shake(2.0f);
                                    screen_flash(140, 200, 255, 50);
                                    break;
                                case SpellId::METEOR:
                                    if (has_target) {
                                        particles_.fall(tx, ty, 15, 255, 160, 40, 0.5f, 8);
                                        particles_.burst(tx, ty, 30, 255, 100, 20, 0.18f, 0.7f, 7);
                                        trigger_screen_shake(6.0f);
                                        screen_flash(255, 160, 40, 80);
                                    }
                                    break;
                                case SpellId::ACID_SPLASH:
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 10, 120, 200, 40, 0.3f, 4);
                                        particles_.fall(tx, ty, 15, 100, 220, 40, 0.8f, 4);
                                    }
                                    break;
                                case SpellId::DISINTEGRATE:
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 22, 200, 40, 200, 0.4f, 3);
                                        particles_.burst(tx, ty, 25, 220, 60, 220, 0.2f, 0.5f, 4);
                                        particles_.burst(tx, ty, 35, 255, 200, 255, 0.25f, 0.3f, 2);
                                        trigger_screen_shake(4.0f);
                                        screen_flash(200, 40, 200, 60);
                                    }
                                    break;
                                // --- Transmutation: earthy/metallic ---
                                case SpellId::HASTEN:
                                    particles_.rise(sp.x, sp.y, 15, 255, 255, 140, 0.5f, 3);
                                    break;
                                case SpellId::STONE_FIST:
                                    particles_.burst(sp.x, sp.y, 12, 160, 140, 100, 0.08f, 0.5f, 7);
                                    break;
                                case SpellId::PHASE:
                                    particles_.burst(sp.x, sp.y, 20, 100, 140, 220, 0.15f, 0.4f, 3);
                                    break;
                                case SpellId::IRON_BODY:
                                    particles_.burst(sp.x, sp.y, 18, 180, 180, 200, 0.05f, 0.8f, 6);
                                    break;
                                case SpellId::POLYMORPH:
                                    if (has_target) particles_.burst(tx, ty, 20, 200, 140, 255, 0.12f, 0.6f, 5);
                                    break;
                                // --- Healing: green/white rising ---
                                case SpellId::CLEANSE:
                                case SpellId::RESTORE:
                                    particles_.rise(sp.x, sp.y, 18, 140, 255, 180, 0.8f, 5);
                                    break;
                                case SpellId::SHIELD_OF_FAITH:
                                    particles_.orbit(sp.x, sp.y, 12, 255, 240, 180, 0.5f, 1.0f, 4);
                                    break;
                                case SpellId::SANCTUARY:
                                    particles_.orbit(sp.x, sp.y, 16, 200, 255, 200, 0.6f, 1.5f, 5);
                                    particles_.rise(sp.x, sp.y, 10, 255, 255, 220, 1.0f, 3);
                                    break;
                                // --- Nature: green bursts/drifts ---
                                case SpellId::BEAST_CALL:
                                case SpellId::SWARM:
                                    particles_.burst(sp.x, sp.y, 15, 80, 180, 60, 0.1f, 0.6f, 5);
                                    break;
                                case SpellId::POISON_CLOUD:
                                    particles_.drift(sp.x, sp.y, 25, 100, 200, 60, 1.5f, 5);
                                    break;
                                case SpellId::EARTHQUAKE:
                                    particles_.burst(sp.x, sp.y, 40, 140, 120, 80, 0.25f, 0.8f, 8);
                                    particles_.burst(sp.x, sp.y, 20, 200, 180, 100, 0.15f, 0.5f, 10);
                                    trigger_screen_shake(8.0f);
                                    screen_flash(140, 100, 50, 60);
                                    break;
                                case SpellId::BARKSKIN:
                                    particles_.burst(sp.x, sp.y, 14, 100, 140, 60, 0.04f, 0.7f, 6);
                                    break;
                                case SpellId::LIGHTNING_STORM:
                                    particles_.burst(sp.x, sp.y, 35, 255, 255, 160, 0.25f, 0.5f, 3);
                                    particles_.burst(sp.x, sp.y, 25, 200, 200, 255, 0.2f, 0.7f, 5);
                                    trigger_screen_shake(5.0f);
                                    break;
                                // --- Dark Arts: purple/red drains ---
                                case SpellId::RAISE_DEAD:
                                    particles_.rise(sp.x, sp.y, 25, 120, 60, 160, 1.2f, 6);
                                    particles_.burst(sp.x, sp.y, 15, 80, 200, 80, 0.08f, 0.8f, 5);
                                    particles_.drift(sp.x, sp.y, 10, 160, 100, 200, 1.5f, 4);
                                    break;
                                case SpellId::HEX:
                                    if (has_target) particles_.drift(tx, ty, 15, 140, 60, 180, 1.0f, 4);
                                    break;
                                case SpellId::SOUL_REND:
                                    if (has_target) {
                                        particles_.trail(tx, ty, sp.x, sp.y, 15, 180, 60, 220, 3);
                                        particles_.burst(tx, ty, 15, 200, 80, 255, 0.12f, 0.4f, 5);
                                    }
                                    break;
                                case SpellId::DARKNESS:
                                    particles_.burst(sp.x, sp.y, 40, 20, 10, 40, 0.15f, 1.5f, 10);
                                    particles_.drift(sp.x, sp.y, 25, 40, 20, 60, 2.0f, 8);
                                    screen_flash(10, 5, 20, 100); // heavy dark flash
                                    break;
                                case SpellId::WITHER:
                                    if (has_target) particles_.fall(tx, ty, 15, 100, 80, 40, 0.8f, 4);
                                    break;
                                case SpellId::BLOOD_PACT:
                                    particles_.burst(sp.x, sp.y, 30, 200, 40, 40, 0.12f, 1.0f, 7);
                                    particles_.rise(sp.x, sp.y, 15, 255, 60, 60, 0.8f, 4);
                                    particles_.fall(sp.x, sp.y, 10, 180, 0, 0, 0.6f, 5);
                                    trigger_screen_shake(3.0f);
                                    screen_flash(180, 20, 20, 70);
                                    break;
                                case SpellId::DOOM:
                                    if (has_target) {
                                        particles_.fall(tx, ty, 30, 80, 0, 120, 1.5f, 8);
                                        particles_.burst(tx, ty, 20, 160, 40, 200, 0.1f, 1.0f, 8);
                                        particles_.drift(tx, ty, 15, 60, 0, 80, 2.0f, 6);
                                        trigger_screen_shake(3.0f);
                                    }
                                    break;
                                // --- Fallback by school ---
                                default: {
                                    auto& si = get_spell_info(spell);
                                    switch (si.school) {
                                        case SpellSchool::CONJURATION:
                                            particles_.spell_fire(sp.x, sp.y); break;
                                        case SpellSchool::TRANSMUTATION:
                                            particles_.spell_ice(sp.x, sp.y); break;
                                        case SpellSchool::DIVINATION:
                                            particles_.spell_holy(sp.x, sp.y); break;
                                        case SpellSchool::HEALING:
                                            particles_.spell_holy(sp.x, sp.y); break;
                                        case SpellSchool::NATURE:
                                            particles_.spell_nature(sp.x, sp.y); break;
                                        case SpellSchool::DARK_ARTS:
                                            particles_.spell_dark(sp.x, sp.y); break;
                                        default:
                                            particles_.spell_effect(sp.x, sp.y, 160, 140, 200); break;
                                    }
                                } break;
                            }
                        }
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

            // Dead state — any key returns to main menu (after a delay)
            if (state_ == GameState::DEAD) {
                if (SDL_GetTicks() - end_screen_time_ < 3000) return; // 3 second delay
                // Hardcore: delete save on death
                if (hardcore_) {
                    std::filesystem::remove(save::default_path());
                }
                // Save meta-progression
                if (dungeon_level_ >= 4) meta_.died_deep = true;
                update_meta_on_end();
                reset_to_main_menu();
                return;
            }

            // Victory state — any key returns to main menu (after a delay)
            if (state_ == GameState::VICTORY) {
                if (SDL_GetTicks() - end_screen_time_ < 1500) return;
                // Mark god completion + save meta
                if (world_.has<GodAlignment>(player_)) {
                    auto& align = world_.get<GodAlignment>(player_);
                    int gi = static_cast<int>(align.god);
                    if (gi >= 0 && gi < GOD_COUNT) meta_.gods_completed[gi] = true;
                }
                update_meta_on_end();
                reset_to_main_menu();
                return;
            }

            {
            auto sym = event.key.keysym.sym;
            auto act = keybinds_.translate(sym);
            switch (act) {
                case Action::MOVE_UP:    try_move_player(0, -1);  break;
                case Action::MOVE_DOWN:  try_move_player(0, 1);   break;
                case Action::MOVE_LEFT:  try_move_player(-1, 0);  break;
                case Action::MOVE_RIGHT: try_move_player(1, 0);   break;
                case Action::MOVE_NW:    try_move_player(-1, -1); break;
                case Action::MOVE_NE:    try_move_player(1, -1);  break;
                case Action::MOVE_SW:    try_move_player(-1, 1);  break;
                case Action::MOVE_SE:    try_move_player(1, 1);   break;

                case Action::WAIT:
                    player_acted_ = true;
                    break;

                case Action::INTERACT:
                    try_interact();
                    break;

                case Action::PICKUP:
                    try_pickup();
                    break;

                case Action::INVENTORY:
                    inventory_screen_.open(player_);
                    break;

                case Action::SPELLBOOK:
                    spell_screen_.open(player_);
                    break;

                case Action::CHARACTER:
                    char_sheet_.open(player_);
                    break;

                case Action::PASSIVE_TREE:
                    passive_tree_screen_.open(player_, &world_, width_, height_);
                    break;

                case Action::SNEAK_TOGGLE:
                    sneaking_ = !sneaking_;
                    if (sneaking_) {
                        audio_.play(SfxId::SELECT);
                        log_.add("You crouch and move carefully.", {140, 140, 200, 255});
                        if (!tips_shown_.first_sneak) {
                            tips_shown_.first_sneak = true;
                            tutorial_popup_.show("Stealth",
                                "Sneaking halves your speed but enemies\n"
                                "detect you from much closer.\n\n"
                                "Attack from sneak for a backstab (2-4x damage).\n"
                                "Bump NPCs while sneaking to pickpocket.");
                        }
                        // Dim player sprite
                        if (world_.has<Renderable>(player_)) {
                            auto& r = world_.get<Renderable>(player_);
                            r.tint.a = 140; // semi-transparent
                        }
                    } else {
                        log_.add("You stand up.", {160, 160, 140, 255});
                        if (world_.has<Renderable>(player_)) {
                            auto& r = world_.get<Renderable>(player_);
                            r.tint.a = 255;
                        }
                    }
                    break;

                // Active abilities (1-4 keys)
                case Action::ABILITY_1: case Action::ABILITY_2:
                case Action::ABILITY_3: case Action::ABILITY_4: {
                    if (!world_.has<PassiveTreeState>(player_)) break;
                    auto& tree = world_.get<PassiveTreeState>(player_);
                    auto bonuses = passive_tree::compute_bonuses(tree);
                    auto& pos = world_.get<Position>(player_);
                    int slot = static_cast<int>(act) - static_cast<int>(Action::ABILITY_1);

                    // Map slot to capstone ability
                    struct AbilityInfo { EffectType type; int cd; const char* name; };
                    AbilityInfo abilities[] = {
                        {EffectType::CAP_WHIRLWIND, bonuses.cap_whirlwind_cd, "Whirlwind"},
                        {EffectType::CAP_TIME_SLIP, bonuses.cap_time_slip_cd, "Time Slip"},
                        {EffectType::CAP_ARCANE_OVERLOAD, bonuses.cap_arcane_overload_cd, "Arcane Overload"},
                        {EffectType::CAP_DIVINE_INTERVENTION, bonuses.cap_divine_intervention_cd, "Divine Intervention"},
                        {EffectType::CAP_UNBREAKABLE, bonuses.cap_unbreakable_cd, "Unbreakable"},
                        {EffectType::CAP_ASPECT_OF_BEAST, bonuses.cap_aspect_of_beast_cd, "Aspect of the Beast"},
                        {EffectType::CAP_DEATH_MARK, bonuses.cap_death_mark_cd, "Death Mark"},
                        {EffectType::CAP_PANDEMIC, bonuses.cap_pandemic_cd, "Pandemic"},
                    };

                    // Slot assignment: first owned capstone = slot 1, etc.
                    int owned = 0;
                    EffectType active_type = EffectType::NONE;
                    int active_cd = 0;
                    const char* active_name = nullptr;
                    for (auto& ab : abilities) {
                        if (ab.cd > 0) {
                            if (owned == slot) {
                                active_type = ab.type;
                                active_cd = ab.cd;
                                active_name = ab.name;
                                break;
                            }
                            owned++;
                        }
                    }

                    if (active_type == EffectType::NONE) {
                        log_.add("No ability in that slot.", {140, 130, 120, 255});
                        break;
                    }

                    // Check cooldown
                    int cd_idx = static_cast<int>(active_type) - static_cast<int>(EffectType::CAP_WHIRLWIND);
                    if (cd_idx < 0 || cd_idx >= PassiveTreeState::MAX_CAPSTONES) break;
                    if (tree.capstone_cooldowns[cd_idx] > 0) {
                        char cdbuf[64];
                        snprintf(cdbuf, sizeof(cdbuf), "%s: %d turns remaining.",
                                 active_name, tree.capstone_cooldowns[cd_idx]);
                        log_.add(cdbuf, {180, 130, 130, 255});
                        break;
                    }

                    // ── Execute capstone abilities ──
                    bool used = false;

                    if (active_type == EffectType::CAP_WHIRLWIND) {
                        static const int DX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                        static const int DY[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                        for (int d = 0; d < 8; d++) {
                            Entity adj = combat::entity_at(world_, pos.x + DX[d], pos.y + DY[d], player_);
                            if (adj != NULL_ENTITY && world_.has<AI>(adj))
                                combat::melee_attack(world_, player_, adj, rng_, log_);
                        }
                        log_.add("Whirlwind!", {255, 200, 100, 255});
                        used = true;
                    }
                    else if (active_type == EffectType::CAP_TIME_SLIP) {
                        // Grant 2 extra actions (3 total this turn)
                        // Implemented as 2 bonus energy grants so player acts 3x
                        if (world_.has<Energy>(player_)) {
                            auto& en = world_.get<Energy>(player_);
                            en.current += en.speed * 2;
                        }
                        log_.add("Time slips. You move in a blur.", {100, 200, 255, 255});
                        used = true;
                    }
                    else if (active_type == EffectType::CAP_ARCANE_OVERLOAD) {
                        // Set a flag: next spell costs 0 MP and deals 2x damage
                        // Use capstone_cooldowns[2] as -1 to signal "overload active"
                        // (cooldown gets set to real value after use in magic.cpp)
                        tree.capstone_cooldowns[cd_idx] = -1; // flag: overload ready
                        log_.add("Arcane energy surges. Next spell amplified.", {100, 140, 255, 255});
                        // Don't set used=true, don't cost a turn
                        break;
                    }
                    else if (active_type == EffectType::CAP_DIVINE_INTERVENTION) {
                        auto& stats = world_.get<Stats>(player_);
                        stats.hp = stats.hp_max;
                        stats.mp = stats.mp_max;
                        // Cleanse all status effects
                        if (world_.has<StatusEffects>(player_))
                            world_.get<StatusEffects>(player_).effects.clear();
                        log_.add("Divine light fills you. Fully restored.", {255, 240, 160, 255});
                        audio_.play(SfxId::HEAL);
                        used = true;
                    }
                    else if (active_type == EffectType::CAP_UNBREAKABLE) {
                        // Add a buff for 8 turns that halves damage
                        if (world_.has<Buffs>(player_)) {
                            world_.get<Buffs>(player_).add(BuffType::SANCTUARY, 8, 10);
                            // Borrowing SANCTUARY buff type for armor; +10 armor for 8 turns
                            auto& stats = world_.get<Stats>(player_);
                            stats.natural_armor += 10;
                        }
                        log_.add("You become unbreakable. Damage halved.", {180, 140, 100, 255});
                        used = true;
                    }
                    else if (active_type == EffectType::CAP_ASPECT_OF_BEAST) {
                        auto& stats = world_.get<Stats>(player_);
                        // +5 all stats for 15 turns
                        for (int a = 0; a < ATTR_COUNT; a++)
                            stats.attributes[a] += 5;
                        stats.base_damage += 3;
                        stats.hp_max += 15; stats.hp += 15;
                        // These will need to be removed after 15 turns
                        // Use capstone_cooldowns[5] as remaining duration (negative = active)
                        tree.capstone_cooldowns[cd_idx] = -15; // negative = duration remaining
                        log_.add("The beast within awakens.", {80, 200, 80, 255});
                        used = true;
                    }
                    else if (active_type == EffectType::CAP_DEATH_MARK) {
                        // Mark: next melee hit is auto-crit with 3x damage
                        // Use capstone_cooldowns[6] as -1 to signal "mark active"
                        tree.capstone_cooldowns[cd_idx] = -1;
                        log_.add("You mark your target for death.", {160, 100, 200, 255});
                        // No turn cost
                        break;
                    }
                    else if (active_type == EffectType::CAP_PANDEMIC) {
                        // Spread all player-applied DoTs to enemies within 3 tiles
                        auto& ai_pool = world_.pool<AI>();
                        int spread = 0;
                        for (size_t i = 0; i < ai_pool.size(); i++) {
                            Entity e = ai_pool.entity_at(i);
                            if (!world_.has<Position>(e) || !world_.has<StatusEffects>(e)) continue;
                            auto& epos = world_.get<Position>(e);
                            int dx = epos.x - pos.x, dy = epos.y - pos.y;
                            if (dx * dx + dy * dy > 9) continue; // range 3
                            auto& se = world_.get<StatusEffects>(e);
                            // Apply poison and bleed if they don't have them
                            bool has_poison = false, has_bleed = false;
                            for (auto& fx : se.effects) {
                                if (fx.type == StatusType::POISON) has_poison = true;
                                if (fx.type == StatusType::BLEED) has_bleed = true;
                            }
                            if (!has_poison) { se.add(StatusType::POISON, 2, 5); spread++; }
                            if (!has_bleed) { se.add(StatusType::BLEED, 1, 5); spread++; }
                        }
                        char pbuf[64];
                        snprintf(pbuf, sizeof(pbuf), "Pandemic! Afflictions spread to %d targets.", spread);
                        log_.add(pbuf, {120, 200, 60, 255});
                        used = true;
                    }

                    if (used) {
                        if (tree.capstone_cooldowns[cd_idx] >= 0) // don't overwrite negative flags
                            tree.capstone_cooldowns[cd_idx] = active_cd;
                        process_turn();
                    }
                    break;
                }

                case Action::BESTIARY:
                    if (bestiary_.empty()) {
                        log_.add("Your bestiary is empty. Kill something first.", {150, 140, 130, 255});
                    } else {
                        log_.add("--- Bestiary ---", {200, 190, 160, 255});
                        for (auto& [name, entry] : bestiary_) {
                            char bbuf[128];
                            snprintf(bbuf, sizeof(bbuf), "  %s — HP:%d Dmg:%d Arm:%d Spd:%d (killed: %d)",
                                     entry.name.c_str(), entry.hp, entry.damage,
                                     entry.armor, entry.speed, entry.kills);
                            log_.add(bbuf, {180, 175, 160, 255});
                        }
                    }
                    break;

                case Action::EXAMINE: {
                    auto& pp = world_.get<Position>(player_);
                    look_x_ = pp.x;
                    look_y_ = pp.y;
                    look_mode_ = true;
                    log_.add("Look mode. Move cursor to examine. x/Esc to exit.", {180, 180, 140, 255});
                    describe_tile(look_x_, look_y_);
                    break;
                }

                case Action::PRAY: {
                    if (!world_.has<GodAlignment>(player_)) break;
                    auto& align = world_.get<GodAlignment>(player_);
                    if (align.god == GodId::NONE) {
                        log_.add("You have no god to pray to.", {150, 140, 130, 255});
                        break;
                    }
                    auto& ginfo = get_god_info(align.god);
                    auto prayers = get_prayers(align.god);
                    if (!prayers) break;
                    char pbuf[128];
                    snprintf(pbuf, sizeof(pbuf), "Pray to %s (favor: %d):", ginfo.name, align.favor);
                    log_.add(pbuf, {200, 190, 160, 255});
                    snprintf(pbuf, sizeof(pbuf), "  1. %s (%d favor) - %s",
                             prayers[0].name, prayers[0].favor_cost, prayers[0].description);
                    log_.add(pbuf, {180, 175, 150, 255});
                    snprintf(pbuf, sizeof(pbuf), "  2. %s (%d favor) - %s",
                             prayers[1].name, prayers[1].favor_cost, prayers[1].description);
                    log_.add(pbuf, {180, 175, 150, 255});
                    // God mastery ability at favor 75+
                    if (align.favor >= 75) {
                        const char* mastery_name = "Divine Mastery";
                        const char* mastery_desc = "Unknown power";
                        switch (align.god) {
                            case GodId::YASHKHET: mastery_name = "Sacrifice Corpse"; mastery_desc = "consume nearest corpse for +1 max HP"; break;
                            case GodId::ZHAVEK:   mastery_name = "Shadow Step"; mastery_desc = "teleport behind nearest enemy, free attack"; break;
                            case GodId::KHAEL:    mastery_name = "Tame Beast"; mastery_desc = "pacify nearest animal permanently"; break;
                            case GodId::MORRETH:  mastery_name = "War Cry"; mastery_desc = "stun adjacent, +3 damage 10 turns"; break;
                            case GodId::SOLETH:   mastery_name = "Consecrate"; mastery_desc = "undead in 5x5 take 5 damage"; break;
                            case GodId::GATHRUUN: mastery_name = "Stone Wall"; mastery_desc = "create 3 wall tiles in a line"; break;
                            default: mastery_name = "Ascendant Prayer"; mastery_desc = "+10 favor, full MP restore"; break;
                        }
                        snprintf(pbuf, sizeof(pbuf), "  3. %s (15 favor) - %s",
                                 mastery_name, mastery_desc);
                        log_.add(pbuf, {220, 200, 100, 255});
                    }
                    prayer_mode_ = true;
                    break;
                }

                case Action::FIRE_RANGED:
                    fire_ranged();
                    break;

                case Action::QUICK_CAST:
                    if (quick_cast_ != SpellId::COUNT && world_.has<Spellbook>(player_)) {
                        auto& qbook = world_.get<Spellbook>(player_);
                        bool known = false;
                        for (auto s : qbook.known_spells) if (s == quick_cast_) { known = true; break; }
                        if (known) {
                            auto& sinfo = get_spell_info(quick_cast_);
                            int tx = 0, ty = 0;
                            bool has_target = false;
                            if (sinfo.hostile && sinfo.range > 0) {
                                Entity tgt = magic::nearest_enemy(world_, player_, map_, sinfo.range);
                                if (tgt != NULL_ENTITY && world_.has<Position>(tgt)) {
                                    auto& tp = world_.get<Position>(tgt);
                                    tx = tp.x; ty = tp.y; has_target = true;
                                }
                            }
                            auto result = magic::cast(world_, player_, quick_cast_, map_, rng_, log_);
                            if (result.consumed_turn) player_acted_ = true;
                            if (result.success) {
                                switch (sinfo.school) {
                                    case SpellSchool::CONJURATION: audio_.play(SfxId::SPELL_FIRE); break;
                                    case SpellSchool::TRANSMUTATION: audio_.play(SfxId::SPELL_BUFF); break;
                                    case SpellSchool::DIVINATION: audio_.play(SfxId::SPELL); break;
                                    case SpellSchool::HEALING: audio_.play(SfxId::HEAL); break;
                                    case SpellSchool::NATURE: audio_.play(SfxId::SPELL_EARTH); break;
                                    case SpellSchool::DARK_ARTS: audio_.play(SfxId::SPELL_IMPACT); break;
                                    default: audio_.play(SfxId::SPELL); break;
                                }
                                if (sinfo.school == SpellSchool::DARK_ARTS) {
                                    meta_.total_dark_arts_casts++;
                                    turn_actions_.used_dark_arts = true;
                                }
                                if (quick_cast_ == SpellId::FIREBALL) turn_actions_.used_fire_magic = true;
                                if (sinfo.school == SpellSchool::HEALING) turn_actions_.used_healing_magic = true;
                                // Particles for quick-cast (simplified — burst at target or self)
                                auto& sp = world_.get<Position>(player_);
                                if (has_target) {
                                    particles_.trail(sp.x, sp.y, tx, ty, 10, 160, 180, 255, 3);
                                    particles_.burst(tx, ty, 15, 180, 160, 220, 0.1f, 0.5f, 5);
                                } else {
                                    particles_.burst(sp.x, sp.y, 12, 140, 200, 160, 0.08f, 0.5f, 4);
                                }
                            }
                        } else {
                            log_.add("You no longer know that spell.", {180, 140, 120, 255});
                            quick_cast_ = SpellId::COUNT;
                        }
                    } else if (quick_cast_ == SpellId::COUNT) {
                        log_.add("No quick-cast spell set. Press z then q on a spell.", {140, 135, 130, 255});
                    }
                    break;

                case Action::REST:
                    try_rest();
                    break;

                case Action::QUEST_LOG:
                    quest_log_.open();
                    break;

                case Action::WORLD_MAP:
                    if (dungeon_level_ <= 0) {
                        world_map_.toggle();
                    } else {
                        log_.add("You can't see the world map underground.", {150, 140, 130, 255});
                    }
                    break;

                case Action::HELP:
                    help_screen_.open();
                    break;

                // Stairs
                case Action::STAIRS_DOWN:
                case Action::STAIRS_UP:
                case Action::STAIRS_ENTER: {
                    auto& pos = world_.get<Position>(player_);
                    auto tile_type = map_.at(pos.x, pos.y).type;
                    if (tile_type == TileType::STAIRS_DOWN &&
                        act != Action::STAIRS_UP) {
                        // Save overworld position before first descent
                        if (dungeon_level_ == 0) {
                            overworld_return_x_ = pos.x;
                            overworld_return_y_ = pos.y;
                            // Find nearest dungeon from registry
                            int prev_dungeon = current_dungeon_idx_;
                            current_dungeon_idx_ = -1;
                            int best_dist = 9999;
                            for (int di = 0; di < static_cast<int>(dungeon_registry_.size()); di++) {
                                int dx = pos.x - dungeon_registry_[di].x;
                                int dy = pos.y - dungeon_registry_[di].y;
                                int d = dx * dx + dy * dy;
                                if (d < best_dist) {
                                    best_dist = d;
                                    current_dungeon_idx_ = di;
                                }
                            }
                            // Only clear cache if entering a DIFFERENT dungeon
                            if (current_dungeon_idx_ != prev_dungeon)
                                floor_cache_.clear();
                        }
                        cache_current_floor(); // persist current floor before leaving
                        ascending_ = false; // going down
                        generate_level(); // increments dungeon_level_ + repositions summons
                        audio_.play(SfxId::STAIRS);
                        start_transition(TransitionType::FADE_IN, 400);

                        // Dungeon entrance text (first floor only)
                        if (dungeon_level_ == 1 && current_dungeon_idx_ >= 0 &&
                            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
                            auto& de = dungeon_registry_[current_dungeon_idx_];
                            char ebuf[128];
                            snprintf(ebuf, sizeof(ebuf), "You descend into %s.", de.name.c_str());
                            log_.add(ebuf, {180, 170, 150, 255});
                            if (!tips_shown_.first_dungeon) {
                                tips_shown_.first_dungeon = true;
                                tutorial_popup_.show("Entering a Dungeon",
                                    "Watch for traps. High PER helps detect them.\n\n"
                                    "R - Rest (heals fully, limited uses per floor)\n"
                                    "O - Toggle sneak\n"
                                    "E - Interact with items, NPCs, containers\n"
                                    "< - Return to the surface");
                            }
                            // Zone-flavored entrance line
                            if (de.zone == "warrens")
                                log_.add("Dirt walls close in around you. The air is thick.", {140, 130, 100, 255});
                            else if (de.zone == "stonekeep")
                                log_.add("Worked stone stretches into darkness. This place is old.", {130, 130, 140, 255});
                            else if (de.zone == "catacombs")
                                log_.add("The dead lie in their alcoves, watching.", {140, 120, 130, 255});
                            else if (de.zone == "molten")
                                log_.add("Heat hits you like a wall. The stone glows red.", {160, 120, 80, 255});
                            else if (de.zone == "sunken")
                                log_.add("Water echoes from every direction. The floor is wet.", {100, 130, 150, 255});
                            else if (de.zone == "deep_halls")
                                log_.add("The ceiling vanishes above you. Something vast was built here.", {130, 125, 140, 255});
                            else if (de.zone == "sepulchre")
                                log_.add("The air turns cold. You feel something notice you.", {130, 100, 130, 255});

                            // Main quest dungeon: brand reactions
                            SDL_Color brand_nar = {200, 180, 120, 255};
                            std::string dname = de.name;
                            if (dname == "The Barrow") {
                                log_.add("Your brand pulses. The dead down here know you're coming.", brand_nar);
                            } else if (dname == "Ashford Ruins") {
                                log_.add("The brand itches. Something written here is meant for you.", brand_nar);
                            } else if (dname == "Stonekeep") {
                                log_.add("Your brand burns bright enough to see by. The walls are warm.", brand_nar);
                            } else if (dname == "Frostmere Depths") {
                                log_.add("The cold should numb your brand. Instead it burns hotter.", brand_nar);
                            } else if (dname == "The Catacombs") {
                                log_.add("The dead here are older than the gods. Your brand illuminates their faces.", brand_nar);
                            } else if (dname == "The Molten Depths") {
                                log_.add("The heat is immense. Your brand matches it. You feel a fragment calling.", brand_nar);
                            } else if (dname == "The Sunken Halls") {
                                log_.add("Water everywhere. Your brand reflects off the surface like a lantern.", brand_nar);
                            } else if (dname == "The Hollowgate") {
                                log_.add("The seal recognizes your brand. The fragments resonate. The way opens.", brand_nar);
                            } else if (dname == "The Sepulchre") {
                                log_.add("This is where it began. Your brand is screaming. You go down anyway.", {220, 180, 100, 255});
                            }
                        } else if (dungeon_level_ >= 2) {
                            // Deeper floor descent text
                            static const char* DEEPER[] = {
                                "Deeper. The air grows heavier.",
                                "The stairs crumble behind you.",
                                "Darkness swallows the passage above.",
                                "The walls press closer. Or is it your imagination?",
                                "Something shifts in the dark below.",
                                "You descend further. The silence thickens.",
                            };
                            log_.add(DEEPER[rng_.range(0, 5)], {140, 135, 130, 255});
                            char dbuf[64];
                            snprintf(dbuf, sizeof(dbuf), "Depth %d.", dungeon_level_);
                            log_.add(dbuf, {120, 115, 110, 255});
                        }
                        // Brittle Bones: take 1-3 damage from stairs
                        for (auto tid : build_traits_) {
                            if (tid == TraitId::BRITTLE_BONES && world_.has<Stats>(player_)) {
                                int fall_dmg = rng_.range(1, 3);
                                world_.get<Stats>(player_).hp -= fall_dmg;
                                char fbuf[64];
                                snprintf(fbuf, sizeof(fbuf), "The descent jars your bones. (-%d HP)", fall_dmg);
                                log_.add(fbuf, {200, 160, 120, 255});
                                break;
                            }
                        }
                    } else if (tile_type == TileType::STAIRS_UP &&
                               act != Action::STAIRS_DOWN) {
                        if (dungeon_level_ > 1) {
                            // Go up one dungeon level: -2 because generate_level increments by 1
                            cache_current_floor(); // persist current floor before leaving
                            ascending_ = true; // going up — place at down-stairs on restored floor
                            dungeon_level_ -= 2;
                            generate_level();
                            ascending_ = false;
                            audio_.play(SfxId::STAIRS);
                            start_transition(TransitionType::FADE_IN, 400);
                        } else if (dungeon_level_ == 1) {
                            // Return to overworld from depth 1 — keep cache for re-entry
                            cache_current_floor();
                            ascending_ = false;
                            dungeon_level_ = -1; // will increment to 0
                            generate_level();
                            audio_.play(SfxId::STAIRS);
                            start_transition(TransitionType::FADE_IN, 500);
                            // Place player at the dungeon entrance they used
                            if (overworld_return_x_ != 0 || overworld_return_y_ != 0) {
                                world_.get<Position>(player_) = {overworld_return_x_, overworld_return_y_};
                                auto& stats = world_.get<Stats>(player_);
                                fov::compute(map_, overworld_return_x_, overworld_return_y_, stats.fov_radius());
                                camera_.center_on(overworld_return_x_, overworld_return_y_);
                            }
                        } else {
                            log_.add("You're already on the surface.", {150, 140, 130, 255});
                        }
                    } else if (act != Action::STAIRS_ENTER) {
                        log_.add("There are no stairs here.", {150, 100, 100, 255});
                    }
                    break;
                }

                case Action::QUICKSAVE:
                    do_save();
                    log_.add("Quick save.", {100, 200, 100, 255});
                    break;
                case Action::QUICKLOAD:
                    do_load();
                    log_.add("Quick load.", {100, 200, 100, 255});
                    break;
                case Action::SCREENSHOT: {
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
            } // end scope for sym/act
        }
    }
}

void Engine::render_god_visuals(const Camera& cam, int y_offset) {
    god_system::render_god_visuals(world_, player_, renderer_, cam, y_offset);
}

void Engine::update_death_anims() {
    const float dt = 1.0f / 60.0f; // ~60fps frame time
    auto& da_pool = world_.pool<DeathAnim>();
    // Collect entities whose animations have finished
    std::vector<Entity> finished;
    for (size_t i = 0; i < da_pool.size(); i++) {
        Entity e = da_pool.entity_at(i);
        auto& da = da_pool.at_index(i);
        da.timer += dt;

        // Emit dissolve particles during the fade phase (after initial flash)
        float t = da.timer / da.duration;
        if (t > 0.25f && world_.has<Position>(e)) {
            auto& pos = world_.get<Position>(e);
            // 1-2 particles per frame, drifting outward
            uint8_t r = 180, g = 160, b = 140;
            if (world_.has<Renderable>(e)) {
                auto& rend = world_.get<Renderable>(e);
                r = rend.tint.r; g = rend.tint.g; b = rend.tint.b;
            }
            particles_.drift(static_cast<float>(pos.x), static_cast<float>(pos.y),
                             1, r, g, b, 0.3f, 1);
        }

        if (da.timer >= da.duration) {
            finished.push_back(e);
        }
    }
    // Finalize deaths — swap to corpse sprite
    for (Entity e : finished) {
        if (world_.has<Renderable>(e)) {
            auto& rend = world_.get<Renderable>(e);
            rend.sprite_sheet = SHEET_TILES;
            rend.sprite_x = 0;
            rend.sprite_y = 21;
            rend.z_order = -1;
            rend.tint = {255, 255, 255, 255};
            rend.flip_h = false;
        }
        world_.remove<DeathAnim>(e);
    }
}

void Engine::render_weather() {
    // Only on overworld, only during gameplay
    if (dungeon_level_ != 0) { weather_particles_.clear(); return; }
    if (state_ != GameState::PLAYING) return;
    if (!world_.has<Position>(player_)) return;

    auto& pos = world_.get<Position>(player_);
    int py = pos.y;
    int px = pos.x;
    int map_h = map_.height(); // 1500 for overworld

    // Determine climate zone and spawn parameters
    enum Climate { CLEAR, ICE, COLD, RAIN, DUST };
    Climate climate = CLEAR;

    if (py < map_h / 6)                     climate = ICE;   // far north (y < 250)
    else if (py < map_h * 4 / 15)           climate = COLD;  // cold zone (y < 400)
    else if (px < 700 && py > 500 && py < 1100) climate = RAIN;  // Greenwood
    else if (py > map_h * 5 / 6)            climate = DUST;  // desert (y > 1250)

    if (climate == CLEAR) { weather_particles_.clear(); return; }

    // Spawn new particles
    int max_particles = 80;
    int spawn_per_frame = 0;
    auto randf = []() { return static_cast<float>(rand()) / RAND_MAX; };

    switch (climate) {
    case ICE:
        spawn_per_frame = 3;
        for (int i = 0; i < spawn_per_frame && (int)weather_particles_.size() < max_particles; i++) {
            float sx = randf() * width_;
            float drift_x = randf() * 0.6f - 0.2f; // slight rightward drift
            float fall_speed = 0.4f + randf() * 0.4f;
            uint8_t bright = 200 + static_cast<uint8_t>(randf() * 55);
            weather_particles_.push_back({
                sx, static_cast<float>(HUD_HEIGHT - 4 + randf() * 8),
                drift_x, fall_speed,
                bright, bright, bright,
                static_cast<uint8_t>(120 + randf() * 80),
                1.0f, 0.004f + randf() * 0.003f,
                2, 2
            });
        }
        break;

    case COLD:
        spawn_per_frame = 1;
        for (int i = 0; i < spawn_per_frame && (int)weather_particles_.size() < max_particles; i++) {
            float sx = randf() * width_;
            float drift_x = randf() * 0.5f - 0.15f;
            float fall_speed = 0.3f + randf() * 0.3f;
            uint8_t bright = 190 + static_cast<uint8_t>(randf() * 50);
            weather_particles_.push_back({
                sx, static_cast<float>(HUD_HEIGHT - 4 + randf() * 8),
                drift_x, fall_speed,
                bright, bright, bright,
                static_cast<uint8_t>(100 + randf() * 60),
                1.0f, 0.005f + randf() * 0.004f,
                2, 2
            });
        }
        break;

    case RAIN:
        spawn_per_frame = 4;
        for (int i = 0; i < spawn_per_frame && (int)weather_particles_.size() < max_particles; i++) {
            float sx = randf() * width_;
            float fall_speed = 2.5f + randf() * 1.5f;
            uint8_t grey = static_cast<uint8_t>(140 + randf() * 40);
            weather_particles_.push_back({
                sx, static_cast<float>(HUD_HEIGHT - 4 + randf() * 8),
                0.1f, fall_speed,
                static_cast<uint8_t>(grey * 0.7f), static_cast<uint8_t>(grey * 0.8f), grey,
                static_cast<uint8_t>(100 + randf() * 80),
                1.0f, 0.006f + randf() * 0.004f,
                1, 3
            });
        }
        break;

    case DUST:
        spawn_per_frame = 1;
        for (int i = 0; i < spawn_per_frame && (int)weather_particles_.size() < max_particles; i++) {
            float sy = HUD_HEIGHT + randf() * (height_ - HUD_HEIGHT - LOG_HEIGHT);
            float drift_x = 0.5f + randf() * 0.8f; // rightward drift
            float drift_y = randf() * 0.3f - 0.15f; // slight vertical wander
            weather_particles_.push_back({
                -2.0f, sy,
                drift_x, drift_y,
                static_cast<uint8_t>(180 + randf() * 40),
                static_cast<uint8_t>(160 + randf() * 30),
                static_cast<uint8_t>(110 + randf() * 30),
                static_cast<uint8_t>(80 + randf() * 60),
                1.0f, 0.003f + randf() * 0.002f,
                3, 3
            });
        }
        break;

    default:
        break;
    }

    // Update and render
    int game_area_bottom = height_ - LOG_HEIGHT;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (auto& p : weather_particles_) {
        p.x += p.vx;
        p.y += p.vy;
        p.life -= p.decay;

        // Draw only if within the game viewport (between HUD and log)
        int ix = static_cast<int>(p.x);
        int iy = static_cast<int>(p.y);
        if (iy >= HUD_HEIGHT && iy < game_area_bottom && ix >= -p.w && ix < width_ + p.w) {
            uint8_t a = static_cast<uint8_t>(p.alpha * std::max(0.0f, std::min(1.0f, p.life)));
            SDL_SetRenderDrawColor(renderer_, p.r, p.g, p.b, a);
            SDL_Rect rect = {ix, iy, p.w, p.h};
            SDL_RenderFillRect(renderer_, &rect);
        }
    }

    // Remove dead or off-screen particles
    weather_particles_.erase(
        std::remove_if(weather_particles_.begin(), weather_particles_.end(),
            [&](const WeatherParticle& p) {
                return p.life <= 0.0f ||
                       p.y > static_cast<float>(game_area_bottom) ||
                       p.x > static_cast<float>(width_ + 10) ||
                       p.x < -10.0f;
            }),
        weather_particles_.end());
}

void Engine::render_day_night() {
    // Only on overworld during gameplay
    if (dungeon_level_ != 0 || state_ != GameState::PLAYING) return;

    // 100-turn cycle: turns 0-39 day, 40-49 dusk, 50-89 night, 90-99 dawn
    int phase = game_turn_ % 100;
    float night_alpha = 0.0f;

    if (phase >= 50 && phase < 90) {
        night_alpha = 1.0f; // full night
    } else if (phase >= 40 && phase < 50) {
        night_alpha = static_cast<float>(phase - 40) / 10.0f; // dusk fade in
    } else if (phase >= 90) {
        night_alpha = 1.0f - static_cast<float>(phase - 90) / 10.0f; // dawn fade out
    }

    if (night_alpha <= 0.0f) return;

    // Dark blue overlay on the game area (between HUD and log)
    int a = static_cast<int>(night_alpha * 85.0f); // max alpha 85 — noticeable darkening
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 10, 12, 35, static_cast<Uint8>(a));
    SDL_Rect overlay = {0, HUD_HEIGHT, width_, height_ - HUD_HEIGHT - LOG_HEIGHT};
    SDL_RenderFillRect(renderer_, &overlay);
}

void Engine::render_zone_tint() {
    // Subtle color tint per dungeon zone
    if (dungeon_level_ <= 0 || state_ != GameState::PLAYING) return;

    std::string zone_str;
    if (current_dungeon_idx_ >= 0 &&
        current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
        zone_str = dungeon_registry_[current_dungeon_idx_].zone;
    }

    uint8_t r = 0, g = 0, b = 0;
    int alpha = 0;
    if (zone_str == "molten") {
        r = 60; g = 15; b = 5; alpha = 40;  // warm red
    } else if (zone_str == "sunken") {
        r = 5; g = 15; b = 45; alpha = 38;  // deep blue
    } else if (zone_str == "warrens") {
        r = 10; g = 25; b = 8; alpha = 30;  // damp green
    } else if (zone_str == "catacombs") {
        r = 20; g = 15; b = 25; alpha = 32; // dusty purple
    } else if (zone_str == "sepulchre") {
        r = 8; g = 5; b = 15; alpha = 45;   // dark violet
    } else if (zone_str == "deep_halls") {
        r = 12; g = 12; b = 18; alpha = 28; // cool grey
    } else if (zone_str == "stonekeep") {
        r = 15; g = 12; b = 10; alpha = 20; // dusty brown
    }
    // stonekeep and fallback: no tint

    if (alpha <= 0) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, r, g, b, static_cast<Uint8>(alpha));
    SDL_Rect overlay = {0, HUD_HEIGHT, width_, height_ - HUD_HEIGHT - LOG_HEIGHT};
    SDL_RenderFillRect(renderer_, &overlay);
}

void Engine::update_screen_shake() {
    if (shake_intensity_ <= 0.2f) {
        shake_intensity_ = 0.0f;
        shake_dx_ = shake_dy_ = 0.0f;
        return;
    }
    // Random offset within intensity, decay quickly
    float angle = static_cast<float>(rand()) / RAND_MAX * 6.283f;
    shake_dx_ = std::cos(angle) * shake_intensity_;
    shake_dy_ = std::sin(angle) * shake_intensity_;
    shake_intensity_ *= 0.75f; // fast decay
}

void Engine::screen_flash(float r, float g, float b, float alpha) {
    flash_r_ = r; flash_g_ = g; flash_b_ = b;
    flash_alpha_ = alpha;
}

void Engine::trigger_screen_shake(float intensity) {
    shake_intensity_ = intensity;
}

void Engine::render_hud() {
    if (!font_) return;
    if (!world_.has<Stats>(player_)) return;

    auto& stats = world_.get<Stats>(player_);
    SDL_Color white = {200, 200, 200, 255};
    int line_h = TTF_FontLineSkip(font_);

    // HUD background
    SDL_Rect hud_bg = {0, 0, width_, HUD_HEIGHT};
    SDL_SetRenderDrawColor(renderer_, 15, 12, 18, 240);
    SDL_RenderFillRect(renderer_, &hud_bg);
    SDL_SetRenderDrawColor(renderer_, 60, 50, 70, 255);
    SDL_RenderDrawLine(renderer_, 0, HUD_HEIGHT, width_, HUD_HEIGHT);

    int bar_h = 14;
    int bar_y = (HUD_HEIGHT - bar_h) / 2;

    // Left half: bars + status indicators. Use Layout for cursor management.
    auto left_hud = ui::Layout::from_rect({0, 0, width_ / 2, HUD_HEIGHT}, line_h);
    left_hud.skip_h(8);

    // Scale bar widths proportionally to HUD width
    int hp_bar_w = width_ / 20;
    int mp_bar_w = width_ / 25;
    int xp_bar_w = width_ / 30;
    int tag_gap = 6;

    // Helper: draw a stat bar + label, advance cursor
    auto draw_bar = [&](const char* label, int val, int max_val,
                         SDL_Color bg_col, SDL_Color fill_col, int bar_w) {
        if (left_hud.remaining_w() < bar_w + 20) return;
        auto bar_col = left_hud.col_left(bar_w);
        SDL_Rect bg = {bar_col.x, bar_y, bar_w, bar_h};
        SDL_SetRenderDrawColor(renderer_, bg_col.r, bg_col.g, bg_col.b, 255);
        SDL_RenderFillRect(renderer_, &bg);
        int fill = (val * bar_w) / std::max(1, max_val);
        SDL_Rect fill_r = {bar_col.x, bar_y, fill, bar_h};
        SDL_SetRenderDrawColor(renderer_, fill_col.r, fill_col.g, fill_col.b, 255);
        SDL_RenderFillRect(renderer_, &fill_r);
        left_hud.skip_h(4);

        int tw = ui::text_width(font_, label);
        auto label_area = left_hud.col_left(tw + 12);
        ui::draw_text(renderer_, font_, label, white, label_area.x, bar_y);
    };

    // Helper: draw a text tag, advance cursor
    auto draw_tag = [&](const char* tag, SDL_Color col) {
        int tw = ui::text_width(font_, tag);
        if (left_hud.remaining_w() < tw + tag_gap) return;
        auto area = left_hud.col_left(tw + tag_gap);
        ui::draw_text(renderer_, font_, tag, col, area.x, bar_y);
    };

    // HP bar
    char hp_text[32];
    snprintf(hp_text, sizeof(hp_text), "HP:%d/%d", stats.hp, stats.hp_max);
    SDL_Color hp_fill = stats.hp > stats.hp_max / 2 ? SDL_Color{140, 40, 40, 255}
                      : stats.hp > stats.hp_max / 4 ? SDL_Color{160, 100, 30, 255}
                                                      : SDL_Color{200, 50, 50, 255};
    draw_bar(hp_text, stats.hp, stats.hp_max, {40, 10, 10, 255}, hp_fill, hp_bar_w);

    // MP bar
    if (stats.mp_max > 0) {
        char mp_text[32];
        snprintf(mp_text, sizeof(mp_text), "MP:%d/%d", stats.mp, stats.mp_max);
        draw_bar(mp_text, stats.mp, stats.mp_max, {10, 10, 40, 255}, {60, 60, 160, 255}, mp_bar_w);
    }

    // XP bar
    char lvl_text[32];
    snprintf(lvl_text, sizeof(lvl_text), "Lv%d", stats.level);
    draw_bar(lvl_text, stats.xp, stats.xp_next, {15, 15, 40, 255}, {80, 80, 180, 255}, xp_bar_w);

    // Status effects
    if (world_.has<StatusEffects>(player_)) {
        auto& fx = world_.get<StatusEffects>(player_);
        for (auto& eff : fx.effects) {
            const char* tag = "";
            SDL_Color col = {200, 200, 200, 255};
            switch (eff.type) {
                case StatusType::POISON:   tag = "PSN"; col = {100, 200, 100, 255}; break;
                case StatusType::BURN:     tag = "BRN"; col = {255, 160, 60, 255}; break;
                case StatusType::BLEED:    tag = "BLD"; col = {200, 80, 80, 255}; break;
                case StatusType::FROZEN:   tag = "FRZ"; col = {140, 200, 255, 255}; break;
                case StatusType::STUNNED:  tag = "STN"; col = {255, 255, 100, 255}; break;
                case StatusType::CONFUSED: tag = "CNF"; col = {200, 140, 255, 255}; break;
                case StatusType::BLIND:    tag = "BLN"; col = {120, 120, 120, 255}; break;
                case StatusType::FEARED:   tag = "FER"; col = {255, 255, 255, 255}; break;
            }
            draw_tag(tag, col);
        }
    }

    // Summon count
    {
        int summon_count = 0;
        auto& ai_pool = world_.pool<AI>();
        for (size_t si = 0; si < ai_pool.size(); si++) {
            if (ai_pool.at_index(si).friendly && world_.has<Stats>(ai_pool.entity_at(si)))
                summon_count++;
        }
        if (summon_count > 0) {
            char sumbuf[24];
            snprintf(sumbuf, sizeof(sumbuf), "SUM:%d/%d", summon_count, 3);
            draw_tag(sumbuf, {120, 180, 140, 255});
        }
    }

    // Sneak
    if (sneaking_) draw_tag("SNK", {140, 140, 200, 255});

    // Rest counter
    if (dungeon_level_ > 0) {
        int max_rests = 2;
        if (world_.has<GodAlignment>(player_)) {
            auto& ga = world_.get<GodAlignment>(player_);
            if (ga.god == GodId::LETHIS) max_rests = 3;
        }
        if (background_ == BackgroundId::MONK_OF_ORDER) max_rests = 3;
        int rests_left = std::max(0, max_rests - rest_count_this_floor_);
        char rstbuf[24];
        snprintf(rstbuf, sizeof(rstbuf), "REST:%d/%d", rests_left, max_rests);
        SDL_Color rst_col = rests_left == 0 ? SDL_Color{160, 100, 80, 255} :
                            rests_left == 1 ? SDL_Color{220, 180, 60, 255} :
                                              SDL_Color{100, 200, 100, 255};
        draw_tag(rstbuf, rst_col);
    }

    // Hardcore
    if (hardcore_) draw_tag("HC", {200, 80, 80, 255});

    // Diseases
    if (world_.has<Diseases>(player_)) {
        auto& diseases = world_.get<Diseases>(player_);
        for (auto did : diseases.active) {
            auto& dinfo = get_disease_info(did);
            draw_tag(dinfo.hud_tag, {180, 120, 200, 255});
        }
    }

    // Unspent passive points
    if (world_.has<PassiveTreeState>(player_)) {
        auto& tree_check = world_.get<PassiveTreeState>(player_);
        if (tree_check.points_available > 0) {
            char ptbuf[32];
            snprintf(ptbuf, sizeof(ptbuf), "+%d [T]", tree_check.points_available);
            draw_tag(ptbuf, {255, 220, 60, 255});
        }
    }

    // Active abilities
    if (world_.has<PassiveTreeState>(player_)) {
        auto& tree = world_.get<PassiveTreeState>(player_);
        auto bonuses = passive_tree::compute_bonuses(tree);

        struct CapInfo { int cd_val; const char* name; int cd_idx; };
        CapInfo caps[] = {
            {bonuses.cap_whirlwind_cd, "WHL", 0},
            {bonuses.cap_time_slip_cd, "TSL", 1},
            {bonuses.cap_arcane_overload_cd, "AOL", 2},
            {bonuses.cap_divine_intervention_cd, "DIV", 3},
            {bonuses.cap_unbreakable_cd, "UNB", 4},
            {bonuses.cap_aspect_of_beast_cd, "BST", 5},
            {bonuses.cap_death_mark_cd, "DMK", 6},
            {bonuses.cap_pandemic_cd, "PND", 7},
        };
        int slot = 1;
        for (auto& cap : caps) {
            if (cap.cd_val == 0) continue;
            if (left_hud.remaining_w() < 60) break;
            int cd_remaining = tree.capstone_cooldowns[cap.cd_idx];
            bool ready = (cd_remaining <= 0);
            char abuf[16];
            if (ready)
                snprintf(abuf, sizeof(abuf), "[%d]%s", slot, cap.name);
            else
                snprintf(abuf, sizeof(abuf), "[%d]%d", slot, cd_remaining);
            draw_tag(abuf, ready ? SDL_Color{200, 220, 140, 255} : SDL_Color{120, 110, 100, 255});
            slot++;
        }
    }

    // Quick-cast
    if (quick_cast_ != SpellId::COUNT) {
        auto& qsi = get_spell_info(quick_cast_);
        char qbuf[64];
        snprintf(qbuf, sizeof(qbuf), "[v] %s", qsi.name);
        draw_tag(qbuf, {120, 140, 180, 255});
    }

    // Help hint (only if space remains)
    draw_tag("? help", {100, 95, 85, 255});

    // Right side: god + location + gold + turn
    const char* god_name = "";
    if (world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        god_name = get_god_info(ga.god).name;
    }
    char info[256];
    if (dungeon_level_ <= 0) {
        const char* location = cached_location_;
        const char* time_icon = is_night() ? "Night" : is_dusk() ? "Dusk" : is_dawn() ? "Dawn" : "Day";
        snprintf(info, sizeof(info), "%s  %s  %s  Gold:%d  T:%d",
                 god_name, location, time_icon, gold_, game_turn_);
    } else {
        const char* dname = "Dungeon";
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            dname = dungeon_registry_[current_dungeon_idx_].name.c_str();
        }
        snprintf(info, sizeof(info), "%s  %s D:%d  Gold:%d  T:%d",
                 god_name, dname, dungeon_level_, gold_, game_turn_);
    }

    // Right half: clip and right-align
    SDL_Rect right_clip = {width_ / 2, 0, width_ / 2, HUD_HEIGHT};
    SDL_RenderSetClipRect(renderer_, &right_clip);
    int info_tw = ui::text_width(font_, info);
    int max_w = width_ / 2 - 8;
    if (info_tw > max_w) {
        ui::draw_text_clipped(renderer_, font_, info, white, width_ - max_w, bar_y - 1, max_w);
    } else {
        ui::draw_text(renderer_, font_, info, white, width_ - info_tw - 8, bar_y - 1);
    }
    SDL_RenderSetClipRect(renderer_, nullptr);
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

    // Intro cinematic
    if (state_ == GameState::INTRO) {
        intro_screen_.render(renderer_, font_, font_title_, width_, height_);
        if (intro_screen_.is_done()) {
            state_ = GameState::PLAYING;
        }
        SDL_RenderPresent(renderer_);
        return;
    }

    // Dark slate background — unexplored areas
    SDL_SetRenderDrawColor(renderer_, 18, 20, 28, 255);
    SDL_RenderClear(renderer_);

    Camera render_cam = camera_;

    // Set player position on camera for FOV edge fade
    if (world_.has<Position>(player_)) {
        auto& pp = world_.get<Position>(player_);
        render_cam.px = pp.x;
        render_cam.py = pp.y;
    }
    if (world_.has<Stats>(player_))
        render_cam.fov_r = world_.get<Stats>(player_).fov_radius();

    // Screen shake: pixel-level offset passed through y_offset
    int y_off = HUD_HEIGHT + static_cast<int>(shake_dy_);

    // Compute per-tile lighting
    {
        int ambient = 220; // overworld daytime default: bright
        if (dungeon_level_ > 0) {
            // Dungeons are darker at deeper floors
            ambient = std::max(80, 160 - dungeon_level_ * 8);
        } else {
            // Overworld: night cycle reduces ambient
            int phase = game_turn_ % 100;
            if (phase >= 50 && phase < 90)
                ambient = 130; // night
            else if (phase >= 40 && phase < 50)
                ambient = 220 - static_cast<int>((phase - 40) * 9.0f); // dusk
            else if (phase >= 90)
                ambient = 130 + static_cast<int>((phase - 90) * 9.0f); // dawn
        }
        render::compute_lighting(map_, world_, ambient, render_cam);
    }

    // Draw map
    render::draw_map(renderer_, sprites_, map_, render_cam, y_off);

    // Draw entities
    render::draw_entities(renderer_, sprites_, world_, map_, render_cam, y_off);

    // God brand glow on player's face
    if (world_.has<GodAlignment>(player_) && world_.has<Position>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        if (ga.god != GodId::NONE) {
            auto& pp = world_.get<Position>(player_);
            if (map_.in_bounds(pp.x, pp.y) && map_.at(pp.x, pp.y).visible) {
                auto& ginfo = get_god_info(ga.god);
                int TS = render_cam.tile_size;
                int px = (pp.x - render_cam.x) * TS;
                int py = (pp.y - render_cam.y) * TS + y_off;

                // Brand position: upper portion of sprite (face area)
                // For a 32px sprite scaled to TS, the face is roughly
                // at 30-45% from top, centered horizontally
                int brand_cx = px + TS / 2;
                int brand_cy = py + TS * 30 / 100; // 30% from top
                int brand_r = TS / 6; // small glow

                // Pulsing glow intensity
                float pulse = 0.7f + 0.3f * sinf(SDL_GetTicks() * 0.003f);
                uint8_t br = ginfo.color.r, bg = ginfo.color.g, bb = ginfo.color.b;
                int base_alpha = static_cast<int>(40 * pulse);

                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

                // Soft radial glow (3 layers)
                for (int layer = 3; layer >= 1; layer--) {
                    int lr = brand_r * layer;
                    int la = base_alpha / layer;
                    SDL_SetRenderDrawColor(renderer_, br, bg, bb, static_cast<uint8_t>(la));
                    for (int dy = -lr; dy <= lr; dy++) {
                        int dx = static_cast<int>(sqrtf(static_cast<float>(lr * lr - dy * dy)));
                        SDL_RenderDrawLine(renderer_, brand_cx - dx, brand_cy + dy,
                                                      brand_cx + dx, brand_cy + dy);
                    }
                }

                // Bright core
                int core_alpha = static_cast<int>(70 * pulse);
                SDL_SetRenderDrawColor(renderer_, br, bg, bb, static_cast<uint8_t>(core_alpha));
                for (int dy = -brand_r / 2; dy <= brand_r / 2; dy++) {
                    int dx = brand_r / 2 - std::abs(dy);
                    SDL_RenderDrawLine(renderer_, brand_cx - dx, brand_cy + dy,
                                                  brand_cx + dx, brand_cy + dy);
                }

                // Tiny bright pixel at center (the mark itself)
                SDL_SetRenderDrawColor(renderer_, std::min(255, br + 80),
                                                  std::min(255, bg + 80),
                                                  std::min(255, bb + 80),
                                                  static_cast<uint8_t>(120 * pulse));
                SDL_RenderDrawPoint(renderer_, brand_cx, brand_cy);
                SDL_RenderDrawPoint(renderer_, brand_cx + 1, brand_cy);
                SDL_RenderDrawPoint(renderer_, brand_cx, brand_cy + 1);
                SDL_RenderDrawPoint(renderer_, brand_cx - 1, brand_cy);
                SDL_RenderDrawPoint(renderer_, brand_cx, brand_cy - 1);
            }
        }
    }

    // Quest NPC indicators (WoW-style)
    //   Gold !  = quest available to accept
    //   Silver ? = quest in progress (accepted, not complete)
    //   Gold ?  = quest ready to turn in
    if (font_) {
        int TS = render_cam.tile_size;
        auto& npc_pool = world_.pool<NPC>();
        Uint32 blink = (SDL_GetTicks() / 500) % 2;
        for (size_t i = 0; i < npc_pool.size(); i++) {
            Entity e = npc_pool.entity_at(i);
            auto& npc = npc_pool.at_index(i);
            if (npc.quest_id < 0) continue;
            if (!world_.has<Position>(e)) continue;
            auto& np = world_.get<Position>(e);
            if (!map_.in_bounds(np.x, np.y) || !map_.at(np.x, np.y).visible) continue;

            auto qid = static_cast<QuestId>(npc.quest_id);
            const char* symbol = "!";
            SDL_Color marker_col = {255, 220, 80, 255}; // gold

            if (journal_.has_quest(qid)) {
                auto state = journal_.get_state(qid);
                if (state == QuestState::COMPLETE) {
                    symbol = "?";
                    marker_col = {255, 220, 80, 255}; // gold ?
                } else if (state == QuestState::ACTIVE) {
                    symbol = "?";
                    marker_col = {180, 180, 190, 255}; // silver ?
                    if (!blink) continue; // silver blinks slower
                } else if (state == QuestState::FINISHED) {
                    continue; // no marker for finished quests
                }
            } else {
                // Quest not in journal: check prerequisites
                // If prereq not met, don't show marker
                auto quest_prereq = [](QuestId id) -> QuestId {
                    int idx = static_cast<int>(id);
                    if (idx <= 0 || idx > static_cast<int>(QuestId::MQ_17_CLAIM_RELIQUARY))
                        return QuestId::COUNT;
                    return static_cast<QuestId>(idx - 1);
                };
                auto prereq = quest_prereq(qid);
                if (prereq != QuestId::COUNT &&
                    (!journal_.has_quest(prereq) || journal_.get_state(prereq) != QuestState::FINISHED)) {
                    continue; // prereq not met, hide marker
                }
                // Gold ! for available quest
                symbol = "!";
                marker_col = {255, 220, 80, 255};
            }

            // Bob the marker up and down
            float bob = sinf(SDL_GetTicks() * 0.004f + i * 0.5f) * 3.0f;
            int sx = (np.x - render_cam.x) * TS + TS / 2;
            int sy = (np.y - render_cam.y) * TS + y_off - 8 + static_cast<int>(bob);

            SDL_Surface* surf = TTF_RenderText_Blended(font_, symbol, marker_col);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
                SDL_Rect dst = {sx - surf->w / 2, sy - surf->h, surf->w, surf->h};
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }

        // Also show markers for dynamic quests
        auto& dq_pool = world_.pool<DynamicQuest>();
        for (size_t i = 0; i < dq_pool.size(); i++) {
            Entity e = dq_pool.entity_at(i);
            auto& dq = dq_pool.at_index(i);
            if (!world_.has<Position>(e)) continue;
            auto& dp = world_.get<Position>(e);
            if (!map_.in_bounds(dp.x, dp.y) || !map_.at(dp.x, dp.y).visible) continue;

            const char* sym = "!";
            SDL_Color col = {255, 220, 80, 255};

            if (dq.accepted && dq.completed) {
                sym = "?";
                col = {255, 220, 80, 255}; // gold ? for turn-in
            } else if (dq.accepted) {
                sym = "?";
                col = {180, 180, 190, 255}; // silver ?
                if (!blink) continue;
            } else {
                sym = "!";
                col = {220, 200, 100, 255}; // slightly dimmer gold for side quests
            }

            float bob = sinf(SDL_GetTicks() * 0.004f + i * 0.7f) * 3.0f;
            int sx = (dp.x - render_cam.x) * TS + TS / 2;
            int sy = (dp.y - render_cam.y) * TS + y_off - 8 + static_cast<int>(bob);

            SDL_Surface* surf = TTF_RenderText_Blended(font_, sym, col);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
                SDL_Rect dst = {sx - surf->w / 2, sy - surf->h, surf->w, surf->h};
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }
    }

    // God visual effects on player (rendered every frame)
    render_god_visuals(render_cam, y_off);

    // Overworld weather particles (screen-space, after entities, before HUD)
    render_weather();

    // Day/night and zone atmosphere handled by per-tile lighting (compute_lighting)

    // Screen flash overlay (decays per frame)
    if (flash_alpha_ > 1.0f) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_Rect flash_rect = {0, 0, width_, height_};
        SDL_SetRenderDrawColor(renderer_, static_cast<uint8_t>(flash_r_),
                                static_cast<uint8_t>(flash_g_),
                                static_cast<uint8_t>(flash_b_),
                                static_cast<uint8_t>(flash_alpha_));
        SDL_RenderFillRect(renderer_, &flash_rect);
        flash_alpha_ *= 0.85f; // rapid decay
    }

    // Sneak visual: keep player alpha synced + draw detection ranges
    if (world_.has<Renderable>(player_)) {
        auto& pr = world_.get<Renderable>(player_);
        pr.tint.a = sneaking_ ? 140 : 255;
    }
    if (sneaking_) {
        // Draw detection range indicators around visible enemies
        int TS = camera_.tile_size;
        auto& ai_pool = world_.pool<AI>();
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        for (size_t i = 0; i < ai_pool.size(); i++) {
            Entity ae = ai_pool.entity_at(i);
            if (ai_pool.at_index(i).friendly) continue;
            if (!world_.has<Position>(ae)) continue;
            auto& apos = world_.get<Position>(ae);
            if (!map_.in_bounds(apos.x, apos.y) || !map_.at(apos.x, apos.y).visible) continue;

            int detect_range = 3;
            if (world_.has<Skills>(player_)) {
                int slv = world_.get<Skills>(player_).get_level(SkillId::STEALTH);
                if (slv >= 50) detect_range = 1;
                else if (slv >= 25) detect_range = 2;
            }

            // Screen coords of monster center
            int sx = (apos.x - camera_.x) * TS + TS / 2;
            int sy = (apos.y - camera_.y) * TS + TS / 2 + HUD_HEIGHT;
            int radius = detect_range * TS;

            // Filled red zone (more visible)
            SDL_SetRenderDrawColor(renderer_, 220, 40, 40, 25);
            for (int fy = -radius; fy <= radius; fy++) {
                int fx = static_cast<int>(std::sqrt(static_cast<float>(radius * radius - fy * fy)));
                SDL_RenderDrawLine(renderer_, sx - fx, sy + fy, sx + fx, sy + fy);
            }
            // Thick red outline (3 concentric circles)
            for (int ro = -1; ro <= 1; ro++) {
                int r2 = radius + ro;
                SDL_SetRenderDrawColor(renderer_, 255, 50, 50, 120);
                for (int angle = 0; angle < 360; angle++) {
                    float rad = angle * 3.14159f / 180.0f;
                    int px = sx + static_cast<int>(r2 * cosf(rad));
                    int py = sy + static_cast<int>(r2 * sinf(rad));
                    SDL_RenderDrawPoint(renderer_, px, py);
                }
            }
        }
    }

    // HUD
    render_hud();

    // Message log
    log_.render(renderer_, font_, 0, height_ - LOG_HEIGHT, width_, LOG_HEIGHT);

    // Dungeon minimap (top-right corner)
    render_minimap();

    // Overlay screens
    inventory_screen_.render(renderer_, font_, sprites_, world_, width_, height_);
    spell_screen_.render(renderer_, font_, world_, width_, height_);
    char_sheet_.render(renderer_, font_, font_title_, sprites_, world_, width_, height_);
    quest_log_.render(renderer_, font_, font_title_, journal_, width_, height_, &world_);
    quest_offer_.render(renderer_, font_, font_title_, width_, height_);
    help_screen_.render(renderer_, font_, font_title_, width_, height_);
    passive_tree_screen_.render(renderer_, font_, font_title_, width_, height_);
    church_screen_.render(renderer_, font_, font_title_, width_, height_);
    // Old levelup_screen_ removed; passive tree replaces it
    shop_screen_.render(renderer_, font_, sprites_, world_, width_, height_);

    // World map overlay — needs player position and tilemap
    if (world_map_.is_open() && world_.has<Position>(player_)) {
        auto& pos = world_.get<Position>(player_);
        world_map_.render(renderer_, font_, font_title_, map_, pos.x, pos.y, width_, height_);
    }

    // Pause menu overlay
    pause_menu_.render(renderer_, font_, font_title_, width_, height_);

    // Death overlay — fades in over 2 seconds, accepts input after 3 seconds
    if (state_ == GameState::DEAD && font_) {
        Uint32 elapsed = SDL_GetTicks() - end_screen_time_;
        int god_id = static_cast<int>(GodId::NONE);
        if (world_.has<GodAlignment>(player_))
            god_id = static_cast<int>(world_.get<GodAlignment>(player_).god);
        render_death_screen(renderer_, font_, font_title_, width_, height_,
                            elapsed, death_cause_, god_id, newly_unlocked_, build_run_summary());
    }

    if (state_ == GameState::VICTORY) {
        render_victory();
    }

    // Particles (use shake-offset y)
    if (!particles_.empty()) {
        particles_.render(renderer_, camera_.x, camera_.y, camera_.tile_size, y_off);
    }

    // Floating damage/heal numbers
    floating_text_.render(renderer_, font_, font_title_, camera_.x, camera_.y, camera_.tile_size, y_off);

    // Day/night darkness overlay (overworld only)
    if (dungeon_level_ == 0) {
        float dark = night_darkness();
        if (dark > 0.01f) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            uint8_t alpha = static_cast<uint8_t>(dark * 120); // max 120 alpha, not full black
            // Blue-tinted darkness
            SDL_SetRenderDrawColor(renderer_, 8, 8, 30, alpha);
            SDL_Rect dark_rect = {0, 0, width_, height_};
            SDL_RenderFillRect(renderer_, &dark_rect);
        }
    }

    // Pet naming overlay
    if (pet_naming_ && font_) {
        int pw = std::min(width_ / 3, 500);
        int ph = TTF_FontLineSkip(font_) * 3 + 24;
        int px = (width_ - pw) / 2;
        int py = height_ / 3;
        ui::draw_panel(renderer_, px, py, pw, ph);
        ui::draw_text_centered(renderer_, font_, "Name your pet:",
                                {200, 190, 140, 255}, width_ / 2, py + 8);
        std::string display = pet_name_buf_;
        if ((SDL_GetTicks() / 500) % 2 == 0) display += "_";
        ui::draw_text_centered(renderer_, font_, display.c_str(),
                                {255, 240, 200, 255}, width_ / 2, py + 8 + TTF_FontLineSkip(font_) + 8);
        ui::draw_text_centered(renderer_, font_, "[Enter] confirm   [Esc] skip",
                                {100, 95, 90, 255}, width_ / 2, py + ph - TTF_FontLineSkip(font_) - 4);
    }

    // Look mode cursor highlight
    if (look_mode_ && map_.in_bounds(look_x_, look_y_)) {
        int px = (look_x_ - camera_.x) * camera_.tile_size;
        int py = (look_y_ - camera_.y) * camera_.tile_size;
        if (px >= 0 && py >= 0 && px < width_ && py < height_ - LOG_HEIGHT - HUD_HEIGHT) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_Rect cursor_rect = {px, py + HUD_HEIGHT, camera_.tile_size, camera_.tile_size};
            SDL_SetRenderDrawColor(renderer_, 255, 255, 100, 80);
            SDL_RenderFillRect(renderer_, &cursor_rect);
            SDL_SetRenderDrawColor(renderer_, 255, 255, 100, 200);
            SDL_RenderDrawRect(renderer_, &cursor_rect);
        }
    }

    // Tutorial popup (on top of everything except transitions)
    tutorial_popup_.render(renderer_, font_, font_title_, width_, height_);

    render_transition();
    SDL_RenderPresent(renderer_);
}

void Engine::render_transition() {
    if (transition_ == TransitionType::NONE) return;

    Uint32 elapsed = SDL_GetTicks() - transition_start_;
    if (elapsed >= transition_duration_) {
        transition_ = TransitionType::NONE;
        return;
    }

    float t = static_cast<float>(elapsed) / static_cast<float>(transition_duration_);
    Uint8 alpha = 0;

    switch (transition_) {
        case TransitionType::FADE_OUT:
            alpha = static_cast<Uint8>(t * 255);
            break;
        case TransitionType::FADE_IN:
            alpha = static_cast<Uint8>((1.0f - t) * 255);
            break;
        case TransitionType::FLASH:
            // Quick flash: bright then fade
            alpha = static_cast<Uint8>((t < 0.3f) ? 200 : 200 * (1.0f - (t - 0.3f) / 0.7f));
            break;
        default: return;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, width_, height_};
    SDL_SetRenderDrawColor(renderer_, transition_color_.r, transition_color_.g,
                            transition_color_.b, alpha);
    SDL_RenderFillRect(renderer_, &overlay);
}

void Engine::render_minimap() {
    // Only show in dungeons, not overworld (overworld has the M world map)
    if (dungeon_level_ <= 0 || !minimap_visible_) return;
    if (!world_.has<Position>(player_)) return;

    auto& pp = world_.get<Position>(player_);
    int map_w = map_.width();
    int map_h = map_.height();
    if (map_w == 0 || map_h == 0) return;

    // Minimap size and position (top-right corner, below HUD)
    int mm_size = std::min(160, std::min(width_ / 5, height_ / 4));
    int mm_margin = 8;
    int mm_x = width_ - mm_size - mm_margin;
    int mm_y = HUD_HEIGHT + mm_margin;

    // Scale: fit map into minimap
    float scale_x = static_cast<float>(mm_size) / map_w;
    float scale_y = static_cast<float>(mm_size) / map_h;
    float scale = std::min(scale_x, scale_y);

    // Center the map in the minimap area
    int drawn_w = static_cast<int>(map_w * scale);
    int drawn_h = static_cast<int>(map_h * scale);
    int ox = mm_x + (mm_size - drawn_w) / 2;
    int oy = mm_y + (mm_size - drawn_h) / 2;

    // Background
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_Rect bg = {mm_x - 2, mm_y - 2, mm_size + 4, mm_size + 4};
    SDL_SetRenderDrawColor(renderer_, 10, 8, 14, 200);
    SDL_RenderFillRect(renderer_, &bg);
    SDL_SetRenderDrawColor(renderer_, 50, 45, 60, 200);
    SDL_RenderDrawRect(renderer_, &bg);

    // Draw explored tiles
    for (int y = 0; y < map_h; y++) {
        for (int x = 0; x < map_w; x++) {
            auto& tile = map_.at(x, y);
            if (!tile.explored) continue;

            int dx = ox + static_cast<int>(x * scale);
            int dy = oy + static_cast<int>(y * scale);
            int dw = std::max(1, static_cast<int>(scale));
            int dh = std::max(1, static_cast<int>(scale));

            if (tile.visible) {
                // Visible: brighter
                if (map_.is_opaque(x, y))
                    SDL_SetRenderDrawColor(renderer_, 80, 75, 90, 255); // wall
                else
                    SDL_SetRenderDrawColor(renderer_, 50, 48, 58, 255); // floor
            } else {
                // Explored but not visible: dim
                SDL_SetRenderDrawColor(renderer_, 35, 33, 42, 255);
            }

            SDL_Rect px_rect = {dx, dy, dw, dh};
            SDL_RenderFillRect(renderer_, &px_rect);

            // Stairs
            if (tile.type == TileType::STAIRS_DOWN) {
                SDL_SetRenderDrawColor(renderer_, 100, 200, 100, 255);
                SDL_RenderFillRect(renderer_, &px_rect);
            } else if (tile.type == TileType::STAIRS_UP) {
                SDL_SetRenderDrawColor(renderer_, 200, 200, 100, 255);
                SDL_RenderFillRect(renderer_, &px_rect);
            }
        }
    }

    // Player dot (bright, pulsing)
    {
        int pdx = ox + static_cast<int>(pp.x * scale);
        int pdy = oy + static_cast<int>(pp.y * scale);
        int ps = std::max(2, static_cast<int>(scale * 2));
        Uint8 pulse = static_cast<Uint8>(200 + static_cast<int>(std::sin(SDL_GetTicks() / 300.0f) * 55));
        SDL_SetRenderDrawColor(renderer_, 255, 255, pulse, 255);
        SDL_Rect pd = {pdx - ps / 2, pdy - ps / 2, ps, ps};
        SDL_RenderFillRect(renderer_, &pd);
    }
}

void Engine::run() {
    while (state_ != GameState::QUIT) {
        handle_input();
        process_turn();
        particles_.update(); // smooth animation between turns
        floating_text_.update();
        update_death_anims();
        update_screen_shake();
        render();
        // vsync handles frame pacing; no SDL_Delay needed
    }
    // Save meta-progression on quit (so kills/depth/etc. aren't lost if the player
    // closes the game without dying or winning)
    if (player_ != NULL_ENTITY) {
        update_meta_on_end();
    }
}
