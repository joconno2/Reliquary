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
#include "components/tenet.h"
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
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
        // Calculate zone difficulty based on distance from Thornwall
        constexpr int START_X = 500, START_Y = 375;
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
                // All dungeons capped at 4 floors (tighter loop)
                de.max_depth = std::min(4, 3 + de.zone_difficulty / 3);
                if (de.zone == "sepulchre") { de.max_depth = 9; de.zone_difficulty = 8; }
                else if (!de.quest.empty() && de.zone != "warrens") de.max_depth = 4;
                // Warrens quest dungeons (The Barrow) stay at 3 — boss on bottom floor
            }
        }
    }

    audio_.init();
    keybinds_.load("save/keybinds.json");
    gamepad_.init();
    input_glyphs_.load(renderer_);
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
    update_music_for_location();
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
                ej["sbhpm"] = ce.stats.base_hp_max; ej["sbmpm"] = ce.stats.base_mp_max;
                ej["sdmg"] = ce.stats.base_damage; ej["sarm"] = ce.stats.natural_armor;
                ej["sspd"] = ce.stats.base_speed; ej["sxp"] = ce.stats.xp_value;
                if (ce.stats.phase_turns > 0) ej["sphase"] = ce.stats.phase_turns;
                if (ce.stats.wyrmkin_breath_ctr > 0) ej["swyrm"] = ce.stats.wyrmkin_breath_ctr;
                if (ce.stats.invisible_turns > 0) ej["sinvis"] = ce.stats.invisible_turns;
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
            if (ce.has_god) {
                ej["god"] = static_cast<int>(ce.god_align.god); ej["gfav"] = ce.god_align.favor;
                ej["crank"] = ce.god_align.church_rank;
                ej["ctask"] = ce.god_align.task_active ? 1 : 0;
                ej["cprog"] = ce.god_align.task_progress;
                ej["ccomp"] = ce.god_align.task_complete ? 1 : 0;
                ej["citem"] = ce.god_align.church_item_claimed ? 1 : 0;
                ej["cbless"] = ce.god_align.church_blessing_claimed ? 1 : 0;
            }
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
                    ce.stats.hp_max = ej.value("shpm", 1);
                    ce.stats.base_hp_max = ej.value("sbhpm", ce.stats.hp_max);
                    ce.stats.base_mp_max = ej.value("sbmpm", ce.stats.mp_max);
                    ce.stats.base_damage = ej.value("sdmg", 1);
                    ce.stats.natural_armor = ej.value("sarm", 0); ce.stats.base_speed = ej.value("sspd", 100);
                    ce.stats.xp_value = ej.value("sxp", 0);
                    ce.stats.phase_turns = ej.value("sphase", 0);
                    ce.stats.wyrmkin_breath_ctr = ej.value("swyrm", 0);
                    ce.stats.invisible_turns = ej.value("sinvis", 0);
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
                    ce.god_align.church_rank = ej.value("crank", 0);
                    ce.god_align.task_active = ej.value("ctask", 0) != 0;
                    ce.god_align.task_progress = ej.value("cprog", 0);
                    ce.god_align.task_complete = ej.value("ccomp", 0) != 0;
                    ce.god_align.church_item_claimed = ej.value("citem", 0) != 0;
                    ce.god_align.church_blessing_claimed = ej.value("cbless", 0) != 0;
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
    // Save overworld explored state before leaving it
    if (dungeon_level_ == 0 && map_.width() > 0) {
        int sz = map_.width() * map_.height();
        overworld_explored_.resize(sz);
        for (int i = 0; i < sz; i++)
            overworld_explored_[i] = map_.at(i % map_.width(), i / map_.width()).explored;
    }

    dungeon_level_++;
    if (dungeon_level_ > run_deepest_) run_deepest_ = dungeon_level_;
    rooms_explored_.clear();
    shrine_xp_this_floor_ = false;
    revenant_saved_this_floor_ = false;
    heavy_hitter_used_this_floor_ = false;
    trollblood_corpses_floor_ = 0;
    trollblood_gorged_ = false;

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
            // Auto-map: reveal entire floor layout
            for (int my = 0; my < map_.height(); my++)
                for (int mx = 0; mx < map_.width(); mx++)
                    map_.at(mx, my).explored = true;
        }
        // Gathruun gains favor from depth
        if (align.god == GodId::GATHRUUN) {
            god_system::adjust_favor(world_, player_, log_, 1);
        }
        // Church task: floor descent tracking (Gathruun, Thessarka)
        if (align.task_active && !align.task_complete) {
            auto next = static_cast<ChurchRank>(align.church_rank + 1);
            auto& task = get_church_task(align.god, next);
            if (task.is_dungeon_quest || (task.items_donated > 0 && align.god == GodId::GATHRUUN)) {
                align.task_progress++;
                int target = task.items_donated > 0 ? task.items_donated : 1;
                if (align.task_progress >= target) {
                    align.task_complete = true; align.task_active = false;
                    log_.add("Church task complete. Return to the church.", {255, 220, 80, 255});
                }
            }
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

    // Reset per-floor capstones (Divine Intervention)
    if (player_ != NULL_ENTITY && world_.has<PassiveTreeState>(player_)) {
        auto& tree = world_.get<PassiveTreeState>(player_);
        int di_idx = static_cast<int>(EffectType::CAP_DIVINE_INTERVENTION) - static_cast<int>(EffectType::CAP_WHIRLWIND);
        if (tree.capstone_cooldowns[di_idx] == 999)
            tree.capstone_cooldowns[di_idx] = 0;
    }

    // Clear old monsters/items but keep player
    // Note: cache_current_floor() is called from stair handlers BEFORE generate_level()
    if (player_ != NULL_ENTITY) {
        clear_entities_except_player();
    }

    // Try to restore cached floor
    if (dungeon_level_ > 0 && restore_floor(dungeon_level_, ascending_)) {
        // Floor restored from cache — skip generation
        // Thessarka auto-map applies on every floor entry (including cache restore)
        if (world_.has<GodAlignment>(player_) && world_.get<GodAlignment>(player_).god == GodId::THESSARKA) {
            for (int my = 0; my < map_.height(); my++)
                for (int mx = 0; mx < map_.width(); mx++)
                    map_.at(mx, my).explored = true;
        }
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
        map_ = mresult.map; // copy from cache (base map without explored state)

        // Restore explored tiles from previous overworld visit
        if (!overworld_explored_.empty()) {
            int sz = map_.width() * map_.height();
            for (int i = 0; i < sz && i < static_cast<int>(overworld_explored_.size()); i++) {
                if (overworld_explored_[i])
                    map_.at(i % map_.width(), i / map_.width()).explored = true;
            }
        }

        // Province-specific building materials: restyle walls near each town
        for (int ti = 0; ti < TOWN_COUNT; ti++) {
            GodId god = get_town_god(ALL_TOWNS[ti].x, ALL_TOWNS[ti].y);
            TileType wall_type = TileType::WALL_STONE_BRICK; // default
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
        // Province-specific farmer lines (indexed by god)
        static const char* FARMER_GREENWOOD[] = {
            "The forest gives us everything. We owe it respect.",
            "Mushrooms growing in patterns. Not natural ones.",
            "My goats won't graze near the old trees anymore.",
        };
        static const char* FARMER_DUST[] = {
            "The ground won't hold seed. Third season running.",
            "We boil the water twice now. It still tastes wrong.",
            "The vultures circle lower each day.",
        };
        static const char* FARMER_FROST[] = {
            "The ice came early. It always comes early now.",
            "We store what we can. The thaw never lasts.",
            "The peaks groan at night. Stone shouldn't sound like that.",
        };
        static const char* FARMER_COAST[] = {
            "The forges buy everything we grow. Not enough hands to harvest.",
            "Soot in the rain. It stains the crops grey.",
            "The mine carts don't stop. Day and night.",
        };
        static const char* FARMER_HEARTLANDS[] = {
            "The garrison takes half. Protection tax, they call it.",
            "Soldiers march through every other day. Something's coming.",
            "Used to be quiet here. I miss quiet.",
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
            "There's a tree in the deep Greenwood that glows at night. Nobody goes near it.",
            "The northern peaks hide an altar of ice. Something rests there. Something old.",
            "Hunters say there's a shrine half-buried in the southern wastes. Sythara's mark.",
            "The deepest dungeons have guardians. Not monsters. Something placed there on purpose.",
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
            "Slow day.",
            "Take a look.",
        };
        static const char* HERBALIST_IDLE[] = {
            "Herbs and poultices.",
            "Antidotes don't grow on trees. Well, some do.",
            "These poultices take days to prepare.", "Mind the red mushrooms. Those aren't for eating.",
        };
        static const char* INNKEEPER_IDLE[] = {
            "10 gold for a room. Best deal you'll find.",
            "The ale's fresh. The beds are... acceptable.",
            "Any news from the road?",
            "Get some sleep.",
        };
        // Province-specific opening lines for merchants/herbalists
        struct ProvinceDialogue {
            const char* shopkeeper;
            const char* herbalist;
            const char* innkeeper;
        };
        auto get_province_dialogue = [](GodId god) -> ProvinceDialogue {
            switch (god) {
                case GodId::KHAEL: return {
                    "Trade goods from the deep wood. Nothing forged, nothing mined.",
                    "Wood goods. No metal.",
                    "Rooms are simple. Walls are living wood. You'll sleep well."};
                case GodId::SYTHARA: return {
                    "What I have is what's left. Take it or leave it.",
                    "Remedies are scarce here. The ground poisons what it grows.",
                    "Water costs extra. Don't ask why."};
                case GodId::GATHRUUN: return {
                    "Furs, tools, and preserved rations. Nothing fancy survives the cold.",
                    "Frostbite remedies and warming draughts. You'll need both.",
                    "Cold out there. Room's warm."};
                case GodId::OSSREN: return {
                    "Forged steel and mining supplies. The coast provides.",
                    "Burns and lung sickness. That's what I treat, mostly.",
                    "Hot food and a cot near the forge wall. Warmest spot in town."};
                case GodId::SOLETH: return {
                    "Blessed goods only. The temple inspects everything I sell.",
                    "The priests handle most healing. I cover what prayer can't.",
                    "Pilgrims and soldiers. That's all we get these days."};
                default: return {
                    "Browse, if you like. I don't haggle.",
                    "Remedies and poultices.",
                    "10 gold for a room. Best deal you'll find."};
            }
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
            npc.idle_lines.push_back(td.rumor4);
            npc.idle_lines.push_back(td.rumor5);
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
        bool mq_assigned[9] = {};
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
                    npc_comp.dialogue = get_province_dialogue(get_town_god(me.x, me.y)).shopkeeper;
                    set_idle(npc_comp, SHOPKEEPER_IDLE, 6);
                    { int sv = (me.x * 11 + me.y * 7) % 2;
                      if (sv == 0) { sx = 2; sy = 6; }      // shopkeep
                      else { sx = 3; sy = 6; } }             // elderly woman (merchant)
                    // Side quest: Thornwall shopkeeper — rats in the cellar
                    if (town_idx == 0 && !sq_ratcellar_assigned) {
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
                    // Ironhearth blacksmith (near Molten Depths)
                    if (town_idx == 7 && !mq_assigned[5]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_06_THIRD_FRAGMENT);
                        npc_comp.name = "Master Smith Brynn";
                        npc_comp.dialogue = "The Molten Depths glow brighter each day. Something burns down there.";
                        mq_assigned[5] = true;
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
                    // MQ_02: Thornwall scholar (brand explanation)
                    if (town_idx == 0 && !mq_assigned[1]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_02_SCHOLAR_CLUE);
                        npc_comp.name = "Scholar Aldric";
                        npc_comp.dialogue = "That brand... I've seen drawings of it. "
                                            "You're not the first. The others didn't survive.";
                        sx = 5; sy = 5; // always scholar sprite
                        mq_assigned[1] = true;
                    }
                    // MQ_03: Greywatch scholar (Stonekeep / first fragment)
                    else if (town_idx == 4 && !mq_assigned[2]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_03_FIRST_FRAGMENT);
                        npc_comp.name = "Captain Voss";
                        npc_comp.dialogue = "Stonekeep is groaning. Whatever you're looking for is down there.";
                        mq_assigned[2] = true;
                    }
                    // MQ_04: Frostmere sage (counsel)
                    else if (town_idx == 3 && !mq_assigned[3]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_04_SAGE_COUNSEL);
                        npc_comp.name = "Sage Yeva";
                        npc_comp.dialogue = "Some names should stay frozen.";
                        mq_assigned[3] = true;
                    }
                    // MQ_05: Millhaven scholar (Catacombs / second fragment)
                    else if (town_idx == 1 && !mq_assigned[4]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_05_SECOND_FRAGMENT);
                        npc_comp.name = "Scholar Maren";
                        npc_comp.dialogue = "The Catacombs have been sealed since before this town existed.";
                        mq_assigned[4] = true;
                    }
                    // MQ_07: Hollowgate scholar (break the seal)
                    else if (town_idx == 6 && !mq_assigned[6]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_07_BREAK_SEAL);
                        npc_comp.name = "Scholar Daven";
                        npc_comp.dialogue = "The seal here is the last one. Beyond it lies the oldest place.";
                        mq_assigned[6] = true;
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
                    if (town_idx == 1 && !sq_amulet_assigned) {
                        npc_comp.quest_id = static_cast<int>(QuestId::SQ_LOST_AMULET);
                        npc_comp.dialogue = "My grandmother's amulet — I lost it in the dungeon nearby. Please, it's all I have of her.";
                        sq_amulet_assigned = true;
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
                    else if (town_idx == 4 && !sq_undead_assigned) {
                        npc_comp.quest_id = static_cast<int>(QuestId::SQ_UNDEAD_PATROL);
                        npc_comp.name = "Sergeant Breck";
                        npc_comp.dialogue = "The dead walk in the tunnels south of here. Thin their numbers.";
                        sx = 2; sy = 1; // female knight
                        sq_undead_assigned = true;
                    }
                    // MQ_04: Greywatch guard (receives tablet)
                    if (town_idx == 2 && !mq_assigned[3]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_04_SAGE_COUNSEL);
                        npc_comp.name = "Captain Voss";
                        npc_comp.dialogue = "I command the largest garrison in the region. Speak plainly.";
                        sx = 0; sy = 1; // knight
                        mq_assigned[3] = true;
                    }
                    // MQ_07: Frostmere guard (frozen key location)
                    else if (town_idx == 3 && !mq_assigned[6]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_05_SECOND_FRAGMENT);
                        npc_comp.name = "Guard Osric";
                        npc_comp.dialogue = "The ice dungeon north of here holds things that stopped being human long ago.";
                        mq_assigned[6] = true;
                    }
                    // MQ_11: Ironhearth guard (Molten Depths)
                    else if (town_idx == 4 && !mq_assigned[10]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_06_THIRD_FRAGMENT);
                        npc_comp.name = "Guard Holt";
                        npc_comp.dialogue = "The volcanic tunnels beneath us run deep. The heat kills anything that isn't already dead.";
                        mq_assigned[10] = true;
                    }
                    // MQ_13: Candlemere guard (Sunken Halls)
                    else if (town_idx == 5 && !mq_assigned[12]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_06_THIRD_FRAGMENT);
                        npc_comp.name = "Guard Thane";
                        npc_comp.dialogue = "The Molten Depths flood more each year. The water there remembers.";
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
                    npc_comp.dialogue = get_province_dialogue(get_town_god(me.x, me.y)).herbalist;
                    set_idle(npc_comp, HERBALIST_IDLE, 4);
                    sx = 3; sy = 6;
                    break;
                case 'M':
                    npc_comp.role = NPCRole::SHOPKEEPER;
                    npc_comp.name = gen_npc_name("Merchant", me.x, me.y);
                    npc_comp.dialogue = get_province_dialogue(get_town_god(me.x, me.y)).shopkeeper;
                    set_idle(npc_comp, SHOPKEEPER_IDLE, 6);
                    sx = 2; sy = 6;
                    break;
                case 'N':
                    npc_comp.role = NPCRole::INNKEEPER;
                    npc_comp.name = gen_npc_name("Innkeeper", me.x, me.y);
                    npc_comp.dialogue = get_province_dialogue(get_town_god(me.x, me.y)).innkeeper;
                    set_idle(npc_comp, INNKEEPER_IDLE, 4);
                    sx = 1; sy = 6;
                    break;
                case 'C': {
                    // Church priest (has Church component, opens church screen)
                    GodId church_god = get_town_god(me.x, me.y);
                    auto& cginfo = get_god_info(church_god);
                    npc_comp.role = NPCRole::PRIEST;
                    char cname[64]; snprintf(cname, sizeof(cname), "High Priest of %s", cginfo.name);
                    npc_comp.name = cname;
                    char cdial[128]; snprintf(cdial, sizeof(cdial),
                        "Welcome to the Church of %s. %s", cginfo.name, cginfo.description);
                    npc_comp.dialogue = cdial;
                    sx = 1; sy = 4; // priest sprite
                    // Church component added after the NPC is created (below)
                    break;
                }
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
            // Province-specific idle lines for farmers/villagers
            if (npc_comp.role == NPCRole::FARMER) {
                GodId pg = get_town_god(me.x, me.y);
                const char** plines = nullptr; int pcount = 0;
                switch (pg) {
                    case GodId::KHAEL:    plines = FARMER_GREENWOOD; pcount = 3; break;
                    case GodId::SYTHARA:  plines = FARMER_DUST;      pcount = 3; break;
                    case GodId::GATHRUUN: plines = FARMER_FROST;     pcount = 3; break;
                    case GodId::OSSREN:   plines = FARMER_COAST;     pcount = 3; break;
                    case GodId::MORRETH:  plines = FARMER_HEARTLANDS;pcount = 3; break;
                    default: break;
                }
                if (plines) for (int pi = 0; pi < pcount; pi++) npc_comp.idle_lines.push_back(plines[pi]);
            }
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

            // Church component for 'C' glyph NPCs
            if (me.glyph == 'C') {
                GodId cg = get_town_god(me.x, me.y);
                world_.add<Church>(npc, {cg, false});
            }

            // Building sign: placed 2 tiles outside the door (not blocking entry)
            const char* sign_label = nullptr;
            switch (me.glyph) {
                case 'S': sign_label = "General Store"; break;
                case 'B': sign_label = "Smithy"; break;
                case 'P': sign_label = "Scholar"; break;
                case 'H': sign_label = "Herbalist"; break;
                case 'M': sign_label = "Trading Post"; break;
                case 'N': sign_label = "Inn"; break;
                case 'C': sign_label = "Church"; break;
                default: break;
            }
            if (sign_label) {
                for (int dy = -3; dy <= 3 && sign_label; dy++) {
                    for (int dx = -3; dx <= 3; dx++) {
                        int sx2 = me.x + dx, sy2 = me.y + dy;
                        if (!map_.in_bounds(sx2, sy2)) continue;
                        if (map_.at(sx2, sy2).type != TileType::DOOR_CLOSED &&
                            map_.at(sx2, sy2).type != TileType::DOOR_OPEN) continue;
                        // Door direction: which side faces away from NPC
                        int out_dx = (sx2 > me.x) ? 1 : (sx2 < me.x) ? -1 : 0;
                        int out_dy = (sy2 > me.y) ? 1 : (sy2 < me.y) ? -1 : 0;
                        // Place sign 2 tiles out (skip the tile right in front of door)
                        int sign_x = sx2 + out_dx * 2;
                        int sign_y = sy2 + out_dy * 2;
                        // Try adjacent if 2-out fails
                        if (!map_.in_bounds(sign_x, sign_y) || !map_.is_walkable(sign_x, sign_y)) {
                            sign_x = sx2 + out_dx * 2 + 1;
                            sign_y = sy2 + out_dy * 2;
                        }
                        if (!map_.in_bounds(sign_x, sign_y) || !map_.is_walkable(sign_x, sign_y)) break;
                        if (combat::entity_at(world_, sign_x, sign_y, player_) != NULL_ENTITY) break;
                        Entity se = world_.create();
                        world_.add<Position>(se, {sign_x, sign_y});
                        world_.add<Renderable>(se, {SHEET_TILES, 7, 17, {255,255,255,255}, 4});
                        world_.add<Sign>(se, {sign_label});
                        sign_label = nullptr; break;
                    }
                }
            }
        }

        // Spawn Herbalist and Merchant NPCs at each town (not in map file)
        auto spawn_extra_npc = [&](int cx, int cy, const char* name, NPCRole role,
                                    const char* dialogue, int spr_x, int spr_y) {
            // Find a tile near town center. Shopkeepers/innkeepers go inside
            // buildings (stone floor); herbalists can be outdoor.
            bool needs_indoor = (role == NPCRole::SHOPKEEPER || role == NPCRole::INNKEEPER);
            for (int attempt = 0; attempt < 80; attempt++) {
                int tx = cx + rng_.range(-15, 15);
                int ty = cy + rng_.range(-15, 15);
                if (!map_.in_bounds(tx, ty) || !map_.is_walkable(tx, ty)) continue;
                if (needs_indoor) {
                    auto tt = map_.at(tx, ty).type;
                    if (tt != TileType::FLOOR_STONE && tt != TileType::FLOOR_COBBLE) continue;
                    // Must be inside a building (2+ adjacent walls)
                    int adj_walls = 0;
                    for (int dy2 = -1; dy2 <= 1; dy2++)
                        for (int dx2 = -1; dx2 <= 1; dx2++) {
                            if (dx2 == 0 && dy2 == 0) continue;
                            if (map_.in_bounds(tx+dx2, ty+dy2) && !map_.is_walkable(tx+dx2, ty+dy2))
                                adj_walls++;
                        }
                    if (adj_walls < 2) continue;
                }
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

        // (Herbalist and merchant idle lines handled by NPC::idle_lines)
        for (int i = 0; i < TOWN_COUNT; i++) {
            auto herb_name = gen_npc_name("Herbalist", ALL_TOWNS[i].x + 1, ALL_TOWNS[i].y);
            auto merch_name = gen_npc_name("Merchant", ALL_TOWNS[i].x, ALL_TOWNS[i].y + 1);
            auto inn_name = gen_npc_name("Innkeeper", ALL_TOWNS[i].x + 2, ALL_TOWNS[i].y + 2);
            auto pd = get_province_dialogue(get_town_god(ALL_TOWNS[i].x, ALL_TOWNS[i].y));
            spawn_extra_npc(ALL_TOWNS[i].x, ALL_TOWNS[i].y, herb_name.c_str(), NPCRole::PRIEST,
                            pd.herbalist, 3, 6);
            spawn_extra_npc(ALL_TOWNS[i].x, ALL_TOWNS[i].y, merch_name.c_str(), NPCRole::SHOPKEEPER,
                            pd.shopkeeper, 2, 6);
            spawn_extra_npc(ALL_TOWNS[i].x, ALL_TOWNS[i].y, inn_name.c_str(), NPCRole::INNKEEPER,
                            pd.innkeeper, 1, 6);
        }

        // Named NPCs for towns that lack unique characters
        // Candlemere (idx 2) - Temple city, Soleth
        spawn_extra_npc(ALL_TOWNS[2].x, ALL_TOWNS[2].y,
            "High Priestess Valara", NPCRole::PRIEST,
            "The eternal flame dims. Something in the Sepulchre draws its light.",
            5, 5);
        spawn_extra_npc(ALL_TOWNS[2].x, ALL_TOWNS[2].y,
            "Acolyte Theren", NPCRole::PRIEST,
            "We pray louder each day. The silence between prayers grows longer.",
            3, 4);

        // Whitepeak (idx 5) - Frozen peaks, Gathruun
        spawn_extra_npc(ALL_TOWNS[5].x, ALL_TOWNS[5].y,
            "Stonemason Greth", NPCRole::BLACKSMITH,
            "I carve names into the mountain. Most are for the dead.",
            4, 5);
        spawn_extra_npc(ALL_TOWNS[5].x, ALL_TOWNS[5].y,
            "Old Halvard", NPCRole::ELDER,
            "I've watched the glacier for sixty years. It moved last week.",
            4, 6);

        // Bramblewood (idx 6) - Greenwood, Khael
        spawn_extra_npc(ALL_TOWNS[6].x, ALL_TOWNS[6].y,
            "Ranger Fael", NPCRole::GUARD,
            "The deep wood is off limits. Not by our law. By theirs.",
            2, 1);
        spawn_extra_npc(ALL_TOWNS[6].x, ALL_TOWNS[6].y,
            "Hedge Witch Nessa", NPCRole::PRIEST,
            "The trees are talking again. Not to us. To each other.",
            3, 6);

        // Dustfall (idx 9) - Dust Provinces, Sythara
        spawn_extra_npc(ALL_TOWNS[9].x, ALL_TOWNS[9].y,
            "Plague Warden Kess", NPCRole::GUARD,
            "We burn the dead now. No exceptions. I don't care whose god disapproves.",
            0, 1);
        spawn_extra_npc(ALL_TOWNS[9].x, ALL_TOWNS[9].y,
            "Apothecary Mave", NPCRole::PRIEST,
            "Every cure I make slows it down. Nothing stops it. Not yet.",
            3, 6);

        // Populate overworld with wilderness content
        populate_overworld();

        // Generate dynamic side quests for each town's NPCs
        for (int i = 0; i < TOWN_COUNT; i++) {
            quest_gen::generate_town_quests(world_, map_, rng_,
                ALL_TOWNS[i].x, ALL_TOWNS[i].y, ALL_TOWNS[i].name);
        }

        // Church priests are now spawned from 'C' glyph in the map (placed by generate_overworld.py)
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
            {TileType::WALL_LARGE_STONE, TileType::FLOOR_STONE,     "The Molten Depths", 18},
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
        params.width = 60;
        params.height = 38;
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
            // Cramped, many small rooms
            params.room_min_w = 4; params.room_max_w = 7;
            params.room_min_h = 4; params.room_max_h = 7;
            params.max_rooms = 7 + dungeon_level_;
            params.corridor_width = 1;
        } else if (zone_key == "stonekeep") {
            // Medium rooms, fortress-like
            params.room_min_w = 5; params.room_max_w = 9;
            params.room_min_h = 5; params.room_max_h = 9;
            params.max_rooms = 6 + dungeon_level_;
            params.corridor_width = rng_.range(1, 2);
        } else if (zone_key == "deep_halls") {
            // Large cavernous rooms, fewer of them
            params.room_min_w = 7; params.room_max_w = 12;
            params.room_min_h = 7; params.room_max_h = 12;
            params.max_rooms = 4 + dungeon_level_;
            params.corridor_width = rng_.range(2, 3);
        } else if (zone_key == "catacombs") {
            // Tight narrow corridors, many small crypts
            params.room_min_w = 4; params.room_max_w = 6;
            params.room_min_h = 4; params.room_max_h = 6;
            params.max_rooms = 7 + dungeon_level_;
            params.corridor_width = 1;
        } else if (zone_key == "molten") {
            // Open volcanic chambers
            params.room_min_w = 6; params.room_max_w = 10;
            params.room_min_h = 6; params.room_max_h = 10;
            params.max_rooms = 5 + dungeon_level_;
            params.corridor_width = 2;
        } else if (zone_key == "sunken") {
            // Flooded halls, medium-large
            params.room_min_w = 6; params.room_max_w = 11;
            params.room_min_h = 6; params.room_max_h = 11;
            params.max_rooms = 5 + dungeon_level_;
            params.corridor_width = rng_.range(2, 3);
        } else if (zone_key == "sepulchre") {
            // The Sepulchre: 9 floors, each themed differently
            // Floors grow larger and more oppressive as you descend
            params.room_min_w = 6 + dungeon_level_ / 3;
            params.room_max_w = 10 + dungeon_level_ / 2;
            params.room_min_h = 6 + dungeon_level_ / 3;
            params.room_max_h = 10 + dungeon_level_ / 2;
            params.max_rooms = 5 + dungeon_level_;
            params.corridor_width = 2 + dungeon_level_ / 5;
            // Per-floor wall/floor themes (the deeper, the stranger)
            if (dungeon_level_ <= 2) {
                // Floors 1-2: Ancient stonework (familiar)
                params.wall_type = TileType::WALL_STONE_BRICK;
                params.floor_type = TileType::FLOOR_STONE;
            } else if (dungeon_level_ <= 4) {
                // Floors 3-4: Bone-lined catacombs
                params.wall_type = TileType::WALL_CATACOMB;
                params.floor_type = TileType::FLOOR_BONE;
            } else if (dungeon_level_ <= 6) {
                // Floors 5-6: Igneous/volcanic (heat from below)
                params.wall_type = TileType::WALL_IGNEOUS;
                params.floor_type = TileType::FLOOR_RED_STONE;
            } else {
                // Floors 7-9: Something alien (ice/void)
                params.wall_type = TileType::WALL_ICE;
                params.floor_type = TileType::FLOOR_ICE;
            }
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

        // Boss room: enlarge and decorate last room on bottom floor of quest dungeons
        bool is_quest_bottom = at_zone_bottom && current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()) &&
            !dungeon_registry_[current_dungeon_idx_].quest.empty();
        if (is_quest_bottom && rooms_.size() >= 2) {
            auto& boss_rm = rooms_.back();
            // Carve arena: expand the room by 2 tiles in each direction
            for (int dy = -2; dy < boss_rm.h + 2; dy++) {
                for (int dx = -2; dx < boss_rm.w + 2; dx++) {
                    int tx = boss_rm.x + dx, ty = boss_rm.y + dy;
                    if (map_.in_bounds(tx, ty) && !map_.is_walkable(tx, ty)) {
                        // Only expand into walls, not void
                        if (map_.at(tx, ty).type != TileType::VOID)
                            map_.at(tx, ty).type = params.floor_type;
                    }
                }
            }
            // Pillar ring around the arena (4 pillars for cover)
            int cx = boss_rm.x + boss_rm.w / 2;
            int cy = boss_rm.y + boss_rm.h / 2;
            int pr = std::min(boss_rm.w, boss_rm.h) / 2 - 1;
            for (int pi = 0; pi < 4; pi++) {
                float angle = pi * 1.5708f; // 90 degrees
                int px = cx + static_cast<int>(pr * cosf(angle));
                int py = cy + static_cast<int>(pr * sinf(angle));
                if (map_.in_bounds(px, py))
                    map_.at(px, py).type = params.wall_type;
            }
            // Zone-themed floor decoration in arena center
            if (zone_key == "molten") {
                // Lava ring
                for (int a = 0; a < 8; a++) {
                    float ang = a * 0.7854f;
                    int lx = cx + static_cast<int>((pr - 2) * cosf(ang));
                    int ly = cy + static_cast<int>((pr - 2) * sinf(ang));
                    if (map_.in_bounds(lx, ly)) map_.at(lx, ly).type = TileType::LAVA;
                }
            } else if (zone_key == "catacombs") {
                // Bone floor center
                for (int dy2 = -1; dy2 <= 1; dy2++)
                    for (int dx2 = -1; dx2 <= 1; dx2++)
                        if (map_.in_bounds(cx+dx2, cy+dy2))
                            map_.at(cx+dx2, cy+dy2).type = TileType::FLOOR_BONE;
            } else if (zone_key == "sepulchre") {
                // Red stone altar center
                for (int dy2 = -1; dy2 <= 1; dy2++)
                    for (int dx2 = -1; dx2 <= 1; dx2++)
                        if (map_.in_bounds(cx+dx2, cy+dy2))
                            map_.at(cx+dx2, cy+dy2).type = TileType::FLOOR_RED_STONE;
            }
        }

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
            // Skip last room (stairs_down) to prevent blocking stair access
            for (size_t ri = 1; ri + 1 < rooms_.size(); ri++) {
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
            // Skip last room (stairs_down) to prevent blocking stair access
            for (size_t ri = 0; ri + 1 < rooms_.size(); ri++) {
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
        shop_screen_.clear_cache();
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

    // Reachability check: flood fill from player start, verify all stairs
    // and doors are reachable. If stairs_down is unreachable, carve a path.
    {
        int mw = map_.width(), mh = map_.height();
        std::vector<bool> visited(mw * mh, false);
        std::vector<std::pair<int,int>> queue;

        // Find stairs_up position (player start)
        int sup_x = start_x, sup_y = start_y;

        // Also find stairs_down position
        int sdn_x = -1, sdn_y = -1;
        for (int fy = 0; fy < mh; fy++)
            for (int fx = 0; fx < mw; fx++) {
                auto tt = map_.at(fx, fy).type;
                if (tt == TileType::STAIRS_DOWN) { sdn_x = fx; sdn_y = fy; }
            }

        // Flood fill from start (walkable tiles + doors)
        auto passable = [&](int x, int y) -> bool {
            if (!map_.in_bounds(x, y)) return false;
            auto t = map_.at(x, y).type;
            return map_.is_walkable(x, y) || t == TileType::DOOR_CLOSED ||
                   t == TileType::STAIRS_UP || t == TileType::STAIRS_DOWN;
        };

        queue.push_back({sup_x, sup_y});
        visited[sup_y * mw + sup_x] = true;
        size_t head = 0;
        while (head < queue.size()) {
            auto [cx, cy] = queue[head++];
            const int dirs[][2] = {{0,-1},{0,1},{-1,0},{1,0}};
            for (auto& [ddx, ddy] : dirs) {
                int nx = cx + ddx, ny = cy + ddy;
                if (nx < 0 || nx >= mw || ny < 0 || ny >= mh) continue;
                if (visited[ny * mw + nx]) continue;
                if (!passable(nx, ny)) continue;
                visited[ny * mw + nx] = true;
                queue.push_back({nx, ny});
            }
        }

        // If stairs_down exists and is unreachable, carve a cardinal path to it
        if (sdn_x >= 0 && !visited[sdn_y * mw + sdn_x]) {
            // Find the nearest visited tile to stairs_down
            int best_x = sup_x, best_y = sup_y;
            int best_dist = std::abs(sup_x - sdn_x) + std::abs(sup_y - sdn_y);
            for (auto& [vx, vy] : queue) {
                int d = std::abs(vx - sdn_x) + std::abs(vy - sdn_y);
                if (d < best_dist) { best_dist = d; best_x = vx; best_y = vy; }
            }

            // Determine zone floor type
            TileType ft = TileType::FLOOR_STONE;
            for (auto& [vx, vy] : queue) {
                auto t = map_.at(vx, vy).type;
                if (t != TileType::STAIRS_UP && t != TileType::DOOR_CLOSED && map_.is_walkable(vx, vy)) {
                    ft = t; break;
                }
            }

            // Carve L-shaped cardinal corridor from nearest reachable tile to stairs
            int cx = best_x, cy = best_y;
            while (cx != sdn_x) {
                cx += (sdn_x > cx) ? 1 : -1;
                if (map_.in_bounds(cx, cy) && !passable(cx, cy))
                    map_.at(cx, cy).type = ft;
            }
            while (cy != sdn_y) {
                cy += (sdn_y > cy) ? 1 : -1;
                if (map_.in_bounds(cx, cy) && !passable(cx, cy))
                    map_.at(cx, cy).type = ft;
            }
        }
    }

    // Ensure doors are accessible (room shape mods can wall off entry points)
    for (int sy = 0; sy < map_.height(); sy++) {
        for (int sx = 0; sx < map_.width(); sx++) {
            if (map_.at(sx, sy).type != TileType::DOOR_CLOSED) continue;
            // A door is a chokepoint: walls on opposite sides, open on the other two.
            // Check which axis the door opens on and ensure both sides are walkable.
            bool wall_n = map_.in_bounds(sx, sy-1) && !map_.is_walkable(sx, sy-1);
            bool wall_s = map_.in_bounds(sx, sy+1) && !map_.is_walkable(sx, sy+1);
            bool wall_e = map_.in_bounds(sx+1, sy) && !map_.is_walkable(sx+1, sy);
            bool wall_w = map_.in_bounds(sx-1, sy) && !map_.is_walkable(sx-1, sy);

            // Find nearby floor type for patching
            TileType ft = TileType::FLOOR_STONE;
            for (int fy = std::max(0, sy-3); fy <= std::min(map_.height()-1, sy+3) && ft == TileType::FLOOR_STONE; fy++)
                for (int fx = std::max(0, sx-3); fx <= std::min(map_.width()-1, sx+3); fx++) {
                    auto nt = map_.at(fx, fy).type;
                    if (nt != TileType::DOOR_CLOSED && map_.is_walkable(fx, fy)) { ft = nt; break; }
                }

            if (wall_n && wall_s) {
                // East-west door: ensure east and west are walkable
                if (wall_e) map_.at(sx+1, sy).type = ft;
                if (wall_w) map_.at(sx-1, sy).type = ft;
            } else if (wall_e && wall_w) {
                // North-south door: ensure north and south are walkable
                if (wall_n) map_.at(sx, sy-1).type = ft;
                if (wall_s) map_.at(sx, sy+1).type = ft;
            } else {
                // Door lost its chokepoint structure (both axes blocked)
                // Remove it, it's no longer a valid door
                map_.at(sx, sy).type = ft;
            }
        }
    }

    // Spawn monsters and items (not in village)
    if (dungeon_level_ > 0) {
        // Effective level = dungeon depth + zone difficulty (distance from start)
        int zone_diff = 0;
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()))
            zone_diff = dungeon_registry_[current_dungeon_idx_].zone_difficulty;
        int effective_level = dungeon_level_ + zone_diff;

        std::string spawn_zone;
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()))
            spawn_zone = dungeon_registry_[current_dungeon_idx_].zone;
        populate::spawn_monsters(world_, map_, rooms_, rng_, effective_level,
                                  spawn_zone, dungeon_level_);
        populate::spawn_items(world_, map_, rooms_, rng_, effective_level);
        populate::spawn_traps(world_, map_, rooms_, rng_, effective_level);

        // Special rooms (20% chance per eligible room, skip first and last)
        for (size_t sri = 2; sri + 1 < rooms_.size(); sri++) {
            if (!rng_.chance(20)) continue;
            auto& sr = rooms_[sri];
            int room_type = rng_.range(0, 2);

            if (room_type == 0) {
                // Treasure Vault: extra gold pile + rare item
                int gx = rng_.range(sr.x + 1, sr.x + sr.w - 2);
                int gy = rng_.range(sr.y + 1, sr.y + sr.h - 2);
                if (map_.is_walkable(gx, gy)) {
                    Entity ge = world_.create();
                    world_.add<Position>(ge, {gx, gy});
                    Item gi; gi.name = "gold cache"; gi.type = ItemType::GOLD;
                    gi.gold_value = 30 + effective_level * 15; gi.stack = gi.gold_value;
                    gi.stackable = true; gi.identified = true;
                    world_.add<Item>(ge, std::move(gi));
                    world_.add<Renderable>(ge, {SHEET_ITEMS, 0, 24, {255,220,80,255}, 1});
                }
                // Decorative: extra barrels
                for (int d = 0; d < 3; d++) {
                    int dx = rng_.range(sr.x + 1, sr.x + sr.w - 2);
                    int dy = rng_.range(sr.y + 1, sr.y + sr.h - 2);
                    if (map_.is_walkable(dx, dy)) {
                        Entity de = world_.create();
                        world_.add<Position>(de, {dx, dy});
                        world_.add<Renderable>(de, {SHEET_TILES, 4, 17, {255,255,255,255}, 0});
                    }
                }
            } else if (room_type == 1 && sr.w >= 6 && sr.h >= 6) {
                // Monster Closet: pack of enemies + guaranteed loot
                int pack_size = 3 + effective_level / 2;
                for (int pi = 0; pi < pack_size; pi++) {
                    int mx = rng_.range(sr.x + 1, sr.x + sr.w - 2);
                    int my = rng_.range(sr.y + 1, sr.y + sr.h - 2);
                    if (!map_.is_walkable(mx, my)) continue;
                    if (combat::entity_at(world_, mx, my, player_) != NULL_ENTITY) continue;
                    // Spawn a tough enemy (use higher index from monster table)
                    int idx = rng_.range(std::min(8, effective_level), std::min(29, 8 + effective_level * 3));
                    auto& def = populate::get_monster_table()[idx];
                    Entity me = world_.create();
                    world_.add<Position>(me, {mx, my});
                    world_.add<Renderable>(me, {def.sheet, def.sprite_x, def.sprite_y, {255,255,255,255}, 5});
                    float hps = 1.0f + effective_level * 0.5f;
                    Stats ms; ms.name = def.name;
                    ms.hp = static_cast<int>(def.hp * hps); ms.hp_max = ms.hp;
                    ms.base_damage = static_cast<int>(def.base_damage * (1.0f + effective_level * 0.35f));
                    ms.natural_armor = def.natural_armor; ms.base_speed = def.speed;
                    ms.xp_value = def.xp_value;
                    world_.add<Stats>(me, std::move(ms));
                    AI mai; mai.state = AIState::HUNTING; mai.flee_threshold = def.flee_threshold;
                    world_.add<AI>(me, mai);
                    world_.add<Energy>(me, {0, def.speed});
                }
                // Guaranteed item in center
                int cx2 = sr.x + sr.w / 2, cy2 = sr.y + sr.h / 2;
                if (map_.is_walkable(cx2, cy2))
                    populate::spawn_items(world_, map_, {sr}, rng_, effective_level + 2);
            } else if (room_type == 2) {
                // Trap Gauntlet: dense traps + reward
                for (int ti = 0; ti < 4 + effective_level; ti++) {
                    int tx2 = rng_.range(sr.x + 1, sr.x + sr.w - 2);
                    int ty2 = rng_.range(sr.y + 1, sr.y + sr.h - 2);
                    if (map_.is_walkable(tx2, ty2) && combat::entity_at(world_, tx2, ty2, player_) == NULL_ENTITY) {
                        Entity te = world_.create();
                        world_.add<Position>(te, {tx2, ty2});
                        int trap_dmg = 3 + effective_level;
                        Trap trap; trap.damage = trap_dmg;
                        trap.revealed = false;
                        world_.add<Trap>(te, trap);
                    }
                }
                // Reward chest at far end
                int rx = sr.x + sr.w - 2, ry = sr.y + sr.h / 2;
                if (map_.is_walkable(rx, ry)) {
                    Entity ce = world_.create();
                    world_.add<Position>(ce, {rx, ry});
                    world_.add<Renderable>(ce, {SHEET_TILES, 2, 17, {255,255,255,255}, 1});
                    Container cont;
                    cont.open_sprite_x = 3; cont.open_sprite_y = 17;
                    Item ci; ci.name = "gold coins"; ci.type = ItemType::GOLD;
                    ci.gold_value = 40 + effective_level * 20; ci.stack = ci.gold_value;
                    ci.stackable = true; ci.identified = true;
                    cont.contents = std::move(ci);
                    world_.add<Container>(ce, std::move(cont));
                }
            }
            break; // only one special room per floor
        }

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
            // Legendary Guardian — unique mini-boss in highest-difficulty generic dungeons
            if (dungeon_level_ >= dentry.max_depth && dentry.zone_difficulty >= 7
                && dentry.quest.empty() && !rooms_.empty()) {
                // Spawn a guardian boss with a guaranteed legendary drop
                auto& last_room = rooms_.back();
                int gx = last_room.x + last_room.w / 2;
                int gy = last_room.y + last_room.h / 2;
                // Find walkable spot if center is blocked
                if (!map_.in_bounds(gx, gy) || !map_.is_walkable(gx, gy)) {
                    for (int r = 1; r <= std::max(last_room.w, last_room.h) / 2; r++) {
                        bool found = false;
                        for (int dy = -r; dy <= r && !found; dy++) {
                            for (int dx = -r; dx <= r && !found; dx++) {
                                int nx = last_room.cx() + dx;
                                int ny = last_room.cy() + dy;
                                if (map_.in_bounds(nx, ny) && map_.is_walkable(nx, ny)) {
                                    gx = nx; gy = ny; found = true;
                                }
                            }
                        }
                        if (found) break;
                    }
                }
                Entity guardian = world_.create();
                world_.add<Position>(guardian, {gx, gy});

                // Guardian type based on zone
                const char* gname = "Ancient Guardian";
                int gsheet = SHEET_MONSTERS, gsx = 0, gsy = 11;
                if (dentry.zone == "deep_halls") { gname = "The Stone King"; gsx = 7; gsy = 7; }
                else if (dentry.zone == "catacombs") { gname = "The Undying"; gsx = 3; gsy = 4; }
                else if (dentry.zone == "molten") { gname = "Ashborn"; gsx = 2; gsy = 8; }
                else { gname = "The Watcher"; gsx = 2; gsy = 5; }

                world_.add<Renderable>(guardian, {gsheet, gsx, gsy, {255, 200, 100, 255}, 5});
                Stats gs; gs.name = gname; gs.hp = 200; gs.hp_max = 200;
                gs.base_damage = 20; gs.natural_armor = 8; gs.base_speed = 90;
                gs.set_attr(Attr::STR, 22); gs.set_attr(Attr::DEX, 14); gs.set_attr(Attr::CON, 22);
                gs.xp_value = 500;
                world_.add<Stats>(guardian, std::move(gs));
                AI gai; gai.state = AIState::HUNTING; gai.flee_threshold = 0;
                world_.add<AI>(guardian, gai);
                world_.add<Energy>(guardian, {0, 90});
                world_.add<StatusEffects>(guardian);
                log_.add("A powerful guardian blocks the way.", {255, 200, 100, 255});
                audio_.play(SfxId::SPELL_IMPACT);
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

        // Unique items — placed on bottom floor of optional dungeons, guarded
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            auto& dentry = dungeon_registry_[current_dungeon_idx_];

            // Check if we're on the bottom floor of this dungeon
            struct ZoneMax { const char* key; int max_depth; };
            static const ZoneMax ZONE_DEPTHS[] = {
                {"warrens", 3}, {"stonekeep", 4}, {"deep_halls", 4},
                {"catacombs", 4}, {"molten", 4}, {"sunken", 4},
                {"sepulchre", 9},
            };
            int zone_max = 3;
            for (auto& zd : ZONE_DEPTHS)
                if (dentry.zone == zd.key) { zone_max = zd.max_depth; break; }

            bool is_bottom = (dungeon_level_ >= zone_max);
            bool is_quest_dungeon = !dentry.quest.empty();

            // Optional dungeons: guaranteed unique + named guardian on bottom floor
            if (is_bottom && !is_quest_dungeon) {
                int effective_depth = dungeon_level_ + dentry.zone_difficulty;
                Entity ue = populate::spawn_unique(world_, rooms_, rng_, effective_depth, dentry.zone);
                if (ue != NULL_ENTITY) {
                    // Named guardian per dungeon (thematic to location)
                    struct DungeonGuardian {
                        const char* dungeon;
                        const char* name;
                        const char* announce;
                        int sx, sy;         // sprite coords
                        int hp, str, dmg, armor, speed;
                        BehaviorType ai;
                        int regen;          // 0 = none
                    };
                    static const DungeonGuardian GUARDIANS[] = {
                        // Warrens
                        {"The Crawl Warren",    "Brood Queen",           "The Brood Queen is here.",
                         8, 6,   70, 14,  8, 2, 110, BehaviorType::BASIC, 0},
                        {"The Worm Warren",     "Tunneler",              "The Tunneler blocks the passage.",
                         9, 6,   90, 18, 10, 3,  70, BehaviorType::CHARGER, 0},
                        {"The Worm Tunnels",    "Rat Patriarch",         "The Rat Patriarch.",
                         11, 6,  65, 14,  9, 1, 130, BehaviorType::PACK, 0},
                        // Stonekeep
                        {"The Grey Citadel",    "Grey Warden",           "The Grey Warden still stands guard.",
                         3, 4,   85, 18, 10, 5,  85, BehaviorType::BASIC, 0},
                        {"The Broken Vault",    "Vault Keeper",          "The Vault Keeper.",
                         2, 7,  100, 16, 12, 6,  60, BehaviorType::BASIC, 0},
                        {"The Silent Citadel",  "Hollow Knight",         "The Hollow Knight.",
                         3, 4,   80, 16,  9, 4, 100, BehaviorType::WRAITH, 0},
                        // Deep Halls
                        {"The Sunless Galleries","Stone Watcher",        "The Stone Watcher.",
                         2, 7,  110, 20, 12, 5,  60, BehaviorType::BASIC, 0},
                        {"The Deep Halls",      "Chained Minotaur",      "The Chained Minotaur.",
                         7, 7,  100, 22, 14, 3,  85, BehaviorType::CHARGER, 0},
                        {"The Hollow Underhall", "Deeproot",              "The Deeproot.",
                         2, 1,   95, 18, 10, 4,  70, BehaviorType::TROLL, 3},
                        // Catacombs
                        {"The Dead Ossuary",    "Bone Sovereign",        "The Bone Sovereign.",
                         2, 4,   85, 16, 11, 3,  90, BehaviorType::NECROMANCER, 0},
                        {"The Ossuary",  "Marrow Eater",          "The Marrow Eater.",
                         5, 4,   75, 18, 12, 2, 110, BehaviorType::THIEF, 0},
                        {"The Grave Ossuary",   "Entombed Priest",       "The Entombed Priest.",
                         2, 4,   90, 14,  9, 3,  85, BehaviorType::LICH, 0},
                        // Molten
                        {"The Ash Forge",       "Forgespawn",            "The Forgespawn.",
                         2, 7,  100, 20, 13, 6,  60, BehaviorType::BASIC, 0},
                        {"The Slag Core",       "Cinder Drake",          "The Cinder Drake.",
                         2, 8,  110, 20, 14, 4,  80, BehaviorType::DRAGON, 0},
                        {"The Cinder Core",     "Living Furnace",        "The Living Furnace.",
                         2, 7,  120, 18, 11, 7,  50, BehaviorType::TROLL, 2},
                        // Sunken
                        {"The Damp Basin",      "Drowned Priestess",     "The Drowned Priestess.",
                         4, 7,   80, 14, 10, 2, 100, BehaviorType::LICH, 0},
                        {"The Salt Grotto",     "Brine Worm",            "The Brine Worm.",
                         9, 6,  100, 16, 11, 3,  70, BehaviorType::CHARGER, 0},
                        {"The Murk Grotto",     "Deep Naga",             "The Deep Naga.",
                         4, 7,   90, 18, 12, 2, 120, BehaviorType::BASIC, 0},
                    };

                    // Find matching guardian for this dungeon
                    const DungeonGuardian* dg = nullptr;
                    for (auto& g : GUARDIANS) {
                        if (dentry.name == g.dungeon) { dg = &g; break; }
                    }

                    if (dg) {
                        Entity guard = populate::spawn_boss(world_, map_, rooms_,
                            dg->name, SHEET_MONSTERS, dg->sx, dg->sy,
                            dg->hp, dg->str, 10, dg->str,
                            dg->dmg, dg->armor, dg->speed, dg->hp);
                        if (guard != NULL_ENTITY) {
                            world_.get<AI>(guard).behavior = dg->ai;
                            if (dg->regen > 0)
                                world_.get<AI>(guard).regen_per_turn = dg->regen;
                            if (dg->ai == BehaviorType::DRAGON)
                                world_.get<AI>(guard).ranged_damage = dg->dmg * 2 / 3;
                            if (dg->ai == BehaviorType::LICH) {
                                world_.get<AI>(guard).ranged_range = 6;
                                world_.get<AI>(guard).ranged_damage = dg->dmg;
                            }
                            world_.add<StatusEffects>(guard);
                            log_.add(dg->announce, {220, 180, 100, 255});
                        }
                    }
                }
            }
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

    // Track The Keeper entity for phase transitions
    keeper_entity_ = 0;
    keeper_phase_ = 1;
    auto& ai_pool_k = world_.pool<AI>();
    for (size_t i = 0; i < ai_pool_k.size(); i++) {
        if (ai_pool_k.at_index(i).behavior == BehaviorType::KEEPER) {
            keeper_entity_ = ai_pool_k.entity_at(i);
            break;
        }
    }

    // Compute initial FOV
    auto& pos = world_.get<Position>(player_);
    auto& stats = world_.get<Stats>(player_);
    fov::compute(map_, pos.x, pos.y, stats.fov_radius());
    camera_.center_on(pos.x, pos.y);

    // Thessarka: auto-map entire floor on entry
    if (dungeon_level_ > 0 && world_.has<GodAlignment>(player_) &&
        world_.get<GodAlignment>(player_).god == GodId::THESSARKA) {
        for (int my = 0; my < map_.height(); my++)
            for (int mx = 0; mx < map_.width(); mx++)
                map_.at(mx, my).explored = true;
        log_.add("The Eyeless reveals the floor's layout.", {140, 140, 220, 255});
    }

    if (dungeon_level_ == 0) {
        log_.add("Thornwall.", {180, 170, 160, 255});
        log_.add("A trading post at the edge of the world. Everyone is watching everyone.",
                 {120, 110, 100, 255});
        log_.add("The dungeon entrance lies to the east.", {100, 100, 90, 255});
    } else {
        // Dungeon zone messages
        static const char* ZONE_NAMES[] = {
            "The Warrens", "Stonekeep", "The Deep Halls",
            "The Catacombs", "The Molten Depths", "The Molten Depths",
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
    bool leveled = skills.grant_xp(skill, amount);
    if (leveled) {
        int new_lv = skills.get_level(skill);
        char buf[96];
        snprintf(buf, sizeof(buf), "%s increased to %d.", skill_name(skill), new_lv);
        log_.add(buf, {140, 200, 160, 255});
        if (!tips_shown_.first_skill_levelup) {
            tips_shown_.first_skill_levelup = true;
            { char tb[256];
              snprintf(tb, sizeof(tb),
                "Skills improve through use and unlock\n"
                "bonuses at levels 25, 50, and 75.\n\n"
                "%s - View character sheet and skills",
                input_glyphs_.label(Action::CHARACTER).c_str());
              tutorial_popup_.show("Skill Up", tb); }
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
    dwarf_moved_last_turn_ = true;
    dwarf_fortified_ = false; // moving breaks fortify
    ranged_target_ = 0;
    target_cycle_idx_ = -1;

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
                { char tb[256];
                  snprintf(tb, sizeof(tb),
                    "NPCs offer quests, shops, and healing.\n\n"
                    "%s - Interact with a nearby NPC\n"
                    "Bump - Walk into an NPC to talk\n"
                    "Sneak + Bump - Pickpocket",
                    input_glyphs_.label(Action::INTERACT).c_str());
                  tutorial_popup_.show("NPCs", tb); }
            }
            open_npc_dialogue(target);
            if (dialogue_screen_.is_open()) return;
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
        bool victim_was_full_hp = false;
        GodId victim_god = GodId::NONE;
        if (world_.has<Stats>(target)) {
            auto& vs = world_.get<Stats>(target);
            victim_name = vs.name;
            victim_hp = vs.hp_max;
            victim_dmg = vs.base_damage;
            victim_arm = vs.natural_armor;
            victim_spd = vs.base_speed;
            victim_was_full_hp = (vs.hp == vs.hp_max);
        }
        if (world_.has<GodAlignment>(target) && world_.has<AI>(target))
            victim_god = world_.get<GodAlignment>(target).god;
        auto atk_result = combat::melee_attack(world_, player_, target, rng_, log_);
        player_acted_ = true;

        // Capture weapon type for gear interactions
        uint32_t player_weapon_tags = 0;
        if (world_.has<Inventory>(player_)) {
            Entity wpn = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
            if (wpn != NULL_ENTITY && world_.has<Item>(wpn))
                player_weapon_tags = world_.get<Item>(wpn).tags;
        }

        // Teleport behind target (Rogue shadow step, teleport strike ring)
        if (atk_result.teleport_behind && world_.has<Position>(player_) &&
            world_.has<Position>(target)) {
            auto& ppos = world_.get<Position>(player_);
            auto& tpos = world_.get<Position>(target);
            // Move to opposite side of target from player's original position
            int dx = tpos.x - ppos.x;
            int dy = tpos.y - ppos.y;
            int behind_x = tpos.x + (dx != 0 ? (dx > 0 ? 1 : -1) : 0);
            int behind_y = tpos.y + (dy != 0 ? (dy > 0 ? 1 : -1) : 0);
            if (map_.in_bounds(behind_x, behind_y) && map_.is_walkable(behind_x, behind_y) &&
                combat::entity_at(world_, behind_x, behind_y, player_) == NULL_ENTITY) {
                ppos.x = behind_x;
                ppos.y = behind_y;
            }
        }

        // Class ability audio from combat triggers
        if (atk_result.shadow_stepped) audio_.play(SfxId::SPELL);
        if (atk_result.parried) audio_.play(SfxId::BLOCK1);
        if (atk_result.flurried) audio_.play(SfxId::HIT2);
        if (atk_result.smited) audio_.play(SfxId::PRAYER);
        if (atk_result.exploited) audio_.play(SfxId::CRIT);
        if (atk_result.raged) audio_.play(SfxId::CRIT);
        if (atk_result.shield_blocked) audio_.play(SfxId::BLOCK2);
        if (atk_result.cycled_element) {
            static const SfxId ELEM_SFX[] = {SfxId::SPELL_FIRE, SfxId::SPELL_ICE, SfxId::SPELL_IMPACT};
            audio_.play(ELEM_SFX[rng_.range(0, 2)]);
        }

        // Tutorial: first combat
        if (!tips_shown_.first_combat) {
            tips_shown_.first_combat = true;
            { char tb[256];
              snprintf(tb, sizeof(tb),
                "Weapon skills improve through use.\n\n"
                "%s - Cast spells\n"
                "%s - Fire ranged weapon\n"
                "%s - Toggle sneak (backstab for 2-4x damage)",
                input_glyphs_.label(Action::SPELLBOOK).c_str(),
                input_glyphs_.label(Action::FIRE_RANGED).c_str(),
                input_glyphs_.label(Action::SNEAK_TOGGLE).c_str());
              tutorial_popup_.show("Combat", tb); }
        }

        // Ranger: Marked Prey (+50% damage vs marked target)
        if (ranger_marked_target_ == target && atk_result.hit && !atk_result.killed &&
            world_.has<Player>(player_) && world_.get<Player>(player_).class_id == ClassId::RANGER &&
            world_.has<Stats>(target)) {
            int mark_bonus = atk_result.damage / 2;
            world_.get<Stats>(target).hp -= mark_bonus;
            atk_result.damage += mark_bonus;
            char mkb[64];
            snprintf(mkb, sizeof(mkb), "Marked prey! (+%d)", mark_bonus);
            log_.add(mkb, {140, 255, 100, 255});
            if (world_.get<Stats>(target).hp <= 0) {
                combat::kill(world_, target, log_);
                atk_result.killed = true;
                ranger_marked_target_ = 0;
            }
        }
        // Ranger: auto-mark on first hit (before next attack so it applies immediately)
        if (world_.has<Player>(player_) && world_.get<Player>(player_).class_id == ClassId::RANGER &&
            atk_result.hit && target != ranger_marked_target_ && !atk_result.killed) {
            ranger_marked_target_ = target;
            if (world_.has<Stats>(target)) {
                char mb[64]; snprintf(mb, sizeof(mb), "Marked: %s", world_.get<Stats>(target).name.c_str());
                log_.add(mb, {200, 255, 140, 255});
                // Visible mark particles
                auto& tp = world_.get<Position>(target);
                particles_.burst((float)tp.x, (float)tp.y, 15, 100, 255, 80, 0.12f, 0.6f, 4);
            }
        }

        // === GEAR INTERACTIONS (class + weapon type = amplified ability) ===
        if (atk_result.hit && !atk_result.killed && world_.has<Player>(player_) && world_.has<Stats>(target)) {
            auto gcid = world_.get<Player>(player_).class_id;

            // Fighter + Sword: parry counter damage +50% (swords are best for riposte)
            // (applied in combat.cpp defender section)

            // Ranger + Bow: mark bonus +25% extra (total +75% vs marked)
            if (gcid == ClassId::RANGER && (player_weapon_tags & TAG_BOW) && ranger_marked_target_ == target) {
                int bow_bonus = atk_result.damage / 4;
                world_.get<Stats>(target).hp -= bow_bonus;
                atk_result.damage += bow_bonus;
            }

            // Templar + Mace: smite damage +4 vs undead
            if (gcid == ClassId::TEMPLAR && (player_weapon_tags & TAG_BLUNT) &&
                is_undead(world_.get<Stats>(target).name.c_str())) {
                world_.get<Stats>(target).hp -= 4;
                atk_result.damage += 4;
            }

            // War Cleric + Mace: fury extends +4 turns per kill (not +3)
            // (applied in on-kill section below)

            // Warlock + Dagger: siphon gives +3 extra MP on kill
            // (applied in on-kill section below)

            // Wyrmkin + Axe: breath counter counts as 2 per hit (effectively 4 hits)
            if (gcid == ClassId::WYRMKIN && (player_weapon_tags & TAG_AXE) && world_.has<Stats>(player_)) {
                world_.get<Stats>(player_).wyrmkin_breath_ctr++; // extra increment
            }

            // Serpentine + Dagger: double stacking
            if (gcid == ClassId::SERPENTINE && (player_weapon_tags & TAG_DAGGER) &&
                atk_result.poison_stacked) {
                serpentine_stacks_++;
            }

            // Dwarf + Hammer/Blunt: fortify gives TRIPLE damage (not double)
            // (applied in fortify section above - check player_weapon_tags there)

            // Necromancer + Staff: corpse explode radius +1 (applied in on-kill)
        }

        // Revenant Lv5: UNDYING FURY (+100% damage)
        if (revenant_fury_turns_ > 0 && atk_result.hit && !atk_result.killed && world_.has<Stats>(target)) {
            int fury_bonus = atk_result.damage; // double it
            world_.get<Stats>(target).hp -= fury_bonus;
            atk_result.damage += fury_bonus;
            if (world_.get<Stats>(target).hp <= 0) {
                combat::kill(world_, target, log_);
                atk_result.killed = true;
            }
        }

        // War Cleric: Zealot's Fury (damage scales with remaining fury turns)
        if (zealot_fury_turns_ > 0 && atk_result.hit && !atk_result.killed && world_.has<Stats>(target)) {
            int fury_dmg = 3 + zealot_fury_turns_ / 2; // scales: 5 turns=5, 11 turns=8, 20 turns=13
            world_.get<Stats>(target).hp -= fury_dmg;
            atk_result.damage += fury_dmg;
            if (world_.get<Stats>(target).hp <= 0) {
                combat::kill(world_, target, log_);
                atk_result.killed = true;
            }
        }

        // Dwarf: FORTIFY (double damage, TRIPLE with hammers/blunt)
        if (dwarf_fortified_ && atk_result.hit && !atk_result.killed &&
            world_.has<Player>(player_) && world_.get<Player>(player_).class_id == ClassId::DWARF &&
            world_.has<Stats>(target)) {
            int fort_mult = (player_weapon_tags & TAG_BLUNT) ? 2 : 1; // triple vs double
            int fort_bonus = atk_result.damage * fort_mult;
            world_.get<Stats>(target).hp -= fort_bonus;
            atk_result.damage += fort_bonus;
            dwarf_fortified_ = false;
            log_.add("Fortified strike!", {200, 180, 100, 255});
                audio_.play(SfxId::CRIT);
            if (world_.get<Stats>(target).hp <= 0) {
                combat::kill(world_, target, log_);
                atk_result.killed = true;
            }
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

        // === TRAIT COMBAT EFFECTS ===
        for (auto tid : build_traits_) {
            // Heavy Hitter: first hit each floor does 3x
            if (tid == TraitId::HEAVY_HITTER && atk_result.hit && !heavy_hitter_used_this_floor_ &&
                !atk_result.killed && world_.has<Stats>(target)) {
                heavy_hitter_used_this_floor_ = true;
                int bonus_3x = atk_result.damage * 2;
                world_.get<Stats>(target).hp -= bonus_3x;
                atk_result.damage += bonus_3x;
                log_.add("DEVASTATING BLOW!", {255, 200, 60, 255});
                audio_.play(SfxId::CRIT);
                trigger_screen_shake(6.0f);
                if (world_.has<Position>(target)) {
                    auto& tp2 = world_.get<Position>(target);
                    particles_.burst((float)tp2.x, (float)tp2.y, 15, 255, 200, 60, 0.15f, 0.9f, 4);
                }
                if (world_.get<Stats>(target).hp <= 0) {
                    combat::kill(world_, target, log_);
                    atk_result.killed = true;
                }
            }
            // Heavy Hitter: +20% miss chance (overswing downside)
            if (tid == TraitId::HEAVY_HITTER && atk_result.hit && rng_.chance(20)) {
                if (world_.has<Stats>(target))
                    world_.get<Stats>(target).hp += atk_result.damage;
                atk_result.hit = false;
                atk_result.damage = 0;
                log_.add("You overswing!", {200, 180, 100, 255});
            }
            // Glass Cannon: +6 flat damage on all hits
            if (tid == TraitId::GLASS_CANNON && atk_result.hit && !atk_result.killed && world_.has<Stats>(target)) {
                world_.get<Stats>(target).hp -= 6;
                atk_result.damage += 6;
                if (world_.get<Stats>(target).hp <= 0) {
                    combat::kill(world_, target, log_);
                    atk_result.killed = true;
                }
            }
            // Nocturnal: +4 damage in dungeons, -4 on surface
            if (tid == TraitId::NOCTURNAL && atk_result.hit && !atk_result.killed && world_.has<Stats>(target)) {
                int noc_bonus = (dungeon_level_ > 0) ? 4 : -4;
                world_.get<Stats>(target).hp -= noc_bonus;
                atk_result.damage += noc_bonus;
                if (world_.get<Stats>(target).hp <= 0) {
                    combat::kill(world_, target, log_);
                    atk_result.killed = true;
                }
            }
            // Spell Glutton: melee damage halved
            if (tid == TraitId::SPELL_GLUTTON && atk_result.hit && !atk_result.killed && world_.has<Stats>(target)) {
                int reduce = atk_result.damage / 2;
                world_.get<Stats>(target).hp += reduce; // refund half damage
                atk_result.damage -= reduce;
            }
            // Berserker: +50% damage below 30% HP
            if (tid == TraitId::BERSERKER && atk_result.hit && !atk_result.killed &&
                world_.has<Stats>(player_) && world_.has<Stats>(target)) {
                auto& pst = world_.get<Stats>(player_);
                if (pst.hp * 100 < pst.hp_max * 30) {
                    int rage_bonus = atk_result.damage / 2;
                    world_.get<Stats>(target).hp -= rage_bonus;
                    atk_result.damage += rage_bonus;
                    if (world_.get<Stats>(target).hp <= 0) {
                        combat::kill(world_, target, log_);
                        atk_result.killed = true;
                    }
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
                    int pct = (fav >= 50) ? 35 : (fav >= 25) ? 25 : 15;
                    if (is_undead(tgt_stats.name.c_str())) {
                        bonus = std::max(1, atk_result.damage * pct / 100);
                        char vb[48]; snprintf(vb, sizeof(vb), "+%d vs undead", bonus);
                        log_.add(vb, {180, 180, 140, 255});
                        particles_.burst((float)nx, (float)ny, 12, 200, 200, 160, 0.12f, 0.5f, 4);
                    }
                    int bone_dmg = (fav >= 75) ? 4 : 2;
                    if (weapon_tags & TAG_BONE_ITEM) bonus += bone_dmg;
                    break;
                }
                case GodId::MORRETH: {
                    // First hit on a target = 2x damage
                    if (victim_was_full_hp) {
                        int first_bonus = atk_result.damage; // double it
                        tgt_stats.hp -= first_bonus;
                        bonus += first_bonus;
                        log_.add("IRON STRIKE!", {220, 180, 60, 255});
                        particles_.burst((float)nx, (float)ny, 10, 200, 160, 80, 0.12f, 0.6f, 3);
                    }
                    // Blunt/axe bonus
                    int wpn_bonus = (fav >= 50) ? 4 : (fav >= 25) ? 3 : 2;
                    if (weapon_tags & (TAG_BLUNT | TAG_AXE)) bonus += wpn_bonus;
                    break;
                }
                case GodId::YASHKHET: {
                    auto& ps = world_.get<Stats>(player_);
                    int steal = std::max(1, atk_result.damage * 15 / 100);
                    int old_hp = ps.hp;
                    ps.hp = std::min(ps.hp + steal, ps.hp_max);
                    int healed = ps.hp - old_hp;
                    if (healed > 0) {
                        char yb[32]; snprintf(yb, sizeof(yb), "+%d HP (blood)", healed);
                        log_.add(yb, {200, 80, 80, 255});
                    }
                    // Low-HP bonus: +25% damage below 50% HP (75% at favor 50)
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
                    if (world_.get<Stats>(player_).invisible_turns > 0) {
                        int mult = (fav >= 50) ? 2 : 1;
                        bonus = atk_result.damage * mult;
                        world_.get<Stats>(player_).invisible_turns = 0;
                        char zb[48]; snprintf(zb, sizeof(zb), "Backstab! (+%d)", bonus);
                        log_.add(zb, {140, 120, 200, 255});
                        particles_.burst((float)nx, (float)ny, 15, 100, 60, 180, 0.15f, 0.5f, 5);
                    }
                    break;
                }
                case GodId::OSSREN: {
                    if (world_.has<Inventory>(player_)) {
                        auto& oinv = world_.get<Inventory>(player_);
                        int equip_count = 0;
                        for (int s = 0; s < EQUIP_SLOT_COUNT; s++)
                            if (oinv.equipped[s] != NULL_ENTITY) equip_count++;
                        bonus = equip_count;
                    }
                    // Ossren bonus is constant, no per-hit message (would spam)
                    break;
                }
                case GodId::GATHRUUN: {
                    // +4 damage underground, -3 on surface
                    if (dungeon_level_ > 0) bonus = 4;
                    else bonus = -3; // penalty on surface
                    break;
                }
                case GodId::SYTHARA:
                    // Poison applied in dramatic passives section below (100%, always)
                    break;
                case GodId::KHAEL:
                    // -4 damage in dungeons (nature weakens underground)
                    if (dungeon_level_ > 0) bonus = -4;
                    // Entangle on hit
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

            // === DRAMATIC GOD PASSIVES ===
            if (atk_result.hit && !atk_result.killed && world_.has<Stats>(player_)) {
                auto& pst = world_.get<Stats>(player_);

                // Yashkhet: lifesteal 15% of damage dealt
                if (ga.god == GodId::YASHKHET && atk_result.damage > 0) {
                    int steal = std::max(1, atk_result.damage * 15 / 100);
                    pst.hp = std::min(pst.hp_max, pst.hp + steal);
                    // Blood stream from enemy to player
                    auto& pp = world_.get<Position>(player_);
                    particles_.projectile((float)nx, (float)ny, (float)pp.x, (float)pp.y,
                                          6, 200, 20, 20, 0.3f, 3);
                    particles_.rise((float)pp.x, (float)pp.y, 4, 180, 30, 30, 0.6f, 2);
                    // Blood drips at hit location
                    particles_.fall((float)nx, (float)ny, 3, 160, 20, 20, 0.4f, 2);
                }

                // Soleth: +3 fire damage on all attacks + burn
                if (ga.god == GodId::SOLETH) {
                    tgt_stats.hp -= 3;
                    atk_result.damage += 3;
                    if (world_.has<StatusEffects>(target))
                        world_.get<StatusEffects>(target).add(StatusType::BURN, 1, 2);
                    log_.add("+3 fire", {255, 160, 60, 255});
                    particles_.burst((float)nx, (float)ny, 15, 255, 140, 40, 0.15f, 0.5f, 5);
                    particles_.rise((float)nx, (float)ny, 8, 255, 200, 60, 0.4f, 3);
                    if (tgt_stats.hp <= 0) { combat::kill(world_, target, log_); atk_result.killed = true; }
                }

                // Sythara: ALL attacks poison (100%)
                if (ga.god == GodId::SYTHARA && world_.has<StatusEffects>(target)) {
                    int sev = (ga.favor >= 75) ? 3 : 2;
                    world_.get<StatusEffects>(target).add(StatusType::POISON, sev, 3);
                    log_.add("Sythara's venom.", {120, 180, 60, 255});
                    particles_.drift((float)nx, (float)ny, 10, 100, 200, 60, 0.8f, 3);
                }

                // Gathruun: earthquake on crit
                if (ga.god == GodId::GATHRUUN && atk_result.critical && dungeon_level_ > 0) {
                    auto& ai_pool = world_.pool<AI>();
                    for (size_t ai = 0; ai < ai_pool.size(); ai++) {
                        Entity ae = ai_pool.entity_at(ai);
                        if (ae == target || ai_pool.at_index(ai).friendly) continue;
                        if (!world_.has<Position>(ae) || !world_.has<Stats>(ae)) continue;
                        auto& ap = world_.get<Position>(ae);
                        if (std::abs(ap.x - nx) <= 2 && std::abs(ap.y - ny) <= 2) {
                            world_.get<Stats>(ae).hp -= 3;
                            if (world_.has<StatusEffects>(ae))
                                world_.get<StatusEffects>(ae).add(StatusType::STUNNED, 0, 1);
                        }
                    }
                    log_.add("The earth shakes!", {180, 140, 80, 255});
                    trigger_screen_shake(6.0f);
                    particles_.burst((float)nx, (float)ny, 15, 160, 120, 60, 0.15f, 0.8f, 4);
                }
            }
        }

        if (atk_result.hit && atk_result.critical) {
            audio_.play(SfxId::CRIT); particles_.crit_flash((float)nx, (float)ny);
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
            // Schema Monk elemental particles on unarmed hit
            if (wtags == 0 && world_.has<Player>(player_) &&
                world_.get<Player>(player_).class_id == ClassId::SCHEMA_MONK) {
                static int elem_vis = 0;
                switch (elem_vis % 3) {
                    case 0: particles_.spell_fire((float)nx, (float)ny); break;
                    case 1: particles_.spell_ice((float)nx, (float)ny); break;
                    case 2: particles_.spell_effect((float)nx, (float)ny, 180, 180, 255); break;
                }
                elem_vis++;
            }
            // Class-specific hit VFX (visual signature per class)
            if (world_.has<Player>(player_)) {
                auto pcid = world_.get<Player>(player_).class_id;
                float fx = (float)nx, fy = (float)ny;
                auto& pp2 = world_.get<Position>(player_);
                float px = (float)pp2.x, py = (float)pp2.y;

                switch (pcid) {
                    case ClassId::FIGHTER:
                        // Gold flash on every hit (disciplined strikes)
                        particles_.hit_spark(fx, fy);
                        break;
                    case ClassId::ROGUE:
                        // Purple shadow trail
                        particles_.trail(px, py, fx, fy, 4, 100, 60, 160, 1);
                        break;
                    case ClassId::RANGER:
                        // Green pulse on marked target
                        if (ranger_marked_target_ == target) {
                            particles_.burst(fx, fy, 12, 100, 255, 80, 0.1f, 0.5f, 4);
                            particles_.rise(fx, fy, 6, 140, 255, 100, 0.4f, 3);
                        }
                        break;
                    case ClassId::BARBARIAN:
                        if (world_.has<Stats>(player_) && world_.get<Stats>(player_).hp * 2 < world_.get<Stats>(player_).hp_max) {
                            particles_.burst(fx, fy, 8, 255, 40, 40, 0.12f, 0.5f, 3);
                        }
                        break;
                    case ClassId::KNIGHT:
                        // Blue defensive sparks
                        particles_.hit_spark(fx, fy);
                        break;
                    case ClassId::MONK:
                        // White speed lines on hit
                        particles_.directional(px, py, fx - px, fy - py, 3, 220, 220, 240, 0.15f, 0.3f, 1);
                        break;
                    case ClassId::TEMPLAR:
                        if (world_.has<Stats>(target) && is_undead(world_.get<Stats>(target).name.c_str())) {
                            particles_.spell_holy(fx, fy);
                            particles_.rise(fx, fy, 6, 255, 240, 140, 0.8f, 3);
                        }
                        break;
                    case ClassId::DRUID:
                        if (druid_beast_turns_ > 0)
                            particles_.burst(fx, fy, 6, 60, 200, 40, 0.1f, 0.4f, 3);
                        else
                            particles_.burst(px, py, 4, 60, 180, 60, 0.06f, 0.4f, 2);
                        break;
                    case ClassId::WAR_CLERIC:
                        if (zealot_fury_turns_ > 0)
                            particles_.rise(fx, fy, 3, 255, 220, 80, 0.5f, 2);
                        break;
                    case ClassId::WARLOCK:
                        // Dark purple wisps
                        particles_.drift(fx, fy, 3, 140, 60, 200, 0.5f, 2);
                        break;
                    case ClassId::DWARF:
                        if (dwarf_fortified_)
                            particles_.burst(fx, fy, 10, 180, 140, 80, 0.12f, 0.6f, 3);
                        break;
                    case ClassId::ELF:
                        // Blue arcane circles
                        particles_.orbit(fx, fy, 3, 120, 160, 255, 0.3f, 0.4f, 1);
                        break;
                    case ClassId::BANDIT:
                        // Red slash on exploit crits
                        if (atk_result.critical)
                            particles_.directional(px, py, fx - px, fy - py, 5, 255, 80, 40, 0.2f, 0.3f, 2);
                        break;
                    case ClassId::NECROMANCER:
                        particles_.spell_dark(fx, fy);
                        break;
                    case ClassId::WYRMKIN:
                        if (world_.has<Stats>(player_) && world_.get<Stats>(player_).wyrmkin_breath_ctr == 0 &&
                            atk_result.damage > 10) {
                            audio_.play(SfxId::SPELL_FIRE);
                            particles_.spell_fire(fx, fy);
                            particles_.burst(fx, fy, 15, 255, 120, 20, 0.14f, 0.9f, 4);
                            trigger_screen_shake(5.0f);
                        } else {
                            // Building toward breath: small fire sparks
                            particles_.burst(fx, fy, 2, 255, 140, 40, 0.06f, 0.3f, 1);
                        }
                        break;
                    case ClassId::REVENANT:
                        // Dark mist from player
                        particles_.drift(px, py, 2, 80, 60, 80, 0.6f, 2);
                        break;
                    case ClassId::SERPENTINE:
                        particles_.fall(fx, fy, 4, 80, 200, 40, 0.6f, 2);
                        break;
                    case ClassId::TROLLBLOOD:
                        // Green regen particles
                        particles_.rise(px, py, 2, 80, 180, 60, 0.4f, 1);
                        break;
                    case ClassId::HERETIC:
                        // Purple absorb sparks
                        particles_.burst(fx, fy, 3, 180, 80, 220, 0.08f, 0.4f, 2);
                        break;
                    default: break;
                }
            }
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
        }

        // Bandit pickpocket gold (legacy, kept for any remaining gold_stolen usage)
        if (atk_result.gold_stolen > 0) {
            gold_ += atk_result.gold_stolen;
            // Church task: Zhavek steal tracking
            if (world_.has<GodAlignment>(player_)) {
                auto& ga = world_.get<GodAlignment>(player_);
                if (ga.task_active && !ga.task_complete && ga.god == GodId::ZHAVEK) {
                    auto next = static_cast<ChurchRank>(ga.church_rank + 1);
                    auto& task = get_church_task(ga.god, next);
                    if (task.items_donated > 0) {
                        ga.task_progress += atk_result.gold_stolen;
                        if (ga.task_progress >= task.items_donated) {
                            ga.task_complete = true; ga.task_active = false;
                            log_.add("Church task complete. Return to the church.", {255, 220, 80, 255});
                        }
                    }
                }
            }
            char gbuf[48]; snprintf(gbuf, sizeof(gbuf), "Stole %d gold!", atk_result.gold_stolen);
            log_.add(gbuf, {255, 220, 80, 255});
            audio_.play(SfxId::GOLD);
        }

        // Serpentine: INJECT stack tracking + detonate (no cap, exponential scaling)
        if (atk_result.poison_stacked && world_.has<Player>(player_) &&
            world_.get<Player>(player_).class_id == ClassId::SERPENTINE) {
            if (serpentine_target_ != target) {
                serpentine_target_ = target;
                serpentine_stacks_ = 0;
            }
            serpentine_stacks_++;
            // Detonate at 5+ stacks. Damage = stacks SQUARED (exponential scaling)
            if (serpentine_stacks_ >= 5 && world_.has<Stats>(target) && !atk_result.killed) {
                int stacks_used = serpentine_stacks_;
                int burst = stacks_used * stacks_used; // 5=25, 7=49, 10=100
                world_.get<Stats>(target).hp -= burst;
                serpentine_stacks_ = 0;
                char sb[64]; snprintf(sb, sizeof(sb), "VENOM BURST! (%d)", burst);
                log_.add(sb, {120, 255, 60, 255});
                audio_.play(SfxId::SPELL_IMPACT);
                if (world_.has<Position>(target)) {
                    auto& tp = world_.get<Position>(target);
                    particles_.burst((float)tp.x, (float)tp.y, 15, 80, 220, 40, 0.14f, 0.9f, 4);
                }
                trigger_screen_shake(5.0f);
                // Lv5 NEUROTOXIN: 8+ stacks also paralyzes (stun 3 turns)
                if (stacks_used >= 8 && world_.has<Stats>(player_) &&
                    world_.get<Stats>(player_).level >= 5 && world_.has<StatusEffects>(target)) {
                    world_.get<StatusEffects>(target).add(StatusType::STUNNED, 0, 3);
                    log_.add("NEUROTOXIN! Paralyzed.", {80, 255, 120, 255});
                }
                if (world_.get<Stats>(target).hp <= 0) {
                    combat::kill(world_, target, log_);
                    atk_result.killed = true;
                }
            } else {
                char sb[32]; snprintf(sb, sizeof(sb), "Venom: %d (burst at 5+)", serpentine_stacks_);
                log_.add(sb, {100, 200, 60, 255});
            }
        }

        // Monk Lv5: PALM STRIKE (every 5th hit stuns 2 turns)
        if (atk_result.hit && world_.has<Player>(player_) &&
            world_.get<Player>(player_).class_id == ClassId::MONK &&
            world_.get<Stats>(player_).level >= 5) {
            monk_hit_counter_++;
            if (monk_hit_counter_ >= 5 && !atk_result.killed && world_.has<StatusEffects>(target)) {
                monk_hit_counter_ = 0;
                world_.get<StatusEffects>(target).add(StatusType::STUNNED, 0, 2);
                log_.add("Palm strike!", {220, 220, 240, 255});
                audio_.play(SfxId::SPELL_IMPACT);
            }
        }

        // Elf: WEAVE counter (every 3rd attack = free spell)
        if (atk_result.hit && world_.has<Player>(player_) &&
            world_.get<Player>(player_).class_id == ClassId::ELF) {
            elf_weave_counter_++;
            int weave_threshold = (player_weapon_tags & TAG_BOW) ? 2 : 3; // bows = every 2 hits
            if (elf_weave_counter_ >= weave_threshold && world_.has<Spellbook>(player_)) {
                elf_weave_counter_ = 0;
                auto& book = world_.get<Spellbook>(player_);
                if (!book.known_spells.empty()) {
                    // Cast a random known spell for free at the target
                    SpellId free_spell = book.known_spells[rng_.range(0, static_cast<int>(book.known_spells.size()) - 1)];
                    auto& pl = world_.get<Player>(player_);
                    pl.weave_cast = true;
                    magic::cast(world_, player_, free_spell, map_, rng_, log_);
                    pl.weave_cast = false;
                    log_.add("Arcane weave!", {140, 180, 255, 255});
                }
            }
        }

        // Heretic: DEVOUR (20% + 5% per unique item on kill, learn a random spell)
        if (atk_result.killed && world_.has<Player>(player_) &&
            world_.get<Player>(player_).class_id == ClassId::HERETIC) {
            // Count unique items equipped for devour bonus
            int devour_chance = 20;
            if (world_.has<Inventory>(player_)) {
                auto& inv = world_.get<Inventory>(player_);
                for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
                    Entity eq = inv.equipped[s];
                    if (eq != NULL_ENTITY && world_.has<Item>(eq) &&
                        world_.get<Item>(eq).rarity >= Rarity::LEGENDARY)
                        devour_chance += 5;
                }
            }
            if (rng_.chance(devour_chance)) {
            if (world_.has<Spellbook>(player_)) {
                // Pick a random spell the player doesn't know
                int total_spells = static_cast<int>(SpellId::COUNT);
                auto& book = world_.get<Spellbook>(player_);
                SpellId candidate = static_cast<SpellId>(rng_.range(0, total_spells - 1));
                bool already_known = false;
                for (auto s : book.known_spells) if (s == candidate) { already_known = true; break; }
                if (!already_known) {
                    book.known_spells.push_back(candidate);
                    auto& sinfo = get_spell_info(candidate);
                    char db[96]; snprintf(db, sizeof(db), "DEVOURED: learned %s!", sinfo.name);
                    log_.add(db, {220, 140, 255, 255});
                    audio_.play(SfxId::SPELL_IMPACT);
                    if (world_.has<Position>(player_)) {
                        auto& pp = world_.get<Position>(player_);
                        particles_.burst((float)pp.x, (float)pp.y, 10, 200, 100, 255, 0.1f, 0.7f, 3);
                    }
                }
            } else {
                // Give spellbook if they don't have one
                Spellbook sb;
                SpellId first = static_cast<SpellId>(rng_.range(0, static_cast<int>(SpellId::COUNT) - 1));
                sb.known_spells.push_back(first);
                world_.add<Spellbook>(player_, std::move(sb));
                auto& sinfo = get_spell_info(first);
                char db[96]; snprintf(db, sizeof(db), "DEVOURED: learned %s!", sinfo.name);
                log_.add(db, {220, 140, 255, 255});
                audio_.play(SfxId::SPELL_IMPACT);
            }
            } // devour chance roll
        }

        // Druid: SHAPESHIFT (kills build form, kills IN form extend it)
        if (atk_result.killed && world_.has<Player>(player_) &&
            world_.get<Player>(player_).class_id == ClassId::DRUID) {
            if (druid_beast_turns_ > 0) {
                // Kill during beast form: EXTEND duration (+3 turns per kill)
                druid_beast_turns_ += 3;
                log_.add("The beast feeds! (+3 turns)", {80, 255, 80, 255});
            } else {
                // Not in beast form: build toward transformation
                druid_kill_counter_++;
                // Spear-type weapons (no TAG_SPEAR yet, use damage threshold as proxy)
                // Shapeshift at 4 kills
                if (druid_kill_counter_ >= 4) {
                    druid_kill_counter_ = 0;
                    druid_beast_turns_ = 10;
                    log_.add("THE BEAST AWAKENS!", {80, 200, 80, 255});
                    audio_.play(SfxId::SPELL_IMPACT);
                    trigger_screen_shake(5.0f);
                    if (world_.has<Position>(player_)) {
                        auto& pp = world_.get<Position>(player_);
                        particles_.burst((float)pp.x, (float)pp.y, 15, 60, 180, 60, 0.12f, 0.9f, 4);
                    }
                    if (world_.has<Stats>(player_)) {
                        auto& ps = world_.get<Stats>(player_);
                        int shape_bonus = 0;
                        if (world_.has<PassiveTreeState>(player_))
                            shape_bonus = passive_tree::compute_bonuses(world_.get<PassiveTreeState>(player_)).shapeshift_dmg_bonus;
                        ps.base_damage += 8 + shape_bonus;
                        ps.base_speed += 30;
                    }
                } else {
                    char kb[32]; snprintf(kb, sizeof(kb), "Beast stirs: %d/5", druid_kill_counter_);
                    log_.add(kb, {80, 160, 80, 255});
                }
            }
        }

        // (Ranger mark moved earlier, near damage application)

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

        // Church task: track kill progress
        if (atk_result.killed && world_.has<GodAlignment>(player_)) {
            auto& ga = world_.get<GodAlignment>(player_);
            if (ga.task_active && !ga.task_complete) {
                auto next = static_cast<ChurchRank>(ga.church_rank + 1);
                auto& task = get_church_task(ga.god, next);
                if (task.kill_target > 0) {
                    ga.task_progress++;
                    if (ga.task_progress >= task.kill_target) {
                        ga.task_complete = true;
                        ga.task_active = false;
                        log_.add("Church task complete. Return to the church.", {255, 220, 80, 255});
                        audio_.play(SfxId::QUEST);
                    }
                }
                // Dungeon floor clear (boss kill counts)
                if (task.dungeon_clears > 0 || task.is_dungeon_quest) {
                    if (world_.has<Stats>(target) && world_.get<Stats>(target).xp_value >= 50) {
                        ga.task_progress++;
                        if (ga.task_progress >= std::max(1, task.dungeon_clears)) {
                            ga.task_complete = true;
                            ga.task_active = false;
                            log_.add("Church task complete. Return to the church.", {255, 220, 80, 255});
                            audio_.play(SfxId::QUEST);
                        }
                    }
                }
            }
        }

        // === CLASS ON-KILL ABILITIES ===
        if (atk_result.killed && world_.has<Player>(player_) && world_.has<Stats>(player_)) {
            auto cid = world_.get<Player>(player_).class_id;
            auto& pstats = world_.get<Stats>(player_);
            passive_tree::TreeBonuses kill_tb{};
            if (world_.has<PassiveTreeState>(player_))
                kill_tb = passive_tree::compute_bonuses(world_.get<PassiveTreeState>(player_));

            // War Cleric: ZEALOT'S FURY (stacking: kills extend + amplify)
            if (cid == ClassId::WAR_CLERIC) {
                int fury_ext = (player_weapon_tags & TAG_BLUNT) ? 4 : 3; // Mace: +4 instead of +3
                fury_ext += kill_tb.fury_chain_bonus;
                if (zealot_fury_turns_ > 0) {
                    zealot_fury_turns_ += fury_ext;
                    log_.add("FURY GROWS!", {255, 200, 60, 255});
                } else {
                    zealot_fury_turns_ = 5;
                    log_.add("Zealot's fury!", {255, 240, 140, 255});
                }
                audio_.play(SfxId::PRAYER);
                auto& pp = world_.get<Position>(player_);
                particles_.burst((float)pp.x, (float)pp.y, 12, 255, 240, 100, 0.12f, 0.8f, 3);
                particles_.rise((float)pp.x, (float)pp.y, 6, 255, 200, 60, 1.0f, 2);
            }

            // Warlock: Soul Siphon (restore MP on kill, daggers +3 extra)
            if (cid == ClassId::WARLOCK) {
                int mp_gain = 5 + pstats.eff_attr(Attr::INT) / 4;
                if (player_weapon_tags & TAG_DAGGER) mp_gain += 3;
                if (kill_tb.siphon_bonus_pct > 0)
                    mp_gain = mp_gain * (100 + kill_tb.siphon_bonus_pct) / 100;
                pstats.mp = std::min(pstats.mp_max, pstats.mp + mp_gain);
                char sb[64]; snprintf(sb, sizeof(sb), "Soul energy flows into you. (+%d MP)", mp_gain);
                log_.add(sb, {160, 100, 200, 255});
                audio_.play(SfxId::SPELL);
                // Purple soul trail from corpse to player
                auto& pp = world_.get<Position>(player_);
                if (world_.has<Position>(target)) {
                    auto& tp = world_.get<Position>(target);
                    particles_.projectile((float)tp.x, (float)tp.y, (float)pp.x, (float)pp.y,
                                          8, 160, 80, 220, 0.25f, 3);
                }
                particles_.orbit((float)pp.x, (float)pp.y, 5, 140, 60, 200, 0.5f, 0.8f, 2);
            }

            // Revenant: Undying (heal on kill)
            if (cid == ClassId::REVENANT) {
                int heal = 2 + pstats.eff_attr(Attr::CON) / 4;
                // Sythara halves all healing
                if (world_.has<GodAlignment>(player_) && world_.get<GodAlignment>(player_).god == GodId::SYTHARA)
                    heal = heal / 2;
                pstats.hp = std::min(pstats.hp_max, pstats.hp + heal);
                char hb[48]; snprintf(hb, sizeof(hb), "Life returns. (+%d HP)", heal);
                log_.add(hb, {140, 200, 140, 255});
                auto& pp = world_.get<Position>(player_);
                particles_.rise((float)pp.x, (float)pp.y, 8, 100, 200, 100, 0.7f, 2);
            }

            // Necromancer: Corpse Explode (25% chance, 4 AoE, staves = radius +1)
            if (cid == ClassId::NECROMANCER && rng_.chance(25) && world_.has<Position>(target)) {
                auto& tpos = world_.get<Position>(target);
                auto& ai_pool = world_.pool<AI>();
                bool hit_any = false;
                // Necromancers always get radius 2 (staff is default weapon)
                int explode_range = 2;
                for (size_t ai = 0; ai < ai_pool.size(); ai++) {
                    Entity ae = ai_pool.entity_at(ai);
                    if (ae == target || ai_pool.at_index(ai).friendly) continue;
                    if (!world_.has<Position>(ae) || !world_.has<Stats>(ae)) continue;
                    auto& ap = world_.get<Position>(ae);
                    if (std::abs(ap.x - tpos.x) <= explode_range && std::abs(ap.y - tpos.y) <= explode_range) {
                        world_.get<Stats>(ae).hp -= (4 + kill_tb.explode_dmg_bonus);
                        hit_any = true;
                    }
                }
                if (hit_any) {
                    log_.add("The corpse explodes!", {200, 100, 80, 255});
                    audio_.play(SfxId::DEATH);
                    particles_.burst((float)tpos.x, (float)tpos.y, 20, 180, 60, 40, 0.15f, 0.7f, 3);
                    particles_.burst((float)tpos.x, (float)tpos.y, 10, 220, 120, 60, 0.1f, 0.5f, 2);
                }
            }
        }

        // === SECOND ABILITIES (Level 5+ unlocks) ===
        if (atk_result.killed && world_.has<Player>(player_) && world_.has<Stats>(player_) &&
            world_.get<Stats>(player_).level >= 5) {
            auto cid2 = world_.get<Player>(player_).class_id;

            // Rogue Lv5: VANISH (re-enter stealth after kill, 3-turn cooldown)
            if (cid2 == ClassId::ROGUE) {
                auto& pc = world_.get<Player>(player_);
                if (pc.vanish_cooldown <= 0) {
                    sneaking_ = true;
                    if (world_.has<Stats>(player_)) world_.get<Stats>(player_).invisible_turns = 2;
                    pc.vanish_cooldown = 3;
                    log_.add("You vanish into shadow.", {100, 80, 160, 255});
                }
            }

            // Barbarian Lv5: CLEAVE (rage hits splash adjacent enemies)
            if (cid2 == ClassId::BARBARIAN && world_.has<Stats>(player_) &&
                world_.get<Stats>(player_).hp * 2 < world_.get<Stats>(player_).hp_max &&
                world_.has<Position>(target)) {
                auto& tp = world_.get<Position>(target);
                auto& ai_pool = world_.pool<AI>();
                for (size_t ai = 0; ai < ai_pool.size(); ai++) {
                    Entity ae = ai_pool.entity_at(ai);
                    if (ae == target || ai_pool.at_index(ai).friendly) continue;
                    if (!world_.has<Position>(ae) || !world_.has<Stats>(ae)) continue;
                    auto& ap = world_.get<Position>(ae);
                    if (std::abs(ap.x - tp.x) <= 1 && std::abs(ap.y - tp.y) <= 1) {
                        world_.get<Stats>(ae).hp -= atk_result.damage / 3;
                    }
                }
                log_.add("Cleave!", {255, 80, 80, 255});
                audio_.play(SfxId::SPELL_EARTH);
            }

            // Bandit Lv5: CUTTHROAT (exploit kills grant invisibility)
            if (cid2 == ClassId::BANDIT && atk_result.critical) {
                if (world_.has<Stats>(player_)) world_.get<Stats>(player_).invisible_turns = 3;
                sneaking_ = true;
                log_.add("You disappear into the chaos.", {180, 140, 80, 255});
            }

            // War Cleric Lv5: SERMON (fury kills heal 5 HP)
            if (cid2 == ClassId::WAR_CLERIC && zealot_fury_turns_ > 0) {
                auto& ps = world_.get<Stats>(player_);
                ps.hp = std::min(ps.hp_max, ps.hp + 5);
            }

            // Templar Lv5: CONSECRATE (kills create holy ground - damages undead 3/turn)
            if (cid2 == ClassId::TEMPLAR && world_.has<Position>(target)) {
                auto& tp = world_.get<Position>(target);
                // Consecrate tiles around the kill
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int cx2 = tp.x + dx, cy2 = tp.y + dy;
                        if (map_.in_bounds(cx2, cy2) && map_.is_walkable(cx2, cy2))
                            map_.at(cx2, cy2).variant = 99; // mark as consecrated (checked in status tick)
                    }
                log_.add("The ground is consecrated.", {255, 240, 160, 255});
                particles_.spell_holy((float)tp.x, (float)tp.y);
            }

            // Druid Lv5: NATURE'S CALL (beast form kills summon a wolf)
            if (cid2 == ClassId::DRUID && druid_beast_turns_ > 0 && world_.has<Position>(target)) {
                auto& tp = world_.get<Position>(target);
                // Spawn friendly wolf
                Entity wolf = world_.create();
                world_.add<Position>(wolf, {tp.x, tp.y});
                world_.add<Renderable>(wolf, {SHEET_ANIMALS, 6, 4, {255,255,255,255}, 5});
                Stats ws; ws.name = "summoned wolf"; ws.hp = 15 + world_.get<Stats>(player_).level * 2;
                ws.hp_max = ws.hp; ws.base_damage = 4 + world_.get<Stats>(player_).level / 2;
                ws.base_speed = 120; ws.xp_value = 0;
                world_.add<Stats>(wolf, std::move(ws));
                AI wai; wai.state = AIState::HUNTING; wai.friendly = true;
                world_.add<AI>(wolf, wai);
                world_.add<Energy>(wolf, {0, 120});
                log_.add("A wolf answers the call.", {80, 200, 80, 255});
                audio_.play(SfxId::SPELL_BUFF);
            }

            // Wyrmkin Lv5: INFERNO (breath kill leaves burning ground 3 turns)
            if (cid2 == ClassId::WYRMKIN && world_.has<Position>(target) &&
                world_.has<Stats>(player_) && world_.get<Stats>(player_).wyrmkin_breath_ctr == 0) {
                // Breath just fired (counter reset) - mark tiles as burning
                auto& tp = world_.get<Position>(target);
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int fx2 = tp.x + dx, fy2 = tp.y + dy;
                        if (map_.in_bounds(fx2, fy2) && map_.is_walkable(fx2, fy2))
                            map_.at(fx2, fy2).type = TileType::LAVA;
                    }
                log_.add("The ground burns.", {255, 140, 40, 255});
            }

            // Trollblood Lv5: GORGE (3 corpses this floor = +5 max HP permanent)
            // (tracked in try_interact consume section)

            // Necromancer Lv5: ARMY (summon cap increase handled in magic.cpp)
        }

        // Bestiary stats (melee has access to victim stats before combat::kill removes them)
        if (atk_result.killed && !victim_name.empty()) {
            auto& entry = bestiary_[victim_name];
            if (entry.hp == 0) {
                entry.name = victim_name;
                entry.hp = victim_hp; entry.damage = victim_dmg;
                entry.armor = victim_arm; entry.speed = victim_spd;
            }
            // Unarmed kill check (only melee)
            if (world_.has<Inventory>(player_)) {
                Entity wpn = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn == NULL_ENTITY) meta_.killed_unarmed = true;
            }
        }

        // Boss loot drops
        if (atk_result.killed && world_.has<Position>(target)) {
            auto& tp = world_.get<Position>(target);
            auto spawn_boss_loot = [&](const char* name, const char* desc, ItemType type,
                                        EquipSlot slot, int dmg, int arm, UniqueEffect ue,
                                        int sx, int sy) {
                Entity le = world_.create();
                world_.add<Position>(le, {tp.x, tp.y});
                Item li;
                li.name = name; li.description = desc;
                li.type = type; li.slot = slot;
                li.damage_bonus = dmg; li.armor_bonus = arm;
                li.unique_effect = ue; li.identified = true;
                li.rarity = Rarity::LEGENDARY; li.gold_value = 300;
                world_.add<Item>(le, std::move(li));
                world_.add<Renderable>(le, {SHEET_ITEMS, sx, sy, {255, 220, 100, 255}, 2});
                char lbuf[96]; snprintf(lbuf, sizeof(lbuf), "%s drops %s!", victim_name.c_str(), name);
                log_.add(lbuf, {255, 220, 80, 255});
            };
            if (victim_name == "The Bone Colossus") {
                spawn_boss_loot("Colossus Skull", "+6 AC. Reflect 3 damage.",
                    ItemType::ARMOR_HEAD, EquipSlot::HEAD, 0, 6, UniqueEffect::THORNS, 4, 15);
            } else if (victim_name == "The Ember Wyrm") {
                spawn_boss_loot("Wyrm Fang", "+9 dmg. +5 fire on hit.",
                    ItemType::WEAPON, EquipSlot::MAIN_HAND, 9, 0, UniqueEffect::FIRE_DAMAGE_BONUS, 3, 0);
                world_.get<Item>(world_.pool<Item>().entity_at(world_.pool<Item>().size()-1)).tags |= TAG_SWORD;
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
                god_system::adjust_favor(world_, player_, log_, 5); // paragon kill bonus
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

        // Bloodletter: crits apply 5-turn bleed to enemies
        if (atk_result.hit && atk_result.critical && !atk_result.killed) {
            for (auto tid : build_traits_) {
                if (tid == TraitId::BLOODLETTER && world_.has<StatusEffects>(target)) {
                    world_.get<StatusEffects>(target).add(StatusType::BLEED, 2, 5);
                    log_.add("Blood flows freely.", {200, 60, 60, 255});
                    break;
                }
            }
        }

        // Trait: Vampiric — heal on kill
        if (atk_result.killed && world_.has<Stats>(player_)) {
            for (TraitId tid : build_traits_) {
                auto& tr = get_trait_info(tid);
                if (tr.hp_on_kill > 0) {
                    auto& ps = world_.get<Stats>(player_);
                    int heal = std::min(tr.hp_on_kill, ps.hp_max - ps.hp);
                    // Sythara halves all healing
                    if (world_.has<GodAlignment>(player_) && world_.get<GodAlignment>(player_).god == GodId::SYTHARA)
                        heal = heal / 2;
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
            if (background_ == BackgroundId::GLADIATOR) {
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
                { char tb[256];
                  snprintf(tb, sizeof(tb),
                    "You earned a passive tree point!\n\n"
                    "%s - Open the passive tree to spend it\n"
                    "%s - Check inventory and equipment\n"
                    "%s - View character sheet",
                    input_glyphs_.label(Action::PASSIVE_TREE).c_str(),
                    input_glyphs_.label(Action::INVENTORY).c_str(),
                    input_glyphs_.label(Action::CHARACTER).c_str());
                  tutorial_popup_.show("Level Up", tb); }
            }
        }
        return;
    }

    // Phase walk (Lethis mastery): walk through walls
    bool phasing = world_.has<Stats>(player_) && world_.get<Stats>(player_).phase_turns > 0;
    if (!map_.is_walkable(nx, ny) && !phasing) return;
    // Can't phase into map edges
    if (!map_.in_bounds(nx, ny)) return;

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

    // Show items on ground at new position
    {
        auto& all_pos = world_.pool<Position>();
        std::string first_name;
        int item_count = 0;
        for (size_t gi = 0; gi < all_pos.size(); gi++) {
            Entity ge = all_pos.entity_at(gi);
            if (ge == player_ || !world_.has<Item>(ge)) continue;
            auto& gp = all_pos.at_index(gi);
            if (gp.x == nx && gp.y == ny) {
                if (item_count == 0) first_name = world_.get<Item>(ge).display_name();
                item_count++;
            }
        }
        if (item_count == 1) {
            char gbuf[128]; snprintf(gbuf, sizeof(gbuf), "You see %s here.", first_name.c_str());
            log_.add(gbuf, {180, 180, 160, 255});
        } else if (item_count > 1) {
            char gbuf[128]; snprintf(gbuf, sizeof(gbuf), "You see %s and %d more items here.", first_name.c_str(), item_count - 1);
            log_.add(gbuf, {180, 180, 160, 255});
        }
    }

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

        // Ixuul: shrines are useless (god of chaos rejects divine order)
        if (ga.god == GodId::IXUUL) {
            log_.add("The shrine means nothing. Ixuul rejects all divine order.", {180, 100, 255, 255});
            log_.add("[Ixuul] Cannot use shrines.", {140, 100, 180, 255});
            player_acted_ = true;
            return;
        }

        if (shrine_god == ga.god) {
            // Same god shrine: +5 favor, small heal, identify curse/bless
            god_system::adjust_favor(world_, player_, log_, 5);
            auto& ps = world_.get<Stats>(player_);
            int heal = std::min(5, ps.hp_max - ps.hp);
            ps.hp += heal;
            // Shrine XP (once per floor)
            if (!shrine_xp_this_floor_) {
                shrine_xp_this_floor_ = true;
                ps.grant_xp(8);
            }
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
                                    case EffectType::BONUS_HP: rs.base_hp_max -= eff.value; rs.hp_max -= eff.value; rs.hp = std::min(rs.hp, rs.hp_max); break;
                                    case EffectType::BONUS_MP: rs.base_mp_max -= eff.value; rs.mp_max -= eff.value; rs.mp = std::min(rs.mp, rs.mp_max); break;
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

    // === KEEPER PHASE TRANSITIONS ===
    if (keeper_entity_ != 0 && world_.has<Stats>(keeper_entity_)) {
        auto& ks = world_.get<Stats>(keeper_entity_);
        int hp_pct = (ks.hp * 100) / std::max(1, ks.hp_max);
        if (keeper_phase_ == 1 && hp_pct <= 50) {
            keeper_phase_ = 2;
            trigger_screen_shake(10.0f);
            screen_flash(255, 220, 100, 120);
            log_.add("THE KEEPER SHEDS ITS ARMOR.", {255, 220, 100, 255});
            log_.add("Something faster unfolds from the shell.", {200, 180, 140, 255});
            audio_.play(SfxId::SPELL_IMPACT);
            ks.base_speed = 110;
            ks.natural_armor = 3;
        } else if (keeper_phase_ == 2 && hp_pct <= 25) {
            keeper_phase_ = 3;
            trigger_screen_shake(15.0f);
            screen_flash(255, 255, 255, 160);
            log_.add("THE RELIQUARY SPEAKS THROUGH IT.", {255, 255, 200, 255});
            log_.add("The air ignites. This is the final shape.", {255, 200, 100, 255});
            audio_.play(SfxId::SPELL_FIRE);
            ks.base_speed = 100;
            ks.base_damage = 30;
            ks.natural_armor = 4;
        }
        // Phase 3: adjacency aura (2 damage/turn if next to player)
        if (keeper_phase_ == 3 && world_.has<Position>(keeper_entity_) && world_.has<Position>(player_)) {
            auto& kp = world_.get<Position>(keeper_entity_);
            auto& pp = world_.get<Position>(player_);
            if (std::abs(kp.x - pp.x) <= 1 && std::abs(kp.y - pp.y) <= 1) {
                if (world_.has<Stats>(player_)) {
                    world_.get<Stats>(player_).hp -= 2;
                    log_.add("The Keeper's aura sears you. (2)", {255, 160, 60, 255});
                }
            }
        }
    } else if (keeper_entity_ != 0 && !world_.has<Stats>(keeper_entity_)) {
        // Keeper died
        if (keeper_phase_ > 0) {
            trigger_screen_shake(15.0f);
            screen_flash(255, 255, 255, 200);
            log_.add("THE KEEPER FALLS.", {255, 240, 180, 255});
            log_.add("The Reliquary is unguarded.", {255, 220, 100, 255});
            audio_.play(SfxId::DEATH);
            keeper_phase_ = 0;
            keeper_entity_ = 0;
        }
    }

    // Class ability turn ticks
    if (zealot_fury_turns_ > 0) zealot_fury_turns_--;
    if (revenant_fury_turns_ > 0) revenant_fury_turns_--;
    if (knight_bulwark_turns_ > 0) {
        knight_bulwark_turns_--;
        if (world_.has<Player>(player_))
            world_.get<Player>(player_).bulwark_turns = knight_bulwark_turns_;
    }
    if (knight_bulwark_cd_ > 0) knight_bulwark_cd_--;
    // Unbreakable capstone tick
    if (world_.has<Player>(player_) && world_.get<Player>(player_).unbreakable_turns > 0) {
        auto& pl = world_.get<Player>(player_);
        pl.unbreakable_turns--;
        if (pl.unbreakable_turns == 0)
            log_.add("Unbreakable fades.", {140, 120, 100, 255});
    }
    // Rogue vanish cooldown tick
    if (world_.has<Player>(player_) && world_.get<Player>(player_).vanish_cooldown > 0) {
        world_.get<Player>(player_).vanish_cooldown--;
    }
    // Druid beast form expiry
    if (druid_beast_turns_ > 0) {
        druid_beast_turns_--;
        if (druid_beast_turns_ == 0 && world_.has<Stats>(player_)) {
            auto& ps = world_.get<Stats>(player_);
            int shape_bonus = 0;
            if (world_.has<PassiveTreeState>(player_))
                shape_bonus = passive_tree::compute_bonuses(world_.get<PassiveTreeState>(player_)).shapeshift_dmg_bonus;
            ps.base_damage -= (8 + shape_bonus);
            ps.base_speed -= 30;
            log_.add("The beast recedes. You return to yourself.", {80, 160, 80, 255});
        }
    }
    // Phase walk decrement
    if (world_.has<Stats>(player_) && world_.get<Stats>(player_).phase_turns > 0) {
        world_.get<Stats>(player_).phase_turns--;
        if (world_.get<Stats>(player_).phase_turns == 0) {
            log_.add("You solidify. Walls are solid again.", {160, 120, 200, 255});
            // If stuck in a wall, teleport to nearest walkable tile
            if (world_.has<Position>(player_)) {
                auto& pp = world_.get<Position>(player_);
                if (!map_.is_walkable(pp.x, pp.y)) {
                    for (int radius = 1; radius <= 10; radius++) {
                        bool found = false;
                        for (int dx = -radius; dx <= radius && !found; dx++) {
                            for (int dy = -radius; dy <= radius && !found; dy++) {
                                if (std::abs(dx) != radius && std::abs(dy) != radius) continue;
                                int tx = pp.x + dx, ty = pp.y + dy;
                                if (map_.in_bounds(tx, ty) && map_.is_walkable(tx, ty)) {
                                    pp.x = tx; pp.y = ty;
                                    log_.add("You phase out of the stone.", {160, 120, 200, 255});
                                    found = true;
                                }
                            }
                        }
                        if (found) break;
                    }
                }
            }
        }
    }
    // Zhavek: permanent invisibility (re-applies each turn after being broken by attack)
    if (world_.has<GodAlignment>(player_) && world_.has<Stats>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        if (ga.god == GodId::ZHAVEK && world_.get<Stats>(player_).invisible_turns == 0)
            world_.get<Stats>(player_).invisible_turns = 2; // refreshes each turn
    }
    // === TRAIT TURN EFFECTS ===
    for (auto tid : build_traits_) {
        // Paranoid: confused 20% of turns
        if (tid == TraitId::PARANOID && rng_.chance(20) && world_.has<StatusEffects>(player_)) {
            world_.get<StatusEffects>(player_).add(StatusType::CONFUSED, 0, 1);
        }
        // Cannibal: eat corpse at your position (auto, no action cost beyond the walk)
        if (tid == TraitId::CANNIBAL && world_.has<Position>(player_) && world_.has<Stats>(player_)) {
            auto& pp = world_.get<Position>(player_);
            // Find corpse at player position (don't destroy during iteration)
            Entity corpse_to_eat = 0;
            auto& corpse_pool = world_.pool<Corpse>();
            for (size_t ci = 0; ci < corpse_pool.size(); ci++) {
                Entity ce = corpse_pool.entity_at(ci);
                if (!world_.has<Position>(ce)) continue;
                auto& cp = world_.get<Position>(ce);
                if (cp.x == pp.x && cp.y == pp.y) { corpse_to_eat = ce; break; }
            }
            if (corpse_to_eat != 0) {
                auto& ps = world_.get<Stats>(player_);
                // Yashkhet blocks all healing (including cannibal)
                bool yash_block = world_.has<GodAlignment>(player_) &&
                                  world_.get<GodAlignment>(player_).god == GodId::YASHKHET;
                if (yash_block) {
                    world_.destroy(corpse_to_eat);
                    log_.add("You devour the corpse, but Yashkhet rejects the healing.", {200, 60, 60, 255});
                    log_.add("[Yashkhet] Only blood heals.", {180, 60, 60, 255});
                } else {
                    int healed = ps.hp_max - ps.hp;
                    // Sythara halves all healing
                    if (world_.has<GodAlignment>(player_) && world_.get<GodAlignment>(player_).god == GodId::SYTHARA)
                        healed = healed / 2;
                    ps.hp = std::min(ps.hp_max, ps.hp + healed);
                    world_.destroy(corpse_to_eat);
                    char eb[64]; snprintf(eb, sizeof(eb), "You devour the corpse. (+%d HP)", healed);
                    log_.add(eb, {180, 100, 80, 255});
                }
                particles_.burst((float)pp.x, (float)pp.y, 8, 180, 60, 40, 0.08f, 0.5f, 2);
            }
        }
        // Vampiric: overworld drains 1 HP/5 turns
        if (tid == TraitId::VAMPIRIC && dungeon_level_ <= 0 && game_turn_ % 5 == 0 && world_.has<Stats>(player_)) {
            world_.get<Stats>(player_).hp -= 1;
            if (game_turn_ % 25 == 0)
                log_.add("Sunlight burns. Get underground.", {200, 180, 100, 255});
        }
        // Bloodletter: slow permanent bleed (1 HP per 8 turns)
        if (tid == TraitId::BLOODLETTER && world_.has<Stats>(player_) && game_turn_ % 8 == 0) {
            world_.get<Stats>(player_).hp -= 1;
        }
    }

    // === GOD PASSIVE TURN EFFECTS ===
    if (world_.has<GodAlignment>(player_) && world_.has<Stats>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        auto& ps = world_.get<Stats>(player_);
        auto& pp = world_.get<Position>(player_);

        // Ixuul: immune to ALL status effects (clear every turn)
        if (ga.god == GodId::IXUUL && world_.has<StatusEffects>(player_)) {
            auto& fx = world_.get<StatusEffects>(player_);
            if (!fx.effects.empty()) {
                fx.effects.clear(); // chaos protects from external influence
            }
        }
        // Ixuul: random stat mutation every 50/80 turns
        if (ga.god == GodId::IXUUL) {
            if (game_turn_ % 50 == 0) {
                auto attr = static_cast<Attr>(rng_.range(0, 5));
                ps.set_attr(attr, ps.attr(attr) + 1);
                particles_.burst((float)pp.x, (float)pp.y, 6, 180, 80, 255, 0.08f, 0.6f, 2);
            }
            if (game_turn_ % 80 == 0) {
                auto attr = static_cast<Attr>(rng_.range(0, 5));
                ps.set_attr(attr, std::max(3, ps.attr(attr) - 1));
                particles_.burst((float)pp.x, (float)pp.y, 4, 100, 40, 160, 0.06f, 0.4f, 2);
            }
        }

        // Soleth: 2 damage every 5 turns in deep dungeon darkness (depth 3+)
        if (ga.god == GodId::SOLETH && dungeon_level_ >= 3 && game_turn_ % 5 == 0) {
            ps.hp -= 2;
            if (game_turn_ % 20 == 0)
                log_.add("The darkness burns. Soleth demands light.", {255, 200, 80, 255});
            particles_.burn_effect((float)pp.x, (float)pp.y);
        }

        // Gathruun: surface penalty (-3 damage applied in combat, 2x energy cost here)
        // Surface damage penalty is handled in combat section below

        // Khael: regen 1 HP/5 turns on surface (overworld)
        if (ga.god == GodId::KHAEL && dungeon_level_ <= 0 && game_turn_ % 5 == 0) {
            if (ps.hp < ps.hp_max) {
                ps.hp++;
                if (game_turn_ % 20 == 0)
                    particles_.rise((float)pp.x, (float)pp.y, 3, 60, 180, 60, 0.5f, 2);
            }
        }

        // Thalara: 3 damage/turn in molten zones
        if (ga.god == GodId::THALARA && dungeon_level_ > 0) {
            std::string zk2;
            if (current_dungeon_idx_ >= 0 &&
                current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()))
                zk2 = dungeon_registry_[current_dungeon_idx_].zone;
            if (zk2 == "molten" && game_turn_ % 3 == 0) {
                ps.hp -= 3;
                if (game_turn_ % 15 == 0)
                    log_.add("The heat sears you. Thalara and fire do not mix.", {255, 140, 80, 255});
            }
        }

        // Lethis: enemies that haven't seen you for 5 turns forget you
        if (ga.god == GodId::LETHIS) {
            auto& ai_pool = world_.pool<AI>();
            for (size_t ai = 0; ai < ai_pool.size(); ai++) {
                auto& aic = ai_pool.at_index(ai);
                if (aic.friendly) continue;
                if (aic.state == AIState::HUNTING && aic.alert_turns >= 5) {
                    Entity ae = ai_pool.entity_at(ai);
                    if (!world_.has<Position>(ae)) continue;
                    auto& ap = world_.get<Position>(ae);
                    int d = std::max(std::abs(ap.x - pp.x), std::abs(ap.y - pp.y));
                    // If not adjacent and haven't been hit, forget
                    if (d > 2) {
                        aic.state = AIState::IDLE;
                        aic.alert_turns = 0;
                    }
                }
            }
        }

        // Thessarka FOV reduction moved to after equipment recalc (see below)

        // Morreth: speed drops to 60 when enemies visible (can't flee)
        if (ga.god == GodId::MORRETH && world_.has<Energy>(player_)) {
            bool enemies_visible = false;
            auto& ai_pool = world_.pool<AI>();
            for (size_t ai = 0; ai < ai_pool.size(); ai++) {
                if (ai_pool.at_index(ai).friendly) continue;
                Entity ae = ai_pool.entity_at(ai);
                if (!world_.has<Position>(ae)) continue;
                auto& ep = world_.get<Position>(ae);
                if (map_.in_bounds(ep.x, ep.y) && map_.at(ep.x, ep.y).visible) {
                    enemies_visible = true; break;
                }
            }
            auto& en = world_.get<Energy>(player_);
            en.speed = enemies_visible ? 60 : 100;
        }
    }

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

    // Drain pending kill names for meta/bestiary (covers spell, prayer, chain lightning, etc.)
    for (auto& kname : world_.pending_kill_names) {
        if (kname.empty()) continue;
        // Bestiary
        auto& entry = bestiary_[kname];
        if (entry.name.empty()) entry.name = kname;
        entry.kills++;
        // Meta
        meta_.total_kills++;
        run_kills_++;
        if (kname == "dragon") meta_.killed_dragon = true;
        if (is_undead(kname.c_str())) meta_.total_undead_kills++;
        if (kname == "giant rat") journal_.add_progress(QuestId::SQ_RAT_CELLAR);
        // Dynamic quest kill tracking
        auto& dq_pool = world_.pool<DynamicQuest>();
        for (size_t di = 0; di < dq_pool.size(); di++) {
            auto& dq = dq_pool.at_index(di);
            if (dq.accepted && !dq.completed && dq.kills_needed > 0 && dq.kill_type == kname) {
                dq.kills_done++;
            }
        }
    }
    world_.pending_kill_names.clear();

    // Exploration XP: grant XP for entering new rooms in dungeons
    if (dungeon_level_ > 0 && world_.has<Position>(player_) && world_.has<Stats>(player_)) {
        auto& pp = world_.get<Position>(player_);
        for (size_t ri = 0; ri < rooms_.size(); ri++) {
            auto& r = rooms_[ri];
            if (pp.x >= r.x && pp.x < r.x + r.w && pp.y >= r.y && pp.y < r.y + r.h) {
                if (rooms_explored_.find(static_cast<int>(ri)) == rooms_explored_.end()) {
                    rooms_explored_.insert(static_cast<int>(ri));
                    int xp = 3 + dungeon_level_;
                    world_.get<Stats>(player_).grant_xp(xp);
                    // Named room descriptions based on zone + room size
                    if (ri > 0) { // skip start room
                        std::string zk;
                        if (current_dungeon_idx_ >= 0 &&
                            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()))
                            zk = dungeon_registry_[current_dungeon_idx_].zone;
                        bool large = (r.w >= 9 && r.h >= 9);
                        unsigned rh = static_cast<unsigned>(r.x * 31 + r.y * 17 + ri * 7);
                        const char* rname = nullptr;
                        if (zk == "warrens") {
                            static const char* W_SM[] = {"A narrow burrow.", "Roots hang from the ceiling.", "Rat droppings everywhere."};
                            static const char* W_LG[] = {"A wide nest chamber.", "The floor is slick with slime.", "Something large lived here."};
                            rname = large ? W_LG[rh % 3] : W_SM[rh % 3];
                        } else if (zk == "stonekeep") {
                            static const char* S_SM[] = {"A guard post.", "Old weapon racks line the walls.", "Arrow slits face the corridor."};
                            static const char* S_LG[] = {"A garrison hall.", "A war room. Maps rotted on the table.", "The barracks. Bunks collapsed."};
                            rname = large ? S_LG[rh % 3] : S_SM[rh % 3];
                        } else if (zk == "catacombs") {
                            static const char* C_SM[] = {"A burial niche.", "Names carved in every surface.", "Offerings left for the dead."};
                            static const char* C_LG[] = {"An ossuary. Bones stacked floor to ceiling.", "A sealed tomb, now open.", "A funerary chapel."};
                            rname = large ? C_LG[rh % 3] : C_SM[rh % 3];
                        } else if (zk == "molten") {
                            static const char* M_SM[] = {"A heat vent.", "Slag crusts the floor.", "The stone glows dull red."};
                            static const char* M_LG[] = {"A magma chamber.", "A collapsed forge.", "An ore processing hall."};
                            rname = large ? M_LG[rh % 3] : M_SM[rh % 3];
                        } else if (zk == "sunken") {
                            static const char* U_SM[] = {"Ankle-deep water.", "Waterlogged shelves.", "The walls weep moisture."};
                            static const char* U_LG[] = {"A flooded cistern.", "A drowned library.", "A reservoir. The water is black."};
                            rname = large ? U_LG[rh % 3] : U_SM[rh % 3];
                        } else if (zk == "deep_halls") {
                            static const char* D_SM[] = {"A carved alcove.", "The ceiling vanishes above.", "Old mining supports."};
                            static const char* D_LG[] = {"A cathedral-sized chamber.", "A throne room, empty.", "Pillars taller than trees."};
                            rname = large ? D_LG[rh % 3] : D_SM[rh % 3];
                        } else if (zk == "sepulchre") {
                            static const char* P_SM[] = {"The walls pulse faintly.", "Inscriptions you can't read.", "Something was kept here."};
                            static const char* P_LG[] = {"A ritual chamber.", "The geometry is wrong.", "An altar to nothing you recognize."};
                            rname = large ? P_LG[rh % 3] : P_SM[rh % 3];
                        }
                        if (rname) log_.add(rname, {140, 135, 125, 255});
                    }
                }
                break;
            }
        }
    }

    // Recalculate equipment-derived stats
    if (world_.has<Stats>(player_) && world_.has<Inventory>(player_)) {
        auto& pstats = world_.get<Stats>(player_);
        auto& pinv = world_.get<Inventory>(player_);

        // Reset equipment bonuses and god-derived armor
        pstats.fov_bonus = 0;
        pstats.equip_str = 0;
        pstats.equip_dex = 0;
        pstats.equip_con = 0;
        pstats.equip_hp = 0;
        pstats.equip_mp = 0;
        pstats.equip_speed = 0;
        pstats.god_armor = 0; // reset god armor before reapplying

        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            Entity eq = pinv.equipped[s];
            if (eq == NULL_ENTITY || !world_.has<Item>(eq)) continue;
            auto& item = world_.get<Item>(eq);

            // Unique effects
            if (item.unique_effect == UniqueEffect::LIGHT_RADIUS)
                pstats.fov_bonus += 2;

            // Attribute bonuses from equipment
            pstats.equip_str += item.str_bonus;
            pstats.equip_dex += item.dex_bonus;
            pstats.equip_con += item.con_bonus;
            pstats.equip_hp += item.affix_hp;
            pstats.equip_mp += item.affix_mp;
            pstats.equip_speed += item.affix_speed;
        }

        // Ossren: -2 speed per equipped slot (weight of permanence)
        if (world_.has<GodAlignment>(player_)) {
            auto pgod = world_.get<GodAlignment>(player_).god;
            // Morreth: +3 armor
            if (pgod == GodId::MORRETH) pstats.god_armor += 3;
            // Gathruun: +5 armor
            if (pgod == GodId::GATHRUUN) pstats.god_armor += 5;
            // Ossren: -2 speed per equipped slot, +1 armor per equipped slot
            if (pgod == GodId::OSSREN) {
                int equipped_count = 0;
                for (int s = 0; s < EQUIP_SLOT_COUNT; s++)
                    if (pinv.equipped[s] != NULL_ENTITY) equipped_count++;
                pstats.equip_speed -= equipped_count * 2;
                pstats.god_armor += equipped_count;
            }
            // Thalara: +20 speed
            if (pgod == GodId::THALARA) {
                pstats.equip_speed += 20;
            }
            // Gathruun: surface = 2x energy cost (halve speed)
            if (pgod == GodId::GATHRUUN && dungeon_level_ <= 0) {
                pstats.equip_speed -= pstats.base_speed / 2;
            }
        }

        // Apply HP/MP bonuses to max values
        // hp_max = base_hp_max + equip_hp + CON bonus from equipment
        // (CON bonus: each point of CON gives +2 hp_max)
        pstats.hp_max = pstats.base_hp_max + pstats.equip_hp + pstats.equip_con * 2;
        pstats.mp_max = pstats.base_mp_max + pstats.equip_mp;

        // Clamp current values
        if (pstats.hp > pstats.hp_max) pstats.hp = pstats.hp_max;
        if (pstats.mp > pstats.mp_max) pstats.mp = pstats.mp_max;

        // Sync Energy speed with effective speed (base + equip bonuses)
        if (world_.has<Energy>(player_)) {
            auto& en = world_.get<Energy>(player_);
            int eff_speed = pstats.base_speed + pstats.equip_speed;
            // Morreth override is handled separately in god passives section
            if (!world_.has<GodAlignment>(player_) || world_.get<GodAlignment>(player_).god != GodId::MORRETH)
                en.speed = std::max(30, eff_speed);
        }

        // Thessarka: FOV reduced when enemies visible (applied AFTER equip recalc)
        if (world_.has<GodAlignment>(player_) && world_.get<GodAlignment>(player_).god == GodId::THESSARKA) {
            bool enemies_near = false;
            auto& ai_pool_th = world_.pool<AI>();
            for (size_t ai = 0; ai < ai_pool_th.size(); ai++) {
                if (ai_pool_th.at_index(ai).friendly) continue;
                Entity ae = ai_pool_th.entity_at(ai);
                if (!world_.has<Position>(ae)) continue;
                auto& ep = world_.get<Position>(ae);
                if (map_.in_bounds(ep.x, ep.y) && map_.at(ep.x, ep.y).visible) {
                    enemies_near = true; break;
                }
            }
            if (enemies_near) pstats.fov_bonus -= 6;
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
                    if (world_.has<Player>(player_))
                        world_.get<Player>(player_).beast_form_turns = 0;
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
            // Revenant: survive lethal once per floor
            if (!revenant_saved_this_floor_ && world_.has<Player>(player_) &&
                world_.get<Player>(player_).class_id == ClassId::REVENANT) {
                revenant_saved_this_floor_ = true;
                // Heavy armor: heal to 25% instead of 1 HP
                bool heavy = false;
                if (world_.has<Inventory>(player_)) {
                    Entity chest = world_.get<Inventory>(player_).get_equipped(EquipSlot::CHEST);
                    if (chest != NULL_ENTITY && world_.has<Item>(chest) &&
                        (world_.get<Item>(chest).tags & TAG_HEAVY_ARMOR))
                        heavy = true;
                }
                stats.hp = heavy ? stats.hp_max / 4 : 1;
                log_.add("Death refused. You endure.", {180, 100, 100, 255});
                audio_.play(SfxId::PRAYER);
                // Lv5: UNDYING FURY (+100% damage for 5 turns after death save)
                if (stats.level >= 5) {
                    revenant_fury_turns_ = 5;
                    log_.add("UNDYING FURY!", {255, 80, 80, 255});
                    trigger_screen_shake(6.0f);
                }
                if (world_.has<Position>(player_)) {
                    auto& pp = world_.get<Position>(player_);
                    particles_.burst((float)pp.x, (float)pp.y, 12, 160, 60, 60, 0.1f, 0.8f, 3);
                    screen_flash(100, 30, 30, 100);
                }
                // Don't die
            } else {
                state_ = GameState::DEAD;
                end_screen_time_ = SDL_GetTicks();
                if (dungeon_level_ >= 4) meta_.died_deep = true;
                update_meta_on_end();
                audio_.play(SfxId::DEATH);
                audio_.stop_all_ambient(500);
                audio_.play_music(MusicId::DEATH, 1500);
                return;
            }
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
    ai::process(world_, map_, player_, rng_, log_, sneaking_);

    // Enemy spell VFX: scan for entities that cast spells this tick
    {
        auto& ai_pool = world_.pool<AI>();
        auto& pp = world_.get<Position>(player_);
        for (size_t i = 0; i < ai_pool.size(); i++) {
            auto& aic = ai_pool.at_index(i);
            if (aic.last_spell == AI::SpellVFX::NONE) continue;
            Entity ae = ai_pool.entity_at(i);
            if (!world_.has<Position>(ae)) { aic.last_spell = AI::SpellVFX::NONE; continue; }
            auto& ap = world_.get<Position>(ae);
            float fx = (float)ap.x, fy = (float)ap.y;
            float px = (float)pp.x, py = (float)pp.y;
            switch (aic.last_spell) {
                case AI::SpellVFX::DRAIN:
                    // Purple bolt from caster to player + screen flash
                    particles_.projectile(fx, fy, px, py, 10, 160, 60, 220, 0.2f, 3);
                    particles_.burst(px, py, 6, 140, 40, 200, 0.1f, 0.5f, 2);
                    screen_flash(100, 40, 160, 60);
                    audio_.play(SfxId::SPELL);
                    break;
                case AI::SpellVFX::SUMMON:
                    // Dark burst at caster position
                    particles_.burst(fx, fy, 12, 120, 80, 180, 0.12f, 0.8f, 3);
                    particles_.rise(fx, fy, 6, 160, 100, 220, 0.8f, 3);
                    audio_.play(SfxId::SPELL_IMPACT);
                    break;
                case AI::SpellVFX::HEAL_ALLY:
                    // Green glow at caster
                    particles_.burst(fx, fy, 8, 80, 220, 80, 0.1f, 0.6f, 2);
                    audio_.play(SfxId::HEAL);
                    break;
                case AI::SpellVFX::BUFF_ALLY:
                    // Orange glow at caster
                    particles_.rise(fx, fy, 6, 255, 200, 80, 0.6f, 2);
                    audio_.play(SfxId::SPELL_BUFF);
                    break;
                case AI::SpellVFX::BREATH_FIRE:
                    particles_.spell_fire(fx, fy);
                    particles_.burst(px, py, 10, 255, 120, 40, 0.15f, 0.8f, 3);
                    screen_flash(200, 80, 20, 60);
                    trigger_screen_shake(4.0f);
                    audio_.play(SfxId::SPELL_FIRE);
                    break;
                case AI::SpellVFX::BREATH_ICE:
                    particles_.burst(px, py, 10, 140, 200, 255, 0.12f, 0.7f, 3);
                    screen_flash(80, 120, 200, 50);
                    audio_.play(SfxId::SPELL_ICE);
                    break;
                default: break;
            }
            aic.last_spell = AI::SpellVFX::NONE;
        }
    }

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
        // Distance scaling for rewards (further from start = better rewards)
        float ow_dist = std::sqrt(static_cast<float>((pp.x - 500) * (pp.x - 500) +
                                                       (pp.y - 375) * (pp.y - 375)));
        int ow_scale = std::max(1, static_cast<int>(1.0f + ow_dist / 100.0f)); // 1x near, 3-4x at edges
        int roll = rng_.range(1, 100);

        if (roll <= 25) {
            // Merchant caravan: shop scales with distance + level
            int caravan_diff = std::max(ps.level, ow_scale * 2);
            int price_mult = 120 + caravan_diff * 5; // caravans charge a premium
            GodId province = GodId::NONE;
            if (world_.has<GodAlignment>(player_))
                province = world_.get<GodAlignment>(player_).god;
            log_.add("A merchant caravan passes. They spread their wares.", {200, 190, 140, 255});
            shop_screen_.open(player_, world_, rng_, &gold_, caravan_diff, price_mult, province);
        } else if (roll <= 45) {
            // Wounded traveler
            bool can_heal = false;
            if (world_.has<Skills>(player_)) {
                int heal_lv = world_.get<Skills>(player_).get_level(SkillId::HEALING);
                int nat_lv = world_.get<Skills>(player_).get_level(SkillId::NATURE_MAGIC);
                if (heal_lv >= 10 || nat_lv >= 10) can_heal = true;
            }
            if (can_heal) {
                int reward = rng_.range(15, 40) * ow_scale;
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
                    int favor_gain = 2 + ow_scale;
                    int shrine_heal = 5 + ow_scale * 5;
                    god_system::adjust_favor(world_, player_, log_, favor_gain);
                    ps.hp = std::min(ps.hp + shrine_heal, ps.hp_max);
                    char sbuf[96];
                    snprintf(sbuf, sizeof(sbuf), "You find a roadside shrine to %s. (+%d favor, +%d HP)", ginfo.name, favor_gain, shrine_heal);
                    log_.add(sbuf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                } else {
                    log_.add("You pass an old shrine. It means nothing to you.", {130, 130, 120, 255});
                }
            }
        } else if (roll <= 85) {
            // Lost supply cache: free consumables (scales with distance)
            int heal = rng_.range(10, 25) * ow_scale;
            ps.hp = std::min(ps.hp + heal, ps.hp_max);
            int gfind = rng_.range(5, 20) * ow_scale;
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
            // Defensive ability audio
            if (mresult.parried) audio_.play(SfxId::BLOCK1);
            if (mresult.shield_blocked) audio_.play(SfxId::BLOCK2);
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
                        if (tid == TraitId::LUCKY && rng_.chance(15)) {
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
                // Lucky downside: crits against you deal 3x (extra 2x on top of normal crit)
                if (mresult.hit && mresult.critical && world_.has<Stats>(player_)) {
                    for (auto tid : build_traits_) {
                        if (tid == TraitId::LUCKY) {
                            world_.get<Stats>(player_).hp -= mresult.damage; // extra damage = triple total
                            break;
                        }
                    }
                }
                // (Bloodletter passive drain handled in process_turn, not per-hit)
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
                    particles_.crit_flash(pp.x, pp.y);
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
                // Heretic: Godless Resolve — 15% chance to resist any status
                bool heretic_resisted = false;
                if (world_.has<Player>(player_) &&
                    world_.get<Player>(player_).class_id == ClassId::HERETIC && rng_.chance(15)) {
                    heretic_resisted = true;
                }
                if (heretic_resisted) {
                    // Skip status entirely
                } else {
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
                } // end else (heretic didn't resist)
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
                    // Sythara: immune to disease (plague-touched body)
                    bool sythara_immune = world_.has<GodAlignment>(player_) &&
                                          world_.get<GodAlignment>(player_).god == GodId::SYTHARA;
                    if (chaos_immune) {
                        log_.add("Your inoculated body rejects the infection.", {120, 200, 60, 255});
                    } else if (sythara_immune) {
                        log_.add("[Sythara] Your plague-touched body rejects the infection.", {120, 180, 60, 255});
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
                audio_.play(SfxId::SPELL_IMPACT);
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
                    if (!immune) {
                        world_.get<StatusEffects>(player_).add(StatusType::FEARED, 0, 3);
                        audio_.play(SfxId::CURSE);
                    } else log_.add("Your iron will resists the fear.", {200, 200, 140, 255});
                }
                log_.add("The death knight's presence freezes your courage.", {160, 100, 160, 255});
            } else if (mstats.name == "naga" && dist <= 4 && rng_.chance(30)) {
                // Naga gaze — applies STUNNED (Glass Jaw extends by 2)
                if (world_.has<StatusEffects>(player_)) {
                    int dur = 2;
                    for (auto tid : build_traits_) if (tid == TraitId::PARANOID) dur += 2; // Glass Jaw
                    world_.get<StatusEffects>(player_).add(StatusType::STUNNED, 0, dur);
                    audio_.play(SfxId::SPELL_IMPACT);
                }
                log_.add("The naga's gaze locks your muscles.", {255, 255, 100, 255});
            } else if (mstats.name == "wraith" && dist <= 3 && rng_.chance(30)) {
                // Wraith wail — applies CONFUSED (blocked by Iron Willed)
                if (world_.has<StatusEffects>(player_)) {
                    bool immune = false;
                    for (auto tid : build_traits_) if (get_trait_info(tid).immune_confuse) immune = true;
                    if (!immune) {
                        int dur = 4;
                        for (auto tid : build_traits_) if (tid == TraitId::SPELL_GLUTTON) dur += 2;
                        world_.get<StatusEffects>(player_).add(StatusType::CONFUSED, 0, dur);
                        audio_.play(SfxId::CURSE);
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
                audio_.play(SfxId::SPELL_FREEZE);
                char mbuf[128];
                snprintf(mbuf, sizeof(mbuf), "A wave of cold hits you. (%d)", dmg);
                log_.add(mbuf, {140, 200, 255, 255});
            } else if (mstats.name == "basilisk" && dist <= 4 && rng_.chance(20)) {
                // Basilisk gaze — BLIND
                if (world_.has<StatusEffects>(player_)) {
                    world_.get<StatusEffects>(player_).add(StatusType::BLIND, 0, 5);
                    audio_.play(SfxId::CURSE);
                }
                log_.add("The basilisk's gaze sears your vision.", {120, 120, 120, 255});
            } else if (mstats.name == "dragon" && dist <= 3 && rng_.chance(35)) {
                // Dragon breathes fire
                int dmg = 10 + rng_.range(0, 10);
                world_.get<Stats>(player_).hp -= dmg;
                audio_.play(SfxId::SPELL_FIRE);
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
            // Revenant death save (same check as primary death path)
            if (!revenant_saved_this_floor_ && world_.has<Player>(player_) &&
                world_.get<Player>(player_).class_id == ClassId::REVENANT) {
                revenant_saved_this_floor_ = true;
                world_.get<Stats>(player_).hp = 1;
                log_.add("Death refused. You endure.", {180, 100, 100, 255});
                audio_.play(SfxId::PRAYER);
            } else {
                state_ = GameState::DEAD;
                end_screen_time_ = SDL_GetTicks();
                if (dungeon_level_ >= 4) meta_.died_deep = true;
                update_meta_on_end();
                audio_.stop_all_ambient(500);
                audio_.play_music(MusicId::DEATH, 1500);
                return;
            }
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
            // Revenant death save (status.cpp death path)
            if (!revenant_saved_this_floor_ && world_.has<Player>(player_) &&
                world_.get<Player>(player_).class_id == ClassId::REVENANT &&
                world_.has<Stats>(player_)) {
                revenant_saved_this_floor_ = true;
                world_.get<Stats>(player_).hp = 1;
                log_.add("Death refused. You endure.", {180, 100, 100, 255});
                audio_.play(SfxId::PRAYER);
            } else {
                death_cause_ = fx_result.death_cause;
                state_ = GameState::DEAD;
                end_screen_time_ = SDL_GetTicks();
                if (dungeon_level_ >= 4) meta_.died_deep = true;
                update_meta_on_end();
                audio_.stop_all_ambient(500);
                audio_.play_music(MusicId::DEATH, 1500);
            }
        }
    }

    // Sepulchre ambient messages
    sepulchre_ambient();

    particles_.update();
    log_.set_turn(game_turn_);

    // Low HP warning (once per run)
    if (!tips_shown_.first_low_hp && world_.has<Stats>(player_)) {
        auto& pst = world_.get<Stats>(player_);
        if (pst.hp > 0 && pst.hp * 100 / std::max(1, pst.hp_max) <= 30) {
            tips_shown_.first_low_hp = true;
            char tb[200];
            snprintf(tb, sizeof(tb),
                "Your HP is low.\n\n"
                "%s - Rest to heal (when no enemies are near)\n"
                "Use potions from your inventory for emergencies.\n"
                "Retreat to the surface if overwhelmed.",
                input_glyphs_.label(Action::REST).c_str());
            tutorial_popup_.show("Danger", tb);
        }
    }
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
    int favor_before = 0;
    if (world_.has<GodAlignment>(player_))
        favor_before = world_.get<GodAlignment>(player_).favor;

    god_system::check_tenets(world_, player_, turn_actions_, game_turn_, log_);

    // VFX on tenet violation: god-colored flash + shake
    if (world_.has<GodAlignment>(player_)) {
        int favor_after = world_.get<GodAlignment>(player_).favor;
        if (favor_after < favor_before) {
            auto& gi = get_god_info(world_.get<GodAlignment>(player_).god);
            screen_flash(gi.color.r, gi.color.g, gi.color.b, 80.0f);
            // God-colored particles burst from player
            if (world_.has<Position>(player_)) {
                auto& pp = world_.get<Position>(player_);
                particles_.burst((float)pp.x, (float)pp.y, 10,
                                 gi.color.r, gi.color.g, gi.color.b, 0.1f, 0.6f, 3);
            }
        }
    }
}

void Engine::execute_prayer(int prayer_idx) {
    player_acted_ = god_system::execute_prayer(world_, player_, map_, rng_,
                                                log_, audio_, particles_,
                                                camera_, prayer_idx);
}

void Engine::fire_ranged() {
    if (!world_.has<Inventory>(player_) || !world_.has<Position>(player_)) return;

    // Morreth: no ranged weapons allowed
    if (world_.has<GodAlignment>(player_) && world_.get<GodAlignment>(player_).god == GodId::MORRETH) {
        log_.add("Morreth forbids striking from distance. Close and fight.", {200, 180, 100, 255});
        return;
    }

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

    // Use manually selected target if valid, else fall back to nearest
    Entity target = 0;
    if (ranged_target_ != 0 && world_.has<Stats>(ranged_target_) &&
        world_.has<Position>(ranged_target_) && world_.get<Stats>(ranged_target_).hp > 0) {
        auto& tp = world_.get<Position>(ranged_target_);
        if (map_.in_bounds(tp.x, tp.y) && map_.at(tp.x, tp.y).visible) {
            auto& pp = world_.get<Position>(player_);
            int d = std::max(std::abs(tp.x - pp.x), std::abs(tp.y - pp.y));
            if (d <= weapon.range) target = ranged_target_;
        }
    }
    if (target == 0) target = magic::nearest_enemy(world_, player_, map_, weapon.range);
    if (target == 0) {
        log_.add("No target in range.", {150, 140, 130, 255});
        return;
    }
    ranged_target_ = 0; // clear after firing
    target_cycle_idx_ = -1;

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
    if (result.critical) { particles_.crit_flash(tgt_x, tgt_y); }
    if (result.killed) { audio_.play(SfxId::DEATH); particles_.death_burst(tgt_x, tgt_y); }

    // Ranger: Marked Prey applies to ranged attacks too
    if (world_.has<Player>(player_) && world_.get<Player>(player_).class_id == ClassId::RANGER) {
        // Apply mark bonus damage
        if (ranger_marked_target_ == target && result.hit && !result.killed &&
            world_.has<Stats>(target)) {
            int mark_bonus = result.damage / 2; // +50%
            // Bow bonus: +25% extra (total +75%)
            if (weapon.tags & TAG_BOW)
                mark_bonus += result.damage / 4;
            world_.get<Stats>(target).hp -= mark_bonus;
            result.damage += mark_bonus;
            char mkb[64];
            snprintf(mkb, sizeof(mkb), "Marked prey! (+%d)", mark_bonus);
            log_.add(mkb, {140, 255, 100, 255});
            particles_.burst((float)tgt_x, (float)tgt_y, 12, 100, 255, 80, 0.1f, 0.5f, 4);
            particles_.rise((float)tgt_x, (float)tgt_y, 6, 140, 255, 100, 0.4f, 3);
            if (world_.get<Stats>(target).hp <= 0) {
                combat::kill(world_, target, log_);
                result.killed = true;
                ranger_marked_target_ = 0;
                audio_.play(SfxId::DEATH); particles_.death_burst(tgt_x, tgt_y);
            }
        }
        // Auto-mark on first hit
        if (result.hit && target != ranger_marked_target_ && !result.killed) {
            ranger_marked_target_ = target;
            if (world_.has<Stats>(target)) {
                char mb[64]; snprintf(mb, sizeof(mb), "Marked: %s", world_.get<Stats>(target).name.c_str());
                log_.add(mb, {200, 255, 140, 255});
                particles_.burst((float)tgt_x, (float)tgt_y, 15, 100, 255, 80, 0.12f, 0.6f, 4);
            }
        }
    }

    // Bestiary stats (ranged has access to victim stats before combat::kill removes them)
    if (result.killed && !victim_name.empty()) {
        auto& entry = bestiary_[victim_name];
        if (entry.hp == 0) {
            entry.name = victim_name;
            entry.hp = victim_hp; entry.damage = victim_dmg;
            entry.armor = victim_arm; entry.speed = victim_spd;
        }
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
                    // Show active status effects
                    if (world_.has<StatusEffects>(e)) {
                        auto& sfx = world_.get<StatusEffects>(e);
                        if (!sfx.effects.empty()) {
                            std::string sfx_str = "  Status:";
                            for (auto& fx : sfx.effects) {
                                switch (fx.type) {
                                    case StatusType::POISON: sfx_str += " Poisoned"; break;
                                    case StatusType::BURN:   sfx_str += " Burning"; break;
                                    case StatusType::BLEED:  sfx_str += " Bleeding"; break;
                                    case StatusType::FROZEN: sfx_str += " Frozen"; break;
                                    case StatusType::STUNNED:sfx_str += " Stunned"; break;
                                    default: break;
                                }
                            }
                            if (sfx_str.size() > 9)
                                log_.add(sfx_str, {200, 180, 100, 255});
                        }
                    }
                }
                auto ins = meta_.examined_creature_names.insert(st.name);
                meta_.total_creatures_examined = static_cast<int>(meta_.examined_creature_names.size());
                // First-examine XP bonus
                if (ins.second && world_.has<Stats>(player_)) {
                    world_.get<Stats>(player_).grant_xp(5);
                }
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
                { char cbuf[128];
                  if (input_glyphs_.using_gamepad())
                      snprintf(cbuf, sizeof(cbuf), "A closed container. Press %s to open.",
                               input_glyphs_.label(Action::PICKUP).c_str());
                  else
                      snprintf(cbuf, sizeof(cbuf), "A closed container. Press g to open.");
                  log_.add(cbuf, {180, 170, 140, 255}); }
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
        case ClassId::KNIGHT:       return meta_.max_dungeon_depth >= 3;
        case ClassId::MONK:         return meta_.killed_unarmed;
        case ClassId::TEMPLAR:      return meta_.total_undead_kills >= 30;
        case ClassId::DRUID:        return meta_.total_quests_completed >= 10;
        case ClassId::WAR_CLERIC:   return meta_.total_hp_healed >= 300;
        case ClassId::WARLOCK:      return meta_.died_deep;
        case ClassId::DWARF:        return meta_.max_dungeon_depth >= 4;
        case ClassId::ELF:          return meta_.total_creatures_examined >= 15;
        case ClassId::BANDIT:       return meta_.max_gold_single_run >= 500;
        case ClassId::NECROMANCER:  return meta_.total_dark_arts_casts >= 30;
        case ClassId::SCHEMA_MONK:  return meta_.class_max_level[static_cast<int>(ClassId::MONK)] >= 12;
        case ClassId::HERETIC:      return meta_.gods_completed_count() >= GOD_COUNT;
        case ClassId::WYRMKIN:      return meta_.killed_dragon;
        case ClassId::REVENANT:     return meta_.total_deaths >= 10;
        case ClassId::SERPENTINE:   return meta_.max_diseases >= 3;
        case ClassId::TROLLBLOOD:   return meta_.max_dungeon_depth >= 4;
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
    overworld::populate(world_, map_, rng_, dungeon_registry_, overworld_cache_.entities);
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
            "Shadows shift.",
            "Scratching behind the walls.",
            "The floor is uneven.",
            "You smell old blood.",
            "A faint whisper.",
            "Your god stirs uneasily.",
            "Silence.",
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
                "Roots hang from the ceiling.", "Water drips on you.",
                "The tunnel narrows to a crawl ahead.", "Insects in the walls.",
                "An abandoned nest.", "The walls are scored with claw marks.",
                "Fungus on the ceiling.",
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
                "The heat is almost unbearable.", "Magma glows in the cracks between stones.",
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

    // Trollblood: CONSUME corpse on interact (adjacent)
    if (world_.has<Player>(player_) && world_.get<Player>(player_).class_id == ClassId::TROLLBLOOD &&
        world_.has<Stats>(player_)) {
        Entity corpse_eat = 0;
        auto& cp = world_.pool<Corpse>();
        for (size_t ci = 0; ci < cp.size(); ci++) {
            Entity ce = cp.entity_at(ci);
            if (!world_.has<Position>(ce)) continue;
            auto& cpos = world_.get<Position>(ce);
            if (std::abs(cpos.x - pos.x) <= 1 && std::abs(cpos.y - pos.y) <= 1) {
                corpse_eat = ce; break;
            }
        }
        if (corpse_eat != 0) {
            auto& ps = world_.get<Stats>(player_);
            int heal = ps.hp_max / 4;
            ps.hp = std::min(ps.hp_max, ps.hp + heal);
            auto& cpos = world_.get<Position>(corpse_eat);
            float cx = (float)cpos.x, cy = (float)cpos.y;
            world_.destroy(corpse_eat);
            // Bloody explosion VFX + sound
            particles_.burst(cx, cy, 15, 200, 40, 40, 0.15f, 0.8f, 4);
            particles_.burst(cx, cy, 8, 180, 80, 60, 0.1f, 0.5f, 3);
            particles_.blood(cx, cy);
            audio_.play(SfxId::DEATH);
            char eb[48]; snprintf(eb, sizeof(eb), "You DEVOUR the corpse. (+%d HP)", heal);
            log_.add(eb, {200, 80, 60, 255});
            // Lv5 GORGE: 3 corpses this floor = +5 permanent max HP
            if (world_.get<Stats>(player_).level >= 5) {
                trollblood_corpses_floor_++;
                if (trollblood_corpses_floor_ >= 3 && !trollblood_gorged_) {
                    trollblood_gorged_ = true;
                    auto& ps2 = world_.get<Stats>(player_);
                    ps2.hp_max += 5;
                    ps2.base_hp_max += 5;
                    ps2.hp += 5;
                    log_.add("GORGED. +5 max HP permanently.", {140, 220, 80, 255});
                    audio_.play(SfxId::LEVELUP);
                }
            }
            player_acted_ = true;
            return;
        }
    }

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
                // Use the same dialogue system as bump interaction
                open_npc_dialogue(target);
                return;
            }
        }
    }

    // 3. Stairs underfoot -> use them
    auto tile_type = map_.at(pos.x, pos.y).type;
    if (tile_type == TileType::STAIRS_DOWN || tile_type == TileType::STAIRS_UP) {
        // Reuse the stair handling by synthesizing a STAIRS_ENTER key event
        SDL_Event synth = {};
        synth.type = SDL_KEYDOWN;
        synth.key.keysym.sym = SDLK_RETURN;
        SDL_PushEvent(&synth);
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

        // First potion tip
        if (!tips_shown_.first_potion && (item.type == ItemType::POTION || item.type == ItemType::FOOD)) {
            tips_shown_.first_potion = true;
            tutorial_popup_.show("Consumables",
                "Potions and food heal you when used.\n\n"
                "Open inventory and select an item to use it.\n"
                "Potion names are randomized each run.\n"
                "Identify them by drinking or with spells.");
        }

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
        if (world_.has<Item>(e)) world_.get<Item>(e).new_pickup = true;
        inv.add(e);

        // Potions must be identified each run (no meta-save carry-over)
        // Alchemist background still auto-IDs below

        // Auto-identify potions: Alchemist background OR Divination 25+
        if (item.type == ItemType::POTION && !item.identified) {
            bool can_id = (background_ == BackgroundId::ALCHEMIST);
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
            if (qid == QuestId::MQ_09_CLAIM_RELIQUARY) {
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
    if (background_ == BackgroundId::FARMER) max_rests = 3;

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
    // Church task: rest tracking (Lethis)
    if (world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        if (ga.task_active && !ga.task_complete) {
            auto next = static_cast<ChurchRank>(ga.church_rank + 1);
            auto& task = get_church_task(ga.god, next);
            if (task.items_donated > 0 && ga.god == GodId::LETHIS) {
                ga.task_progress++;
                if (ga.task_progress >= task.items_donated) {
                    ga.task_complete = true; ga.task_active = false;
                    log_.add("Church task complete. Return to the church.", {255, 220, 80, 255});
                }
            }
        }
    }

    // Heal HP (depth-scaled percentage) and restore all MP
    bool is_vampire = world_.has<Diseases>(player_) &&
                      world_.get<Diseases>(player_).has(DiseaseId::VAMPIRISM);

    // Yashkhet: CANNOT heal from rest (only lifesteal)
    bool yashkhet_block = world_.has<GodAlignment>(player_) &&
                          world_.get<GodAlignment>(player_).god == GodId::YASHKHET;
    // Thalara: dry rest heals no HP (MP only)
    bool thalara_dry = false;
    if (world_.has<GodAlignment>(player_) && world_.get<GodAlignment>(player_).god == GodId::THALARA) {
        // Check if in a water-themed zone (sunken = ok, others = dry)
        std::string rzone;
        if (current_dungeon_idx_ >= 0 && current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()))
            rzone = dungeon_registry_[current_dungeon_idx_].zone;
        if (rzone != "sunken") thalara_dry = true;
    }

    int hp_actual = 0;
    int mp_actual = 0;

    // Sythara: ALL healing halved
    bool sythara_halve = world_.has<GodAlignment>(player_) &&
                         world_.get<GodAlignment>(player_).god == GodId::SYTHARA;

    if (yashkhet_block) {
        // Yashkhet forbids rest healing
        log_.add("Yashkhet rejects your rest. Only blood heals.", {200, 60, 60, 255});
    } else if (thalara_dry) {
        // Thalara: dry rest only restores MP
        log_.add("The land is too dry. Thalara grants no healing here.", {80, 160, 200, 255});
    } else if (!is_vampire) {
        // Rest heals a percentage of max HP (scales down with depth)
        // Overworld: 100%, Depth 1: 80%, Depth 2: 70%, Depth 3: 60%, Depth 4: 50%
        int heal_pct = (dungeon_level_ <= 0) ? 100 : std::max(50, 90 - dungeon_level_ * 10);
        // Lethis passive: full rest heal always
        if (world_.has<GodAlignment>(player_) && world_.get<GodAlignment>(player_).god == GodId::LETHIS)
            heal_pct = 100;
        int max_heal = stats.hp_max * heal_pct / 100;
        hp_actual = std::min(max_heal, stats.hp_max - stats.hp);
        if (sythara_halve) hp_actual = hp_actual / 2;
        stats.hp = std::min(stats.hp_max, stats.hp + hp_actual);
    }
    if (stats.mp_max > 0) {
        mp_actual = stats.mp_max - stats.mp;
        stats.mp = stats.mp_max;
    }
    if (hp_actual > 0) meta_.total_hp_healed += hp_actual;

    // Yashkhet tenet: healed above 75%
    if (stats.hp * 4 > stats.hp_max * 3)
        turn_actions_.healed_above_75pct = true;

    // Costs 10 turns (reduced by rest efficiency from passive tree)
    int rest_turns = 10;
    if (world_.has<PassiveTreeState>(player_)) {
        int eff = passive_tree::compute_bonuses(world_.get<PassiveTreeState>(player_)).rest_efficiency;
        if (eff > 0) rest_turns = std::max(4, rest_turns * (100 - eff) / 100);
    }
    game_turn_ += rest_turns;

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
        snprintf(buf, sizeof(buf), "You rest. (+%d HP, +%d MP)", hp_actual, mp_actual);
    log_.add(buf, {100, 220, 100, 255});

    if (!tips_shown_.first_rest) {
        tips_shown_.first_rest = true;
        tutorial_popup_.show("Resting",
            "Resting heals HP and restores MP.\n\n"
            "In dungeons, you have limited rests per floor.\n"
            "Deeper floors heal less. Plan your fights.");
    }

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
            // God material restrictions on equip
            if (world_.has<GodAlignment>(player_) && !inv.is_equipped(item_e)) {
                auto& ga = world_.get<GodAlignment>(player_);
                bool is_metal = (item.material == MaterialType::IRON ||
                                 item.material == MaterialType::SILVER ||
                                 item.material == MaterialType::MITHRIL ||
                                 item.material == MaterialType::ADAMANTINE);
                bool is_armor = (item.type == ItemType::ARMOR_CHEST || item.type == ItemType::ARMOR_HEAD ||
                                 item.type == ItemType::ARMOR_HANDS || item.type == ItemType::ARMOR_FEET);
                bool is_weapon = (item.type == ItemType::WEAPON);
                bool is_heavy_armor = is_armor && (item.armor_bonus >= 3); // chain+ is heavy

                // Vethrik: no metal armor
                if (ga.god == GodId::VETHRIK && is_armor && is_metal && item.material != MaterialType::BONE) {
                    log_.add("[Vethrik] Metal armor is forbidden. Only bone protects the dead.", {160, 160, 200, 255});
                    break;
                }
                // Khael: no metal weapons
                if (ga.god == GodId::KHAEL && is_weapon && is_metal && item.material != MaterialType::BONE) {
                    log_.add("[Khael] Metal weapons offend nature. Use wood or bone.", {80, 200, 80, 255});
                    break;
                }
                // Morreth: no ranged weapons
                if (ga.god == GodId::MORRETH && is_weapon && item.range > 0) {
                    log_.add("[Morreth] The Iron Father demands you fight up close.", {200, 180, 140, 255});
                    break;
                }
                // Zhavek: heavy armor = instant excommunication
                if (ga.god == GodId::ZHAVEK && is_heavy_armor) {
                    log_.add("[Zhavek] Heavy armor shatters your bond with shadow.", {60, 60, 100, 255});
                    ga.favor = -100;
                    log_.add("EXCOMMUNICATED.", {255, 40, 40, 255});
                    trigger_screen_shake(8.0f);
                    screen_flash(60, 60, 100, 120);
                    break;
                }
            }
            // Avatar of the Wild keystone: no armor
            if (!inv.is_equipped(item_e) && world_.has<PassiveTreeState>(player_)) {
                bool is_armor_slot = (item.type == ItemType::ARMOR_CHEST || item.type == ItemType::ARMOR_HEAD ||
                                      item.type == ItemType::ARMOR_HANDS || item.type == ItemType::ARMOR_FEET);
                if (is_armor_slot && passive_tree::compute_bonuses(world_.get<PassiveTreeState>(player_)).avatar_of_wild) {
                    log_.add("The wild rejects armor. Your summons are your shield.", {80, 200, 80, 255});
                    break;
                }
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
                        if (ga.god == GodId::YASHKHET) {
                            heal_amt = 0;
                            log_.add("[Yashkhet] Only blood heals. Potions are rejected.", {200, 60, 60, 255});
                        }
                        else if (ga.god == GodId::SYTHARA) heal_amt = heal_amt / 2;
                    }
                    // Berserker: cannot use healing items at all
                    for (auto tid : build_traits_) {
                        if (tid == TraitId::BERSERKER) {
                            log_.add("The berserker rage rejects healing.", {200, 80, 80, 255});
                            heal_amt = 0; break;
                        }
                    }
                    // Passive tree: potion effectiveness bonus
                    if (heal_amt > 0 && world_.has<PassiveTreeState>(player_)) {
                        int eff = passive_tree::compute_bonuses(world_.get<PassiveTreeState>(player_)).potion_effectiveness;
                        if (eff > 0) heal_amt = heal_amt * (100 + eff) / 100;
                    }
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
                        // Disgraced Scholar passive: tomes never fail
                        int fail_chance = (background_ == BackgroundId::SCHOLAR)
                            ? 0
                            : std::max(5, 60 - world_.get<Stats>(player_).attr(Attr::INT) * 2);
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
                            if (!tips_shown_.first_spell) {
                                tips_shown_.first_spell = true;
                                char stb[256]; snprintf(stb, sizeof(stb),
                                    "You learned a spell!\n\n"
                                    "%s - Open spellbook to cast\n"
                                    "Q in spellbook - Set as quick-cast\n"
                                    "%s - Quick-cast the set spell",
                                    input_glyphs_.label(Action::SPELLBOOK).c_str(),
                                    input_glyphs_.label(Action::QUICK_CAST).c_str());
                                tutorial_popup_.show("Spells", stb);
                            }
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
            // Ossren: can never drop equipment (consumables are fine)
            if (world_.has<GodAlignment>(player_) && world_.get<GodAlignment>(player_).god == GodId::OSSREN &&
                item.slot != EquipSlot::NONE) {
                log_.add("Ossren binds your gear. It cannot leave you.", {220, 180, 80, 255});
                log_.add("[Ossren] Cannot drop equipment.", {180, 150, 80, 255});
                break;
            }
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

// Map a gamepad Action to a synthetic SDL keycode for UI screens
static SDL_Keycode gamepad_action_to_key(Action act) {
    switch (act) {
        case Action::MOVE_UP:     return SDLK_UP;
        case Action::MOVE_DOWN:   return SDLK_DOWN;
        case Action::MOVE_LEFT:   return SDLK_LEFT;
        case Action::MOVE_RIGHT:  return SDLK_RIGHT;
        // No diagonal movement
        case Action::INTERACT:    return SDLK_RETURN;
        case Action::WAIT:        return SDLK_PERIOD;
        case Action::PICKUP:      return SDLK_g;
        case Action::STAIRS_DOWN: return SDLK_GREATER;
        case Action::STAIRS_UP:   return SDLK_LESS;
        case Action::STAIRS_ENTER:return SDLK_RETURN;
        case Action::FIRE_RANGED: return SDLK_f;
        case Action::REST:        return SDLK_r;
        case Action::PRAY:        return SDLK_p;
        case Action::EXAMINE:     return SDLK_x;
        case Action::SNEAK_TOGGLE:return SDLK_o;
        case Action::INVENTORY:   return SDLK_i;
        case Action::SPELLBOOK:   return SDLK_z;
        case Action::CHARACTER:   return SDLK_c;
        case Action::PASSIVE_TREE:return SDLK_t;
        case Action::QUEST_LOG:   return SDLK_q;
        case Action::WORLD_MAP:   return SDLK_m;
        case Action::CYCLE_TARGET: return SDLK_TAB;
        case Action::BESTIARY:    return SDLK_b;
        case Action::HELP:        return SDLK_SLASH;
        case Action::QUICK_CAST:  return SDLK_v;
        case Action::QUICKSAVE:   return SDLK_F5;
        case Action::QUICKLOAD:   return SDLK_F6;
        default: return SDLK_UNKNOWN;
    }
}

void Engine::handle_input() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            state_ = GameState::QUIT;
            return;
        }

        // Track input mode (keyboard vs gamepad) for UI glyph display
        input_glyphs_.update(event);

        // Gamepad: translate controller events into synthetic key events
        // so all existing screen handlers work without modification
        if (event.type == SDL_CONTROLLERBUTTONDOWN ||
            event.type == SDL_CONTROLLERBUTTONUP ||
            event.type == SDL_CONTROLLERAXISMOTION ||
            event.type == SDL_CONTROLLERDEVICEADDED ||
            event.type == SDL_CONTROLLERDEVICEREMOVED) {
            Action gp_act = gamepad_.translate(event);

            // B button = Escape (cancel/back) in all contexts
            if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                SDL_Event synth = {};
                synth.type = SDL_KEYDOWN;
                synth.key.keysym.sym = SDLK_ESCAPE;
                SDL_PushEvent(&synth);
                continue;
            }

            // Start = Escape (pause)
            if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                SDL_Event synth = {};
                synth.type = SDL_KEYDOWN;
                synth.key.keysym.sym = SDLK_ESCAPE;
                SDL_PushEvent(&synth);
                continue;
            }

            if (gp_act != Action::COUNT) {
                SDL_Keycode key = gamepad_action_to_key(gp_act);
                if (key != SDLK_UNKNOWN) {
                    SDL_Event synth = {};
                    synth.type = SDL_KEYDOWN;
                    synth.key.keysym.sym = key;
                    SDL_PushEvent(&synth);
                }
            }
            continue;
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
            // "Overwrite save?" confirmation sub-dialog
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
                        prog(meta_.max_dungeon_depth, 3, "depth"));
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
                        prog(meta_.max_dungeon_depth, 4, "depth"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::ELF),
                        prog(meta_.total_creatures_examined, 15, "examined"));
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
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::WYRMKIN),
                        meta_.killed_dragon ? "Complete!" : "Not yet achieved");
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::REVENANT),
                        prog(meta_.total_deaths, 10, "deaths"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::SERPENTINE),
                        prog(meta_.max_diseases, 3, "diseases"));
                    creation_screen_.set_unlock_progress(static_cast<int>(ClassId::TROLLBLOOD),
                        prog(meta_.max_dungeon_depth, 4, "depth"));

                    start_transition(TransitionType::FADE_OUT, 400);
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
                start_transition(TransitionType::FADE_IN, 500);
                state_ = GameState::PLAYING;
                // Welcome tip on first game
                if (!tips_shown_.first_overworld) {
                    tips_shown_.first_overworld = true;
                    char tb[300];
                    snprintf(tb, sizeof(tb),
                        "Talk to the townsfolk. The elder has a task.\n\n"
                        "%s - Move and bump to interact\n"
                        "%s - Open inventory\n"
                        "%s - View quests\n"
                        "%s - Open world map\n"
                        "%s - Help / all controls",
                        input_glyphs_.label(Action::MOVE_UP).c_str(),
                        input_glyphs_.label(Action::INVENTORY).c_str(),
                        input_glyphs_.label(Action::QUEST_LOG).c_str(),
                        input_glyphs_.label(Action::WORLD_MAP).c_str(),
                        input_glyphs_.label(Action::HELP).c_str());
                    tutorial_popup_.show("Welcome", tb);
                }
            }
            continue;
        }

        // Tutorial popup (blocks all game input until dismissed)
        if (tutorial_popup_.is_open()) {
            tutorial_popup_.handle_input(event);
            continue;
        }

        // Dialogue screen
        if (dialogue_screen_.is_open()) {
            int dact = dialogue_screen_.handle_input(event);
            if (dact == -2) {
                // Leave
                dialogue_screen_.close();
            } else if (dact == 1) {
                // Shop: check cannibal trait first
                bool cannibal_blocked = false;
                if (world_.has<Player>(player_)) {
                    for (auto tid : world_.get<Player>(player_).traits) {
                        if (tid == TraitId::CANNIBAL) { cannibal_blocked = true; break; }
                    }
                }
                if (cannibal_blocked) {
                    dialogue_screen_.open(dialogue_screen_.get_npc_name(),
                        "The merchant recoils. \"Get away from me, flesh-eater.\"", {});
                } else {
                dialogue_screen_.close();
                if (!tips_shown_.first_shop) {
                    tips_shown_.first_shop = true;
                    tutorial_popup_.show("Shopping",
                        "Buy tab: purchase items from the shopkeeper.\n"
                        "Sell tab: sell your items for half their value.\n\n"
                        "Shops in distant towns carry better gear\n"
                        "at higher prices. Each shop has fixed stock.");
                }
                if (world_.has<NPC>(dialogue_npc_) && world_.has<Stats>(player_)) {
                    auto& ps = world_.get<Stats>(player_);
                    GodId province = GodId::NONE;
                    if (world_.has<Position>(dialogue_npc_))
                        province = get_town_god(world_.get<Position>(dialogue_npc_).x,
                                                 world_.get<Position>(dialogue_npc_).y);
                    int shop_diff = ps.level;
                    if (world_.has<Position>(dialogue_npc_)) {
                        auto& sp = world_.get<Position>(dialogue_npc_);
                        float sd = std::sqrt(static_cast<float>((sp.x-500)*(sp.x-500)+(sp.y-375)*(sp.y-375)));
                        shop_diff = std::max(shop_diff, static_cast<int>(sd / 40.0f));
                    }
                    shop_screen_.open(player_, world_, rng_, &gold_, shop_diff, 100, province, dialogue_npc_);
                }
                } // end cannibal else
            } else if (dact == 2) {
                // Talk: cycle to next idle line, stay in dialogue
                if (world_.has<NPC>(dialogue_npc_)) {
                    auto& npc = world_.get<NPC>(dialogue_npc_);
                    std::string line = npc.dialogue;
                    if (!npc.idle_lines.empty()) {
                        npc.line_idx = (npc.line_idx + 1) % static_cast<int>(npc.idle_lines.size());
                        if (!npc.idle_lines[npc.line_idx].empty()) line = npc.idle_lines[npc.line_idx];
                    }
                    // Rebuild options (same as original open)
                    std::vector<DialogueOption> opts;
                    if (npc.role == NPCRole::SHOPKEEPER || npc.role == NPCRole::BLACKSMITH)
                        opts.push_back({"Browse wares", 1, true});
                    if (npc.role == NPCRole::INNKEEPER) {
                        int inn_cost = 10;
                        if (world_.has<Position>(player_)) {
                            auto& ip = world_.get<Position>(player_);
                            float id = std::sqrt(static_cast<float>((ip.x-500)*(ip.x-500)+(ip.y-375)*(ip.y-375)));
                            inn_cost = 10 + static_cast<int>(id / 30.0f);
                        }
                        char rl[48]; snprintf(rl, sizeof(rl), "Rest (%d gold)", inn_cost);
                        opts.push_back({rl, 3, gold_ >= inn_cost});
                    }
                    opts.push_back({"Talk", 2, true});
                    if (npc.quest_id >= 0) {
                        auto qid = static_cast<QuestId>(npc.quest_id);
                        if (journal_.get_state(qid) == QuestState::COMPLETE)
                            opts.push_back({"Turn in quest", 4, true});
                        else if (!journal_.has_quest(qid))
                            opts.push_back({"Ask about work", 4, true});
                        else if (journal_.get_state(qid) == QuestState::ACTIVE)
                            opts.push_back({"Ask about quest", 4, true});
                    }
                    dialogue_screen_.open(npc.name, line, opts);
                }
            } else if (dact == 5) {
                // Church services: open the church screen
                dialogue_screen_.close();
                if (!tips_shown_.first_church) {
                    tips_shown_.first_church = true;
                    tutorial_popup_.show("Church",
                        "Churches offer services based on your rank.\n"
                        "Rank up by completing tasks and gaining favor.\n\n"
                        "Initiate: rest + heal.\n"
                        "Acolyte: identify, enchant, learn spell.\n"
                        "Devoted: exclusive holy item.\n"
                        "Champion: legendary god weapon.");
                }
                if (world_.has<Church>(dialogue_npc_) && world_.has<GodAlignment>(player_)) {
                    auto& church = world_.get<Church>(dialogue_npc_);
                    auto& ga = world_.get<GodAlignment>(player_);
                    church_screen_.open(player_, &world_, church.god,
                                         ga.god == church.god ? ga.favor : 0);
                }
            } else if (dact == 10 || dact == 11) {
                // Prayer 1 or 2
                dialogue_screen_.close();
                execute_prayer(dact - 10);
                process_turn();
            } else if (dact == 12) {
                // God mastery: inject key '3' into prayer_mode handler
                dialogue_screen_.close();
                prayer_mode_ = true;
                // Inject a synthetic SDL_KEYDOWN event for '3'
                SDL_Event synth;
                synth.type = SDL_KEYDOWN;
                synth.key.keysym.sym = SDLK_3;
                SDL_PushEvent(&synth);
            } else if (dact == 3 || dact == 4) {
                // Inn rest or Quest: delegate to npc_interaction
                dialogue_screen_.close();
                if (world_.has<NPC>(dialogue_npc_) && world_.has<Position>(dialogue_npc_)) {
                    auto& dp = world_.get<Position>(dialogue_npc_);
                    npc_interaction::Context dctx {
                        world_, player_, log_, audio_, rng_, particles_,
                        shop_screen_, quest_offer_, levelup_screen_, journal_,
                        meta_, gold_, game_turn_, dungeon_level_,
                        pending_levelup_, pending_quest_npc_
                    };
                    npc_interaction::interact(dctx, dialogue_npc_, dp.x, dp.y);
                }
            }
            continue;
        }

        // Church screen
        if (church_screen_.is_open()) {
            ChurchAction cact = church_screen_.handle_input(event);
            if (cact == ChurchAction::CLOSE) {
                church_screen_.close();
            } else if (cact == ChurchAction::REST) {
                // Full heal, costs 5 favor
                if (world_.has<Stats>(player_) && world_.has<GodAlignment>(player_)) {
                    auto& ga = world_.get<GodAlignment>(player_);
                    if (ga.favor < 5) {
                        log_.add("Not enough favor. (5 required)", {180, 120, 120, 255});
                    } else {
                        ga.favor -= 5;
                        auto& ps = world_.get<Stats>(player_);
                        ps.hp = ps.hp_max;
                        ps.mp = ps.mp_max;
                        if (world_.has<StatusEffects>(player_))
                            world_.get<StatusEffects>(player_).effects.clear();
                        log_.add("You rest in the church. Fully restored. (-5 favor)", {100, 200, 140, 255});
                        audio_.play(SfxId::HEAL);
                        particles_.heal_effect(world_.get<Position>(player_).x, world_.get<Position>(player_).y);
                    }
                }
                church_screen_.close();
            } else if (cact == ChurchAction::IDENTIFY) {
                // Identify all items, costs 8 favor
                if (!world_.has<GodAlignment>(player_) || world_.get<GodAlignment>(player_).favor < 8) {
                    log_.add("Not enough favor. (8 required)", {180, 120, 120, 255});
                    church_screen_.close(); continue;
                }
                world_.get<GodAlignment>(player_).favor -= 8;
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
                    // Church task: identify tracking (Thessarka)
                    if (id_count > 0 && world_.has<GodAlignment>(player_)) {
                        auto& ga = world_.get<GodAlignment>(player_);
                        if (ga.task_active && !ga.task_complete) {
                            auto next = static_cast<ChurchRank>(ga.church_rank + 1);
                            auto& task = get_church_task(ga.god, next);
                            if (task.items_donated > 0 && ga.god == GodId::THESSARKA) {
                                ga.task_progress += id_count;
                                if (ga.task_progress >= task.items_donated) {
                                    ga.task_complete = true; ga.task_active = false;
                                    log_.add("Church task complete. Return to the church.", {255, 220, 80, 255});
                                }
                            }
                        }
                    }
                }
                church_screen_.close();
            } else if (cact == ChurchAction::ENCHANT) {
                // Enchant main-hand weapon, costs 12 favor
                if (!world_.has<GodAlignment>(player_) || world_.get<GodAlignment>(player_).favor < 12) {
                    log_.add("Not enough favor. (12 required)", {180, 120, 120, 255});
                    church_screen_.close(); continue;
                }
                world_.get<GodAlignment>(player_).favor -= 12;
                auto& rewards = get_church_rewards(church_screen_.get_god());
                if (world_.has<Inventory>(player_)) {
                    Entity wpn = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
                    if (wpn != NULL_ENTITY && world_.has<Item>(wpn)) {
                        auto& item = world_.get<Item>(wpn);
                        item.damage_bonus += rewards.enchant_bonus;
                        char buf[96];
                        snprintf(buf, sizeof(buf), "%s enchanted with %s (+%d damage).",
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
                // One-time Devoted item, costs 10 favor
                if (!world_.has<GodAlignment>(player_)) { church_screen_.close(); continue; }
                auto& ga_item = world_.get<GodAlignment>(player_);
                if (ga_item.church_item_claimed) {
                    log_.add("You have already received this item.", {180, 140, 120, 255});
                    church_screen_.close(); continue;
                }
                if (ga_item.favor < 10) {
                    log_.add("Not enough favor. (10 required)", {180, 120, 120, 255});
                    church_screen_.close(); continue;
                }
                ga_item.favor -= 10;
                ga_item.church_item_claimed = true;

                auto& rewards = get_church_rewards(church_screen_.get_god());
                Entity item_e = world_.create();
                Item ci;
                ci.name = rewards.exclusive_item_name;
                ci.description = rewards.exclusive_item_desc;
                ci.damage_bonus = rewards.exclusive_item_damage;
                ci.armor_bonus = rewards.exclusive_item_armor;
                ci.identified = true;
                ci.rarity = Rarity::RARE;
                ci.gold_value = 200;
                // Determine slot from item name/stats
                if (ci.damage_bonus > 0 && ci.armor_bonus == 0) {
                    ci.type = ItemType::WEAPON;
                    ci.slot = EquipSlot::MAIN_HAND;
                } else if (std::string(rewards.exclusive_item_name).find("Shield") != std::string::npos ||
                           std::string(rewards.exclusive_item_name).find("Aegis") != std::string::npos) {
                    ci.type = ItemType::SHIELD;
                    ci.slot = EquipSlot::OFF_HAND;
                } else if (std::string(rewards.exclusive_item_name).find("Ring") != std::string::npos ||
                           std::string(rewards.exclusive_item_name).find("Signet") != std::string::npos) {
                    ci.type = ItemType::RING;
                    ci.slot = EquipSlot::RING_1;
                } else if (std::string(rewards.exclusive_item_name).find("Gauntlet") != std::string::npos) {
                    ci.type = ItemType::ARMOR_HANDS;
                    ci.slot = EquipSlot::HANDS;
                } else if (std::string(rewards.exclusive_item_name).find("Helm") != std::string::npos ||
                           std::string(rewards.exclusive_item_name).find("Cowl") != std::string::npos ||
                           std::string(rewards.exclusive_item_name).find("Circlet") != std::string::npos ||
                           std::string(rewards.exclusive_item_name).find("Crown") != std::string::npos) {
                    ci.type = ItemType::ARMOR_HEAD;
                    ci.slot = EquipSlot::HEAD;
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
                // One-time Champion legendary, costs 20 favor
                auto& rewards = get_church_rewards(church_screen_.get_god());
                if (!world_.has<GodAlignment>(player_)) { church_screen_.close(); continue; }
                auto& ga_bless = world_.get<GodAlignment>(player_);
                if (ga_bless.church_blessing_claimed) {
                    log_.add("You have already received this reward.", {180, 140, 120, 255});
                    church_screen_.close(); continue;
                }
                if (ga_bless.favor < 20) {
                    log_.add("Not enough favor. (20 required)", {180, 120, 120, 255});
                    church_screen_.close(); continue;
                }
                ga_bless.favor -= 20;
                ga_bless.church_blessing_claimed = true;
                // Champion reward: legendary item themed to god
                {
                    auto god = church_screen_.get_god();
                    auto& gi = get_god_info(god);
                    Entity item_e = world_.create();
                    Item ci;
                    ci.identified = true;
                    ci.rarity = Rarity::LEGENDARY;
                    ci.gold_value = 500;
                    // Each god grants a unique legendary
                    switch (god) {
                        case GodId::VETHRIK:
                            ci.name = "Ossuary Crown"; ci.description = "+5 AC. Kills heal 10% max HP.";
                            ci.type = ItemType::ARMOR_HEAD; ci.slot = EquipSlot::HEAD;
                            ci.armor_bonus = 5; ci.unique_effect = UniqueEffect::ON_KILL_HEAL; break;
                        case GodId::THESSARKA:
                            ci.name = "Eye of the Eyeless"; ci.description = "+3 AC, +2 dodge. Auto-identify on pickup.";
                            ci.type = ItemType::AMULET; ci.slot = EquipSlot::AMULET;
                            ci.armor_bonus = 3; ci.dodge_bonus = 2; ci.unique_effect = UniqueEffect::IDENTIFY_ON_PICKUP; break;
                        case GodId::MORRETH:
                            ci.name = "Iron Father's Greatsword"; ci.description = "+10 dmg, +3 STR. Cannot be unequipped.";
                            ci.type = ItemType::WEAPON; ci.slot = EquipSlot::MAIN_HAND;
                            ci.damage_bonus = 10; ci.str_bonus = 3; ci.curse_state = 1; ci.tags |= TAG_SWORD; break;
                        case GodId::YASHKHET:
                            ci.name = "The Wound's Edge"; ci.description = "+7 dmg. Heals 20% of damage dealt.";
                            ci.type = ItemType::WEAPON; ci.slot = EquipSlot::MAIN_HAND;
                            ci.damage_bonus = 7; ci.tags |= TAG_DAGGER; ci.unique_effect = UniqueEffect::LIFESTEAL; break;
                        case GodId::KHAEL:
                            ci.name = "Verdant Mantle"; ci.description = "+4 AC. Immune to poison.";
                            ci.type = ItemType::ARMOR_CHEST; ci.slot = EquipSlot::CHEST;
                            ci.armor_bonus = 4; ci.unique_effect = UniqueEffect::POISON_IMMUNE; break;
                        case GodId::SOLETH:
                            ci.name = "Sunforged Crown"; ci.description = "+4 AC. +5 fire damage on all melee hits.";
                            ci.type = ItemType::ARMOR_HEAD; ci.slot = EquipSlot::HEAD;
                            ci.armor_bonus = 4; ci.unique_effect = UniqueEffect::FIRE_DAMAGE_BONUS; break;
                        case GodId::IXUUL:
                            ci.name = "Entropic Signet"; ci.description = "+3 STR, +3 DEX, +3 CON. +50 speed on kill.";
                            ci.type = ItemType::RING; ci.slot = EquipSlot::RING_1;
                            ci.str_bonus = 3; ci.dex_bonus = 3; ci.con_bonus = 3; ci.unique_effect = UniqueEffect::KILL_HASTE; break;
                        case GodId::ZHAVEK:
                            ci.name = "Nightcloak"; ci.description = "+4 AC, +3 DEX. Invisible 2 turns after kill.";
                            ci.type = ItemType::ARMOR_CHEST; ci.slot = EquipSlot::CHEST;
                            ci.armor_bonus = 4; ci.dex_bonus = 3; ci.unique_effect = UniqueEffect::KILL_INVIS; break;
                        case GodId::THALARA:
                            ci.name = "Tidecaller Trident"; ci.description = "+8 dmg. Freeze on hit.";
                            ci.type = ItemType::WEAPON; ci.slot = EquipSlot::MAIN_HAND;
                            ci.damage_bonus = 8; ci.unique_effect = UniqueEffect::FREEZE_ON_HIT; break;
                        case GodId::OSSREN:
                            ci.name = "Hammerforged Gauntlets"; ci.description = "+4 AC, +4 dmg. Thorns (3 reflect).";
                            ci.type = ItemType::ARMOR_HANDS; ci.slot = EquipSlot::HANDS;
                            ci.armor_bonus = 4; ci.damage_bonus = 4; ci.unique_effect = UniqueEffect::THORNS; break;
                        case GodId::LETHIS:
                            ci.name = "Dreamer's Cowl"; ci.description = "+4 AC, +3 CON. Survive lethal 1/floor.";
                            ci.type = ItemType::ARMOR_HEAD; ci.slot = EquipSlot::HEAD;
                            ci.armor_bonus = 4; ci.con_bonus = 3; ci.unique_effect = UniqueEffect::DEATHWARD; break;
                        case GodId::GATHRUUN:
                            ci.name = "Granite Aegis"; ci.description = "+8 AC. Reflect 3 damage to attackers.";
                            ci.type = ItemType::SHIELD; ci.slot = EquipSlot::OFF_HAND;
                            ci.armor_bonus = 8; ci.unique_effect = UniqueEffect::THORNS; break;
                        case GodId::SYTHARA:
                            ci.name = "Blightbringer"; ci.description = "+6 dmg. All hits poison + bleed.";
                            ci.type = ItemType::WEAPON; ci.slot = EquipSlot::MAIN_HAND;
                            ci.damage_bonus = 6; ci.tags |= TAG_DAGGER; ci.unique_effect = UniqueEffect::POISON_BLEED_HIT; break;
                        default: ci.name = "Divine Token"; ci.description = "A symbol of devotion.";
                            ci.type = ItemType::AMULET; ci.slot = EquipSlot::AMULET; break;
                    }
                    world_.add<Item>(item_e, std::move(ci));
                    if (world_.has<Inventory>(player_))
                        world_.get<Inventory>(player_).add(item_e);
                    world_.add<Renderable>(item_e, {SHEET_ITEMS, 0, 16, {gi.color.r, gi.color.g, gi.color.b, 255}, 2});
                    char buf[128];
                    snprintf(buf, sizeof(buf), "The Champion's reward: %s", ci.name.c_str());
                    log_.add(buf, {255, 220, 80, 255});
                    audio_.play(SfxId::LEVELUP);
                    screen_flash(gi.color.r, gi.color.g, gi.color.b, 120);
                    trigger_screen_shake(5.0f);
                    if (world_.has<Position>(player_)) {
                        auto& pp = world_.get<Position>(player_);
                        particles_.burst((float)pp.x, (float)pp.y, 20, gi.color.r, gi.color.g, gi.color.b, 0.2f, 1.2f, 5);
                    }
                }
                church_screen_.close();
            } else if (cact == ChurchAction::ACCEPT_TASK) {
                // Accept the next rank-up task
                if (world_.has<GodAlignment>(player_)) {
                    auto& ga = world_.get<GodAlignment>(player_);
                    auto next = static_cast<ChurchRank>(ga.church_rank + 1);
                    if (next < ChurchRank::RANK_COUNT) {
                        ga.task_active = true;
                        ga.task_progress = 0;
                        ga.task_complete = false;
                        auto& task = get_church_task(ga.god, next);
                        char tbuf[128];
                        snprintf(tbuf, sizeof(tbuf), "Church task accepted: %s", task.description);
                        log_.add(tbuf, {200, 200, 140, 255});
                        audio_.play(SfxId::QUEST);
                    }
                }
                church_screen_.close();
            } else if (cact == ChurchAction::RANK_UP) {
                // Advance church rank
                if (world_.has<GodAlignment>(player_)) {
                    auto& ga = world_.get<GodAlignment>(player_);
                    ga.church_rank++;
                    ga.task_active = false;
                    ga.task_complete = false;
                    ga.task_progress = 0;
                    auto rank = static_cast<ChurchRank>(ga.church_rank);
                    char rbuf[64];
                    snprintf(rbuf, sizeof(rbuf), "Promoted to %s of %s!",
                             church_rank_name(rank), get_god_info(ga.god).name);
                    log_.add(rbuf, {255, 220, 80, 255});
                    audio_.play(SfxId::LEVELUP);
                    auto& gi = get_god_info(ga.god);
                    trigger_screen_shake(3.0f);
                    if (world_.has<Position>(player_)) {
                        auto& pp = world_.get<Position>(player_);
                        particles_.burst((float)pp.x, (float)pp.y, 15, gi.color.r, gi.color.g, gi.color.b, 0.15f, 1.0f, 4);
                    }
                    // Grant passive tree point on rank up
                    if (world_.has<PassiveTreeState>(player_))
                        world_.get<PassiveTreeState>(player_).grant_point();
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
                                    case EffectType::BONUS_HP: stats.base_hp_max += eff.value; stats.hp_max += eff.value; stats.hp += eff.value; break;
                                    case EffectType::BONUS_MP: stats.base_mp_max += eff.value; stats.mp_max += eff.value; stats.mp += eff.value; break;
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
                    case PauseChoice::SAVE_AND_QUIT:
                        do_save();
                        pause_menu_.close();
                        floor_cache_.clear();
                        main_menu_.set_can_continue(true);
                        state_ = GameState::MAIN_MENU;
                        audio_.stop_all_ambient(500);
                        audio_.play_music(MusicId::TITLE, 1500);
                        audio_.play_ambient(AmbientId::FIRE_CRACKLE, 1000);
                        audio_.play_ambient(AmbientId::FOREST_NIGHT_RAIN, 1000);
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
                    auto lact = keybinds_.translate(lsym, event.key.keysym.mod);
                    switch (lact) {
                        case Action::MOVE_LEFT:  dx = -1; break;
                        case Action::MOVE_RIGHT: dx =  1; break;
                        case Action::MOVE_UP:    dy = -1; break;
                        case Action::MOVE_DOWN:  dy =  1; break;
                        // Diagonal movement removed (cardinal only)
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
                                        ps.base_hp_max += 1;
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
                                int dx = 1; // default east
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
                            case GodId::VETHRIK: {
                                // Raise nearest corpse as 5-turn friendly
                                auto& corpse_pool = world_.pool<Corpse>();
                                bool raised = false;
                                for (size_t ci = 0; ci < corpse_pool.size(); ci++) {
                                    Entity ce = corpse_pool.entity_at(ci);
                                    if (!world_.has<Position>(ce)) continue;
                                    auto& cp = world_.get<Position>(ce);
                                    int cd = std::max(std::abs(cp.x - pp.x), std::abs(cp.y - pp.y));
                                    if (cd <= 3) {
                                        // Convert corpse to friendly skeleton
                                        world_.remove<Corpse>(ce);
                                        Stats ss; ss.name = "risen dead"; ss.hp = ps.level * 3; ss.hp_max = ss.hp;
                                        ss.base_damage = 4 + ps.level / 2; ss.base_speed = 100;
                                        world_.add<Stats>(ce, std::move(ss));
                                        AI sai; sai.state = AIState::HUNTING; sai.friendly = true;
                                        world_.add<AI>(ce, sai);
                                        world_.add<Energy>(ce, {0, 100});
                                        log_.add("The dead rise at your command.", {160, 160, 200, 255});
                                        particles_.spell_dark((float)cp.x, (float)cp.y);
                                        raised = true; break;
                                    }
                                }
                                if (!raised) { log_.add("No corpse nearby.", {150, 130, 130, 255}); ga.favor += 15; acted = false; }
                                break;
                            }
                            case GodId::THESSARKA: {
                                // Reveal all enemies + traps, auto-identify inventory
                                auto& ai_pool2 = world_.pool<AI>();
                                for (size_t ai = 0; ai < ai_pool2.size(); ai++) {
                                    Entity ae = ai_pool2.entity_at(ai);
                                    if (world_.has<Position>(ae)) {
                                        auto& ep = world_.get<Position>(ae);
                                        if (map_.in_bounds(ep.x, ep.y)) map_.at(ep.x, ep.y).explored = true;
                                    }
                                }
                                // Identify all items in inventory
                                if (world_.has<Inventory>(player_)) {
                                    auto& inv = world_.get<Inventory>(player_);
                                    for (auto item_e : inv.items) {
                                        if (world_.has<Item>(item_e))
                                            world_.get<Item>(item_e).identified = true;
                                    }
                                }
                                log_.add("Knowledge floods your mind. All is revealed.", {140, 140, 220, 255});
                                particles_.burst((float)pp.x, (float)pp.y, 15, 140, 140, 255, 0.15f, 1.0f, 2);
                                break;
                            }
                            case GodId::IXUUL: {
                                // Random mutation: +3 to one stat, -1 to another
                                int gain_attr = rng_.range(0, 5); // STR through PER
                                int lose_attr = rng_.range(0, 5);
                                while (lose_attr == gain_attr) lose_attr = rng_.range(0, 5);
                                auto ga_attr = static_cast<Attr>(gain_attr);
                                auto la_attr = static_cast<Attr>(lose_attr);
                                ps.set_attr(ga_attr, ps.attr(ga_attr) + 3);
                                ps.set_attr(la_attr, std::max(3, ps.attr(la_attr) - 1));
                                log_.add("Your flesh shifts. Mutation!", {180, 100, 255, 255});
                                trigger_screen_shake(4.0f);
                                break;
                            }
                            case GodId::THALARA: {
                                // Freeze all visible enemies 3 turns
                                auto& ai_pool3 = world_.pool<AI>();
                                int frozen_count = 0;
                                for (size_t ai = 0; ai < ai_pool3.size(); ai++) {
                                    Entity ae = ai_pool3.entity_at(ai);
                                    if (ai_pool3.at_index(ai).friendly) continue;
                                    if (!world_.has<Position>(ae) || !world_.has<StatusEffects>(ae)) continue;
                                    auto& ep = world_.get<Position>(ae);
                                    if (map_.in_bounds(ep.x, ep.y) && map_.at(ep.x, ep.y).visible) {
                                        world_.get<StatusEffects>(ae).add(StatusType::FROZEN, 0, 3);
                                        particles_.spell_ice((float)ep.x, (float)ep.y);
                                        frozen_count++;
                                    }
                                }
                                trigger_screen_shake(3.0f);
                                break;
                            }
                            case GodId::OSSREN: {
                                // Temper weapon: +2 permanent damage
                                if (world_.has<Inventory>(player_)) {
                                    Entity wpn = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
                                    if (wpn != NULL_ENTITY && world_.has<Item>(wpn)) {
                                        world_.get<Item>(wpn).damage_bonus += 2;
                                        log_.add("Your weapon glows. The edge sharpens permanently.", {220, 180, 80, 255});
                                        particles_.burst((float)pp.x, (float)pp.y, 10, 220, 160, 40, 0.08f, 0.7f, 3);
                                    } else {
                                        log_.add("You have no weapon to temper.", {150, 130, 130, 255});
                                        ga.favor += 15; acted = false;
                                    }
                                }
                                break;
                            }
                            case GodId::LETHIS: {
                                // Phase through walls 5 turns
                                ps.phase_turns = 5;
                                log_.add("You drift between worlds. Walls cannot hold you.", {160, 120, 200, 255});
                                particles_.drift((float)pp.x, (float)pp.y, 10, 140, 100, 180, 1.2f, 2);
                                break;
                            }
                            case GodId::SYTHARA: {
                                // All visible enemies get 5-turn poison
                                auto& ai_pool4 = world_.pool<AI>();
                                for (size_t ai = 0; ai < ai_pool4.size(); ai++) {
                                    Entity ae = ai_pool4.entity_at(ai);
                                    if (ai_pool4.at_index(ai).friendly) continue;
                                    if (!world_.has<Position>(ae) || !world_.has<StatusEffects>(ae)) continue;
                                    auto& ep = world_.get<Position>(ae);
                                    if (map_.in_bounds(ep.x, ep.y) && map_.at(ep.x, ep.y).visible) {
                                        world_.get<StatusEffects>(ae).add(StatusType::POISON, 3, 5);
                                        particles_.poison_effect((float)ep.x, (float)ep.y);
                                    }
                                }
                                log_.add("Plague erupts from you. Everything rots.", {120, 180, 60, 255});
                                particles_.burst((float)pp.x, (float)pp.y, 12, 100, 180, 40, 0.12f, 0.9f, 3);
                                break;
                            }
                            default: {
                                // Fallback (shouldn't happen)
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
                        // Prefer Tab-selected target if valid
                        int tx = 0, ty = 0;
                        bool has_target = false;
                        if (sinfo.hostile && sinfo.range > 0) {
                            Entity tgt = 0;
                            if (ranged_target_ != 0 && world_.has<Position>(ranged_target_) &&
                                world_.has<Stats>(ranged_target_) && world_.get<Stats>(ranged_target_).hp > 0) {
                                auto& rtp = world_.get<Position>(ranged_target_);
                                auto& pp = world_.get<Position>(player_);
                                int d = std::max(std::abs(rtp.x - pp.x), std::abs(rtp.y - pp.y));
                                if (d <= sinfo.range && map_.in_bounds(rtp.x, rtp.y) && map_.at(rtp.x, rtp.y).visible)
                                    tgt = ranged_target_;
                            }
                            if (tgt == 0) tgt = magic::nearest_enemy(world_, player_, map_, sinfo.range);
                            if (tgt != NULL_ENTITY && world_.has<Position>(tgt)) {
                                auto& tp = world_.get<Position>(tgt);
                                tx = tp.x; ty = tp.y; has_target = true;
                            }
                        }
                        auto result = magic::cast(world_, player_, spell,
                                                   map_, rng_, log_);
                        if (result.consumed_turn) {
                            player_acted_ = true;
                            ranged_target_ = 0; target_cycle_idx_ = -1;
                        }
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
                                // === CONJURATION: scaled by power ===
                                case SpellId::SPARK: // 8 dmg, cheap
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 6, 255, 255, 100, 0.4f, 3);
                                        particles_.burst(tx, ty, 8, 255, 255, 140, 0.08f, 0.3f, 3);
                                    }
                                    break;
                                case SpellId::FORCE_BOLT: // 15 dmg
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 14, 100, 140, 255, 0.3f, 5);
                                        particles_.burst(tx, ty, 18, 140, 180, 255, 0.12f, 0.5f, 5);
                                        screen_flash(100, 140, 255, 30);
                                    }
                                    break;
                                case SpellId::ICE_SHARD: // 18 dmg + freeze
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 12, 140, 200, 255, 0.3f, 5);
                                        particles_.burst(tx, ty, 20, 180, 220, 255, 0.12f, 0.5f, 5);
                                        particles_.drift(tx, ty, 15, 200, 240, 255, 0.8f, 4);
                                        screen_flash(140, 200, 255, 30);
                                    }
                                    break;
                                case SpellId::FIREBALL: // 25 dmg + burn
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 18, 255, 140, 40, 0.25f, 6);
                                        particles_.burst(tx, ty, 30, 255, 120, 30, 0.15f, 0.7f, 8);
                                        particles_.burst(tx, ty, 20, 255, 200, 60, 0.1f, 0.5f, 6);
                                        screen_flash(255, 120, 30, 60);
                                    }
                                    break;
                                case SpellId::LIGHTNING: // 30 dmg
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 20, 255, 255, 180, 0.5f, 2);
                                        particles_.burst(tx, ty, 25, 255, 255, 140, 0.2f, 0.4f, 4);
                                        screen_flash(255, 255, 220, 80);
                                    }
                                    break;
                                case SpellId::CHAIN_LIGHTNING: // 20 dmg x3
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 22, 255, 255, 180, 0.5f, 2);
                                        particles_.burst(tx, ty, 30, 255, 255, 140, 0.25f, 0.5f, 5);
                                        particles_.burst(sp.x, sp.y, 20, 200, 200, 255, 0.15f, 0.6f, 4);
                                        screen_flash(255, 255, 200, 90);
                                    }
                                    break;
                                case SpellId::GLACIAL_SPIKE: // 35 dmg + stun
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 20, 160, 220, 255, 0.25f, 6);
                                        particles_.burst(tx, ty, 35, 200, 240, 255, 0.18f, 0.6f, 7);
                                        particles_.drift(tx, ty, 20, 220, 240, 255, 1.0f, 5);
                                        screen_flash(180, 220, 255, 60);
                                        trigger_screen_shake(4.0f);
                                    }
                                    break;
                                case SpellId::ACID_SPLASH: // 12 dmg + armor shred
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 12, 120, 200, 40, 0.3f, 5);
                                        particles_.fall(tx, ty, 20, 100, 220, 40, 0.8f, 5);
                                        particles_.drift(tx, ty, 10, 80, 180, 30, 0.6f, 3);
                                    }
                                    break;
                                case SpellId::FROST_NOVA: // 15 dmg AoE + freeze
                                    particles_.burst(sp.x, sp.y, 50, 160, 220, 255, 0.22f, 1.2f, 7);
                                    particles_.drift(sp.x, sp.y, 35, 200, 240, 255, 1.5f, 5);
                                    particles_.burst(sp.x, sp.y, 20, 255, 255, 255, 0.25f, 0.5f, 3);
                                    screen_flash(140, 200, 255, 60);
                                    trigger_screen_shake(3.0f);
                                    break;
                                case SpellId::METEOR: // 45 dmg
                                    if (has_target) {
                                        particles_.fall(tx, ty, 25, 255, 160, 40, 0.5f, 10);
                                        particles_.burst(tx, ty, 40, 255, 100, 20, 0.2f, 0.8f, 8);
                                        particles_.burst(tx, ty, 20, 255, 200, 60, 0.15f, 0.5f, 6);
                                        trigger_screen_shake(8.0f);
                                        screen_flash(255, 160, 40, 100);
                                    }
                                    break;
                                case SpellId::DISINTEGRATE: // 60 dmg
                                    if (has_target) {
                                        particles_.projectile(sp.x, sp.y, tx, ty, 25, 200, 40, 200, 0.35f, 3);
                                        particles_.burst(tx, ty, 40, 220, 60, 220, 0.22f, 0.6f, 6);
                                        particles_.burst(tx, ty, 50, 255, 200, 255, 0.3f, 0.4f, 3);
                                        particles_.drift(tx, ty, 20, 180, 40, 180, 1.0f, 5);
                                        trigger_screen_shake(6.0f);
                                        screen_flash(200, 40, 200, 80);
                                    }
                                    break;
                                // === TRANSMUTATION: distinct per spell ===
                                case SpellId::HARDEN_SKIN:
                                    particles_.orbit(sp.x, sp.y, 14, 160, 150, 120, 0.3f, 0.8f, 6);
                                    break;
                                case SpellId::HASTEN:
                                    particles_.rise(sp.x, sp.y, 20, 255, 255, 140, 0.6f, 4);
                                    particles_.burst(sp.x, sp.y, 12, 255, 240, 100, 0.15f, 0.3f, 3);
                                    break;
                                case SpellId::STONE_FIST:
                                    particles_.burst(sp.x, sp.y, 18, 180, 140, 80, 0.06f, 0.6f, 7);
                                    screen_flash(140, 100, 60, 30);
                                    break;
                                case SpellId::PHASE:
                                    particles_.burst(sp.x, sp.y, 25, 100, 140, 220, 0.2f, 0.5f, 4);
                                    screen_flash(100, 140, 220, 40);
                                    break;
                                case SpellId::IRON_BODY:
                                    particles_.orbit(sp.x, sp.y, 20, 200, 200, 220, 0.4f, 1.0f, 7);
                                    particles_.burst(sp.x, sp.y, 15, 180, 180, 200, 0.05f, 0.8f, 6);
                                    screen_flash(180, 180, 200, 40);
                                    break;
                                case SpellId::SLOW:
                                    if (has_target) {
                                        particles_.drift(tx, ty, 15, 140, 120, 80, 1.0f, 5);
                                        particles_.fall(tx, ty, 10, 120, 100, 60, 0.6f, 4);
                                    }
                                    break;
                                case SpellId::POLYMORPH:
                                    if (has_target) {
                                        particles_.burst(tx, ty, 30, 200, 140, 255, 0.15f, 0.7f, 6);
                                        screen_flash(200, 140, 255, 40);
                                    }
                                    break;
                                case SpellId::MIRROR_IMAGE:
                                    particles_.orbit(sp.x, sp.y, 18, 180, 200, 255, 0.5f, 1.2f, 5);
                                    particles_.burst(sp.x, sp.y, 12, 200, 220, 255, 0.1f, 0.4f, 4);
                                    break;
                                // === DIVINATION: blue/white ===
                                case SpellId::REVEAL_MAP:
                                case SpellId::DETECT_MONSTERS:
                                case SpellId::FARSIGHT:
                                    particles_.burst(sp.x, sp.y, 25, 120, 140, 220, 0.2f, 0.8f, 5);
                                    particles_.rise(sp.x, sp.y, 12, 160, 180, 255, 0.6f, 4);
                                    break;
                                case SpellId::IDENTIFY:
                                case SpellId::SCRY:
                                    particles_.burst(sp.x, sp.y, 15, 220, 220, 255, 0.08f, 0.5f, 5);
                                    break;
                                case SpellId::FORESIGHT:
                                case SpellId::TRUESIGHT:
                                    particles_.orbit(sp.x, sp.y, 14, 180, 200, 255, 0.4f, 0.8f, 5);
                                    break;
                                case SpellId::CLAIRVOYANCE:
                                    particles_.burst(sp.x, sp.y, 30, 140, 160, 255, 0.2f, 1.0f, 6);
                                    particles_.rise(sp.x, sp.y, 20, 180, 200, 255, 0.8f, 5);
                                    screen_flash(140, 160, 255, 40);
                                    break;
                                // === HEALING: green/white rising ===
                                case SpellId::MINOR_HEAL:
                                    particles_.heal_effect(sp.x, sp.y);
                                    break;
                                case SpellId::CURE_POISON:
                                    particles_.rise(sp.x, sp.y, 18, 200, 255, 200, 0.7f, 5);
                                    break;
                                case SpellId::MAJOR_HEAL:
                                    particles_.rise(sp.x, sp.y, 30, 80, 240, 80, 1.2f, 7);
                                    particles_.rise(sp.x, sp.y, 15, 180, 255, 180, 0.9f, 5);
                                    screen_flash(80, 240, 80, 30);
                                    break;
                                case SpellId::CLEANSE:
                                    particles_.rise(sp.x, sp.y, 20, 255, 255, 200, 0.8f, 5);
                                    particles_.burst(sp.x, sp.y, 12, 200, 255, 180, 0.1f, 0.5f, 4);
                                    break;
                                case SpellId::RESTORE:
                                    particles_.rise(sp.x, sp.y, 22, 140, 255, 180, 0.9f, 6);
                                    particles_.burst(sp.x, sp.y, 10, 100, 200, 255, 0.08f, 0.4f, 4);
                                    break;
                                case SpellId::SHIELD_OF_FAITH:
                                    particles_.orbit(sp.x, sp.y, 16, 255, 240, 180, 0.5f, 1.2f, 5);
                                    screen_flash(255, 240, 180, 25);
                                    break;
                                case SpellId::SANCTUARY:
                                    particles_.orbit(sp.x, sp.y, 22, 200, 255, 200, 0.6f, 1.5f, 6);
                                    particles_.rise(sp.x, sp.y, 15, 255, 255, 220, 1.0f, 4);
                                    screen_flash(200, 255, 200, 40);
                                    break;
                                case SpellId::RESURRECTION:
                                    particles_.orbit(sp.x, sp.y, 25, 255, 255, 200, 0.7f, 2.0f, 7);
                                    particles_.rise(sp.x, sp.y, 20, 255, 240, 140, 1.5f, 6);
                                    screen_flash(255, 255, 200, 50);
                                    trigger_screen_shake(3.0f);
                                    break;
                                // === NATURE: green ===
                                case SpellId::ENTANGLE:
                                    particles_.burst(sp.x, sp.y, 25, 60, 160, 40, 0.15f, 0.8f, 6);
                                    particles_.drift(sp.x, sp.y, 15, 80, 200, 60, 1.0f, 4);
                                    break;
                                case SpellId::BEAST_CALL:
                                    particles_.burst(sp.x, sp.y, 20, 80, 180, 60, 0.12f, 0.7f, 6);
                                    particles_.rise(sp.x, sp.y, 10, 120, 200, 80, 0.6f, 4);
                                    break;
                                case SpellId::SWARM:
                                    particles_.burst(sp.x, sp.y, 15, 60, 140, 40, 0.1f, 0.5f, 5);
                                    break;
                                case SpellId::POISON_CLOUD:
                                    particles_.drift(sp.x, sp.y, 35, 100, 200, 60, 1.8f, 6);
                                    particles_.burst(sp.x, sp.y, 15, 80, 180, 40, 0.1f, 1.0f, 5);
                                    break;
                                case SpellId::THORNWALL:
                                    particles_.burst(sp.x, sp.y, 20, 100, 140, 60, 0.12f, 0.7f, 6);
                                    particles_.drift(sp.x, sp.y, 12, 140, 80, 40, 0.8f, 4);
                                    break;
                                case SpellId::REJUVENATE:
                                    particles_.rise(sp.x, sp.y, 20, 100, 220, 80, 1.0f, 5);
                                    break;
                                case SpellId::BARKSKIN:
                                    particles_.orbit(sp.x, sp.y, 16, 100, 140, 60, 0.3f, 0.8f, 6);
                                    break;
                                case SpellId::EARTHQUAKE:
                                    particles_.burst(sp.x, sp.y, 50, 140, 120, 80, 0.3f, 1.0f, 10);
                                    particles_.burst(sp.x, sp.y, 30, 200, 180, 100, 0.2f, 0.6f, 10);
                                    particles_.fall(sp.x, sp.y, 15, 160, 140, 80, 0.8f, 6);
                                    trigger_screen_shake(10.0f);
                                    screen_flash(140, 100, 50, 70);
                                    break;
                                case SpellId::LIGHTNING_STORM:
                                    particles_.burst(sp.x, sp.y, 40, 255, 255, 160, 0.3f, 0.6f, 4);
                                    particles_.burst(sp.x, sp.y, 30, 200, 200, 255, 0.25f, 0.8f, 6);
                                    particles_.burst(sp.x, sp.y, 15, 255, 255, 255, 0.15f, 0.3f, 3);
                                    trigger_screen_shake(6.0f);
                                    screen_flash(255, 255, 200, 80);
                                    break;
                                // === DARK ARTS: purple/red ===
                                case SpellId::DRAIN_LIFE:
                                    if (has_target) {
                                        particles_.trail(tx, ty, sp.x, sp.y, 18, 140, 60, 180, 5);
                                        particles_.burst(tx, ty, 15, 160, 80, 200, 0.1f, 0.5f, 6);
                                    }
                                    break;
                                case SpellId::FEAR:
                                    particles_.burst(sp.x, sp.y, 25, 100, 60, 140, 0.15f, 0.8f, 7);
                                    particles_.drift(sp.x, sp.y, 15, 80, 40, 120, 1.0f, 5);
                                    break;
                                case SpellId::RAISE_DEAD:
                                    particles_.rise(sp.x, sp.y, 30, 120, 60, 160, 1.2f, 7);
                                    particles_.burst(sp.x, sp.y, 18, 80, 200, 80, 0.08f, 0.9f, 6);
                                    particles_.drift(sp.x, sp.y, 12, 160, 100, 200, 1.5f, 5);
                                    break;
                                case SpellId::HEX:
                                    if (has_target) {
                                        particles_.drift(tx, ty, 20, 140, 60, 180, 1.2f, 5);
                                        particles_.burst(tx, ty, 10, 180, 80, 220, 0.1f, 0.5f, 4);
                                    }
                                    break;
                                case SpellId::SOUL_REND:
                                    if (has_target) {
                                        particles_.trail(tx, ty, sp.x, sp.y, 20, 180, 60, 220, 4);
                                        particles_.burst(tx, ty, 20, 200, 80, 255, 0.15f, 0.5f, 6);
                                        screen_flash(180, 60, 220, 30);
                                    }
                                    break;
                                case SpellId::DARKNESS:
                                    particles_.burst(sp.x, sp.y, 50, 20, 10, 40, 0.18f, 1.8f, 12);
                                    particles_.drift(sp.x, sp.y, 30, 40, 20, 60, 2.5f, 8);
                                    screen_flash(10, 5, 20, 120);
                                    break;
                                case SpellId::WITHER:
                                    if (has_target) {
                                        particles_.fall(tx, ty, 20, 100, 80, 40, 1.0f, 5);
                                        particles_.drift(tx, ty, 12, 80, 60, 30, 0.8f, 4);
                                    }
                                    break;
                                case SpellId::BLOOD_PACT:
                                    particles_.burst(sp.x, sp.y, 35, 200, 40, 40, 0.15f, 1.2f, 8);
                                    particles_.rise(sp.x, sp.y, 20, 255, 60, 60, 0.9f, 5);
                                    particles_.fall(sp.x, sp.y, 15, 180, 0, 0, 0.7f, 6);
                                    trigger_screen_shake(4.0f);
                                    screen_flash(180, 20, 20, 80);
                                    break;
                                case SpellId::DOOM:
                                    if (has_target) {
                                        particles_.fall(tx, ty, 40, 80, 0, 120, 1.8f, 10);
                                        particles_.burst(tx, ty, 25, 160, 40, 200, 0.12f, 1.2f, 8);
                                        particles_.drift(tx, ty, 20, 60, 0, 80, 2.5f, 7);
                                        trigger_screen_shake(5.0f);
                                        screen_flash(80, 0, 120, 60);
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
                if (SDL_GetTicks() - end_screen_time_ < 3000) return;
                if (hardcore_) {
                    std::filesystem::remove(save::default_path());
                }
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
            auto act = keybinds_.translate(sym, event.key.keysym.mod);
            switch (act) {
                case Action::MOVE_UP:    try_move_player(0, -1);  break;
                case Action::MOVE_DOWN:  try_move_player(0, 1);   break;
                case Action::MOVE_LEFT:  try_move_player(-1, 0);  break;
                case Action::MOVE_RIGHT: try_move_player(1, 0);   break;
                // Diagonal movement removed (cardinal only)

                case Action::WAIT:
                    player_acted_ = true;
                    dwarf_moved_last_turn_ = false;
                    // Dwarf: FORTIFY on wait
                    if (world_.has<Player>(player_) && world_.get<Player>(player_).class_id == ClassId::DWARF) {
                        dwarf_fortified_ = true;
                        log_.add("FORTIFIED. Next attack deals double.", {180, 160, 100, 255});
                    }
                    // Knight Lv5: BULWARK (50% block for 3 turns, cooldown 10)
                    if (world_.has<Player>(player_) && world_.get<Player>(player_).class_id == ClassId::KNIGHT &&
                        world_.get<Stats>(player_).level >= 5 && knight_bulwark_cd_ == 0) {
                        knight_bulwark_turns_ = 3;
                        knight_bulwark_cd_ = 10;
                        world_.get<Player>(player_).bulwark_turns = 3;
                        log_.add("BULWARK! Block chance doubled.", {200, 200, 255, 255});
                        audio_.play(SfxId::PRAYER);
                    }
                    // (Trollblood consume moved to INTERACT action)
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
                                "Walk into NPCs while sneaking to pickpocket.");
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
                        screen_flash(255, 255, 200, 180);
                        if (world_.has<Position>(player_)) {
                            auto& pp = world_.get<Position>(player_);
                            particles_.burst((float)pp.x, (float)pp.y, 20, 255, 240, 160, 0.15f, 1.0f, 4);
                        }
                        // Once per floor: set huge cooldown, reset on floor change
                        tree.capstone_cooldowns[cd_idx] = 999;
                        process_turn();
                        break; // skip normal cooldown set
                    }
                    else if (active_type == EffectType::CAP_UNBREAKABLE) {
                        // Halve all incoming damage for 8 turns
                        if (world_.has<Player>(player_))
                            world_.get<Player>(player_).unbreakable_turns = 8;
                        log_.add("You become unbreakable. Damage halved.", {180, 140, 100, 255});
                        audio_.play(SfxId::BLOCK1);
                        if (world_.has<Position>(player_)) {
                            auto& pp = world_.get<Position>(player_);
                            particles_.burst((float)pp.x, (float)pp.y, 12, 180, 160, 120, 0.12f, 0.8f, 3);
                        }
                        used = true;
                    }
                    else if (active_type == EffectType::CAP_ASPECT_OF_BEAST) {
                        auto& stats = world_.get<Stats>(player_);
                        // +5 all stats for 15 turns, natural attacks
                        for (int a = 0; a < ATTR_COUNT; a++)
                            stats.attributes[a] += 5;
                        stats.base_damage += 3;
                        stats.hp_max += 15; stats.hp += 15;
                        if (world_.has<Player>(player_))
                            world_.get<Player>(player_).beast_form_turns = 15;
                        // Use capstone_cooldowns[5] as remaining duration (negative = active)
                        tree.capstone_cooldowns[cd_idx] = -15; // negative = duration remaining
                        log_.add("The beast within awakens. Claws and fangs replace steel.", {80, 200, 80, 255});
                        audio_.play(SfxId::SPELL_IMPACT);
                        trigger_screen_shake(4.0f);
                        if (world_.has<Position>(player_)) {
                            auto& pp = world_.get<Position>(player_);
                            particles_.burst((float)pp.x, (float)pp.y, 15, 60, 200, 60, 0.14f, 1.0f, 4);
                        }
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
                    if (!tips_shown_.first_prayer) {
                        tips_shown_.first_prayer = true;
                        tutorial_popup_.show("Prayer",
                            "Prayers cost favor. Each god has 2 prayers\n"
                            "and a mastery at 75+ favor.\n\n"
                            "Favor rises slowly over time if you follow\n"
                            "your god's tenets. Violations lose favor.\n\n"
                            "Visit a church to access rank services.");
                    }
                    auto& ginfo = get_god_info(align.god);
                    auto prayers = get_prayers(align.god);
                    if (!prayers) break;
                    // Open prayer dialogue screen
                    char title[64]; snprintf(title, sizeof(title), "Pray to %s", ginfo.name);
                    char desc[96]; snprintf(desc, sizeof(desc), "Favor: %d. Choose a prayer.", align.favor);
                    std::vector<DialogueOption> opts;
                    char p1[80]; snprintf(p1, sizeof(p1), "%s (%d favor)", prayers[0].name, prayers[0].favor_cost);
                    opts.push_back({p1, 10, align.favor >= prayers[0].favor_cost});
                    char p2[80]; snprintf(p2, sizeof(p2), "%s (%d favor)", prayers[1].name, prayers[1].favor_cost);
                    opts.push_back({p2, 11, align.favor >= prayers[1].favor_cost});
                    if (align.favor >= 75) {
                        opts.push_back({"Mastery (15 favor)", 12, align.favor >= 15});
                    }
                    dialogue_screen_.open(title, desc, opts);
                    dialogue_npc_ = 0; // no NPC, prayer mode
                    break;
                }

                case Action::CYCLE_TARGET: {
                    // Build target list and cycle
                    int range = 8;
                    if (world_.has<Inventory>(player_)) {
                        Entity wpn = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
                        if (wpn != NULL_ENTITY && world_.has<Item>(wpn))
                            range = std::max(range, world_.get<Item>(wpn).range);
                    }
                    visible_targets_ = magic::all_visible_enemies(world_, player_, map_, range);
                    if (visible_targets_.empty()) {
                        log_.add("No targets in range.", {160, 160, 160, 255});
                        ranged_target_ = 0;
                    } else {
                        target_cycle_idx_ = (target_cycle_idx_ + 1) % static_cast<int>(visible_targets_.size());
                        ranged_target_ = visible_targets_[target_cycle_idx_];
                        if (world_.has<Stats>(ranged_target_) && world_.has<Position>(ranged_target_)) {
                            auto& ts = world_.get<Stats>(ranged_target_);
                            auto& tp = world_.get<Position>(ranged_target_);
                            auto& pp = world_.get<Position>(player_);
                            int d = std::max(std::abs(tp.x - pp.x), std::abs(tp.y - pp.y));
                            char tb[96]; snprintf(tb, sizeof(tb), "Target: %s (dist %d)", ts.name.c_str(), d);
                            log_.add(tb, {255, 220, 80, 255});
                        }
                    }
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
                                Entity tgt = 0;
                                if (ranged_target_ != 0 && world_.has<Position>(ranged_target_) &&
                                    world_.has<Stats>(ranged_target_) && world_.get<Stats>(ranged_target_).hp > 0) {
                                    auto& rtp = world_.get<Position>(ranged_target_);
                                    auto& pp = world_.get<Position>(player_);
                                    int d = std::max(std::abs(rtp.x - pp.x), std::abs(rtp.y - pp.y));
                                    if (d <= sinfo.range && map_.in_bounds(rtp.x, rtp.y) && map_.at(rtp.x, rtp.y).visible)
                                        tgt = ranged_target_;
                                }
                                if (tgt == 0) tgt = magic::nearest_enemy(world_, player_, map_, sinfo.range);
                                if (tgt != NULL_ENTITY && world_.has<Position>(tgt)) {
                                    auto& tp = world_.get<Position>(tgt);
                                    tx = tp.x; ty = tp.y; has_target = true;
                                }
                            }
                            auto result = magic::cast(world_, player_, quick_cast_, map_, rng_, log_);
                            if (result.consumed_turn) {
                                player_acted_ = true;
                                ranged_target_ = 0; target_cycle_idx_ = -1;
                            }
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
                        { char qbuf[128];
                          if (input_glyphs_.using_gamepad())
                              snprintf(qbuf, sizeof(qbuf), "No quick-cast spell set. Open %s and set one.",
                                       input_glyphs_.label(Action::SPELLBOOK).c_str());
                          else
                              snprintf(qbuf, sizeof(qbuf), "No quick-cast spell set. Press z then q on a spell.");
                          log_.add(qbuf, {140, 135, 130, 255}); }
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
                        if (!world_map_.is_open() && world_.has<Position>(player_)) {
                            auto& wmp = world_.get<Position>(player_);
                            world_map_.center_on(wmp.x, wmp.y);
                        }
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
                        // Brief black frame to cover synchronous generation
                        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
                        SDL_RenderClear(renderer_);
                        SDL_RenderPresent(renderer_);
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
                                { char tb[256];
                              snprintf(tb, sizeof(tb),
                                "Watch for traps. High PER helps detect them.\n\n"
                                "%s - Rest (heals HP + MP, limited uses per floor)\n"
                                "%s - Toggle sneak\n"
                                "%s - Interact with items, NPCs, containers\n"
                                "%s - Return to the surface",
                                input_glyphs_.label(Action::REST).c_str(),
                                input_glyphs_.label(Action::SNEAK_TOGGLE).c_str(),
                                input_glyphs_.label(Action::INTERACT).c_str(),
                                input_glyphs_.label(Action::STAIRS_UP).c_str());
                              tutorial_popup_.show("Entering a Dungeon", tb); }
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
                            } else if (dname == "Stonekeep") {
                                log_.add("The brand itches. Something written here is meant for you.", brand_nar);
                            } else if (dname == "Stonekeep") {
                                log_.add("Your brand burns bright enough to see by. The walls are warm.", brand_nar);
                            } else if (dname == "The Catacombs") {
                                log_.add("The cold should numb your brand. Instead it burns hotter.", brand_nar);
                            } else if (dname == "The Catacombs") {
                                log_.add("The dead here are older than the gods. Your brand illuminates their faces.", brand_nar);
                            } else if (dname == "The Molten Depths") {
                                log_.add("The heat is immense. Your brand matches it. You feel a fragment calling.", brand_nar);
                            } else if (dname == "The Molten Depths") {
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
                        // (stair damage removed with trait overhaul)
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

    // Status effects (abbreviate if many active)
    if (world_.has<StatusEffects>(player_)) {
        auto& fx = world_.get<StatusEffects>(player_);
        bool abbrev = fx.effects.size() > 4;
        for (auto& eff : fx.effects) {
            const char* tag = "";
            const char* short_tag = "";
            SDL_Color col = {200, 200, 200, 255};
            switch (eff.type) {
                case StatusType::POISON:   tag = "Poison"; short_tag = "PSN"; col = {100, 200, 100, 255}; break;
                case StatusType::BURN:     tag = "Burn";   short_tag = "BRN"; col = {255, 160, 60, 255}; break;
                case StatusType::BLEED:    tag = "Bleed";  short_tag = "BLD"; col = {200, 80, 80, 255}; break;
                case StatusType::FROZEN:   tag = "Frozen"; short_tag = "FRZ"; col = {140, 200, 255, 255}; break;
                case StatusType::STUNNED:  tag = "Stun";   short_tag = "STN"; col = {255, 255, 100, 255}; break;
                case StatusType::CONFUSED: tag = "Confuse";short_tag = "CNF"; col = {200, 140, 255, 255}; break;
                case StatusType::BLIND:    tag = "Blind";  short_tag = "BLN"; col = {120, 120, 120, 255}; break;
                case StatusType::FEARED:   tag = "Fear";   short_tag = "FER"; col = {255, 255, 255, 255}; break;
            }
            draw_tag(abbrev ? short_tag : tag, col);
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
        if (background_ == BackgroundId::FARMER) max_rests = 3;
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

    // Key hints
    draw_tag("x look", {100, 95, 85, 255});
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
    render::draw_entities(renderer_, sprites_, world_, map_, render_cam, y_off, ranger_marked_target_);

    // Target reticle
    if (ranged_target_ != 0 && world_.has<Position>(ranged_target_) &&
        world_.has<Stats>(ranged_target_) && world_.get<Stats>(ranged_target_).hp > 0) {
        auto& tp = world_.get<Position>(ranged_target_);
        if (map_.in_bounds(tp.x, tp.y) && map_.at(tp.x, tp.y).visible) {
            int ts = render_cam.tile_size;
            int sx = (tp.x - render_cam.x) * ts;
            int sy = (tp.y - render_cam.y) * ts + y_off;
            // Pulsing bracket corners (not a plain rectangle)
            Uint32 ticks = SDL_GetTicks();
            int alpha = 160 + static_cast<int>(80.0f * sinf(ticks * 0.006f));
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 255, 200, 40, static_cast<Uint8>(alpha));
            int corner = ts / 4;
            // Top-left corner
            SDL_RenderDrawLine(renderer_, sx, sy, sx + corner, sy);
            SDL_RenderDrawLine(renderer_, sx, sy, sx, sy + corner);
            // Top-right corner
            SDL_RenderDrawLine(renderer_, sx + ts - 1, sy, sx + ts - 1 - corner, sy);
            SDL_RenderDrawLine(renderer_, sx + ts - 1, sy, sx + ts - 1, sy + corner);
            // Bottom-left corner
            SDL_RenderDrawLine(renderer_, sx, sy + ts - 1, sx + corner, sy + ts - 1);
            SDL_RenderDrawLine(renderer_, sx, sy + ts - 1, sx, sy + ts - 1 - corner);
            // Bottom-right corner
            SDL_RenderDrawLine(renderer_, sx + ts - 1, sy + ts - 1, sx + ts - 1 - corner, sy + ts - 1);
            SDL_RenderDrawLine(renderer_, sx + ts - 1, sy + ts - 1, sx + ts - 1, sy + ts - 1 - corner);
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        }
    }

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
                int qidx = static_cast<int>(qid);
                bool is_main = (qidx >= 0 && qidx <= static_cast<int>(QuestId::MQ_09_CLAIM_RELIQUARY));
                if (is_main) {
                    // Main quest: previous quest must be FINISHED
                    if (qidx > 0) {
                        auto prereq = static_cast<QuestId>(qidx - 1);
                        if (!journal_.has_quest(prereq) || journal_.get_state(prereq) != QuestState::FINISHED)
                            continue;
                    }
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

    // God favor edge vignette (champion = god glow, wrath = red pulse)
    if (world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        if (ga.god != GodId::NONE && (ga.favor >= 75 || ga.favor <= -30)) {
            auto& gi = get_god_info(ga.god);
            Uint32 t = SDL_GetTicks();
            float pulse = 0.4f + 0.6f * (0.5f + 0.5f * sinf(t * 0.002f));
            int alpha;
            uint8_t vr, vg, vb;
            if (ga.favor >= 75) {
                vr = gi.color.r; vg = gi.color.g; vb = gi.color.b;
                alpha = static_cast<int>(35 * pulse);
            } else {
                vr = 200; vg = 30; vb = 30;
                alpha = static_cast<int>(50 * pulse);
            }
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            // Top edge
            for (int i = 0; i < 20; i++) {
                int a = alpha * (20 - i) / 20;
                SDL_SetRenderDrawColor(renderer_, vr, vg, vb, static_cast<Uint8>(a));
                SDL_RenderDrawLine(renderer_, 0, HUD_HEIGHT + i, width_, HUD_HEIGHT + i);
            }
            // Bottom edge
            for (int i = 0; i < 20; i++) {
                int a = alpha * (20 - i) / 20;
                SDL_SetRenderDrawColor(renderer_, vr, vg, vb, static_cast<Uint8>(a));
                SDL_RenderDrawLine(renderer_, 0, height_ - LOG_HEIGHT - i, width_, height_ - LOG_HEIGHT - i);
            }
            // Left edge
            for (int i = 0; i < 15; i++) {
                int a = alpha * (15 - i) / 15;
                SDL_SetRenderDrawColor(renderer_, vr, vg, vb, static_cast<Uint8>(a));
                SDL_RenderDrawLine(renderer_, i, HUD_HEIGHT, i, height_ - LOG_HEIGHT);
            }
            // Right edge
            for (int i = 0; i < 15; i++) {
                int a = alpha * (15 - i) / 15;
                SDL_SetRenderDrawColor(renderer_, vr, vg, vb, static_cast<Uint8>(a));
                SDL_RenderDrawLine(renderer_, width_ - 1 - i, HUD_HEIGHT, width_ - 1 - i, height_ - LOG_HEIGHT);
            }
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        }
    }

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

    // God panel (right side, below minimap)
    render_god_panel();

    // Build panel (left side, below HUD)
    render_build_panel();

    // Overlay screens
    inventory_screen_.render(renderer_, font_, sprites_, world_, width_, height_);
    spell_screen_.render(renderer_, font_, world_, width_, height_);
    char_sheet_.render(renderer_, font_, font_title_, sprites_, world_, width_, height_);
    { int qpx = 0, qpy = 0;
      if (dungeon_level_ > 0) {
          qpx = overworld_return_x_;
          qpy = overworld_return_y_;
      } else if (world_.has<Position>(player_)) {
          qpx = world_.get<Position>(player_).x;
          qpy = world_.get<Position>(player_).y;
      }
      quest_log_.render(renderer_, font_, font_title_, journal_, width_, height_, &world_, qpx, qpy); }
    quest_offer_.render(renderer_, font_, font_title_, width_, height_);
    help_screen_.render(renderer_, font_, font_title_, width_, height_);
    passive_tree_screen_.render(renderer_, font_, font_title_, width_, height_);
    church_screen_.render(renderer_, font_, font_title_, width_, height_);
    dialogue_screen_.render(renderer_, font_, font_title_, width_, height_);
    shop_screen_.render(renderer_, font_, sprites_, world_, width_, height_);

    // World map overlay — needs player position and tilemap
    if (world_map_.is_open() && world_.has<Position>(player_)) {
        auto& pos = world_.get<Position>(player_);

        // Set quest target marker for the next incomplete main quest
        world_map_.set_quest_target(-1, -1);
        struct QuestTarget { QuestId id; int x, y; };
        static const QuestTarget QUEST_TARGETS[] = {
            {QuestId::MQ_01_BARROW_WIGHT,   560, 375},  // The Barrow
            {QuestId::MQ_02_SCHOLAR_CLUE,    500, 375},  // Thornwall
            {QuestId::MQ_03_FIRST_FRAGMENT,  650, 325},  // Stonekeep
            {QuestId::MQ_04_SAGE_COUNSEL,    525, 225},  // Frostmere
            {QuestId::MQ_05_SECOND_FRAGMENT, 425, 475},  // The Catacombs
            {QuestId::MQ_06_THIRD_FRAGMENT,  700, 375},  // The Molten Depths
            {QuestId::MQ_07_BREAK_SEAL,      275, 275},  // Hollowgate
            {QuestId::MQ_08_ENTER_SEPULCHRE, 500, 75},   // The Sepulchre
            {QuestId::MQ_09_CLAIM_RELIQUARY, 500, 75},   // The Sepulchre
        };
        for (auto& qt : QUEST_TARGETS) {
            if (journal_.has_quest(qt.id) && journal_.get_state(qt.id) == QuestState::ACTIVE) {
                world_map_.set_quest_target(qt.x, qt.y);
                break;
            }
        }

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

    // Look mode cursor highlight + examine info panel
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

            // Examine info panel: find entity at cursor, show details
            if (font_) {
                int line_h = TTF_FontLineSkip(font_);
                Entity examine_ent = 0;
                auto& pos_pool = world_.pool<Position>();
                for (size_t ei = 0; ei < pos_pool.size(); ei++) {
                    auto& epos = pos_pool.at_index(ei);
                    if (epos.x == look_x_ && epos.y == look_y_) {
                        Entity ee = pos_pool.entity_at(ei);
                        if (ee != player_ && world_.has<Stats>(ee)) {
                            examine_ent = ee;
                            break;
                        }
                    }
                }

                if (examine_ent && world_.has<Stats>(examine_ent)) {
                    auto& est = world_.get<Stats>(examine_ent);
                    // Build info lines
                    char lines[8][128];
                    int nlines = 0;
                    snprintf(lines[nlines++], 128, "%s", est.name.c_str());
                    snprintf(lines[nlines++], 128, "HP %d/%d   Dmg %d   Arm %d",
                             est.hp, est.hp_max, est.melee_damage(), est.protection());
                    snprintf(lines[nlines++], 128, "STR %d  DEX %d  CON %d  WIL %d",
                             est.eff_attr(Attr::STR), est.eff_attr(Attr::DEX),
                             est.eff_attr(Attr::CON), est.eff_attr(Attr::WIL));
                    // Status effects
                    if (world_.has<StatusEffects>(examine_ent)) {
                        auto& efx = world_.get<StatusEffects>(examine_ent);
                        if (!efx.effects.empty()) {
                            std::string sline = "Status:";
                            for (auto& sf : efx.effects) {
                                switch (sf.type) {
                                    case StatusType::POISON:  sline += " Poisoned"; break;
                                    case StatusType::BURN:    sline += " Burning"; break;
                                    case StatusType::BLEED:   sline += " Bleeding"; break;
                                    case StatusType::FROZEN:  sline += " Frozen"; break;
                                    case StatusType::STUNNED: sline += " Stunned"; break;
                                    case StatusType::CONFUSED:sline += " Confused"; break;
                                    case StatusType::BLIND:   sline += " Blinded"; break;
                                    case StatusType::FEARED:  sline += " Feared"; break;
                                }
                            }
                            snprintf(lines[nlines++], 128, "%s", sline.c_str());
                        }
                    }
                    // Behavior hint
                    if (world_.has<AI>(examine_ent)) {
                        auto& eai = world_.get<AI>(examine_ent);
                        const char* bhint = "";
                        switch (eai.behavior) {
                            case BehaviorType::LICH: bhint = "Drains life. Teleports."; break;
                            case BehaviorType::DRAGON: bhint = "Breathes fire."; break;
                            case BehaviorType::TROLL: bhint = "Regenerates."; break;
                            case BehaviorType::CHARGER: bhint = "Charges in lines."; break;
                            case BehaviorType::PACK: bhint = "Flanks with allies."; break;
                            case BehaviorType::WRAITH: bhint = "Phases through walls."; break;
                            case BehaviorType::NECROMANCER: bhint = "Raises the dead."; break;
                            case BehaviorType::SHAMAN: bhint = "Heals and buffs allies."; break;
                            case BehaviorType::ARCHER: bhint = "Ranged attacks."; break;
                            case BehaviorType::THIEF: bhint = "Hit and run."; break;
                            default: break;
                        }
                        if (bhint[0]) snprintf(lines[nlines++], 128, "%s", bhint);
                    }

                    // Position panel to the right of cursor, or left if near edge
                    int panel_w = 250;
                    int panel_h = nlines * (line_h + 2) + 12;
                    int panel_x = cursor_rect.x + cursor_rect.w + 8;
                    int panel_y = cursor_rect.y;
                    if (panel_x + panel_w > width_ - 10)
                        panel_x = cursor_rect.x - panel_w - 8;
                    if (panel_y + panel_h > height_ - 40)
                        panel_y = height_ - 40 - panel_h;

                    ui::draw_panel(renderer_, panel_x, panel_y, panel_w, panel_h);

                    SDL_Color name_col = {220, 200, 140, 255};
                    SDL_Color stat_col = {180, 170, 160, 255};
                    SDL_Color hint_col = {140, 160, 120, 255};
                    int ty = panel_y + 6;
                    for (int li = 0; li < nlines; li++) {
                        SDL_Color c = (li == 0) ? name_col : (li == nlines - 1 && world_.has<AI>(examine_ent)) ? hint_col : stat_col;
                        ui::draw_text_clipped(renderer_, font_, lines[li], c,
                                               panel_x + 8, ty, panel_w - 16);
                        ty += line_h + 2;
                    }
                }
            }
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

    // Minimap (scales with screen, no hard cap)
    int mm_size = std::min(width_ / 3, height_ / 3);
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

void Engine::open_npc_dialogue(Entity target) {
    if (!world_.has<NPC>(target) || dialogue_screen_.is_open()) return;
    auto& npc = world_.get<NPC>(target);
    std::vector<DialogueOption> opts;

    // Church priest: add church services
    if (world_.has<Church>(target) && world_.has<GodAlignment>(player_)) {
        auto& church = world_.get<Church>(target);
        auto& ga = world_.get<GodAlignment>(player_);
        if (ga.god == church.god || ga.god == GodId::NONE) {
            opts.push_back({"Church services", 5, true});
            opts.push_back({"Talk", 2, true});
            dialogue_screen_.open(npc.name, npc.dialogue, opts);
            dialogue_npc_ = target;
            return;
        } else {
            auto& ginfo = get_god_info(church.god);
            char buf[128];
            snprintf(buf, sizeof(buf), "This is the Church of %s. You serve another god.", ginfo.name);
            opts.push_back({"Talk", 2, true});
            dialogue_screen_.open(npc.name, buf, opts);
            dialogue_npc_ = target;
            return;
        }
    }

    // Role-based options
    if (npc.role == NPCRole::SHOPKEEPER || npc.role == NPCRole::BLACKSMITH) {
        opts.push_back({"Browse wares", 1, true});
        opts.push_back({"Talk", 2, true});
    } else if (npc.role == NPCRole::INNKEEPER) {
        int inn_cost = 10;
        if (world_.has<Position>(player_)) {
            auto& ip = world_.get<Position>(player_);
            float id = std::sqrt(static_cast<float>((ip.x-500)*(ip.x-500)+(ip.y-375)*(ip.y-375)));
            inn_cost = 10 + static_cast<int>(id / 30.0f);
        }
        char rest_label[48]; snprintf(rest_label, sizeof(rest_label), "Rest (%d gold)", inn_cost);
        opts.push_back({rest_label, 3, gold_ >= inn_cost});
        opts.push_back({"Talk", 2, true});
    } else {
        opts.push_back({"Talk", 2, true});
    }

    // Quest option
    if (npc.quest_id >= 0) {
        auto qid = static_cast<QuestId>(npc.quest_id);
        int qidx = static_cast<int>(qid);
        bool prereq_ok = true;
        if (qidx > 0 && qidx <= static_cast<int>(QuestId::MQ_09_CLAIM_RELIQUARY)) {
            auto prereq = static_cast<QuestId>(qidx - 1);
            if (!journal_.has_quest(prereq) || journal_.get_state(prereq) != QuestState::FINISHED)
                prereq_ok = false;
        }
        if (journal_.get_state(qid) == QuestState::COMPLETE)
            opts.push_back({"Turn in quest", 4, true});
        else if (!journal_.has_quest(qid) && prereq_ok)
            opts.push_back({"Ask about work", 4, true});
        else if (journal_.get_state(qid) == QuestState::ACTIVE)
            opts.push_back({"Ask about quest", 4, true});
    }

    if (!opts.empty()) {
        dialogue_screen_.open(npc.name, npc.dialogue, opts);
        dialogue_npc_ = target;
    }
}

void Engine::render_build_panel() {
    if (!font_ || !world_.has<Player>(player_)) return;

    auto& player = world_.get<Player>(player_);
    int line_h = TTF_FontLineSkip(font_);

    // Panel on left side (proportional, readable)
    int panel_w = width_ / 6;
    int panel_x = 6;
    int panel_y = HUD_HEIGHT + 6;
    int text_w = panel_w - 14;
    int tx = panel_x + 7;
    int ty = panel_y + 6;

    // Measure content height first (class + description + abilities + traits + background)
    int est_lines = 3; // class name + spacing
    est_lines += 4;    // class description (~3-4 wrapped lines)
    est_lines += 5;    // gear synergy + lv5 ability + active hint + prayer
    est_lines += static_cast<int>(build_traits_.size()) * 3; // trait name + desc
    est_lines += 3;    // background
    int panel_h = line_h * est_lines + 20;
    int max_h = height_ - LOG_HEIGHT - panel_y - 8;
    if (panel_h > max_h) panel_h = max_h;

    // Background
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_Rect bg = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(renderer_, 10, 8, 14, 180);
    SDL_RenderFillRect(renderer_, &bg);

    // Accent line
    uint8_t ar = 180, ag = 160, ab = 100;
    switch (player.class_id) {
        case ClassId::WIZARD: case ClassId::WARLOCK: case ClassId::NECROMANCER: case ClassId::SCHEMA_MONK:
            ar = 140; ag = 140; ab = 220; break;
        case ClassId::DRUID: case ClassId::RANGER:
            ar = 80; ag = 180; ab = 80; break;
        case ClassId::TEMPLAR: case ClassId::WAR_CLERIC:
            ar = 220; ag = 200; ab = 100; break;
        default: break;
    }
    SDL_SetRenderDrawColor(renderer_, ar, ag, ab, 200);
    SDL_Rect accent = {panel_x, panel_y, 3, panel_h};
    SDL_RenderFillRect(renderer_, &accent);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);

    // Helper: measure actual wrapped text height using TTF
    auto wrap_h = [&](const char* text) -> int {
        if (!text || !text[0] || text_w <= 0) return line_h;
        SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(font_, text, {255,255,255,255},
                                                            static_cast<Uint32>(text_w));
        if (!surf) return line_h;
        int h = surf->h;
        SDL_FreeSurface(surf);
        return h;
    };

    // Class name
    const auto& cls = get_class_info(player.class_id);
    SDL_Color class_col = {ar, ag, ab, 255};
    ui::draw_text_clipped(renderer_, font_, cls.name, class_col, tx, ty, text_w);
    ty += line_h + 4;

    // Class description (wrapped, skip by actual height)
    ui::draw_text_wrapped(renderer_, font_, cls.description, {160, 155, 145, 255}, tx, ty, text_w);
    ty += wrap_h(cls.description) + 6;

    // Class abilities section
    auto details = get_class_details(player.class_id);
    SDL_Color ability_col = {200, 200, 140, 255};
    SDL_Color dim_ability = {150, 145, 130, 255};

    // Gear synergy
    ui::draw_text_wrapped(renderer_, font_, details.gear_synergy, dim_ability, tx, ty, text_w);
    ty += wrap_h(details.gear_synergy) + 4;

    // Level 5 ability
    if (world_.has<Stats>(player_) && world_.get<Stats>(player_).level >= 5) {
        ui::draw_text_wrapped(renderer_, font_, details.level5_ability, ability_col, tx, ty, text_w);
        ty += wrap_h(details.level5_ability) + 4;
    } else {
        char lv5buf[64]; snprintf(lv5buf, sizeof(lv5buf), "Lv5: ???");
        ui::draw_text(renderer_, font_, lv5buf, {80, 75, 70, 255}, tx, ty);
        ty += line_h + 4;
    }

    // Active ability reminder (Knight/Dwarf wait, prayer key)
    {
        const char* active_hint = nullptr;
        switch (player.class_id) {
            case ClassId::KNIGHT: active_hint = "[.] Wait = Bulwark (block 2x)"; break;
            case ClassId::DWARF:  active_hint = "[.] Wait = Fortify (next hit 2x)"; break;
            default: break;
        }
        if (active_hint) {
            ui::draw_text_wrapped(renderer_, font_, active_hint, {180, 200, 140, 255}, tx, ty, text_w);
            ty += line_h + 2;
        }
    }

    // Prayer reminder
    if (world_.has<GodAlignment>(player_) && world_.get<GodAlignment>(player_).god != GodId::NONE) {
        ui::draw_text(renderer_, font_, "[P] Pray (1/2/3)", {160, 160, 180, 255}, tx, ty);
        ty += line_h + 2;
    }

    ty += 4;

    // Traits with descriptions
    if (!build_traits_.empty()) {
        for (auto tid : build_traits_) {
            if (ty >= panel_y + panel_h - line_h * 2) break;
            const auto& tr = get_trait_info(tid);
            ui::draw_text_clipped(renderer_, font_, tr.name, {220, 200, 140, 255}, tx, ty, text_w);
            ty += line_h + 2;
            ui::draw_text_wrapped(renderer_, font_, tr.description, {140, 135, 125, 255}, tx, ty, text_w);
            ty += wrap_h(tr.description) + 6;
        }
    }

    // Background
    if (ty < panel_y + panel_h - line_h * 2) {
        auto& bg_info = get_background_info(background_);
        ui::draw_text_clipped(renderer_, font_, bg_info.name, {160, 180, 140, 255}, tx, ty, text_w);
        ty += line_h + 2;
        if (bg_info.passive_desc[0]) {
            ui::draw_text_wrapped(renderer_, font_, bg_info.passive_desc, {130, 125, 115, 255}, tx, ty, text_w);
            ty += wrap_h(bg_info.passive_desc) + 4;
        }
    }
}

void Engine::render_god_panel() {
    if (!font_ || !world_.has<GodAlignment>(player_)) return;
    auto& ga = world_.get<GodAlignment>(player_);
    if (ga.god == GodId::NONE) return; // heretics have no god panel

    auto& ginfo = get_god_info(ga.god);
    auto tenets = get_god_tenets(ga.god);
    int line_h = TTF_FontLineSkip(font_);
    Uint32 ticks = SDL_GetTicks();

    // Panel on right side (proportional, readable)
    int panel_w = width_ / 6;
    int panel_x = width_ - panel_w - 4;
    int panel_y = HUD_HEIGHT + 8;
    if (dungeon_level_ > 0) {
        int god_mm = std::min(width_ / 3, height_ / 3);
        panel_y = HUD_HEIGHT + god_mm + 12;
    }
    // Expand vertically based on content (wrapped text takes more lines)
    int content_lines = 5 + tenets.count * 2; // name, bar, passive (2 lines), tenets (2 each), status
    if (zealot_fury_turns_ > 0 || (world_.has<Stats>(player_) && world_.get<Stats>(player_).phase_turns > 0))
        content_lines += 1;
    if (druid_beast_turns_ > 0) content_lines += 1;
    if (serpentine_stacks_ > 0) content_lines += 1;
    int panel_h = line_h * content_lines + 20;
    int max_panel_h = height_ - LOG_HEIGHT - panel_y - 8;
    if (panel_h > max_panel_h) panel_h = max_panel_h;

    // Semi-transparent background with god-colored edge glow
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_Rect bg = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(renderer_, 10, 8, 14, 200);
    SDL_RenderFillRect(renderer_, &bg);

    // God-colored left accent bar (pulsing intensity based on favor)
    float pulse = 0.7f + 0.3f * sinf(ticks * 0.003f);
    int accent_alpha = static_cast<int>(220 * pulse);
    SDL_SetRenderDrawColor(renderer_, ginfo.color.r, ginfo.color.g, ginfo.color.b,
                           static_cast<Uint8>(accent_alpha));
    SDL_Rect accent = {panel_x, panel_y, 3, panel_h};
    SDL_RenderFillRect(renderer_, &accent);

    // Top glow line
    SDL_SetRenderDrawColor(renderer_, ginfo.color.r, ginfo.color.g, ginfo.color.b, 100);
    SDL_RenderDrawLine(renderer_, panel_x, panel_y, panel_x + panel_w, panel_y);

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);

    int tx = panel_x + 10;
    int ty = panel_y + 6;

    // God name + title
    SDL_Color god_col = {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255};
    int text_max_w = panel_w - 12;
    ui::draw_text_clipped(renderer_, font_, ginfo.name, god_col, tx, ty, text_max_w);
    ty += line_h;

    // Favor bar (wider, more detailed)
    int bar_w = panel_w - 20;
    int bar_h = 10;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_Rect bar_bg = {tx, ty + 1, bar_w, bar_h};
    SDL_SetRenderDrawColor(renderer_, 25, 20, 30, 255);
    SDL_RenderFillRect(renderer_, &bar_bg);

    int fill = (ga.favor + 100) * bar_w / 200;
    fill = std::max(0, std::min(bar_w, fill));
    SDL_Rect bar_fill = {tx, ty + 1, fill, bar_h};

    // Gradient: deep red -> dim -> god color
    if (ga.favor <= -60)
        SDL_SetRenderDrawColor(renderer_, 180, 30, 30, 255);
    else if (ga.favor < 0)
        SDL_SetRenderDrawColor(renderer_, 180, 80, 40, 255);
    else if (ga.favor >= 75)
        SDL_SetRenderDrawColor(renderer_, ginfo.color.r, ginfo.color.g, ginfo.color.b, 255);
    else
        SDL_SetRenderDrawColor(renderer_, ginfo.color.r * 3/4, ginfo.color.g * 3/4, ginfo.color.b * 3/4, 255);
    SDL_RenderFillRect(renderer_, &bar_fill);

    // Threshold markers at 25, 50, 75
    SDL_SetRenderDrawColor(renderer_, 80, 70, 90, 180);
    for (int thresh : {25, 50, 75}) {
        int mx = tx + (thresh + 100) * bar_w / 200;
        SDL_RenderDrawLine(renderer_, mx, ty + 1, mx, ty + 1 + bar_h);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);

    // Favor number (right-aligned)
    char fbuf[16]; snprintf(fbuf, sizeof(fbuf), "%d", ga.favor);
    SDL_Color fav_col = ga.favor < 0 ? SDL_Color{200, 80, 60, 255} : SDL_Color{180, 180, 160, 255};
    ui::draw_text(renderer_, font_, fbuf, fav_col, tx + bar_w - 20, ty);
    ty += line_h + 2;

    // Passive effect (short, god-colored)
    const char* passive_short = "";
    switch (ga.god) {
        case GodId::VETHRIK:  passive_short = "Undead ignore. Bone 2x."; break;
        case GodId::THESSARKA:passive_short = "Auto-map. Identify all."; break;
        case GodId::MORRETH:  passive_short = "First hit 2x. No retreat."; break;
        case GodId::YASHKHET: passive_short = "Lifesteal 15%. No rest heal."; break;
        case GodId::KHAEL:    passive_short = "Animals allied. -4 in dungeon."; break;
        case GodId::SOLETH:   passive_short = "+3 fire all hits. Dark burns."; break;
        case GodId::IXUUL:    passive_short = "Status immune. Mutating."; break;
        case GodId::ZHAVEK:   passive_short = "Invisible. Backstab 3x."; break;
        case GodId::THALARA:  passive_short = "+20 speed. Fire 2x weak."; break;
        case GodId::OSSREN:   passive_short = "+1/gear slot. Gear bound."; break;
        case GodId::LETHIS:   passive_short = "Lethal save. Full rest heal."; break;
        case GodId::GATHRUUN: passive_short = "+5 armor. +4 underground."; break;
        case GodId::SYTHARA:  passive_short = "All hits poison. Heal halved."; break;
        default: break;
    }
    SDL_Color passive_col = {ginfo.color.r * 3/4 + 60, ginfo.color.g * 3/4 + 60,
                             ginfo.color.b * 3/4 + 60, 255};
    // Measure actual wrapped text height using TTF
    auto god_wrap_h = [&](const char* text) -> int {
        if (!text || !text[0] || text_max_w <= 0) return line_h;
        SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(font_, text, {255,255,255,255},
                                                            static_cast<Uint32>(text_max_w));
        if (!surf) return line_h;
        int h = surf->h;
        SDL_FreeSurface(surf);
        return h;
    };

    ui::draw_text_wrapped(renderer_, font_, passive_short, passive_col, tx, ty, text_max_w);
    ty += god_wrap_h(passive_short) + 4;

    // Tenets (wrapped, proper spacing)
    SDL_Color tenet_col = {130, 125, 115, 255};
    for (int i = 0; i < tenets.count; i++) {
        if (ty >= panel_y + panel_h - line_h) break;
        ui::draw_text_wrapped(renderer_, font_, tenets.tenets[i].description, tenet_col, tx, ty, text_max_w);
        ty += god_wrap_h(tenets.tenets[i].description) + 4;
    }

    // Prayers available ([P] to use)
    auto prayers = get_prayers(ga.god);
    if (prayers && ty < panel_y + panel_h - line_h * 4) {
        ui::draw_text(renderer_, font_, "[P] Prayers:", {180, 180, 200, 255}, tx, ty);
        ty += line_h + 2;
        for (int pi = 0; pi < 2; pi++) {
            if (ty >= panel_y + panel_h - line_h * 2) break;
            char pbuf[80];
            snprintf(pbuf, sizeof(pbuf), "%d. %s (%d)", pi + 1, prayers[pi].name, prayers[pi].favor_cost);
            ui::draw_text_clipped(renderer_, font_, pbuf, {160, 155, 145, 255}, tx, ty, text_max_w);
            ty += line_h;
        }
        if (ga.favor >= 75) {
            ui::draw_text_clipped(renderer_, font_, "3. Mastery (15)", {220, 200, 100, 255}, tx, ty, text_max_w);
            ty += line_h;
        }
        ty += 4;
    }

    // Active buffs/status
    if (zealot_fury_turns_ > 0) {
        char zb[32]; snprintf(zb, sizeof(zb), "ZEALOT FURY %d", zealot_fury_turns_);
        ui::draw_text(renderer_, font_, zb, {255, 220, 80, 255}, tx, ty);
        ty += line_h;
    }
    if (world_.has<Stats>(player_) && world_.get<Stats>(player_).phase_turns > 0) {
        char pb[32]; snprintf(pb, sizeof(pb), "PHASING %d", world_.get<Stats>(player_).phase_turns);
        ui::draw_text(renderer_, font_, pb, {160, 100, 220, 255}, tx, ty);
        ty += line_h;
    }

    // Rank indicator (bottom, prominent)
    const char* rank_text = nullptr;
    SDL_Color rank_col = {200, 200, 200, 255};
    if (ga.favor >= 75) { rank_text = "CHAMPION"; rank_col = {255, 220, 80, 255}; }
    else if (ga.favor >= 50) { rank_text = "DEVOTED"; rank_col = {200, 200, 140, 255}; }
    else if (ga.favor <= -60) { rank_text = "WRATH"; rank_col = {255, 40, 40, 255}; }
    else if (ga.favor <= -30) { rank_text = "DISPLEASED"; rank_col = {220, 120, 60, 255}; }
    if (rank_text) {
        ui::draw_text(renderer_, font_, rank_text, rank_col, tx, ty);
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
