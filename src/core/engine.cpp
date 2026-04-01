#include "core/engine.h"
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

    // Get display resolution for fullscreen default
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        width_ = dm.w;
        height_ = dm.h;
    }

    window_ = SDL_CreateWindow(
        "Reliquary",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width_, height_,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN_DESKTOP
    );
    fullscreen_ = true;
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
    settings_.set_audio(&audio_);
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
            overworld_cache_ = mapfile::load("data/maps/overworld.map");
            overworld_loaded_ = true;
        }
        auto& mresult = overworld_cache_;
        map_ = mresult.map; // copy (preserves cache, gets fresh explored state)
        start_x = mresult.start_x;
        start_y = mresult.start_y;
        rooms_.clear();

        // Dialogue pools — deterministic selection via position hash
        static const char* FARMER_DIALOGUE[] = {
            "Used to be quiet here. Before they opened the barrow.",
            "My grandfather said there were older things than gods buried in these hills.",
            "The scholar knows more than he lets on. Always reading those old texts.",
        };
        static const char* GUARD_DIALOGUE[] = {
            "Keep your blade sheathed in town.",
            "Something's been killing livestock east of here. Stay sharp.",
            "The barrow's been sealed for generations. Now it's open.",
        };
        static const char* SCHOLAR_DIALOGUE[] = {
            "The Reliquary predates the gods we know. What made it? I don't think we want to know.",
            "Each god claims the Reliquary is theirs by right. They're all wrong.",
            "The inscriptions in the deep places are in no language I recognize.",
        };
        static const char* BLACKSMITH_DIALOGUE[] = {
            "Iron holds. Steel bites. That's all you need to know.",
            "I can repair anything made by human hands. What's down there... I'm not sure.",
            "The ore from the deep mines has a strange color. I don't like working with it.",
        };

        // Helper: deterministic dialogue pick from position hash
        auto pick_dialogue = [](const char* pool[], int pool_size, int x, int y) -> const char* {
            unsigned h = static_cast<unsigned>(x * 31 + y * 17 + x * y * 7);
            return pool[h % pool_size];
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
                    npc_comp.name = "Shopkeeper";
                    npc_comp.dialogue = "Browse, if you like. I don't haggle.";
                    sx = 2; sy = 6; // shopkeep sprite (row 7)
                    // Side quest: Ashford shopkeeper — rats in the cellar
                    if (town_idx == 1 && !sq_ratcellar_assigned) {
                        npc_comp.quest_id = static_cast<int>(QuestId::SQ_RAT_CELLAR);
                        npc_comp.dialogue = "Rats in the cellar. Every night, more of them. I'll pay you to clear them out.";
                        sq_ratcellar_assigned = true;
                    }
                    break;
                case 'B':
                    npc_comp.role = NPCRole::BLACKSMITH;
                    npc_comp.name = "Blacksmith";
                    npc_comp.dialogue = pick_dialogue(BLACKSMITH_DIALOGUE, 3, me.x, me.y);
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
                    npc_comp.name = "Scholar";
                    npc_comp.dialogue = pick_dialogue(SCHOLAR_DIALOGUE, 3, me.x, me.y);
                    sx = 5; sy = 5; // scholar sprite (row 6)
                    // MQ_02: Thornwall scholar
                    if (town_idx == 0 && !mq_assigned[1]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_02_SCHOLAR_CLUE);
                        npc_comp.name = "Scholar Aldric";
                        npc_comp.dialogue = "The old texts speak of things that should stay buried.";
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
                    npc_comp.name = "Farmer";
                    npc_comp.dialogue = pick_dialogue(FARMER_DIALOGUE, 3, me.x, me.y);
                    sx = 0; sy = 5; // farmer sprite (row 6)
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
                    npc_comp.name = "Guard";
                    npc_comp.dialogue = pick_dialogue(GUARD_DIALOGUE, 3, me.x, me.y);
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
                    npc_comp.name = "Villager";
                    npc_comp.dialogue = pick_dialogue(FARMER_DIALOGUE, 3, me.x, me.y);
                    sx = 1; sy = 6;
                    break;
                case 'H':
                    npc_comp.role = NPCRole::PRIEST; // herbalist uses priest role
                    npc_comp.name = "Herbalist";
                    npc_comp.dialogue = "The wilds hold remedies for every ill, if you know where to look.";
                    sx = 3; sy = 6;
                    break;
                case 'M':
                    npc_comp.role = NPCRole::SHOPKEEPER;
                    npc_comp.name = "Merchant";
                    npc_comp.dialogue = "I trade in what the road provides. Take a look.";
                    sx = 2; sy = 6;
                    break;
                case 'E':
                    npc_comp.role = NPCRole::ELDER;
                    npc_comp.name = "Elder Maren";
                    npc_comp.dialogue = "A wight has risen in the barrow just east of town. "
                                        "Follow the road east. You will see the entrance.";
                    npc_comp.quest_id = static_cast<int>(QuestId::MQ_01_BARROW_WIGHT);
                    mq_assigned[0] = true;
                    sx = 4; sy = 6; // elderly man sprite (row 7)
                    break;
            }
            npc_comp.home_x = me.x;
            npc_comp.home_y = me.y;
            npc_comp.god_affiliation = get_town_god(me.x, me.y);
            world_.add<NPC>(npc, std::move(npc_comp));
            world_.add<Renderable>(npc, {SHEET_ROGUES, sx, sy, {255, 255, 255, 255}, 5});

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
            spawn_extra_npc(ALL_TOWNS[i].x, ALL_TOWNS[i].y, "Herbalist", NPCRole::PRIEST,
                            HERBALIST_LINES[rng_.range(0, 2)], 3, 6);
            spawn_extra_npc(ALL_TOWNS[i].x, ALL_TOWNS[i].y, "Merchant", NPCRole::SHOPKEEPER,
                            MERCHANT_LINES[rng_.range(0, 2)], 2, 6);
        }

        // Populate overworld with wilderness content
        populate_overworld();

        // Generate dynamic side quests for each town's NPCs
        for (int i = 0; i < TOWN_COUNT; i++) {
            quest_gen::generate_town_quests(world_, map_, rng_,
                ALL_TOWNS[i].x, ALL_TOWNS[i].y, ALL_TOWNS[i].name);
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
        // NPC — talk instead of fight
        if (world_.has<NPC>(target)) {
            npc_interaction::Context npc_ctx {
                world_, player_, log_, audio_, rng_, particles_,
                shop_screen_, quest_offer_, levelup_screen_,
                journal_, meta_, gold_, game_turn_, dungeon_level_,
                pending_levelup_, pending_quest_npc_
            };
            if (npc_interaction::interact(npc_ctx, target, nx, ny))
                return;
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

            switch (ga.god) {
                case GodId::VETHRIK:
                    // +15% vs undead, +2 with bone weapons
                    if (is_undead(tgt_stats.name.c_str())) bonus = std::max(1, atk_result.damage * 15 / 100);
                    if (weapon_tags & TAG_BONE_ITEM) bonus += 2;
                    break;
                case GodId::MORRETH:
                    // +2 damage with blunt or axe weapons
                    if (weapon_tags & (TAG_BLUNT | TAG_AXE)) bonus = 2;
                    break;
                case GodId::YASHKHET: {
                    // +15% below 50% HP, +1 with daggers
                    auto& ps = world_.get<Stats>(player_);
                    if (ps.hp * 2 < ps.hp_max) bonus = std::max(1, atk_result.damage * 15 / 100);
                    if (weapon_tags & TAG_DAGGER) bonus += 1;
                    break;
                }
                case GodId::SOLETH:
                    // +2 vs undead (holy smite)
                    if (is_undead(tgt_stats.name.c_str())) bonus = 2;
                    break;
                case GodId::ZHAVEK:
                    // 2x from stealth, break invisibility
                    if (world_.get<Stats>(player_).invisible_turns > 0) {
                        bonus = atk_result.damage; // 2x total
                        world_.get<Stats>(player_).invisible_turns = 0;
                    }
                    break;
                case GodId::OSSREN:
                    // All equipped weapons deal +1 (craftsmanship)
                    if (weapon_tags != 0) bonus = 1;
                    break;
                case GodId::GATHRUUN:
                    // +2 in dungeons (stone's strength)
                    if (dungeon_level_ > 0) bonus = 2;
                    break;
                case GodId::SYTHARA:
                    // 15% chance to poison enemy on hit
                    if (rng_.chance(15) && !atk_result.killed) {
                        if (!world_.has<StatusEffects>(target))
                            world_.add<StatusEffects>(target, {});
                        world_.get<StatusEffects>(target).add(StatusType::POISON, 2, 8);
                        log_.add("Your touch carries Sythara's gift.", {120, 180, 60, 255});
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

        if (atk_result.hit && atk_result.critical) { audio_.play(SfxId::CRIT); particles_.crit_flash((float)nx, (float)ny); trigger_screen_shake(4.0f); }
        else if (atk_result.hit) { audio_.play_hit(); particles_.blood((float)nx, (float)ny); }
        else audio_.play_miss();
        if (atk_result.killed) { audio_.play(SfxId::DEATH); particles_.death_burst((float)nx, (float)ny); trigger_screen_shake(3.0f); }

        // Quest target killed?
        if (atk_result.quest_target_id >= 0) {
            auto qid = static_cast<QuestId>(atk_result.quest_target_id);
            if (journal_.has_quest(qid) && journal_.get_state(qid) == QuestState::ACTIVE) {
                journal_.set_state(qid, QuestState::COMPLETE);
                auto& qinfo = get_quest_info(qid);
                // Flavor text based on quest type
                bool is_main = (static_cast<int>(qid) < 20); // main quests are low IDs
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
                        snprintf(hbuf, sizeof(hbuf), "Bloodlust. (+%d HP)", heal);
                        log_.add(hbuf, {200, 100, 100, 255});
                    }
                }
            }
        }

        // Crow scavenges gold from kills
        if (atk_result.killed && world_.has<Inventory>(player_)) {
            Entity pet_item = world_.get<Inventory>(player_).get_equipped(EquipSlot::PET);
            if (pet_item != NULL_ENTITY && world_.has<Item>(pet_item)
                && world_.get<Item>(pet_item).pet_id == static_cast<int>(PetId::CROW)) {
                int scav = rng_.range(1, 5 + dungeon_level_);
                gold_ += scav;
                char sbuf[64];
                snprintf(sbuf, sizeof(sbuf), "Your crow scavenges %d gold.", scav);
                log_.add(sbuf, {200, 190, 100, 255});
            }
        }

        // Check for level-up
        if (world_.has<Stats>(player_) && world_.get<Stats>(player_).level > level_before) {
            pending_levelup_ = true;
            levelup_screen_.open(player_, rng_);
            audio_.play(SfxId::LEVELUP);
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

    // Town arrival text (first visit only)
    if (dungeon_level_ == 0) {
        int ti = near_town(nx, ny, 20);
        if (ti >= 0 && visited_towns_.find(ti) == visited_towns_.end()) {
            visited_towns_.insert(ti);
            char tbuf[128];
            snprintf(tbuf, sizeof(tbuf), "You arrive at %s.", ALL_TOWNS[ti].name);
            log_.add(tbuf, {200, 190, 160, 255});
            // Province flavor
            const char* province = get_province_name(nx, ny);
            char pbuf[128];
            snprintf(pbuf, sizeof(pbuf), "A settlement in the %s.", province);
            log_.add(pbuf, {160, 155, 140, 255});
        }
    }

    // Overworld travel events — rare roadside discoveries
    if (dungeon_level_ == 0 && near_town(nx, ny, 40) < 0 && rng_.chance(2)) {
        int ev = rng_.range(1, 100);
        if (ev <= 20) {
            // Find a small pouch of gold
            int amount = rng_.range(5, 20);
            gold_ += amount;
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

    // Check tenet violations for this turn's actions
    check_tenets();
    turn_actions_.clear();

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
    ai::process(world_, map_, player_, rng_);

    // NPC wandering (overworld only)
    if (dungeon_level_ == 0) {
        process_npc_wander();
        // Check for town proximity music change every 10 turns
        if (game_turn_ % 10 == 0) update_music_for_location();
    }

    // Overworld enemy spawning
    if (dungeon_level_ == 0 && game_turn_ % 12 == 0) {
        try_spawn_overworld_enemy();
    }

    // Monsters attack player — melee if adjacent, ranged if in range
    auto& ai_pool = world_.pool<AI>();
    for (size_t i = 0; i < ai_pool.size(); i++) {
        Entity e = ai_pool.entity_at(i);
        if (!world_.has<Position>(e) || !world_.has<Stats>(e)) continue;
        if (!world_.has<Energy>(e) || !world_.get<Energy>(e).can_act()) continue;

        auto& ai_comp = ai_pool.at_index(i);
        if (ai_comp.state != AIState::HUNTING) continue;

        auto& mpos = world_.get<Position>(e);
        auto& ppos = world_.get<Position>(player_);

        int dx = std::abs(mpos.x - ppos.x);
        int dy = std::abs(mpos.y - ppos.y);
        int dist = std::max(dx, dy);

        if (dist <= 1 && dist > 0) {
            // Melee attack
            auto mresult = combat::melee_attack(world_, e, player_, rng_, log_);
            if (mresult.hit) {
                auto& pp = world_.get<Position>(player_);
                if (mresult.critical) { particles_.crit_flash(pp.x, pp.y); trigger_screen_shake(5.0f); }
                else particles_.blood(pp.x, pp.y);
                // Track what hit us for death screen
                if (world_.has<Stats>(e))
                    death_cause_ = world_.get<Stats>(e).name;
            }
            // Status effects from monster hits
            if (mresult.hit && world_.has<Stats>(e) && world_.has<StatusEffects>(player_)) {
                auto& mname = world_.get<Stats>(e).name;
                auto& fx = world_.get<StatusEffects>(player_);
                // Poison: spiders, naga, snakes
                if ((mname == "giant spider" || mname == "naga" || mname == "snake") && rng_.chance(25))
                    fx.add(StatusType::POISON, 2, 5);
                // Bleed: ghouls, wolves, bears
                else if ((mname == "ghoul" || mname == "wolf" || mname == "dire wolf" || mname == "bear") && rng_.chance(20))
                    fx.add(StatusType::BLEED, 1, 8);
                // Burn: dragons
                else if (mname == "dragon" && rng_.chance(30))
                    fx.add(StatusType::BURN, 3, 4);
                // Stun: trolls, orc warchief (heavy hit)
                else if ((mname == "troll" || mname == "orc warchief") && rng_.chance(20))
                    fx.add(StatusType::STUNNED, 0, 1);
                // Freeze: ice creatures
                else if ((mname == "ice elemental" || mname == "frost drake") && rng_.chance(25))
                    fx.add(StatusType::FROZEN, 0, 1);
                // Confuse: wraiths, banshees
                else if ((mname == "wraith" || mname == "banshee") && rng_.chance(20))
                    fx.add(StatusType::CONFUSED, 0, 3);
                // Fear: death knight on crit
                else if (mname == "death knight" && mresult.critical)
                    fx.add(StatusType::FEARED, 0, 2);
                // Blind: basilisk
                else if (mname == "basilisk" && rng_.chance(15))
                    fx.add(StatusType::BLIND, 0, 3);
            }
            // Troll/slime regeneration — heals HP per turn if alive
            if (world_.has<Stats>(e)) {
                auto& ms = world_.get<Stats>(e);
                if (ms.name == "troll" && ms.hp > 0 && ms.hp < ms.hp_max)
                    ms.hp = std::min(ms.hp_max, ms.hp + 2);
                if (ms.name == "slime" && ms.hp > 0 && ms.hp < ms.hp_max)
                    ms.hp = std::min(ms.hp_max, ms.hp + 1);
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
                    if (!con_resist()) {
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
                if (rresult.hit) { audio_.play_bow_hit(); particles_.hit_spark(pp.x, pp.y); }
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
                // Naga gaze — applies STUNNED
                if (world_.has<StatusEffects>(player_))
                    world_.get<StatusEffects>(player_).add(StatusType::STUNNED, 0, 2);
                log_.add("The naga's gaze locks your muscles.", {255, 255, 100, 255});
            } else if (mstats.name == "wraith" && dist <= 3 && rng_.chance(30)) {
                // Wraith wail — applies CONFUSED (blocked by Iron Willed)
                if (world_.has<StatusEffects>(player_)) {
                    bool immune = false;
                    for (auto tid : build_traits_) if (get_trait_info(tid).immune_confuse) immune = true;
                    if (!immune) world_.get<StatusEffects>(player_).add(StatusType::CONFUSED, 0, 4);
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
    if (result.hit) { audio_.play_bow_hit(); particles_.hit_spark(tgt_x, tgt_y); }
    if (result.critical) { trigger_screen_shake(4.0f); }
    if (result.killed) { audio_.play(SfxId::DEATH); particles_.death_burst(tgt_x, tgt_y); trigger_screen_shake(3.0f); }

    // Quest target killed?
    if (result.quest_target_id >= 0) {
        auto qid = static_cast<QuestId>(result.quest_target_id);
        if (journal_.has_quest(qid) && journal_.get_state(qid) == QuestState::ACTIVE) {
            journal_.set_state(qid, QuestState::COMPLETE);
            auto& qinfo = get_quest_info(qid);
            char qbuf[128];
            snprintf(qbuf, sizeof(qbuf), "Quest objective complete: %s", qinfo.name);
            log_.add(qbuf, {120, 220, 120, 255});
        }
    }

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
        pending_levelup_ = true;
        levelup_screen_.open(player_, rng_);
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
            log_.add(buf, {180, 200, 160, 255});
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
        bool in_town = false;
        if (world_.has<Position>(player_)) {
            auto& pp = world_.get<Position>(player_);
            in_town = (near_town(pp.x, pp.y, 25) >= 0);
        }

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
        bool has_boss = false;
        auto& ai_pool = world_.pool<AI>();
        for (size_t i = 0; i < ai_pool.size(); i++) {
            Entity e = ai_pool.entity_at(i);
            if (world_.has<GodAlignment>(e) || world_.has<QuestTarget>(e)) {
                has_boss = true;
                break;
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
        default: return false;
    }
}

void Engine::update_meta_on_end() {
    // Snapshot old unlock state
    bool was_unlocked[CLASS_COUNT];
    for (int i = 0; i < CLASS_COUNT; i++)
        was_unlocked[i] = is_class_unlocked(static_cast<ClassId>(i));

    // Update max depth
    if (dungeon_level_ > meta_.max_dungeon_depth)
        meta_.max_dungeon_depth = dungeon_level_;

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

void Engine::render_victory() {
    int god_id = static_cast<int>(GodId::NONE);
    if (world_.has<GodAlignment>(player_))
        god_id = static_cast<int>(world_.get<GodAlignment>(player_).god);
    render_victory_screen(renderer_, font_, font_title_, width_, height_,
                          god_id, newly_unlocked_);
}

void Engine::reset_to_main_menu() {
    player_ = NULL_ENTITY;
    pet_entity_ = NULL_ENTITY;
    run_kills_ = 0;
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
            gold_ += item.gold_value;
            char buf[64];
            snprintf(buf, sizeof(buf), "You pick up %d gold.", item.gold_value);
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

        char buf[128];
        snprintf(buf, sizeof(buf), "You pick up the %s.", item.display_name().c_str());
        log_.add(buf, {180, 175, 160, 255});
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

        // Auto-identify potions known from previous runs
        if (item.type == ItemType::POTION && !item.identified
            && meta_.identified_potions.count(item.name)) {
            item.identified = true;
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

    // In dungeon: 40% chance of interruption (+ 3% per floor)
    if (dungeon_level_ > 0 && rng_.chance(40 + dungeon_level_ * 3)) {
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

            // Depth-scaled ambush monster
            Stats ms;
            ms.name = "something";
            ms.hp = 12 + dungeon_level_ * 4; ms.hp_max = ms.hp;
            ms.base_damage = 3 + dungeon_level_;
            ms.xp_value = 15 + dungeon_level_ * 5;

            // Use a generic hostile sprite
            world_.add<Renderable>(mob, {SHEET_MONSTERS, 11, 6, {255, 255, 255, 255}, 5});
            world_.add<AI>(mob, {AIState::HUNTING, ppos.x, ppos.y, 0, 20});
            world_.add<Energy>(mob, {0, 100});
            world_.add<Stats>(mob, std::move(ms));

            log_.add("Your rest is interrupted!", {255, 120, 80, 255});
            player_acted_ = true;
            return;
        }
    }

    // Track rest for tenets
    turn_actions_.rested = true;
    rested_this_floor_ = true;
    if (dungeon_level_ <= 0) turn_actions_.rested_on_surface = true;

    // Rest succeeds — restore 15% of max HP and MP (Lethis: 30%)
    int rest_pct = 7; // default: ~15% (divide by 7)
    if (world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        if (ga.god == GodId::LETHIS) rest_pct = 3; // ~33%
    }
    int hp_restore = std::max(1, stats.hp_max / rest_pct);
    int mp_restore = std::max(1, stats.mp_max / rest_pct);

    // Vampirism: no natural HP regen
    bool is_vampire = world_.has<Diseases>(player_) &&
                      world_.get<Diseases>(player_).has(DiseaseId::VAMPIRISM);
    if (is_vampire) hp_restore = 0;

    int hp_actual = std::min(hp_restore, stats.hp_max - stats.hp);
    int mp_actual = (stats.mp_max > 0) ? std::min(mp_restore, stats.mp_max - stats.mp) : 0;
    stats.hp += hp_actual;
    stats.mp += mp_actual;
    if (hp_actual > 0) meta_.total_hp_healed += hp_actual;

    // Yashkhet tenet: healed above 75%
    if (stats.hp * 4 > stats.hp_max * 3)
        turn_actions_.healed_above_75pct = true;

    // Costs 25 turns (rest takes real time — monsters move, things happen)
    game_turn_ += 25;

    char buf[128];
    if (is_vampire && hp_actual == 0 && mp_actual > 0)
        snprintf(buf, sizeof(buf), "You rest, but your dead flesh does not mend. (+%d MP)", mp_actual);
    else if (is_vampire && hp_actual == 0 && mp_actual == 0)
        snprintf(buf, sizeof(buf), "You rest, but nothing heals. The hunger gnaws.");
    else
        snprintf(buf, sizeof(buf), "You rest for a while. (+%d HP, +%d MP)", hp_actual, mp_actual);
    log_.add(buf, {100, 200, 100, 255});
    audio_.play(SfxId::REST);

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
                    int healed = std::min(heal_amt, stats.hp_max - stats.hp);
                    stats.hp += healed;
                    if (healed > 0) meta_.total_hp_healed += healed;
                    // Yashkhet tenet: healed above 75%
                    if (stats.hp * 4 > stats.hp_max * 3)
                        turn_actions_.healed_above_75pct = true;
                    snprintf(use_buf, sizeof(use_buf), "You consume the %s. Healed %d.",
                             item.display_name().c_str(), healed);
                    log_.add(use_buf, {100, 200, 100, 255});
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
            creation_screen_.handle_input(event);
            if (creation_screen_.is_done()) {
                // Full reset for new game — clear any leftover state from previous run
                player_ = NULL_ENTITY;
                pet_entity_ = NULL_ENTITY;
                world_ = World();
                dungeon_level_ = -1;
                game_turn_ = 0;
                gold_ = 0;
                run_kills_ = 0;
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

                state_ = GameState::PLAYING;
                hardcore_ = creation_screen_.get_build().hardcore;
                // Pre-populate bestiary from meta-progression
                for (auto& [name, me] : meta_.bestiary) {
                    auto& entry = bestiary_[name];
                    entry.name = name;
                    entry.hp = me.hp;
                    entry.damage = me.damage;
                    entry.armor = me.armor;
                    entry.speed = me.speed;
                    entry.kills = 0; // kills reset per run, totals tracked in meta
                }
                generate_level();

                // Opening messages
                log_.add("You arrive at the outskirts of Thornwall.", {180, 170, 150, 255});
                log_.add("Rumors of ancient evil stir beneath the land.", {150, 140, 130, 255});
                log_.add("Press ? for help.  Press q for your quest journal.", {120, 130, 110, 255});
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
                    auto sym = event.key.keysym.sym;
                    switch (sym) {
                        case SDLK_LEFT:  case SDLK_h: case SDLK_KP_4: case SDLK_a: dx = -1; break;
                        case SDLK_RIGHT: case SDLK_l: case SDLK_KP_6: case SDLK_d: dx =  1; break;
                        case SDLK_UP:    case SDLK_k: case SDLK_KP_8: case SDLK_w: dy = -1; break;
                        case SDLK_DOWN:  case SDLK_j: case SDLK_KP_2: case SDLK_s: dy =  1; break;
                        case SDLK_y: case SDLK_KP_7: dx = -1; dy = -1; break;
                        case SDLK_u: case SDLK_KP_9: dx =  1; dy = -1; break;
                        case SDLK_b: case SDLK_KP_1: dx = -1; dy =  1; break;
                        case SDLK_n: case SDLK_KP_3: dx =  1; dy =  1; break;
                        case SDLK_ESCAPE: case SDLK_x:
                            look_mode_ = false;
                            log_.add("Look mode off.", {140, 140, 140, 255});
                            break;
                        default: break;
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
                    else if (sym == SDLK_ESCAPE) { prayer_mode_ = false; }
                }
                return;
            }

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
                                case SpellSchool::DIVINATION: audio_.play(SfxId::SPELL_ICE); break;
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
                                    if (has_target) particles_.burst(tx, ty, 12, 255, 255, 100, 0.12f, 0.4f, 5);
                                    break;
                                case SpellId::FORCE_BOLT:
                                    if (has_target) {
                                        particles_.trail(sp.x, sp.y, tx, ty, 10, 140, 160, 255, 4);
                                        particles_.burst(tx, ty, 15, 160, 180, 255, 0.1f, 0.5f, 6);
                                    }
                                    break;
                                case SpellId::FIREBALL:
                                    if (has_target) {
                                        particles_.trail(sp.x, sp.y, tx, ty, 12, 255, 140, 40, 4);
                                        particles_.burst(tx, ty, 25, 255, 120, 30, 0.15f, 0.6f, 8);
                                        particles_.burst(tx, ty, 15, 255, 200, 60, 0.1f, 0.4f, 6);
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
                                        particles_.trail(sp.x, sp.y, tx, ty, 8, 140, 200, 255, 3);
                                        particles_.drift(tx, ty, 12, 180, 220, 255, 0.6f, 4);
                                    }
                                    break;
                                case SpellId::LIGHTNING:
                                case SpellId::CHAIN_LIGHTNING:
                                    if (has_target) {
                                        particles_.trail(sp.x, sp.y, tx, ty, 15, 255, 255, 180, 2);
                                        particles_.burst(tx, ty, 20, 255, 255, 140, 0.2f, 0.3f, 3);
                                    }
                                    break;
                                case SpellId::FROST_NOVA:
                                    particles_.burst(sp.x, sp.y, 30, 160, 220, 255, 0.15f, 0.8f, 5);
                                    particles_.drift(sp.x, sp.y, 20, 200, 240, 255, 1.0f, 3);
                                    break;
                                case SpellId::METEOR:
                                    if (has_target) {
                                        particles_.fall(tx, ty, 15, 255, 160, 40, 0.5f, 8);
                                        particles_.burst(tx, ty, 30, 255, 100, 20, 0.18f, 0.7f, 7);
                                    }
                                    break;
                                case SpellId::ACID_SPLASH:
                                    if (has_target) {
                                        particles_.trail(sp.x, sp.y, tx, ty, 8, 120, 200, 40, 3);
                                        particles_.fall(tx, ty, 15, 100, 220, 40, 0.8f, 4);
                                    }
                                    break;
                                case SpellId::DISINTEGRATE:
                                    if (has_target) {
                                        particles_.trail(sp.x, sp.y, tx, ty, 20, 200, 40, 200, 2);
                                        particles_.burst(tx, ty, 25, 220, 60, 220, 0.2f, 0.5f, 4);
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
                                    particles_.burst(sp.x, sp.y, 30, 140, 120, 80, 0.2f, 0.5f, 8);
                                    break;
                                case SpellId::BARKSKIN:
                                    particles_.burst(sp.x, sp.y, 14, 100, 140, 60, 0.04f, 0.7f, 6);
                                    break;
                                case SpellId::LIGHTNING_STORM:
                                    particles_.burst(sp.x, sp.y, 25, 255, 255, 160, 0.2f, 0.4f, 3);
                                    particles_.burst(sp.x, sp.y, 15, 200, 200, 255, 0.15f, 0.6f, 5);
                                    break;
                                // --- Dark Arts: purple/red drains ---
                                case SpellId::RAISE_DEAD:
                                    particles_.rise(sp.x, sp.y, 15, 120, 60, 160, 1.0f, 5);
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
                                    particles_.burst(sp.x, sp.y, 25, 40, 20, 60, 0.12f, 1.0f, 7);
                                    break;
                                case SpellId::WITHER:
                                    if (has_target) particles_.fall(tx, ty, 15, 100, 80, 40, 0.8f, 4);
                                    break;
                                case SpellId::BLOOD_PACT:
                                    particles_.burst(sp.x, sp.y, 18, 200, 40, 40, 0.08f, 0.7f, 6);
                                    break;
                                case SpellId::DOOM:
                                    if (has_target) {
                                        particles_.fall(tx, ty, 20, 80, 0, 120, 1.2f, 6);
                                        particles_.burst(tx, ty, 12, 160, 40, 200, 0.08f, 0.8f, 7);
                                    }
                                    break;
                                // --- Fallback by school ---
                                default: {
                                    auto& si = get_spell_info(spell);
                                    switch (si.school) {
                                        case SpellSchool::CONJURATION:
                                            particles_.burst(sp.x, sp.y, 15, 255, 180, 80, 0.1f, 0.5f, 5); break;
                                        case SpellSchool::TRANSMUTATION:
                                            particles_.burst(sp.x, sp.y, 12, 180, 160, 120, 0.06f, 0.6f, 5); break;
                                        case SpellSchool::DIVINATION:
                                            particles_.burst(sp.x, sp.y, 15, 120, 160, 240, 0.1f, 0.5f, 4); break;
                                        case SpellSchool::HEALING:
                                            particles_.rise(sp.x, sp.y, 15, 120, 240, 140, 0.8f, 5); break;
                                        case SpellSchool::NATURE:
                                            particles_.drift(sp.x, sp.y, 15, 80, 180, 60, 0.8f, 5); break;
                                        case SpellSchool::DARK_ARTS:
                                            particles_.burst(sp.x, sp.y, 15, 140, 60, 180, 0.1f, 0.6f, 5); break;
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

            switch (event.key.keysym.sym) {
                case SDLK_UP:    case SDLK_KP_8: case SDLK_w: try_move_player(0, -1); break;
                case SDLK_DOWN:  case SDLK_KP_2: case SDLK_s: try_move_player(0, 1);  break;
                case SDLK_LEFT:  case SDLK_KP_4: case SDLK_a: try_move_player(-1, 0); break;
                case SDLK_RIGHT: case SDLK_KP_6: case SDLK_d: try_move_player(1, 0);  break;
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

                // Bestiary (B key is shared with diagonal — use K for "Kills")
                case SDLK_TAB:
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

                // Examine / look mode
                case SDLK_x: {
                    auto& pp = world_.get<Position>(player_);
                    look_x_ = pp.x;
                    look_y_ = pp.y;
                    look_mode_ = true;
                    log_.add("Look mode. Move cursor to examine. x/Esc to exit.", {180, 180, 140, 255});
                    describe_tile(look_x_, look_y_);
                    break;
                }

                // Pray
                case SDLK_p: {
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
                    prayer_mode_ = true;
                    break;
                }

                // Fire ranged weapon
                case SDLK_f:
                    fire_ranged();
                    break;

                // Rest
                case SDLK_r:
                    try_rest();
                    break;

                // Quest log
                case SDLK_q:
                    quest_log_.open();
                    break;

                // World map (overworld only)
                case SDLK_m:
                    if (dungeon_level_ <= 0) {
                        world_map_.toggle();
                    } else {
                        log_.add("You can't see the world map underground.", {150, 140, 130, 255});
                    }
                    break;

                // Help / keybinds
                case SDLK_QUESTION:
                case SDLK_SLASH:
                    if (event.key.keysym.mod & KMOD_SHIFT || event.key.keysym.sym == SDLK_QUESTION) {
                        help_screen_.open();
                    }
                    break;

                // Stairs — Enter on any stairs, > to descend, < to ascend
                case SDLK_GREATER:
                case SDLK_LESS:
                case SDLK_RETURN: {
                    auto& pos = world_.get<Position>(player_);
                    auto tile_type = map_.at(pos.x, pos.y).type;
                    if (tile_type == TileType::STAIRS_DOWN &&
                        event.key.keysym.sym != SDLK_LESS) {
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
                        generate_level(); // increments dungeon_level_
                        audio_.play(SfxId::STAIRS);

                        // Dungeon entrance text (first floor only)
                        if (dungeon_level_ == 1 && current_dungeon_idx_ >= 0 &&
                            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
                            auto& de = dungeon_registry_[current_dungeon_idx_];
                            char ebuf[128];
                            snprintf(ebuf, sizeof(ebuf), "You descend into %s.", de.name.c_str());
                            log_.add(ebuf, {180, 170, 150, 255});
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
                    } else if (tile_type == TileType::STAIRS_UP &&
                               event.key.keysym.sym != SDLK_GREATER) {
                        if (dungeon_level_ > 1) {
                            // Go up one dungeon level: -2 because generate_level increments by 1
                            cache_current_floor(); // persist current floor before leaving
                            ascending_ = true; // going up — place at down-stairs on restored floor
                            dungeon_level_ -= 2;
                            generate_level();
                            ascending_ = false;
                            audio_.play(SfxId::STAIRS);
                        } else if (dungeon_level_ == 1) {
                            // Return to overworld from depth 1 — keep cache for re-entry
                            cache_current_floor();
                            ascending_ = false;
                            dungeon_level_ = -1; // will increment to 0
                            generate_level();
                            audio_.play(SfxId::STAIRS);
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
                    } else if (event.key.keysym.sym != SDLK_RETURN) {
                        log_.add("There are no stairs here.", {150, 100, 100, 255});
                    }
                    break;
                }

                // Screenshot
                case SDLK_F5:
                    do_save();
                    log_.add("Quick save.", {100, 200, 100, 255});
                    break;
                case SDLK_F6:
                    do_load();
                    log_.add("Quick load.", {100, 200, 100, 255});
                    break;
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
    int a = static_cast<int>(night_alpha * 55.0f); // max alpha 55 — subtle
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
        r = 60; g = 15; b = 5; alpha = 25;  // warm red
    } else if (zone_str == "sunken") {
        r = 5; g = 15; b = 45; alpha = 22;  // deep blue
    } else if (zone_str == "warrens") {
        r = 10; g = 25; b = 8; alpha = 18;  // damp green
    } else if (zone_str == "catacombs") {
        r = 20; g = 15; b = 25; alpha = 18; // dusty purple
    } else if (zone_str == "sepulchre") {
        r = 8; g = 5; b = 15; alpha = 28;   // dark violet
    } else if (zone_str == "deep_halls") {
        r = 12; g = 12; b = 18; alpha = 15; // cool grey
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

void Engine::trigger_screen_shake(float intensity) {
    shake_intensity_ = intensity;
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

    // Status effect indicators
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
            SDL_Surface* surf = TTF_RenderText_Blended(font_, tag, col);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
                SDL_Rect dst = {cursor, bar_y, surf->w, surf->h};
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                cursor += surf->w + 6;
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }
    }

    // Hardcore indicator
    if (hardcore_) {
        SDL_Color hc_col = {200, 80, 80, 255};
        SDL_Surface* hc_surf = TTF_RenderText_Blended(font_, "HC", hc_col);
        if (hc_surf) {
            SDL_Texture* hc_tex = SDL_CreateTextureFromSurface(renderer_, hc_surf);
            SDL_Rect hc_dst = {cursor, bar_y, hc_surf->w, hc_surf->h};
            SDL_RenderCopy(renderer_, hc_tex, nullptr, &hc_dst);
            cursor += hc_surf->w + 6;
            SDL_DestroyTexture(hc_tex);
            SDL_FreeSurface(hc_surf);
        }
    }

    // Disease indicators (permanent, purple-tinted)
    if (world_.has<Diseases>(player_)) {
        auto& diseases = world_.get<Diseases>(player_);
        for (auto did : diseases.active) {
            auto& dinfo = get_disease_info(did);
            SDL_Color dcol = {180, 120, 200, 255};
            SDL_Surface* dsurf = TTF_RenderText_Blended(font_, dinfo.hud_tag, dcol);
            if (dsurf) {
                SDL_Texture* dtex = SDL_CreateTextureFromSurface(renderer_, dsurf);
                SDL_Rect ddst = {cursor, bar_y, dsurf->w, dsurf->h};
                SDL_RenderCopy(renderer_, dtex, nullptr, &ddst);
                cursor += dsurf->w + 6;
                SDL_DestroyTexture(dtex);
                SDL_FreeSurface(dsurf);
            }
        }
    }

    // Help hint
    {
        SDL_Color hint = {65, 60, 55, 255};
        SDL_Surface* hs = TTF_RenderText_Blended(font_, "? help", hint);
        if (hs) {
            SDL_Texture* ht = SDL_CreateTextureFromSurface(renderer_, hs);
            SDL_Rect hd = {cursor + 4, bar_y, hs->w, hs->h};
            SDL_RenderCopy(renderer_, ht, nullptr, &hd);
            SDL_DestroyTexture(ht);
            SDL_FreeSurface(hs);
        }
    }

    // Right side: god + location + gold + turn
    const char* god_name = "";
    if (world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        god_name = get_god_info(ga.god).name;
    }
    char info[128];
    if (dungeon_level_ <= 0) {
        // Show nearest town name, or province if in the wilderness
        const char* location = "Wilderness";
        if (world_.has<Position>(player_)) {
            auto& pp = world_.get<Position>(player_);
            int ti = near_town(pp.x, pp.y, 25);
            if (ti >= 0) {
                location = ALL_TOWNS[ti].name;
            } else {
                location = get_province_name(pp.x, pp.y);
            }
        }
        // Day/night indicator
        int phase = game_turn_ % 100;
        const char* time_icon = (phase >= 50 && phase < 90) ? "Night" :
                                (phase >= 40 && phase < 50) ? "Dusk" :
                                (phase >= 90) ? "Dawn" : "Day";
        snprintf(info, sizeof(info), "%s  %s  %s  Gold:%d  T:%d",
                 god_name, location, time_icon, gold_, game_turn_);
    } else {
        // Show dungeon name if available
        const char* dname = "Dungeon";
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            dname = dungeon_registry_[current_dungeon_idx_].name.c_str();
        }
        snprintf(info, sizeof(info), "%s  %s D:%d  Gold:%d  T:%d",
                 god_name, dname, dungeon_level_, gold_, game_turn_);
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

    // Screen shake: pixel-level offset passed through y_offset
    int y_off = HUD_HEIGHT + static_cast<int>(shake_dy_);

    // Draw map
    render::draw_map(renderer_, sprites_, map_, render_cam, y_off);

    // Draw entities
    render::draw_entities(renderer_, sprites_, world_, map_, render_cam, y_off);

    // Quest NPC indicators — gold "!" rendered as text above their heads
    if (font_) {
        int TS = render_cam.tile_size;
        auto& npc_pool = world_.pool<NPC>();
        Uint32 blink = (SDL_GetTicks() / 600) % 2;
        for (size_t i = 0; i < npc_pool.size(); i++) {
            Entity e = npc_pool.entity_at(i);
            auto& npc = npc_pool.at_index(i);
            if (npc.quest_id < 0) continue;
            if (!world_.has<Position>(e)) continue;
            auto& np = world_.get<Position>(e);
            if (!map_.in_bounds(np.x, np.y) || !map_.at(np.x, np.y).visible) continue;
            if (!blink) continue;
            int sx = (np.x - render_cam.x) * TS + TS / 2;
            int sy = (np.y - render_cam.y) * TS + y_off - 4;
            SDL_Color gold = {255, 220, 80, 255};
            SDL_Surface* surf = TTF_RenderText_Blended(font_, "!", gold);
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

    // Day/night overlay (subtle darkening during night phase)
    render_day_night();

    // Dungeon zone color tint
    render_zone_tint();

    // HUD
    render_hud();

    // Message log
    log_.render(renderer_, font_, 0, height_ - LOG_HEIGHT, width_, LOG_HEIGHT);

    // Overlay screens
    inventory_screen_.render(renderer_, font_, sprites_, world_, width_, height_);
    spell_screen_.render(renderer_, font_, world_, width_, height_);
    char_sheet_.render(renderer_, font_, font_title_, sprites_, world_, width_, height_);
    quest_log_.render(renderer_, font_, font_title_, journal_, width_, height_, &world_);
    quest_offer_.render(renderer_, font_, font_title_, width_, height_);
    help_screen_.render(renderer_, font_, font_title_, width_, height_);
    levelup_screen_.render(renderer_, font_, width_, height_);
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
                            elapsed, death_cause_, god_id, newly_unlocked_);
    }

    if (state_ == GameState::VICTORY) {
        render_victory();
    }

    // Particles (use shake-offset y)
    if (!particles_.empty()) {
        particles_.render(renderer_, camera_.x, camera_.y, camera_.tile_size, y_off);
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

    SDL_RenderPresent(renderer_);
}

void Engine::run() {
    while (state_ != GameState::QUIT) {
        handle_input();
        process_turn();
        particles_.update(); // smooth animation between turns
        update_death_anims();
        update_screen_shake();
        render();
        SDL_Delay(16); // ~60fps cap
    }
}
