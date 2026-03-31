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
#include "components/prayer.h"
#include "components/status_effect.h"
#include "components/container.h"
#include "components/buff.h"
#include "components/disease.h"
#include "components/pet.h"
#include "components/corpse.h"
#include "components/quest_target.h"
#include "components/dynamic_quest.h"
#include "ui/ui_draw.h"
#include "systems/magic.h"
#include "generation/quest_gen.h"
#include "components/background.h"
#include "components/traits.h"
#include "systems/fov.h"
#include "systems/combat.h"
#include "systems/ai.h"
#include "generation/dungeon.h"
#include "generation/populate.h"
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
    overworld_return_x_ = data.overworld_return_x;
    overworld_return_y_ = data.overworld_return_y;
    hardcore_ = data.hardcore;
    build_traits_ = data.traits;

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

bool Engine::restore_floor(int level) {
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

    // Restore player position to where they were on this floor
    if (world_.has<Position>(player_)) {
        auto& pp = world_.get<Position>(player_);
        pp.x = fs.player_x;
        pp.y = fs.player_y;
    }

    return true;
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
            adjust_favor(2);
            align.items_identified_floor = 0; // reset auto-ID for new floor
        }
        // Gathruun gains favor from depth
        if (align.god == GodId::GATHRUUN) {
            adjust_favor(1);
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
            adjust_favor(-2);
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
    if (dungeon_level_ > 0 && restore_floor(dungeon_level_)) {
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

        // Quest town centers for proximity-based quest assignment
        struct QuestTown { int x, y; const char* name; };
        static constexpr QuestTown QUEST_TOWNS[] = {
            {1000, 750, "Thornwall"},
            { 750, 650, "Ashford"},
            {1300, 670, "Greywatch"},
            {1050, 450, "Frostmere"},
            {1400, 750, "Ironhearth"},
            {1450, 500, "Candlemere"},
            { 550, 550, "Hollowgate"},
            { 850, 950, "Millhaven"},
        };
        static constexpr int QUEST_TOWN_COUNT = sizeof(QUEST_TOWNS) / sizeof(QUEST_TOWNS[0]);
        constexpr int TOWN_RADIUS = 30;

        // Helper: find which quest town an NPC is near (-1 if none)
        auto near_quest_town = [&](int mx, int my) -> int {
            for (int i = 0; i < QUEST_TOWN_COUNT; i++) {
                if (std::abs(mx - QUEST_TOWNS[i].x) < TOWN_RADIUS &&
                    std::abs(my - QUEST_TOWNS[i].y) < TOWN_RADIUS)
                    return i;
            }
            return -1;
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
                    sx = 2; sy = 6;
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
                    sx = 4; sy = 6;
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
                    sx = 5; sy = 6;
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
                    sx = 0; sy = 6;
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
                    sx = 0; sy = 1;
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
                        sq_undead_assigned = true;
                    }
                    // MQ_04: Greywatch guard (receives tablet)
                    if (town_idx == 2 && !mq_assigned[3]) {
                        npc_comp.quest_id = static_cast<int>(QuestId::MQ_04_GREYWATCH_WARNING);
                        npc_comp.name = "Captain Voss";
                        npc_comp.dialogue = "I command the largest garrison in the region. Speak plainly.";
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
                    npc_comp.dialogue = "A wight has risen in the barrow east of here. "
                                        "People have died. Will you put it down?";
                    npc_comp.quest_id = static_cast<int>(QuestId::MQ_01_BARROW_WIGHT);
                    mq_assigned[0] = true;
                    sx = 4; sy = 6;
                    break;
            }
            npc_comp.home_x = me.x;
            npc_comp.home_y = me.y;
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

        struct TownSpawn { int x, y; };
        static const TownSpawn TOWN_SPAWNS[] = {
            {1000,750}, {750,650}, {1300,670}, {850,950}, {1200,930},
            {1050,450}, {650,800}, {1400,750}, {1000,1100}, {800,400},
            {1250,1100}, {550,550}, {1450,500}, {900,1200}, {1100,300},
            {700,1050}, {1350,1000}, {1150,550}, {600,700}, {1500,850},
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
        for (auto& ts : TOWN_SPAWNS) {
            spawn_extra_npc(ts.x, ts.y, "Herbalist", NPCRole::PRIEST,
                            HERBALIST_LINES[rng_.range(0, 2)], 3, 6);
            spawn_extra_npc(ts.x, ts.y, "Merchant", NPCRole::SHOPKEEPER,
                            MERCHANT_LINES[rng_.range(0, 2)], 2, 6);
        }

        // Populate overworld with wilderness content
        populate_overworld();

        // Generate dynamic side quests for each town's NPCs
        struct TownInfo { int x, y; const char* name; };
        static const TownInfo TOWNS[] = {
            {1000, 750, "Thornwall"},
            {750, 650, "Ashford"},
            {1300, 670, "Greywatch"},
            {850, 950, "Millhaven"},
            {1200, 930, "Stonehollow"},
            {1050, 450, "Frostmere"},
            {650, 800, "Bramblewood"},
            {1400, 750, "Ironhearth"},
            {1000, 1100, "Dustfall"},
            {800, 400, "Whitepeak"},
            {1250, 1100, "Drywell"},
            {550, 550, "Hollowgate"},
            {1450, 500, "Candlemere"},
            {900, 1200, "Sandmoor"},
            {1100, 300, "Glacierveil"},
            {700, 1050, "Tanglewood"},
            {1350, 1000, "Redrock"},
            {1150, 550, "Ravenshold"},
            {600, 700, "Fenwatch"},
            {1500, 850, "Endgate"},
        };
        for (auto& t : TOWNS) {
            quest_gen::generate_town_quests(world_, map_, rng_, t.x, t.y, t.name);
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
        // Subtle god-colored sprite tint
        SDL_Color player_tint = {255, 255, 255, 255};
        if (build.god != GodId::NONE) {
            auto& gi = get_god_info(build.god);
            // Blend toward god color ~20% (subtle tint, not a full recolor)
            player_tint.r = static_cast<Uint8>(255 - (255 - gi.color.r) / 5);
            player_tint.g = static_cast<Uint8>(255 - (255 - gi.color.g) / 5);
            player_tint.b = static_cast<Uint8>(255 - (255 - gi.color.b) / 5);
        }
        world_.add<Renderable>(player_, {SHEET_ROGUES, cls.sprite_x, cls.sprite_y,
                                         player_tint, 10});
        world_.add<Energy>(player_, {0, 100});
        world_.add<Inventory>(player_);
        world_.add<GodAlignment>(player_, {build.god, 0});
        world_.add<StatusEffects>(player_);
        world_.add<Diseases>(player_);
        world_.add<Buffs>(player_);

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

        // Apply trait stat + gameplay modifiers
        build_traits_ = build.traits;
        for (TraitId tid : build.traits) {
            const TraitInfo& tr = get_trait_info(tid);
            player_stats.set_attr(Attr::STR, player_stats.attr(Attr::STR) + tr.str_mod);
            player_stats.set_attr(Attr::DEX, player_stats.attr(Attr::DEX) + tr.dex_mod);
            player_stats.set_attr(Attr::CON, player_stats.attr(Attr::CON) + tr.con_mod);
            player_stats.set_attr(Attr::INT, player_stats.attr(Attr::INT) + tr.int_mod);
            player_stats.set_attr(Attr::WIL, player_stats.attr(Attr::WIL) + tr.wil_mod);
            player_stats.set_attr(Attr::PER, player_stats.attr(Attr::PER) + tr.per_mod);
            player_stats.set_attr(Attr::CHA, player_stats.attr(Attr::CHA) + tr.cha_mod);
            player_stats.hp_max += tr.bonus_hp;
            player_stats.hp += tr.bonus_hp;
            player_stats.natural_armor += tr.bonus_natural_armor;
            player_stats.base_speed += tr.bonus_speed;
            player_stats.fire_resist += tr.fire_resist;
            player_stats.poison_resist += tr.poison_resist;
            player_stats.bleed_resist += tr.bleed_resist;
        }

        // God-specific resistances and passive stat mods
        switch (build.god) {
            case GodId::SOLETH:
                player_stats.fire_resist = 30;
                break;
            case GodId::KHAEL:
                player_stats.poison_resist = 25;
                break;
            case GodId::THALARA:
                player_stats.poison_resist = 100; // immune
                player_stats.fire_resist = -20;   // weakness
                break;
            case GodId::SYTHARA:
                player_stats.poison_resist = 100; // immune — you ARE the plague
                break;
            case GodId::GATHRUUN:
                player_stats.natural_armor += 3; // stone's endurance
                break;
            default: break;
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
            case ClassId::DRUID:
                book.learn(SpellId::ENTANGLE);
                book.learn(SpellId::MINOR_HEAL);
                break;
            case ClassId::WAR_CLERIC:
                book.learn(SpellId::MINOR_HEAL);
                book.learn(SpellId::MAJOR_HEAL);
                break;
            case ClassId::WARLOCK:
                book.learn(SpellId::DRAIN_LIFE);
                book.learn(SpellId::FEAR);
                break;
            case ClassId::NECROMANCER:
                book.learn(SpellId::DRAIN_LIFE);
                book.learn(SpellId::FEAR);
                book.learn(SpellId::RAISE_DEAD);
                break;
            case ClassId::SCHEMA_MONK:
                book.learn(SpellId::HARDEN_SKIN);
                break;
            case ClassId::TEMPLAR:
                book.learn(SpellId::MINOR_HEAL);
                break;
            default:
                book.learn(SpellId::MINOR_HEAL); // everyone gets minor heal
                break;
        }
        world_.add<Spellbook>(player_, std::move(book));

        // Starting gear per class
        auto give_item = [&](const char* name, const char* desc, ItemType type, EquipSlot slot,
                              int sx, int sy, int dmg, int arm, int atk, int dge, int range_val) {
            Entity ie = world_.create();
            Item item;
            item.name = name; item.description = desc;
            item.type = type; item.slot = slot;
            item.damage_bonus = dmg; item.armor_bonus = arm;
            item.attack_bonus = atk; item.dodge_bonus = dge;
            item.range = range_val;
            item.identified = true; item.gold_value = 0;
            world_.add<Item>(ie, std::move(item));
            world_.add<Renderable>(ie, {SHEET_ITEMS, sx, sy, {255,255,255,255}, 1});
            auto& inv = world_.get<Inventory>(player_);
            inv.add(ie);
            inv.equip(world_.get<Item>(ie).slot, ie);
        };

        switch (build.class_id) {
            case ClassId::FIGHTER:
                give_item("short sword", "A reliable sidearm.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 0, 3, 0, 0, 0, 0);
                give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
                break;
            case ClassId::ROGUE:
                give_item("dagger", "A short, sharp blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0, 2, 0, 1, 1, 0);
                give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
                break;
            case ClassId::WIZARD:
                give_item("dagger", "A short, sharp blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0, 2, 0, 0, 0, 0);
                break;
            case ClassId::RANGER:
                give_item("short bow", "Light and quick.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 9, 3, 0, 1, 0, 6);
                give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
                break;
            case ClassId::BARBARIAN:
                give_item("battle axe", "Heavy. Splits bone.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 3, 6, 0, -1, 0, 0);
                break;
            case ClassId::KNIGHT:
                give_item("long sword", "A well-balanced blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0, 5, 0, 0, 0, 0);
                give_item("buckler", "A small, round shield.", ItemType::SHIELD, EquipSlot::OFF_HAND, 0, 11, 0, 2, 0, 1, 0);
                give_item("chain mail", "Rings of iron.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 3, 12, 0, 4, 0, -1, 0);
                break;
            case ClassId::MONK:
            case ClassId::SCHEMA_MONK:
                // Unarmed specialists — no gear
                break;
            case ClassId::TEMPLAR:
                give_item("mace", "Blunt and merciless.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5, 4, 0, 0, 0, 0);
                give_item("buckler", "A small, round shield.", ItemType::SHIELD, EquipSlot::OFF_HAND, 0, 11, 0, 2, 0, 1, 0);
                give_item("chain mail", "Rings of iron.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 3, 12, 0, 4, 0, -1, 0);
                break;
            case ClassId::DRUID:
                give_item("spear", "Long reach.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 6, 4, 0, 1, 0, 0);
                give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
                break;
            case ClassId::WAR_CLERIC:
                give_item("mace", "Blunt and merciless.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5, 4, 0, 0, 0, 0);
                give_item("chain mail", "Rings of iron.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 3, 12, 0, 4, 0, -1, 0);
                break;
            case ClassId::WARLOCK:
            case ClassId::NECROMANCER:
                give_item("dagger", "A short, sharp blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0, 2, 0, 0, 0, 0);
                break;
            case ClassId::DWARF:
                give_item("war hammer", "Crushes plate.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 4, 8, 0, -1, 0, 0);
                give_item("chain mail", "Rings of iron.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 3, 12, 0, 4, 0, -1, 0);
                break;
            case ClassId::ELF:
                give_item("long sword", "A well-balanced blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0, 5, 0, 0, 0, 0);
                give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
                break;
            case ClassId::BANDIT:
                give_item("dagger", "A short, sharp blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0, 2, 0, 1, 1, 0);
                give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
                break;
            case ClassId::HERETIC:
                // Godless — starts with nothing
                break;
            default: break;
        }
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

        // Dungeon doodads (chests, jars, mushrooms, coffins, etc.)
        {
            std::string zone_name;
            if (current_dungeon_idx_ >= 0 &&
                current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()))
                zone_name = dungeon_registry_[current_dungeon_idx_].zone;
            populate::spawn_doodads(world_, map_, rooms_, rng_, effective_level, zone_name);
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
                char pbuf[128];
                snprintf(pbuf, sizeof(pbuf),
                    "You sense a presence. Another paragon — a servant of %s.", ginfo.name);
                log_.add(pbuf, {200, 160, 200, 255});
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
        }

        // Spawn quest bosses at specific depths
        if (dungeon_level_ == 3) {
            // Barrow Wight — bottom of The Warrens (first dungeon)
            // Uses death knight sprite (row 4 col 3) for a menacing undead look
            Entity wight = populate::spawn_boss(world_, map_, rooms_,
                "Barrow Wight", SHEET_MONSTERS, 3, 4,
                45, 16, 12, 14, 8, 3, 90, 100);
            if (wight != NULL_ENTITY) {
                world_.add<QuestTarget>(wight, {QuestId::MQ_01_BARROW_WIGHT, true});
            }
        }

        // The Sepulchre depth-triggered quests (MQ_15/16/17)
        bool in_sepulchre = false;
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            in_sepulchre = (dungeon_registry_[current_dungeon_idx_].zone == "sepulchre");
        }
        if (in_sepulchre) {
            // MQ_15: auto-activate on entering The Sepulchre
            if (dungeon_level_ == 1 && !journal_.has_quest(QuestId::MQ_15_THE_SEPULCHRE)) {
                auto prereq = QuestId::MQ_14_HOLLOWGATE_SEAL;
                if (journal_.has_quest(prereq) && journal_.get_state(prereq) == QuestState::FINISHED) {
                    journal_.add_quest(QuestId::MQ_15_THE_SEPULCHRE);
                    log_.add("Quest started: Enter The Sepulchre", {220, 200, 100, 255});
                    log_.add("Your god is screaming.", {180, 80, 80, 255});
                }
            }
            // MQ_16: auto-activate at depth 4+
            if (dungeon_level_ >= 4 && !journal_.has_quest(QuestId::MQ_16_THE_DESCENT)) {
                if (journal_.has_quest(QuestId::MQ_15_THE_SEPULCHRE) &&
                    journal_.get_state(QuestId::MQ_15_THE_SEPULCHRE) == QuestState::ACTIVE) {
                    journal_.set_state(QuestId::MQ_15_THE_SEPULCHRE, QuestState::COMPLETE);
                    journal_.set_state(QuestId::MQ_15_THE_SEPULCHRE, QuestState::FINISHED);
                    journal_.add_quest(QuestId::MQ_16_THE_DESCENT);
                    log_.add("Quest started: The Descent", {220, 200, 100, 255});
                    log_.add("The architecture stops making sense. You hear other footsteps.", {180, 80, 80, 255});
                }
            }
            // MQ_17: auto-activate at depth 6 (the bottom)
            if (dungeon_level_ >= 6 && !journal_.has_quest(QuestId::MQ_17_CLAIM_RELIQUARY)) {
                if (journal_.has_quest(QuestId::MQ_16_THE_DESCENT) &&
                    journal_.get_state(QuestId::MQ_16_THE_DESCENT) == QuestState::ACTIVE) {
                    journal_.set_state(QuestId::MQ_16_THE_DESCENT, QuestState::COMPLETE);
                    journal_.set_state(QuestId::MQ_16_THE_DESCENT, QuestState::FINISHED);
                    journal_.add_quest(QuestId::MQ_17_CLAIM_RELIQUARY);
                    log_.add("Quest started: Claim the Reliquary", {220, 200, 100, 255});
                    log_.add("You see it. A vessel of light that hurts to look at.", {255, 220, 100, 255});
                }
            }
            // Sepulchre atmospheric entry messages
            static const char* SEPULCHRE_ENTRY[] = {
                "The air changes. Something is wrong with this place.",
                "The walls here are older than stone should be.",
                "The geometry stops making sense. Corners that shouldn't exist.",
                "You hear footsteps that aren't yours.",
                "The walls are breathing.",
                "The Reliquary is here. You can feel it pulling.",
            };
            if (dungeon_level_ >= 1 && dungeon_level_ <= 6) {
                log_.add(SEPULCHRE_ENTRY[dungeon_level_ - 1], {160, 100, 140, 255});
            }
        }

        // Spawn quest items at the bottom of their respective dungeons
        if (current_dungeon_idx_ >= 0 &&
            current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size())) {
            auto& dentry = dungeon_registry_[current_dungeon_idx_];

            // Helper: spawn a quest item in the last room
            auto spawn_quest_item = [&](const char* name, const char* desc,
                                        int sprite_x, int sprite_y,
                                        QuestId qid,
                                        SDL_Color tint = {255,255,255,255}) {
                if (rooms_.size() < 2) return;
                auto& room = rooms_.back();
                int x = room.cx();
                int y = room.cy() + 1;
                Entity e = world_.create();
                world_.add<Position>(e, {x, y});
                world_.add<Renderable>(e, {SHEET_ITEMS, sprite_x, sprite_y, tint, 2});
                Item item;
                item.name = name;
                item.description = desc;
                item.type = ItemType::KEY;
                item.identified = true;
                item.quest_id = static_cast<int>(qid);
                item.gold_value = 0;
                world_.add<Item>(e, std::move(item));
            };

            // Determine the zone's max depth to know if we're at the bottom
            struct ZoneMax { const char* key; int max_depth; };
            static const ZoneMax ZONE_DEPTHS[] = {
                {"warrens", 3}, {"stonekeep", 6}, {"deep_halls", 9},
                {"catacombs", 12}, {"molten", 15}, {"sunken", 18},
                {"sepulchre", 6}, // Sepulchre has 6 levels
            };
            int zone_max = 3; // default
            for (auto& zd : ZONE_DEPTHS) {
                if (dentry.zone == zd.key) { zone_max = zd.max_depth; break; }
            }
            bool is_bottom = (dungeon_level_ >= zone_max);

            if (is_bottom) {
                // MQ_03: Stone Tablet in Ashford Ruins
                if (dentry.quest == "MQ_03") {
                    spawn_quest_item("Stone Tablet",
                        "A heavy stone tablet. The inscriptions shift when you aren't looking.",
                        0, 21, QuestId::MQ_03_ASHFORD_TABLET);
                }
                // MQ_05: Ancient Inscription in Stonekeep
                if (dentry.quest == "MQ_05") {
                    spawn_quest_item("Ancient Inscription",
                        "A page of burned stone. The words are too heavy for the rock.",
                        7, 21, QuestId::MQ_05_STONEKEEP_DEPTHS);
                }
                // MQ_07: Frozen Key in Frostmere Depths
                if (dentry.quest == "MQ_07") {
                    spawn_quest_item("Frozen Key",
                        "A key of impossible cold. It burns your hand.",
                        2, 22, QuestId::MQ_07_FROZEN_KEY);
                }
                // MQ_09: Reliquary Fragment in The Catacombs
                if (dentry.quest == "MQ_08") {
                    spawn_quest_item("Reliquary Fragment",
                        "A shard of solidified memory. It hums with warmth.",
                        2, 16, QuestId::MQ_09_OSSUARY_FRAGMENT);
                }
                // MQ_11: Molten Fragment in The Molten Depths
                if (dentry.quest == "MQ_11") {
                    spawn_quest_item("Molten Fragment",
                        "Cold even in the heart of the furnace. Two of three.",
                        2, 16, QuestId::MQ_11_MOLTEN_TRIAL,
                        {255, 120, 80, 255}); // red tint
                }
                // MQ_13: Sunken Fragment in The Sunken Halls
                if (dentry.quest == "MQ_13") {
                    spawn_quest_item("Sunken Fragment",
                        "The water remembers. Three fragments. They pull toward each other.",
                        2, 16, QuestId::MQ_13_SUNKEN_FRAGMENT,
                        {100, 160, 255, 255}); // blue tint
                }
                // MQ_14: Seal Stone in The Hollowgate
                if (dentry.quest == "MQ_14") {
                    spawn_quest_item("Seal Stone",
                        "The fragments resonate near it. Break the seal.",
                        5, 16, QuestId::MQ_14_HOLLOWGATE_SEAL);
                }
                // MQ_17: The Reliquary in The Sepulchre (depth 6)
                if (dentry.quest == "MQ_15" && dungeon_level_ >= 6) {
                    spawn_quest_item("The Reliquary",
                        "A vessel of light that hurts to look at. It was here before the gods.",
                        6, 16, QuestId::MQ_17_CLAIM_RELIQUARY,
                        {255, 220, 100, 255}); // golden tint

                    // Spawn The Keeper — final boss guarding the Reliquary
                    Entity keeper = populate::spawn_boss(world_, map_, rooms_,
                        "The Keeper", SHEET_MONSTERS, 0, 11,
                        150, 24, 14, 20, 20, 5, 85, 500);
                    if (keeper != NULL_ENTITY) {
                        // Give the Keeper a golden tint to match the Reliquary's glow
                        world_.get<Renderable>(keeper).tint = {255, 220, 100, 255};
                    }
                }
            }
        }

        // Side quest items — spawn in any dungeon when quest is active
        if (journal_.has_quest(QuestId::SQ_LOST_AMULET) &&
            journal_.get_state(QuestId::SQ_LOST_AMULET) == QuestState::ACTIVE &&
            dungeon_level_ >= 1 && rooms_.size() >= 3) {
            auto& room = rooms_[rooms_.size() / 2]; // mid dungeon
            int ax = room.cx(), ay = room.cy();
            Entity ae = world_.create();
            world_.add<Position>(ae, {ax, ay});
            world_.add<Renderable>(ae, {SHEET_ITEMS, 0, 16, {255, 255, 255, 255}, 1});
            Item amulet;
            amulet.name = "family amulet";
            amulet.description = "A tarnished silver amulet. Worthless to anyone but its owner.";
            amulet.type = ItemType::AMULET;
            amulet.slot = EquipSlot::NONE;
            amulet.quest_id = static_cast<int>(QuestId::SQ_LOST_AMULET);
            amulet.identified = true;
            amulet.gold_value = 0;
            world_.add<Item>(ae, std::move(amulet));
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

    // Check for entity at target tile
    Entity target = combat::entity_at(world_, nx, ny, player_);
    if (target != NULL_ENTITY) {
        // NPC — talk instead of fight
        if (world_.has<NPC>(target)) {
            auto& npc = world_.get<NPC>(target);

            // Check if bumping this NPC satisfies any delivery quest's target town
            {
                auto& dq_pool = world_.pool<DynamicQuest>();
                for (size_t di = 0; di < dq_pool.size(); di++) {
                    auto& dq = dq_pool.at_index(di);
                    if (dq.accepted && !dq.completed && dq.target_town_x >= 0 && !dq.reached_target) {
                        if (std::abs(nx - dq.target_town_x) < 30 &&
                            std::abs(ny - dq.target_town_y) < 30) {
                            dq.reached_target = true;
                        }
                    }
                }
            }

            // Shopkeeper — open shop screen (unless they have a dynamic quest to offer)
            // Calculate shop difficulty from town distance to Thornwall
            auto calc_shop_diff = [&]() -> int {
                if (dungeon_level_ > 0) return 0; // dungeon shops = base
                if (!world_.has<Position>(target)) return 0;
                auto& sp = world_.get<Position>(target);
                float d = std::sqrt(static_cast<float>((sp.x - 1000) * (sp.x - 1000) +
                                                        (sp.y - 750) * (sp.y - 750)));
                return std::min(8, static_cast<int>(d / 80.0f)); // ~80 tiles per difficulty level
            };

            if (npc.role == NPCRole::SHOPKEEPER && !world_.has<DynamicQuest>(target)) {
                shop_screen_.open(player_, world_, rng_, &gold_, calc_shop_diff());
                return;
            }
            // Merchant with dynamic quest: show dialogue + quest, then can shop next time
            if (npc.role == NPCRole::SHOPKEEPER && world_.has<DynamicQuest>(target)) {
                auto& dq = world_.get<DynamicQuest>(target);
                if (dq.completed) {
                    shop_screen_.open(player_, world_, rng_, &gold_, calc_shop_diff());
                    return;
                }
            }

            char buf[256];
            snprintf(buf, sizeof(buf), "%s: \"%s\"", npc.name.c_str(), npc.dialogue.c_str());
            log_.add(buf, {180, 180, 140, 255});

            // God-aware NPC reactions (priests/scholars react to player's god)
            if (npc.role == NPCRole::PRIEST && world_.has<GodAlignment>(player_)) {
                auto& align = world_.get<GodAlignment>(player_);
                if (align.god == GodId::IXUUL) {
                    log_.add("The priest eyes you warily. \"Your god is... unwelcome here.\"",
                             {180, 140, 140, 255});
                } else if (align.god == GodId::YASHKHET) {
                    log_.add("The priest flinches at your scars. \"The Wound's followers unsettle me.\"",
                             {180, 140, 140, 255});
                } else if (align.god == GodId::NONE) {
                    log_.add("The priest looks at you with pity. \"No god watches over you.\"",
                             {160, 150, 140, 255});
                } else if (align.favor > 50) {
                    auto& ginfo = get_god_info(align.god);
                    char gbuf[128];
                    snprintf(gbuf, sizeof(gbuf),
                        "The priest nods respectfully. \"%s's favor is strong in you.\"", ginfo.name);
                    log_.add(gbuf, {160, 180, 200, 255});
                }
            }

            // Show direction hint if quest is active
            if (npc.quest_id >= 0) {
                auto hint_qid = static_cast<QuestId>(npc.quest_id);
                if (journal_.has_quest(hint_qid) &&
                    journal_.get_state(hint_qid) == QuestState::ACTIVE) {
                    const char* hint = get_quest_hint(hint_qid);
                    if (hint) {
                        char hbuf[256];
                        snprintf(hbuf, sizeof(hbuf), "%s: \"%s\"", npc.name.c_str(), hint);
                        log_.add(hbuf, {200, 190, 150, 255});
                    }
                }
            }

            // Quest giving — static quests with prerequisite chaining
            if (npc.quest_id >= 0) {
                auto qid = static_cast<QuestId>(npc.quest_id);

                // Check prerequisite: for main quests, the previous quest must be FINISHED
                auto quest_prereq = [](QuestId id) -> QuestId {
                    int idx = static_cast<int>(id);
                    if (idx <= 0 || idx > static_cast<int>(QuestId::MQ_17_CLAIM_RELIQUARY))
                        return QuestId::COUNT; // no prereq
                    return static_cast<QuestId>(idx - 1);
                };
                auto prereq = quest_prereq(qid);
                bool prereq_met = (prereq == QuestId::COUNT) ||
                                  (journal_.has_quest(prereq) &&
                                   journal_.get_state(prereq) == QuestState::FINISHED);

                if (prereq_met && !journal_.has_quest(qid)) {
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
                    audio_.play(SfxId::QUEST);
                    gold_ += qinfo.gold_reward;
                    adjust_favor(5); // gods reward quest completion
                    if (world_.has<Stats>(player_) && qinfo.xp_reward > 0) {
                        if (world_.get<Stats>(player_).grant_xp(qinfo.xp_reward)) {
                            pending_levelup_ = true;
                            levelup_screen_.open(player_, rng_);
                            audio_.play(SfxId::LEVELUP);
                            { auto& lp = world_.get<Position>(player_); particles_.levelup_effect(lp.x, lp.y); }
                        }
                    }
                }
            }

            // Dynamic quest handling
            if (world_.has<DynamicQuest>(target)) {
                auto& dq = world_.get<DynamicQuest>(target);
                if (!dq.accepted) {
                    // Offer the quest
                    char qbuf[256];
                    snprintf(qbuf, sizeof(qbuf), "[Quest] %s", dq.name.c_str());
                    log_.add(qbuf, {220, 200, 100, 255});
                    log_.add(dq.description.c_str(), {180, 170, 140, 255});
                    snprintf(qbuf, sizeof(qbuf), "Objective: %s", dq.objective.c_str());
                    log_.add(qbuf, {160, 160, 130, 255});
                    snprintf(qbuf, sizeof(qbuf), "Reward: %d XP, %d gold", dq.xp_reward, dq.gold_reward);
                    log_.add(qbuf, {160, 160, 130, 255});
                    dq.accepted = true;
                    dq.accepted_turn = game_turn_;
                    log_.add("Quest accepted.", {120, 220, 120, 255});
                } else if (!dq.completed) {
                    if (dq.conditions_met(game_turn_)) {
                        // Conditions satisfied — complete the quest
                        dq.completed = true;
                        meta_.total_quests_completed++;
                        char qbuf[256];
                        snprintf(qbuf, sizeof(qbuf), "Quest complete: %s (+%dxp, +%dgold)",
                                 dq.name.c_str(), dq.xp_reward, dq.gold_reward);
                        log_.add(qbuf, {120, 220, 120, 255});
                        audio_.play(SfxId::QUEST);
                        log_.add(dq.complete_text.c_str(), {180, 170, 140, 255});
                        gold_ += dq.gold_reward;
                        if (world_.has<Stats>(player_) && dq.xp_reward > 0) {
                            if (world_.get<Stats>(player_).grant_xp(dq.xp_reward)) {
                                pending_levelup_ = true;
                                levelup_screen_.open(player_, rng_);
                                audio_.play(SfxId::LEVELUP);
                            }
                        }
                    } else {
                        // Tell the player what's still needed
                        if (dq.requires_dungeon && !dq.visited_dungeon)
                            log_.add("\"Come back when you've actually been down there.\"", {180, 170, 140, 255});
                        else if (dq.target_town_x >= 0 && !dq.reached_target)
                            log_.add("\"You haven't made the journey yet. Go.\"", {180, 170, 140, 255});
                        else
                            log_.add("\"It hasn't been long enough. These things take time.\"", {180, 170, 140, 255});
                    }
                }
            }
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

        if (atk_result.hit && atk_result.critical) { audio_.play(SfxId::CRIT); particles_.crit_flash((float)nx, (float)ny); }
        else if (atk_result.hit) { audio_.play_hit(); particles_.blood((float)nx, (float)ny); }
        else audio_.play(SfxId::MISS);
        if (atk_result.killed) { audio_.play(SfxId::DEATH); particles_.death_burst((float)nx, (float)ny); }

        // Quest target killed?
        if (atk_result.quest_target_id >= 0) {
            auto qid = static_cast<QuestId>(atk_result.quest_target_id);
            if (journal_.has_quest(qid) && journal_.get_state(qid) == QuestState::ACTIVE) {
                journal_.set_state(qid, QuestState::COMPLETE);
                auto& qinfo = get_quest_info(qid);
                char qbuf[128];
                snprintf(qbuf, sizeof(qbuf), "Quest objective complete: %s", qinfo.name);
                log_.add(qbuf, {120, 220, 120, 255});
                log_.add("Return to the quest giver.", {160, 155, 140, 255});
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
                if (gain != 0) adjust_favor(gain);
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
                adjust_favor(10); // large favor bonus
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
            adjust_favor(5);
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
            adjust_favor(2);
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
                if (mresult.critical) particles_.crit_flash(pp.x, pp.y);
                else particles_.blood(pp.x, pp.y);
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
            // Troll regeneration — heals 2 HP per turn if alive
            if (world_.has<Stats>(e)) {
                auto& ms = world_.get<Stats>(e);
                if (ms.name == "troll" && ms.hp > 0 && ms.hp < ms.hp_max)
                    ms.hp = std::min(ms.hp_max, ms.hp + 2);
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
                audio_.play(SfxId::ARROW_FIRE);
                auto rresult = combat::ranged_attack(world_, e, player_, ai_comp.ranged_damage, rng_, log_);
                if (rresult.hit) particles_.hit_spark(pp.x, pp.y);
                if (rresult.killed) { audio_.play(SfxId::DEATH); }
            }
        } else if (dist <= 6 && dist > 1 && world_.has<Stats>(e) &&
                   map_.in_bounds(mpos.x, mpos.y) && map_.at(mpos.x, mpos.y).visible) {
            // Monster special abilities at range
            auto& mstats = world_.get<Stats>(e);
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
    process_status_effects();

    // Sepulchre ambient messages
    sepulchre_ambient();

    particles_.update();
    log_.set_turn(game_turn_);
}

void Engine::process_npc_wander() {
    auto& npc_pool = world_.pool<NPC>();
    for (size_t i = 0; i < npc_pool.size(); i++) {
        Entity e = npc_pool.entity_at(i);
        auto& npc = npc_pool.at_index(i);

        // Shopkeepers stay put (need to be findable for shops)
        if (npc.role == NPCRole::SHOPKEEPER) continue;

        if (!world_.has<Position>(e) || !world_.has<Energy>(e)) continue;
        auto& energy = world_.get<Energy>(e);
        if (!energy.can_act()) continue;
        energy.spend();

        // 80% chance to just stand still (frequent pausing)
        if (rng_.chance(80)) continue;

        auto& pos = world_.get<Position>(e);

        // Don't stray more than 4 tiles from home
        int dx = rng_.range(-1, 1);
        int dy = rng_.range(-1, 1);
        if (dx == 0 && dy == 0) continue;

        int nx = pos.x + dx;
        int ny = pos.y + dy;
        int home_dist = std::max(std::abs(nx - npc.home_x), std::abs(ny - npc.home_y));
        if (home_dist > 4) continue;

        if (!map_.is_walkable(nx, ny)) continue;

        // Don't walk into other entities
        bool blocked = false;
        auto& positions = world_.pool<Position>();
        for (size_t j = 0; j < positions.size(); j++) {
            Entity other = positions.entity_at(j);
            if (other == e) continue;
            auto& op = positions.at_index(j);
            if (op.x == nx && op.y == ny && world_.has<Stats>(other)) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;

        int old_x = pos.x;
        pos.x = nx;
        pos.y = ny;
        if (world_.has<Renderable>(e) && old_x != nx) {
            world_.get<Renderable>(e).flip_h = (nx > old_x);
        }
    }
}

void Engine::try_spawn_overworld_enemy() {
    if (!world_.has<Position>(player_)) return;
    auto& ppos = world_.get<Position>(player_);

    // Don't spawn near towns
    static const struct { int x, y; } TOWN_POS[] = {
        {500,375}, {375,325}, {650,335}, {525,225}, {700,375},
        {725,250}, {275,275}, {425,475}, {600,465}, {325,400},
        {400,200}, {625,550}, {500,550}, {450,600}, {550,150},
        {350,525}, {675,500}, {575,275}, {300,350}, {750,425},
    };
    for (auto& t : TOWN_POS) {
        int d = std::max(std::abs(ppos.x - t.x), std::abs(ppos.y - t.y));
        if (d < 35) return;
    }

    // Count nearby hostile entities
    int nearby = 0;
    auto& ai_pool = world_.pool<AI>();
    for (size_t i = 0; i < ai_pool.size(); i++) {
        Entity e = ai_pool.entity_at(i);
        if (!world_.has<Position>(e)) continue;
        auto& mp = world_.get<Position>(e);
        int dist = std::max(std::abs(mp.x - ppos.x), std::abs(mp.y - ppos.y));
        if (dist <= 30) nearby++;
    }
    if (nearby >= 4) return;

    // Overworld monster definitions
    struct OWMonster {
        const char* name;
        int sheet, sx, sy, hp, str, dex, con, dmg, armor, speed, flee, xp;
    };
    static const OWMonster OW_TABLE[] = {
        {"wolf",         SHEET_ANIMALS,  6, 4, 12, 10, 14,  8, 3, 0, 120, 30, 15}, // wolf = 5.g (row4,col6)
        {"wild boar",    SHEET_ANIMALS,  7, 9, 18, 14,  8, 12, 4, 1,  90, 20, 20}, // boar = 10.h (row9,col7)
        {"highwayman",   SHEET_ROGUES,   4, 0, 16, 12, 12, 10, 3, 1, 100, 25, 25}, // bandit sprite
        {"giant spider", SHEET_MONSTERS, 8, 6, 10,  8, 14,  6, 3, 0, 120, 30, 15},
        {"bear",         SHEET_ANIMALS,  0, 0, 24, 16,  8, 14, 5, 2,  80, 15, 30}, // grizzly
        {"bandit",       SHEET_ROGUES,   4, 0, 14, 11, 13, 10, 3, 1, 105, 30, 20},
        {"snake",        SHEET_ANIMALS,  0, 7,  6,  6, 16,  6, 2, 0, 130, 40, 10},
        {"dire wolf",    SHEET_ANIMALS,  6, 4, 20, 14, 14, 12, 5, 1, 125, 15, 35}, // wolf sprite (row4,col6)
        {"wandering skeleton", SHEET_MONSTERS, 0, 4, 16, 10, 10, 10, 3, 2, 90, 0, 20},
    };
    static constexpr int OW_COUNT = sizeof(OW_TABLE) / sizeof(OW_TABLE[0]);

    // Try to spawn at edge of visibility
    for (int attempt = 0; attempt < 15; attempt++) {
        int dist = rng_.range(14, 20);
        float angle = rng_.range_f(0.0f, 6.283f);
        int sx = ppos.x + static_cast<int>(dist * std::cos(angle));
        int sy = ppos.y + static_cast<int>(dist * std::sin(angle));

        if (!map_.in_bounds(sx, sy) || !map_.is_walkable(sx, sy)) continue;

        // Only spawn on wilderness tiles
        auto tt = map_.at(sx, sy).type;
        if (tt != TileType::FLOOR_GRASS && tt != TileType::FLOOR_DIRT &&
            tt != TileType::FLOOR_SAND && tt != TileType::FLOOR_ICE &&
            tt != TileType::BRUSH) continue;

        auto& def = OW_TABLE[rng_.range(0, OW_COUNT - 1)];

        Entity e = world_.create();
        world_.add<Position>(e, {sx, sy});
        world_.add<Renderable>(e, {def.sheet, def.sx, def.sy,
                                    {255, 255, 255, 255}, 5});
        Stats stats;
        stats.name = def.name;
        stats.hp = def.hp;
        stats.hp_max = def.hp;
        stats.set_attr(Attr::STR, def.str);
        stats.set_attr(Attr::DEX, def.dex);
        stats.set_attr(Attr::CON, def.con);
        stats.base_damage = def.dmg;
        stats.natural_armor = def.armor;
        stats.base_speed = def.speed;
        stats.xp_value = def.xp;
        world_.add<Stats>(e, std::move(stats));
        world_.add<AI>(e, {AIState::IDLE, -1, -1, 0, def.flee});
        world_.add<Energy>(e, {0, def.speed});
        return; // one at a time
    }
}

void Engine::adjust_favor(int amount) {
    if (!world_.has<GodAlignment>(player_)) return;
    auto& align = world_.get<GodAlignment>(player_);
    int old_favor = align.favor;
    align.favor = std::max(-100, std::min(100, align.favor + amount));
    if (align.favor != old_favor && amount > 0) {
        char buf[64];
        auto& ginfo = get_god_info(align.god);
        snprintf(buf, sizeof(buf), "%s is pleased. (+%d favor)", ginfo.name, amount);
        log_.add(buf, {180, 200, 255, 255});
    } else if (align.favor != old_favor && amount < 0) {
        char buf[64];
        auto& ginfo = get_god_info(align.god);
        snprintf(buf, sizeof(buf), "%s is displeased. (%d favor)", ginfo.name, amount);
        log_.add(buf, {255, 160, 120, 255});
    }
}

void Engine::check_tenets() {
    if (!world_.has<GodAlignment>(player_)) return;
    auto& align = world_.get<GodAlignment>(player_);
    if (align.god == GodId::NONE) return;

    auto tenets = get_god_tenets(align.god);
    if (!tenets.tenets || tenets.count == 0) return;

    for (int i = 0; i < tenets.count; i++) {
        auto& t = tenets.tenets[i];
        bool violated = false;

        switch (t.check) {
            case TenetCheck::NEVER_KILL_ANIMAL:
                violated = turn_actions_.killed_animal;
                break;
            case TenetCheck::NEVER_USE_DARK_ARTS:
                violated = turn_actions_.used_dark_arts;
                break;
            case TenetCheck::NEVER_USE_FIRE_MAGIC:
                violated = turn_actions_.used_fire_magic;
                break;
            case TenetCheck::NEVER_USE_POISON:
                violated = turn_actions_.used_poison;
                break;
            case TenetCheck::NEVER_BACKSTAB:
                violated = turn_actions_.used_stealth_attack;
                break;
            case TenetCheck::NEVER_FLEE_COMBAT:
                violated = turn_actions_.fled_combat;
                break;
            case TenetCheck::NEVER_USE_HEALING_MAGIC:
                violated = turn_actions_.used_healing_magic;
                break;
            case TenetCheck::NEVER_WEAR_HEAVY_ARMOR:
                violated = turn_actions_.wore_heavy_armor;
                break;
            case TenetCheck::NEVER_CARRY_LIGHT:
                violated = turn_actions_.used_light_source;
                break;
            case TenetCheck::NEVER_DESTROY_BOOK:
                violated = turn_actions_.destroyed_book;
                break;
            case TenetCheck::NEVER_KILL_SLEEPING:
                violated = turn_actions_.killed_sleeping;
                break;
            case TenetCheck::NEVER_DIG_WALLS:
                violated = turn_actions_.dug_wall;
                break;
            case TenetCheck::NEVER_REST_ON_SURFACE:
                violated = turn_actions_.rested_on_surface;
                break;
            case TenetCheck::NEVER_HEAL_ABOVE_75:
                violated = turn_actions_.healed_above_75pct;
                break;
            case TenetCheck::MUST_DESCEND:
                // Checked on floor transition, not per-turn
                break;
            case TenetCheck::MUST_KILL_UNDEAD:
                // Soleth: if undead is visible and you leave the floor without killing it
                // Too complex for per-turn — skip for now
                break;
            case TenetCheck::MUST_REST_EACH_FLOOR:
                // Checked on floor transition
                break;
        }

        if (violated) {
            adjust_favor(t.violation_favor);
            auto& ginfo = get_god_info(align.god);
            char buf[192];
            snprintf(buf, sizeof(buf), "Tenet broken: %s", t.description);
            log_.add(buf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
        }
    }

    // Check equipment-based tenet flags (per-turn scan of equipped items)
    if (world_.has<Inventory>(player_)) {
        auto& inv = world_.get<Inventory>(player_);
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            Entity eq = inv.equipped[s];
            if (eq != NULL_ENTITY && world_.has<Item>(eq)) {
                auto& item = world_.get<Item>(eq);
                if (item.tags & TAG_HEAVY_ARMOR) turn_actions_.wore_heavy_armor = true;
                if (item.tags & TAG_TORCH) turn_actions_.used_light_source = true;
                if (item.curse_state == 1) turn_actions_.carrying_cursed = true;
            }
        }
        // Also check carried (not just equipped) items for torch/cursed
        for (auto ie : inv.items) {
            if (ie != NULL_ENTITY && world_.has<Item>(ie)) {
                auto& item = world_.get<Item>(ie);
                if (item.tags & TAG_TORCH) turn_actions_.used_light_source = true;
                if (item.curse_state == 1) turn_actions_.carrying_cursed = true;
            }
        }
    }

    // Soleth: carrying cursed items drains favor slowly
    if (align.god == GodId::SOLETH && turn_actions_.carrying_cursed && game_turn_ % 10 == 0) {
        adjust_favor(-1);
        log_.add("The cursed item in your possession offends Soleth.", {255, 220, 100, 255});
    }

    // Passive favor gain: +1 favor every 20 turns with no violations this cycle
    if (game_turn_ % 20 == 0 && align.favor < 100) {
        // Only gain passive favor if no violation flags were set
        bool clean = !turn_actions_.killed_animal && !turn_actions_.used_dark_arts
            && !turn_actions_.fled_combat && !turn_actions_.used_stealth_attack;
        if (clean) {
            align.favor = std::min(100, align.favor + 1);
        }
    }
}

void Engine::execute_prayer(int prayer_idx) {
    if (!world_.has<GodAlignment>(player_) || !world_.has<Stats>(player_)) return;
    auto& align = world_.get<GodAlignment>(player_);
    auto prayers = get_prayers(align.god);
    if (!prayers || prayer_idx < 0 || prayer_idx > 1) return;

    auto& prayer = prayers[prayer_idx];
    auto& stats = world_.get<Stats>(player_);

    // Excommunicated — prayers always fail
    if (align.favor <= -100) {
        log_.add("You are excommunicated. No god answers.", {180, 80, 80, 255});
        return;
    }
    // Negative favor — prayers fail below -50
    if (align.favor < -50) {
        auto& ginfo = get_god_info(align.god);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s does not answer.", ginfo.name);
        log_.add(buf, {180, 120, 120, 255});
        return;
    }
    // Negative favor — prayer costs doubled
    int cost = prayer.favor_cost;
    if (align.favor < 0) cost *= 2;
    if (align.favor < cost) {
        log_.add("Not enough favor.", {180, 120, 120, 255});
        return;
    }

    align.favor -= cost;
    player_acted_ = true;
    audio_.play(SfxId::PRAYER);

    auto& ppos = world_.get<Position>(player_);

    // God-colored prayer particles
    auto& ginfo = get_god_info(align.god);
    uint8_t pr = ginfo.color.r, pg = ginfo.color.g, pb = ginfo.color.b;
    particles_.prayer_effect(ppos.x, ppos.y, pr, pg, pb);

    // Execute prayer effect based on god and index
    switch (align.god) {
        case GodId::VETHRIK:
            if (prayer_idx == 0) {
                // Lay to Rest — heal
                int heal = 5 + stats.attr(Attr::WIL) / 2;
                stats.hp = std::min(stats.hp + heal, stats.hp_max);
                char buf[128];
                snprintf(buf, sizeof(buf), "The dead grant you respite. (+%d HP)", heal);
                log_.add(buf, {160, 200, 180, 255});
                particles_.heal_effect(ppos.x, ppos.y);
            } else {
                // Death's Grasp — damage nearest
                Entity target = magic::nearest_enemy(world_, player_, map_, 8);
                if (target != NULL_ENTITY && world_.has<Stats>(target)) {
                    int dmg = stats.attr(Attr::WIL) * 2;
                    auto& tgt = world_.get<Stats>(target);
                    tgt.hp -= dmg;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "A skeletal hand tears at the %s. (%d damage)",
                             tgt.name.c_str(), dmg);
                    log_.add(buf, {200, 180, 255, 255});
                    if (tgt.hp <= 0) {
                        combat::kill(world_, target, log_);
                    }
                } else {
                    log_.add("There is nothing nearby to grasp.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost; // refund
                    player_acted_ = false;
                }
            }
            break;

        case GodId::THESSARKA:
            if (prayer_idx == 0) {
                // Clarity — restore MP
                int restored = stats.mp_max - stats.mp;
                stats.mp = stats.mp_max;
                char buf[128];
                snprintf(buf, sizeof(buf), "Your mind clears. (+%d MP)", restored);
                log_.add(buf, {160, 200, 255, 255});
            } else {
                // True Sight — reveal level
                for (int y = 0; y < map_.height(); y++) {
                    for (int x = 0; x < map_.width(); x++) {
                        map_.at(x, y).explored = true;
                    }
                }
                log_.add("The veil lifts. You see everything.", {200, 200, 255, 255});
            }
            break;

        case GodId::MORRETH:
            if (prayer_idx == 0) {
                // Iron Resolve — heal
                int heal = 10 + stats.attr(Attr::STR) / 2;
                stats.hp = std::min(stats.hp + heal, stats.hp_max);
                char buf[128];
                snprintf(buf, sizeof(buf), "Iron will sustains you. (+%d HP)", heal);
                log_.add(buf, {200, 200, 160, 255});
                particles_.heal_effect(ppos.x, ppos.y);
            } else {
                // War Cry — damage all adjacent
                int dmg = stats.attr(Attr::STR);
                int hit_count = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        Entity target = combat::entity_at(world_, ppos.x + dx, ppos.y + dy, player_);
                        if (target != NULL_ENTITY && world_.has<Stats>(target) &&
                            world_.has<AI>(target)) {
                            world_.get<Stats>(target).hp -= dmg;
                            hit_count++;
                            if (world_.get<Stats>(target).hp <= 0) {
                                combat::kill(world_, target, log_);
                            }
                        }
                    }
                }
                if (hit_count > 0) {
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Your war cry shakes the air. (%d enemies struck for %d)", hit_count, dmg);
                    log_.add(buf, {255, 200, 140, 255});
                } else {
                    log_.add("Your cry echoes unanswered.", {150, 140, 130, 255});
                }
            }
            break;

        case GodId::YASHKHET:
            if (prayer_idx == 0) {
                // Blood Offering — sacrifice HP for MP, gain favor
                int sacrifice = stats.hp_max / 4;
                if (stats.hp <= sacrifice + 1) {
                    log_.add("You don't have enough blood to offer.", {180, 120, 120, 255});
                    player_acted_ = false;
                    break;
                }
                stats.hp -= sacrifice;
                int restore = std::min(sacrifice, stats.mp_max - stats.mp);
                stats.mp += restore;
                adjust_favor(5);
                char buf[128];
                snprintf(buf, sizeof(buf), "Your blood sings. (-%d HP, +%d MP)", sacrifice, restore);
                log_.add(buf, {200, 100, 100, 255});
            } else {
                // Sanguine Burst — sacrifice HP, damage nearest
                if (stats.hp <= 11) {
                    log_.add("You don't have enough blood.", {180, 120, 120, 255});
                    align.favor += prayer.favor_cost; // refund
                    player_acted_ = false;
                    break;
                }
                Entity target = magic::nearest_enemy(world_, player_, map_, 8);
                if (target != NULL_ENTITY && world_.has<Stats>(target)) {
                    stats.hp -= 10;
                    int dmg = 10 + stats.attr(Attr::WIL);
                    auto& tgt = world_.get<Stats>(target);
                    tgt.hp -= dmg;
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Your blood erupts outward, striking the %s. (%d damage)", tgt.name.c_str(), dmg);
                    log_.add(buf, {200, 80, 80, 255});
                    if (tgt.hp <= 0) {
                        combat::kill(world_, target, log_);
                    }
                } else {
                    log_.add("There is nothing nearby to strike.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost; // refund
                    player_acted_ = false;
                }
            }
            break;

        case GodId::KHAEL:
            if (prayer_idx == 0) {
                // Regrowth — heal 30%
                int heal = stats.hp_max * 3 / 10;
                stats.hp = std::min(stats.hp + heal, stats.hp_max);
                char buf[128];
                snprintf(buf, sizeof(buf), "Green light mends your wounds. (+%d HP)", heal);
                log_.add(buf, {120, 220, 120, 255});
                particles_.heal_effect(ppos.x, ppos.y);
            } else {
                // Nature's Wrath — damage nearest
                Entity target = magic::nearest_enemy(world_, player_, map_, 8);
                if (target != NULL_ENTITY && world_.has<Stats>(target)) {
                    int dmg = stats.attr(Attr::WIL) + 10;
                    auto& tgt = world_.get<Stats>(target);
                    tgt.hp -= dmg;
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Thorns erupt from the ground beneath the %s. (%d damage)", tgt.name.c_str(), dmg);
                    log_.add(buf, {100, 200, 100, 255});
                    if (tgt.hp <= 0) {
                        combat::kill(world_, target, log_);
                    }
                } else {
                    log_.add("The wilderness is quiet.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost; // refund
                    player_acted_ = false;
                }
            }
            break;

        case GodId::SOLETH:
            if (prayer_idx == 0) {
                // Purifying Flame — damage all adjacent
                int dmg = stats.attr(Attr::WIL) + 5;
                int hit_count = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        Entity target = combat::entity_at(world_, ppos.x + dx, ppos.y + dy, player_);
                        if (target != NULL_ENTITY && world_.has<Stats>(target) &&
                            world_.has<AI>(target)) {
                            world_.get<Stats>(target).hp -= dmg;
                            hit_count++;
                            if (world_.get<Stats>(target).hp <= 0) {
                                combat::kill(world_, target, log_);
                            }
                        }
                    }
                }
                if (hit_count > 0) {
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Holy fire erupts around you. (%d enemies burned for %d)", hit_count, dmg);
                    log_.add(buf, {255, 220, 100, 255});
                } else {
                    log_.add("The flame finds nothing to purify.", {150, 140, 130, 255});
                }
            } else {
                // Holy Light — full heal
                int heal = stats.hp_max - stats.hp;
                stats.hp = stats.hp_max;
                char buf[128];
                snprintf(buf, sizeof(buf), "Blinding light restores you. (+%d HP)", heal);
                log_.add(buf, {255, 255, 200, 255});
                particles_.heal_effect(ppos.x, ppos.y);
            }
            break;

        case GodId::IXUUL:
            if (prayer_idx == 0) {
                // Warp — teleport to random walkable tile
                for (int attempt = 0; attempt < 100; attempt++) {
                    int tx = rng_.range(1, map_.width() - 2);
                    int ty = rng_.range(1, map_.height() - 2);
                    if (map_.is_walkable(tx, ty)) {
                        ppos.x = tx;
                        ppos.y = ty;
                        fov::compute(map_, ppos.x, ppos.y, stats.fov_radius());
                        camera_.center_on(ppos.x, ppos.y);
                        log_.add("Reality blinks. You are elsewhere.", {180, 120, 255, 255});
                        break;
                    }
                }
            } else {
                // Chaos Surge — random damage to random visible enemy
                std::vector<Entity> visible_enemies;
                auto& ai_pool = world_.pool<AI>();
                for (size_t i = 0; i < ai_pool.size(); i++) {
                    Entity e = ai_pool.entity_at(i);
                    if (!world_.has<Position>(e) || !world_.has<Stats>(e)) continue;
                    auto& mp = world_.get<Position>(e);
                    if (map_.in_bounds(mp.x, mp.y) && map_.at(mp.x, mp.y).visible) {
                        visible_enemies.push_back(e);
                    }
                }
                if (!visible_enemies.empty()) {
                    Entity target = visible_enemies[rng_.range(0, static_cast<int>(visible_enemies.size()) - 1)];
                    int dmg = rng_.range(5, 35);
                    auto& tgt = world_.get<Stats>(target);
                    tgt.hp -= dmg;
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Chaos lashes at the %s. (%d damage)", tgt.name.c_str(), dmg);
                    log_.add(buf, {200, 100, 255, 255});
                    if (tgt.hp <= 0) {
                        combat::kill(world_, target, log_);
                    }
                } else {
                    log_.add("The void has nothing to consume.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost; // refund
                    player_acted_ = false;
                }
            }
            break;

        case GodId::ZHAVEK:
            if (prayer_idx == 0) {
                // Vanish — invisible for 8 turns or until attack
                stats.invisible_turns = 8;
                log_.add("You slip between the spaces. The world forgets you.", {80, 80, 120, 255});
            } else {
                // Silence — all enemies in FOV lose track
                auto& ai_pool2 = world_.pool<AI>();
                int silenced = 0;
                for (size_t i = 0; i < ai_pool2.size(); i++) {
                    Entity e = ai_pool2.entity_at(i);
                    if (!world_.has<Position>(e)) continue;
                    auto& mp = world_.get<Position>(e);
                    if (map_.in_bounds(mp.x, mp.y) && map_.at(mp.x, mp.y).visible) {
                        auto& ai = world_.get<AI>(e);
                        ai.state = AIState::IDLE;
                        ai.target = NULL_ENTITY;
                        silenced++;
                    }
                }
                char buf2[128];
                snprintf(buf2, sizeof(buf2),
                    "Silence falls. %d creatures lose your trail.", silenced);
                log_.add(buf2, {80, 80, 120, 255});
            }
            break;

        case GodId::THALARA:
            if (prayer_idx == 0) {
                // Riptide — pull all visible enemies 3 tiles toward player
                auto& ai_pool3 = world_.pool<AI>();
                int pulled = 0;
                for (size_t i = 0; i < ai_pool3.size(); i++) {
                    Entity e = ai_pool3.entity_at(i);
                    if (!world_.has<Position>(e) || !world_.has<Stats>(e)) continue;
                    auto& mp = world_.get<Position>(e);
                    if (!map_.in_bounds(mp.x, mp.y) || !map_.at(mp.x, mp.y).visible) continue;
                    // Pull toward player
                    for (int step = 0; step < 3; step++) {
                        int dx = (ppos.x > mp.x) ? 1 : (ppos.x < mp.x) ? -1 : 0;
                        int dy = (ppos.y > mp.y) ? 1 : (ppos.y < mp.y) ? -1 : 0;
                        int nx = mp.x + dx;
                        int ny = mp.y + dy;
                        if (map_.is_walkable(nx, ny) && !(nx == ppos.x && ny == ppos.y)) {
                            mp.x = nx;
                            mp.y = ny;
                        } else break;
                    }
                    pulled++;
                }
                char buf3[128];
                snprintf(buf3, sizeof(buf3),
                    "The tide pulls. %d creatures dragged toward you.", pulled);
                log_.add(buf3, {80, 180, 200, 255});
            } else {
                // Drown — deal WIL damage/turn for 5 turns to nearest enemy
                Entity target = magic::nearest_enemy(world_, player_, map_, 8);
                if (target != NULL_ENTITY) {
                    auto& tgt = world_.get<Stats>(target);
                    tgt.drown_turns = 5;
                    tgt.drown_damage = stats.attr(Attr::WIL);
                    char buf4[128];
                    snprintf(buf4, sizeof(buf4),
                        "Water fills the %s's lungs.", tgt.name.c_str());
                    log_.add(buf4, {80, 180, 200, 255});
                } else {
                    log_.add("No one to drown.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost;
                    player_acted_ = false;
                }
            }
            break;

        case GodId::OSSREN:
            if (prayer_idx == 0) {
                // Temper — upgrade one equipped item +1 quality
                Entity weapon_e = world_.get<Inventory>(player_).get_equipped(EquipSlot::MAIN_HAND);
                if (weapon_e != NULL_ENTITY && world_.has<Item>(weapon_e)) {
                    auto& item = world_.get<Item>(weapon_e);
                    item.damage_bonus += 1;
                    char buf5[128];
                    snprintf(buf5, sizeof(buf5),
                        "Ossren's hand steadies the forge. Your %s is tempered.", item.name.c_str());
                    log_.add(buf5, {220, 180, 80, 255});
                    particles_.hit_spark(ppos.x, ppos.y);
                } else {
                    log_.add("You have nothing to temper.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost;
                    player_acted_ = false;
                }
            } else {
                // Unyielding — double armor for 15 turns
                stats.unyielding_turns = 15;
                log_.add("Your armor hardens beyond what metal should allow.", {220, 180, 80, 255});
            }
            break;

        case GodId::LETHIS:
            if (prayer_idx == 0) {
                // Sleepwalk — all visible enemies fall asleep for 5 turns
                auto& ai_pool4 = world_.pool<AI>();
                int slept = 0;
                for (size_t i = 0; i < ai_pool4.size(); i++) {
                    Entity e = ai_pool4.entity_at(i);
                    if (!world_.has<Position>(e) || !world_.has<Stats>(e)) continue;
                    auto& mp = world_.get<Position>(e);
                    if (map_.in_bounds(mp.x, mp.y) && map_.at(mp.x, mp.y).visible) {
                        world_.get<Stats>(e).sleep_turns = 5;
                        auto& ai = world_.get<AI>(e);
                        ai.state = AIState::IDLE;
                        ai.target = NULL_ENTITY;
                        slept++;
                    }
                }
                char buf6[128];
                snprintf(buf6, sizeof(buf6),
                    "A dream passes through. %d creatures slumber.", slept);
                log_.add(buf6, {160, 120, 200, 255});
            } else {
                // Forget — target enemy permanently forgets you
                Entity target = magic::nearest_enemy(world_, player_, map_, 8);
                if (target != NULL_ENTITY) {
                    auto& ai = world_.get<AI>(target);
                    ai.state = AIState::IDLE;
                    ai.target = NULL_ENTITY;
                    ai.forget_player = true;
                    auto& tgt = world_.get<Stats>(target);
                    char buf7[128];
                    snprintf(buf7, sizeof(buf7),
                        "The %s blinks. You were never here.", tgt.name.c_str());
                    log_.add(buf7, {160, 120, 200, 255});
                } else {
                    log_.add("No one to forget.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost;
                    player_acted_ = false;
                }
            }
            break;

        case GodId::GATHRUUN:
            if (prayer_idx == 0) {
                // Tremor — earthquake damage to all enemies on floor (WIL + level based)
                int depth_dmg = stats.attr(Attr::WIL) / 2 + stats.level;
                auto& ai_pool5 = world_.pool<AI>();
                int hit_count = 0;
                for (size_t i = 0; i < ai_pool5.size(); i++) {
                    Entity e = ai_pool5.entity_at(i);
                    if (!world_.has<Stats>(e)) continue;
                    auto& es = world_.get<Stats>(e);
                    es.hp -= depth_dmg;
                    hit_count++;
                    // Stun adjacent enemies (sleep as stun substitute)
                    if (world_.has<Position>(e)) {
                        auto& mp = world_.get<Position>(e);
                        int dx = std::abs(mp.x - ppos.x);
                        int dy = std::abs(mp.y - ppos.y);
                        if (dx <= 1 && dy <= 1) {
                            es.sleep_turns = 2;
                        }
                    }
                    if (es.hp <= 0) combat::kill(world_, e, log_);
                }
                char buf8[128];
                snprintf(buf8, sizeof(buf8),
                    "The earth convulses. %d creatures take %d damage.", hit_count, depth_dmg);
                log_.add(buf8, {160, 130, 90, 255});
                particles_.burst(ppos.x, ppos.y, 20, 160, 130, 90, 0.12f, 0.8f, 6);
            } else {
                // Stone Skin — +10 armor, can't move, 20 turns
                stats.stone_skin_turns = 20;
                stats.stone_skin_armor = 10;
                log_.add("Your flesh becomes stone. You cannot move, but nothing can reach you.", {160, 130, 90, 255});
            }
            break;

        case GodId::SYTHARA:
            if (prayer_idx == 0) {
                // Miasma — poison all visible enemies for 10 turns
                auto& ai_pool6 = world_.pool<AI>();
                int poisoned = 0;
                for (size_t i = 0; i < ai_pool6.size(); i++) {
                    Entity e = ai_pool6.entity_at(i);
                    if (!world_.has<Position>(e) || !world_.has<Stats>(e)) continue;
                    auto& mp = world_.get<Position>(e);
                    if (map_.in_bounds(mp.x, mp.y) && map_.at(mp.x, mp.y).visible) {
                        if (!world_.has<StatusEffects>(e))
                            world_.add<StatusEffects>(e, {});
                        world_.get<StatusEffects>(e).add(StatusType::POISON, 2, 10);
                        poisoned++;
                    }
                }
                char buf9[128];
                snprintf(buf9, sizeof(buf9),
                    "A sickly cloud spreads. %d creatures choke.", poisoned);
                log_.add(buf9, {120, 180, 60, 255});
                particles_.drift(ppos.x, ppos.y, 15, 120, 180, 60, 1.5f, 5);
            } else {
                // Unravel — target loses 50% natural armor
                Entity target = magic::nearest_enemy(world_, player_, map_, 8);
                if (target != NULL_ENTITY) {
                    auto& tgt = world_.get<Stats>(target);
                    int lost = tgt.natural_armor / 2;
                    if (lost < 1) lost = 1;
                    tgt.natural_armor = std::max(0, tgt.natural_armor - lost);
                    char buf10[128];
                    snprintf(buf10, sizeof(buf10),
                        "The %s's armor corrodes and flakes away. (-%d armor)", tgt.name.c_str(), lost);
                    log_.add(buf10, {120, 180, 60, 255});
                } else {
                    log_.add("Nothing to corrode.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost;
                    player_acted_ = false;
                }
            }
            break;

        default: break;
    }
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
    audio_.play(SfxId::ARROW_FIRE);
    particles_.arrow_trail(shooter.x, shooter.y, tgt_x, tgt_y);
    if (result.hit) { audio_.play(SfxId::ARROW_HIT); particles_.hit_spark(tgt_x, tgt_y); }
    if (result.killed) { audio_.play(SfxId::DEATH); particles_.death_burst(tgt_x, tgt_y); }

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
            if (gain != 0) adjust_favor(gain);
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

void Engine::process_status_effects() {
    if (!world_.has<StatusEffects>(player_) || !world_.has<Stats>(player_)) return;
    auto& fx = world_.get<StatusEffects>(player_);
    auto& stats = world_.get<Stats>(player_);

    bool has_blackblood = world_.has<Diseases>(player_) &&
                          world_.get<Diseases>(player_).has(DiseaseId::BLACKBLOOD);

    auto& pp = world_.get<Position>(player_);
    for (auto& eff : fx.effects) {
        // Blackblood: immune to poison
        if (eff.type == StatusType::POISON && has_blackblood) {
            eff.turns_remaining = 0;
            log_.add("Your blackened blood neutralizes the poison.", {80, 40, 80, 255});
            continue;
        }
        // Apply resistance reduction
        int dmg = eff.damage;
        if (eff.type == StatusType::POISON && stats.poison_resist > 0) {
            dmg = dmg * (100 - stats.poison_resist) / 100;
            if (stats.poison_resist >= 100) { eff.turns_remaining = 0; continue; } // immune
        }
        if (eff.type == StatusType::BURN && stats.fire_resist > 0) {
            dmg = dmg * (100 - stats.fire_resist) / 100;
            if (stats.fire_resist >= 100) { eff.turns_remaining = 0; continue; }
        }
        if (eff.type == StatusType::BLEED && stats.bleed_resist > 0) {
            dmg = dmg * (100 - stats.bleed_resist) / 100;
            if (stats.bleed_resist >= 100) { eff.turns_remaining = 0; continue; }
        }
        if (dmg < 0) dmg = 0;
        stats.hp -= dmg;
        char buf[128];
        if (dmg > 0) {
            switch (eff.type) {
                case StatusType::POISON:
                    snprintf(buf, sizeof(buf), "Poison burns through your veins. (%d)", dmg);
                    log_.add(buf, {100, 200, 100, 255});
                    audio_.play(SfxId::POISON);
                    particles_.poison_effect(pp.x, pp.y);
                    break;
                case StatusType::BURN:
                    snprintf(buf, sizeof(buf), "Fire sears your flesh. (%d)", dmg);
                    log_.add(buf, {255, 160, 60, 255});
                    audio_.play(SfxId::BURN);
                    particles_.burn_effect(pp.x, pp.y);
                    break;
                case StatusType::BLEED:
                    snprintf(buf, sizeof(buf), "Blood seeps from your wounds. (%d)", dmg);
                    log_.add(buf, {200, 80, 80, 255});
                    particles_.bleed_effect(pp.x, pp.y);
                    break;
                default: break; // non-DOT statuses (frozen, stunned, etc.) don't tick damage
            }
        }
    }
    fx.tick();

    // Disease tick effects
    if (world_.has<Diseases>(player_)) {
        auto& diseases = world_.get<Diseases>(player_);

        // Sporebloom: regen 1 HP every 5 turns in dungeons
        if (diseases.has(DiseaseId::SPOREBLOOM) && dungeon_level_ > 0
            && game_turn_ % 5 == 0 && stats.hp < stats.hp_max) {
            stats.hp++;
        }

        // Vampirism: surface (overworld) hurts — 1 damage every 3 turns
        if (diseases.has(DiseaseId::VAMPIRISM) && dungeon_level_ <= 0
            && game_turn_ % 3 == 0) {
            stats.hp--;
            if (game_turn_ % 15 == 0) // don't spam
                log_.add("The sunlight scalds your skin.", {200, 160, 100, 255});
        }
    }

    // Tick spell buffs and revert expired ones
    if (world_.has<Buffs>(player_)) {
        auto& buffs = world_.get<Buffs>(player_);
        buffs.tick();
        auto expired = buffs.collect_expired();
        for (auto& b : expired) {
            switch (b.type) {
                case BuffType::HARDEN_SKIN:
                case BuffType::FORESIGHT:
                case BuffType::SHIELD_OF_FAITH:
                case BuffType::SANCTUARY:
                    stats.natural_armor = std::max(0, stats.natural_armor - b.value);
                    break;
                case BuffType::HASTEN:
                    stats.base_speed -= b.value;
                    break;
                case BuffType::STONE_FIST:
                    stats.base_damage = std::max(1, stats.base_damage - b.value);
                    break;
                case BuffType::IRON_BODY:
                    stats.natural_armor = std::max(0, stats.natural_armor - b.value);
                    stats.base_speed += b.value2; // restore speed penalty
                    break;
                case BuffType::BARKSKIN:
                    stats.natural_armor = std::max(0, stats.natural_armor - b.value);
                    stats.poison_resist -= b.value2;
                    break;
            }
            log_.add("A spell effect wears off.", {140, 130, 120, 255});
        }
    }

    // Tick god-specific status effects on player
    if (stats.invisible_turns > 0) stats.invisible_turns--;
    if (stats.unyielding_turns > 0) stats.unyielding_turns--;
    if (stats.stone_skin_turns > 0) stats.stone_skin_turns--;

    // Tick drown, sleep, invisible on ALL entities (monsters)
    auto& all_stats_pool = world_.pool<Stats>();
    for (size_t i = 0; i < all_stats_pool.size(); i++) {
        Entity e = all_stats_pool.entity_at(i);
        if (e == player_) continue;
        auto& es = all_stats_pool.at_index(i);
        // Drown tick
        if (es.drown_turns > 0) {
            es.hp -= es.drown_damage;
            es.drown_turns--;
            if (es.hp <= 0) combat::kill(world_, e, log_);
        }
        // Sleep tick
        if (es.sleep_turns > 0) es.sleep_turns--;
    }

    // Soleth passive: undead adjacent to player take 1 damage/turn
    if (world_.has<GodAlignment>(player_)) {
        auto& ga = world_.get<GodAlignment>(player_);
        if (ga.god == GodId::SOLETH) {
            auto& ai_pool_s = world_.pool<AI>();
            for (size_t i = 0; i < ai_pool_s.size(); i++) {
                Entity e = ai_pool_s.entity_at(i);
                if (!world_.has<Position>(e) || !world_.has<Stats>(e)) continue;
                auto& mp = world_.get<Position>(e);
                int dx = std::abs(mp.x - pp.x);
                int dy = std::abs(mp.y - pp.y);
                if (dx <= 1 && dy <= 1) {
                    auto& es = world_.get<Stats>(e);
                    if (is_undead(es.name.c_str())) {
                        es.hp -= 1;
                        if (es.hp <= 0) combat::kill(world_, e, log_);
                    }
                }
            }
        }

        // Sythara passive: 15% chance enemies you damaged get diseased
        // (handled in combat resolution, not here)

        // Thalara passive: fire zones hurt
        if (ga.god == GodId::THALARA && dungeon_level_ > 0 && game_turn_ % 3 == 0) {
            // Check if in a fire-themed zone (Molten Depths)
            std::string zone;
            if (current_dungeon_idx_ >= 0 && current_dungeon_idx_ < static_cast<int>(dungeon_registry_.size()))
                zone = dungeon_registry_[current_dungeon_idx_].zone;
            if (zone == "molten" || zone == "molten_depths") {
                stats.hp -= 1;
                if (game_turn_ % 15 == 0)
                    log_.add("The heat sears you. Thalara's domain is water, not fire.", {80, 180, 200, 255});
            }
        }

        // Lethis passive: lethal save (once per floor)
        if (ga.god == GodId::LETHIS && stats.hp <= 0 && !ga.lethal_save_used) {
            ga.lethal_save_used = true;
            stats.hp = 1;
            log_.add("You die. Then you wake up.", {160, 120, 200, 255});
            particles_.prayer_effect(pp.x, pp.y, 160, 120, 200);
        }

        // === Negative favor punishments (escalating) ===
        if (ga.god != GodId::NONE && ga.favor < 0) {
            auto& ginfo = get_god_info(ga.god);

            // Mild: favor -1 to -30 — prayer costs doubled (handled in execute_prayer)
            // Moderate: favor -31 to -60 — random stat drain every 50 turns
            if (ga.favor <= -30 && game_turn_ % 50 == 0) {
                int attr_idx = rng_.range(0, 6);
                int cur = stats.attributes[attr_idx];
                if (cur > 3) {
                    stats.attributes[attr_idx] = cur - 1;
                    static const char* ATTR_NAMES[] = {"STR","DEX","CON","INT","WIL","PER","CHA"};
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%s's displeasure weakens you. (-1 %s)", ginfo.name, ATTR_NAMES[attr_idx]);
                    log_.add(buf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                }
            }

            // Severe: favor -61 to -99 — periodic HP damage
            if (ga.favor <= -60 && game_turn_ % 20 == 0) {
                int dmg = 1 + (-ga.favor) / 30;
                stats.hp -= dmg;
                if (game_turn_ % 60 == 0) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%s punishes your faithlessness. (%d)", ginfo.name, dmg);
                    log_.add(buf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                }
            }

            // Excommunication: favor == -100
            if (ga.favor <= -100 && game_turn_ % 40 == 0) {
                // Spawn a divine enemy near the player
                for (int a = 0; a < 30; a++) {
                    int mx = pp.x + rng_.range(-4, 4);
                    int my = pp.y + rng_.range(-4, 4);
                    if (mx == pp.x && my == pp.y) continue;
                    if (!map_.in_bounds(mx, my) || !map_.is_walkable(mx, my)) continue;
                    if (combat::entity_at(world_, mx, my, player_) != NULL_ENTITY) continue;
                    Entity de = world_.create();
                    world_.add<Position>(de, {mx, my});
                    world_.add<Renderable>(de, {SHEET_MONSTERS, 3, 4, // death knight sprite
                                                 {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255}, 5});
                    Stats ds; ds.name = "divine avenger"; ds.hp = 30 + stats.level * 5;
                    ds.hp_max = ds.hp; ds.base_damage = 6 + stats.level; ds.base_speed = 110;
                    ds.xp_value = 25 + stats.level * 5;
                    world_.add<Stats>(de, std::move(ds));
                    world_.add<AI>(de, {AIState::HUNTING, pp.x, pp.y, 0, 0}); // never flees
                    world_.add<Energy>(de, {0, 110});
                    if (game_turn_ % 120 == 0) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "%s sends an avenger.", ginfo.name);
                        log_.add(buf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                    }
                    break;
                }
            }
        }
    }

    if (stats.hp <= 0) {
        state_ = GameState::DEAD;
    }
}

void Engine::update_music_for_location() {
    if (state_ != GameState::PLAYING) return;

    if (dungeon_level_ <= 0) {
        // Overworld — check if near a town
        static const struct { int x, y; } TOWN_CENTERS[] = {
            {1000,750}, {750,650}, {1300,670}, {850,950}, {1200,930},
            {1050,450}, {650,800}, {1400,750}, {1000,1100}, {800,400},
            {1250,1100}, {550,550}, {1450,500}, {900,1200}, {1100,300},
            {700,1050}, {1350,1000}, {1150,550}, {600,700}, {1500,850},
        };
        bool near_town = false;
        if (world_.has<Position>(player_)) {
            auto& pp = world_.get<Position>(player_);
            for (auto& tc : TOWN_CENTERS) {
                if (std::abs(pp.x - tc.x) < 25 && std::abs(pp.y - tc.y) < 25) {
                    near_town = true;
                    break;
                }
            }
        }

        if (near_town) {
            // Only switch if not already playing town music
            if (audio_.current_music() != MusicId::TOWN1 &&
                audio_.current_music() != MusicId::TOWN2) {
                audio_.stop_all_ambient(800);
                MusicId town[] = {MusicId::TOWN1, MusicId::TOWN2};
                audio_.play_music(town[rng_.range(0, 1)], 2000);
                audio_.play_ambient(AmbientId::INTERIOR_DAY, 1000);
            }
        } else {
            // Only switch if not already playing overworld music
            if (audio_.current_music() != MusicId::OVERWORLD1 &&
                audio_.current_music() != MusicId::OVERWORLD2 &&
                audio_.current_music() != MusicId::OVERWORLD3) {
                audio_.stop_all_ambient(800);
                MusicId ow_tracks[] = {MusicId::OVERWORLD1, MusicId::OVERWORLD2, MusicId::OVERWORLD3};
                audio_.play_music(ow_tracks[rng_.range(0, 2)], 2000);
                audio_.play_ambient(AmbientId::FOREST_DAY, 1000);
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
        audio_.play_ambient(AmbientId::CAVE, 1000);
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
    // Helper: spawn a wilderness NPC
    auto spawn_ow_npc = [&](int x, int y, const char* name, const char* dialogue,
                             NPCRole role, int spr_x, int spr_y, int wander_speed = 35) {
        // Find a walkable tile near the target
        for (int a = 0; a < 20; a++) {
            int tx = x + rng_.range(-3, 3);
            int ty = y + rng_.range(-3, 3);
            if (!map_.in_bounds(tx, ty) || !map_.is_walkable(tx, ty)) continue;
            Entity e = world_.create();
            world_.add<Position>(e, {tx, ty});
            NPC nc;
            nc.role = role; nc.name = name; nc.dialogue = dialogue;
            nc.home_x = tx; nc.home_y = ty;
            world_.add<NPC>(e, std::move(nc));
            world_.add<Renderable>(e, {SHEET_ROGUES, spr_x, spr_y, {255,255,255,255}, 5});
            Stats ns; ns.name = name; ns.hp = 999; ns.hp_max = 999;
            world_.add<Stats>(e, std::move(ns));
            world_.add<Energy>(e, {0, wander_speed});
            return;
        }
    };

    // Helper: place a lore item on the ground
    auto place_lore = [&](int x, int y, const char* name, const char* text) {
        for (int a = 0; a < 10; a++) {
            int tx = x + rng_.range(-2, 2);
            int ty = y + rng_.range(-2, 2);
            if (!map_.in_bounds(tx, ty) || !map_.is_walkable(tx, ty)) continue;
            Entity e = world_.create();
            world_.add<Position>(e, {tx, ty});
            world_.add<Renderable>(e, {SHEET_ITEMS, 2, 20, {255,255,255,255}, 1});
            Item item;
            item.name = name; item.description = text;
            item.type = ItemType::SCROLL; item.identified = true; item.gold_value = 5;
            world_.add<Item>(e, std::move(item));
            return;
        }
    };

    // Helper: paint a small water feature
    auto paint_lake = [&](int cx, int cy, int radius) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx*dx + dy*dy > radius*radius) continue;
                int tx = cx + dx, ty = cy + dy;
                if (!map_.in_bounds(tx, ty)) continue;
                auto& t = map_.at(tx, ty);
                if (t.type == TileType::FLOOR_GRASS || t.type == TileType::FLOOR_DIRT)
                    t.type = TileType::WATER;
            }
        }
    };

    // Helper: paint a small ruin (scattered stone walls + floor)
    auto paint_ruin = [&](int cx, int cy) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                int tx = cx + dx, ty = cy + dy;
                if (!map_.in_bounds(tx, ty)) continue;
                auto& t = map_.at(tx, ty);
                if (t.type != TileType::FLOOR_GRASS && t.type != TileType::FLOOR_DIRT) continue;
                // Outer ring = scattered walls, inner = stone floor
                if (std::abs(dx) == 2 || std::abs(dy) == 2) {
                    if (rng_.chance(40)) t.type = TileType::WALL_STONE_ROUGH;
                } else {
                    t.type = TileType::FLOOR_STONE;
                }
            }
        }
    };

    // =============================================
    // WANDERING WILDERNESS NPCs
    // =============================================

    // Travelers on roads between towns
    static const struct { int x, y; const char* dialogue; } TRAVELERS[] = {
        {875, 700, "The roads aren't safe. But then, nothing is."},
        {1150, 710, "I'm heading to Greywatch. They say there's work there."},
        {950, 850, "Used to be farmers here. Before the barrow opened."},
        {1100, 600, "The cold gets worse the further north you go."},
        {700, 750, "Bramblewood's seen better days. The forest is closing in."},
        {1300, 850, "Ironhearth's forges never stop. You can hear them for miles."},
        {800, 1050, "Something's wrong with the water south of here."},
        {1050, 950, "I saw lights in the hills last night. Moving."},
    };
    for (auto& t : TRAVELERS) {
        spawn_ow_npc(t.x, t.y, "Traveler", t.dialogue, NPCRole::FARMER, 1, 6); // peasant sprite
    }

    // Pilgrims (near dungeon entrances or holy sites)
    spawn_ow_npc(1060, 730, "Pilgrim", "The barrow calls to the faithful. And the foolish.", NPCRole::FARMER, 7, 0);
    spawn_ow_npc(1450, 520, "Pilgrim", "Soleth's fire burns in Candlemere. I go to pray.", NPCRole::FARMER, 7, 0);
    spawn_ow_npc(580, 560, "Pilgrim", "The seal at Hollowgate. Have you seen it? It's cracking.", NPCRole::FARMER, 7, 0);

    // Hunters in the deep wilderness
    spawn_ow_npc(300, 500, "Hunter", "The game's thin out here. Something's scaring them deeper into the woods.", NPCRole::FARMER, 2, 0);
    spawn_ow_npc(1700, 700, "Hunter", "I track wolves. They've been moving in packs larger than I've ever seen.", NPCRole::FARMER, 2, 0);
    spawn_ow_npc(500, 1100, "Hunter", "Don't go south. The swamp takes people.", NPCRole::FARMER, 2, 0);

    // Hermits (isolated, deeper dialogue)
    spawn_ow_npc(200, 300, "Hermit", "I left the towns years ago. The gods are louder out here.", NPCRole::PRIEST, 7, 0);
    spawn_ow_npc(1800, 400, "Old Woman", "I remember when there were no dungeons. Then the ground opened.", NPCRole::FARMER, 3, 7);
    spawn_ow_npc(400, 1200, "Hermit", "The Reliquary isn't what they think. It was here before the gods.", NPCRole::PRIEST, 4, 7);
    spawn_ow_npc(1600, 1100, "Madman", "I HEARD IT. Under the stone. Breathing.", NPCRole::FARMER, 1, 7);

    // =============================================
    // ENCAMPMENTS (small NPC + lore clusters)
    // =============================================

    // Abandoned camp — between Ashford and Hollowgate
    paint_ruin(650, 600);
    place_lore(650, 600, "abandoned journal",
        "Day 3. We found the entrance. Day 5. Markus didn't come back. Day 7. None of us are going back in.");
    spawn_ow_npc(655, 600, "Deserter", "I was a guard once. Then I saw what's down there.", NPCRole::GUARD, 0, 1);

    // Mercenary camp — between Greywatch and Ironhearth
    spawn_ow_npc(1350, 720, "Sellsword", "We're waiting for a contract. Know anyone who needs killing?", NPCRole::GUARD, 1, 1);
    spawn_ow_npc(1355, 725, "Sellsword", "Gold talks. Everything else walks.", NPCRole::GUARD, 1, 1);

    // Scholar's camp — between Frostmere and Glacierveil
    spawn_ow_npc(1080, 370, "Field Scholar", "The inscriptions up north predate the current pantheon by centuries.", NPCRole::PRIEST, 5, 6);
    place_lore(1075, 370, "field notes",
        "The symbols near Glacierveil match nothing in our records. They resemble the Sepulchre markings.");

    // Refugee camp — between Dustfall and Sandmoor
    spawn_ow_npc(950, 1150, "Refugee", "The southern dungeons drove us out. We can't go home.", NPCRole::FARMER, 0, 6);
    spawn_ow_npc(955, 1155, "Refugee", "My children are hungry. The road north is dangerous.", NPCRole::FARMER, 3, 7);

    // =============================================
    // POINTS OF INTEREST
    // =============================================

    // Standing stones — ancient, pre-god monuments
    auto paint_standing_stone = [&](int x, int y) {
        if (map_.in_bounds(x, y)) map_.at(x, y).type = TileType::FLOOR_STONE;
        if (map_.in_bounds(x-1, y)) map_.at(x-1, y).type = TileType::ROCK;
        if (map_.in_bounds(x+1, y)) map_.at(x+1, y).type = TileType::ROCK;
    };

    paint_standing_stone(400, 400);
    place_lore(400, 402, "worn inscription",
        "BEFORE THE SEVEN. BEFORE THE NAMING. THIS PLACE REMEMBERS.");

    paint_standing_stone(1600, 300);
    place_lore(1600, 302, "cracked tablet",
        "The Reliquary was not made. It arrived. The stones grew around it.");

    paint_standing_stone(800, 1300);
    place_lore(800, 1302, "eroded pillar text",
        "Seven gods claimed it. None of them made it. Who will claim it next?");

    // Graveyard — north of Thornwall
    for (int i = 0; i < 8; i++) {
        int gx = 980 + (i % 4) * 4;
        int gy = 690 + (i / 4) * 4;
        if (map_.in_bounds(gx, gy)) map_.at(gx, gy).type = TileType::FLOOR_BONE;
    }
    place_lore(982, 695, "gravestone",
        "Here lies the second paragon of Morreth. He did not fail. He chose to stop.");

    // Old battlefield — between Redrock and Stonehollow
    for (int i = 0; i < 12; i++) {
        int bx = 1280 + rng_.range(-8, 8);
        int by = 960 + rng_.range(-8, 8);
        if (map_.in_bounds(bx, by) && map_.is_walkable(bx, by))
            map_.at(bx, by).type = TileType::FLOOR_BONE;
    }
    place_lore(1280, 960, "rusted helm",
        "Hundreds died here. The grass grew back. The bones didn't leave.");

    // Shrine of the older gods — deep wilderness
    paint_ruin(250, 800);
    place_lore(250, 800, "ancient shrine inscription",
        "This shrine predates the Seven. It honors something that has no name. The stone is warm.");

    // Watchtower ruins — hilltop between Whitepeak and Frostmere
    paint_ruin(920, 420);
    spawn_ow_npc(920, 420, "Tower Guard", "I watch the north. Nothing comes from there anymore. That worries me.", NPCRole::GUARD, 0, 1);

    // Witch's hut — deep forest
    spawn_ow_npc(350, 700, "Hedge Witch", "I know what you seek. Everyone who comes here seeks the same thing.", NPCRole::PRIEST, 4, 7);
    place_lore(355, 700, "witch's note",
        "The herbs won't help. The prayers won't help. The only cure for what's down there is not going down there.");

    // =============================================
    // WATER FEATURES
    // =============================================

    // Small lakes
    paint_lake(300, 600, 4);  // western wilderness lake
    paint_lake(1700, 500, 3); // northeastern lake
    paint_lake(900, 1350, 5); // southern marsh
    paint_lake(500, 350, 3);  // northwestern pond
    paint_lake(1400, 1200, 4); // southeastern lake

    // River segments (short chains of water tiles)
    auto paint_river = [&](int x1, int y1, int x2, int y2) {
        int steps = std::max(std::abs(x2-x1), std::abs(y2-y1));
        for (int i = 0; i <= steps; i++) {
            float t = (steps > 0) ? static_cast<float>(i) / steps : 0;
            int rx = x1 + static_cast<int>((x2-x1) * t);
            int ry = y1 + static_cast<int>((y2-y1) * t);
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 0; dx++) {
                    int tx = rx + dx, ty = ry + dy;
                    if (map_.in_bounds(tx, ty)) {
                        auto& tile = map_.at(tx, ty);
                        if (tile.type == TileType::FLOOR_GRASS || tile.type == TileType::FLOOR_DIRT)
                            tile.type = TileType::WATER;
                    }
                }
            }
        }
    };

    paint_river(300, 580, 350, 620);  // flows into western lake
    paint_river(1680, 490, 1720, 510); // northeastern stream
    paint_river(880, 1330, 920, 1370); // southern marsh outflow

    // =============================================
    // TOWN DECORATIONS
    // =============================================

    // Town decoration clusters
    static const struct { int x, y; } TOWN_POS[] = {
        {500,375}, {375,325}, {650,335}, {425,475}, {600,465},
        {525,225}, {325,400}, {700,375}, {500,550}, {400,200},
        {625,550}, {275,275}, {725,250}, {450,600}, {550,150},
        {350,525}, {675,500}, {575,275}, {300,350}, {750,425},
    };

    // Helper: check if a tile is adjacent to a wall (building exterior)
    auto is_wall = [&](int x, int y) -> bool {
        if (!map_.in_bounds(x, y)) return false;
        auto t = map_.at(x, y).type;
        return t == TileType::WALL_STONE_BRICK || t == TileType::WALL_STONE_ROUGH ||
               t == TileType::WALL_DIRT || t == TileType::WALL_WOOD || t == TileType::DOOR_CLOSED;
    };
    auto adjacent_to_wall = [&](int x, int y) -> bool {
        return is_wall(x-1,y) || is_wall(x+1,y) || is_wall(x,y-1) || is_wall(x,y+1);
    };

    // Helper: place doodads against building walls in a town
    auto place_against_walls = [&](int cx, int cy, int radius, int count,
                                    int sx, int sy) {
        int placed = 0;
        for (int attempt = 0; attempt < count * 10 && placed < count; attempt++) {
            int tx = cx + rng_.range(-radius, radius);
            int ty = cy + rng_.range(-radius, radius);
            if (!map_.in_bounds(tx, ty) || !map_.is_walkable(tx, ty)) continue;
            if (!adjacent_to_wall(tx, ty)) continue;
            // Don't block doors
            if (map_.at(tx, ty).type == TileType::DOOR_OPEN) continue;
            Entity e = world_.create();
            world_.add<Position>(e, {tx, ty});
            world_.add<Renderable>(e, {SHEET_TILES, sx, sy, {255,255,255,255}, 0});
            placed++;
        }
    };

    // Helper: place plants on open ground (not near walls)
    auto place_on_open_ground = [&](int cx, int cy, int radius, int count,
                                     int sx, int sy) {
        int placed = 0;
        for (int attempt = 0; attempt < count * 8 && placed < count; attempt++) {
            int tx = cx + rng_.range(-radius, radius);
            int ty = cy + rng_.range(-radius, radius);
            if (!map_.in_bounds(tx, ty)) continue;
            auto tt = map_.at(tx, ty).type;
            if (tt != TileType::FLOOR_GRASS && tt != TileType::FLOOR_DIRT) continue;
            if (adjacent_to_wall(tx, ty)) continue; // not against buildings
            Entity e = world_.create();
            world_.add<Position>(e, {tx, ty});
            world_.add<Renderable>(e, {SHEET_TILES, sx, sy, {255,255,255,255}, 0});
            placed++;
        }
    };

    // Helper: place animated light sources against walls
    auto place_lights = [&](int cx, int cy, int radius, int count, int anim_row) {
        int placed = 0;
        for (int attempt = 0; attempt < count * 15 && placed < count; attempt++) {
            int tx = cx + rng_.range(-radius, radius);
            int ty = cy + rng_.range(-radius, radius);
            if (!map_.in_bounds(tx, ty) || !map_.is_walkable(tx, ty)) continue;
            if (!adjacent_to_wall(tx, ty)) continue;
            Entity e = world_.create();
            world_.add<Position>(e, {tx, ty});
            world_.add<Renderable>(e, {SHEET_ANIMATED, 0, anim_row, {255,255,255,255}, 0});
            placed++;
        }
    };

    for (auto& tp : TOWN_POS) {
        // Braziers against building walls (1-2 per town)
        place_lights(tp.x, tp.y, 18, rng_.range(1, 2), 1); // brazier lit = row 1
        // Barrels against building walls (3-5 per town)
        place_against_walls(tp.x, tp.y, 18, rng_.range(3, 5), 4, 17);
        // Log piles against walls (1-3)
        place_against_walls(tp.x, tp.y, 18, rng_.range(1, 3), 6, 17);
        // Ore sacks against walls (0-1)
        if (rng_.chance(40))
            place_against_walls(tp.x, tp.y, 15, 1, 5, 17);
        // Crops/plants on open ground at town outskirts (4-8)
        int crop = rng_.range(0, 15);
        place_on_open_ground(tp.x, tp.y, 25, rng_.range(4, 8), crop, 19);
    }

    // =============================================
    // WOOD BUILDINGS — cabins, outposts, hamlets
    // =============================================

    // Helper: build a small wood cabin (exterior walls + dirt floor + door)
    auto build_cabin = [&](int cx, int cy, int w, int h) {
        for (int dy = 0; dy < h; dy++) {
            for (int dx = 0; dx < w; dx++) {
                int tx = cx + dx, ty = cy + dy;
                if (!map_.in_bounds(tx, ty)) continue;
                auto& t = map_.at(tx, ty);
                if (t.type == TileType::TREE || t.type == TileType::ROCK) continue; // don't overwrite trees
                bool is_edge = (dx == 0 || dx == w-1 || dy == 0 || dy == h-1);
                if (is_edge) {
                    t.type = TileType::WALL_WOOD;
                } else {
                    t.type = TileType::FLOOR_DIRT;
                }
            }
        }
        // Door on the south wall (bottom center)
        int door_x = cx + w / 2;
        int door_y = cy + h - 1;
        if (map_.in_bounds(door_x, door_y))
            map_.at(door_x, door_y).type = TileType::DOOR_CLOSED;
    };

    // Isolated cabins with hermits/NPCs
    struct CabinDef { int x, y, w, h; const char* npc_name; const char* dialogue; };
    static const CabinDef CABINS[] = {
        {180, 450, 5, 4, "Woodsman",
         "I built this place with my hands. The forest gives. The forest takes."},
        {1750, 350, 4, 4, "Recluse",
         "Go away. No, wait. When did you last see another person on the road?"},
        {450, 1150, 5, 4, "Swamp Hermit",
         "The water here glows some nights. I don't drink from it."},
        {1650, 950, 4, 4, "Retired Soldier",
         "I fought in the wars before the barrows opened. We thought THAT was bad."},
        {350, 250, 5, 4, "Old Hunter",
         "There's a standing stone to the east. Don't touch it."},
        {1550, 200, 4, 4, "Cartographer",
         "I've mapped every road. The map changes. I don't think the roads are moving."},
    };

    for (auto& cd : CABINS) {
        build_cabin(cd.x, cd.y, cd.w, cd.h);
        // Spawn NPC inside the cabin
        spawn_ow_npc(cd.x + cd.w/2, cd.y + cd.h/2, cd.npc_name, cd.dialogue,
                      NPCRole::FARMER, 7, 0); // elderly man sprite
        // Barrel or log pile against the outside wall
        place_against_walls(cd.x - 1, cd.y, cd.w + 2, 1, 4, 17); // barrel
        place_against_walls(cd.x - 1, cd.y, cd.w + 2, 1, 6, 17); // log pile
        // Torch by the door
        place_lights(cd.x + cd.w/2, cd.y + cd.h, 3, 1, 5); // torch lit = row 5
    }

    // Small hamlets — 2-3 cabins clustered together
    struct HamletDef { int x, y; const char* name; };
    static const HamletDef HAMLETS[] = {
        {300, 650, "Thornbrook"},
        {1200, 200, "Icewind Post"},
        {1100, 1250, "Dry Creek"},
        {500, 900, "Mosshaven"},
    };

    for (auto& hm : HAMLETS) {
        // 2-3 cabins in a cluster
        build_cabin(hm.x, hm.y, 5, 4);
        build_cabin(hm.x + 7, hm.y + 1, 4, 4);
        if (rng_.chance(60))
            build_cabin(hm.x + 2, hm.y + 6, 5, 3);

        // Hamlet NPCs
        spawn_ow_npc(hm.x + 2, hm.y + 2, "Villager",
            "This place has no name on the maps. We like it that way.", NPCRole::FARMER, 1, 6);
        spawn_ow_npc(hm.x + 9, hm.y + 3, "Villager",
            "Trade comes through once a season. If we're lucky.", NPCRole::FARMER, 0, 6);

        // Doodads around hamlet
        place_against_walls(hm.x - 1, hm.y - 1, 14, rng_.range(2, 4), 4, 17); // barrels
        place_against_walls(hm.x - 1, hm.y - 1, 14, rng_.range(1, 2), 6, 17); // log piles
    }

    // Outposts — single fortified structures (guard post at crossroads)
    struct OutpostDef { int x, y; const char* dialogue; };
    static const OutpostDef OUTPOSTS[] = {
        {1050, 660, "Road's clear, last I checked. That was yesterday."},
        {850, 500, "I watch the northern pass. Nothing human comes through anymore."},
        {700, 950, "The southern road gets worse every year. We need more guards."},
    };

    for (auto& op : OUTPOSTS) {
        build_cabin(op.x, op.y, 6, 5);
        spawn_ow_npc(op.x + 3, op.y + 2, "Road Guard", op.dialogue, NPCRole::GUARD, 0, 1);
        place_against_walls(op.x - 1, op.y - 1, 8, 2, 4, 17); // barrels
        place_lights(op.x + 3, op.y + 5, 3, 1, 5); // torch at entrance
    }

    // =============================================
    // OVERWORLD VEGETATION BY REGION
    // =============================================

    // Temperate zone: varied flowers and grasses
    for (int i = 0; i < 80; i++) {
        int x = rng_.range(100, 1900);
        int y = rng_.range(500, 900);
        if (!map_.in_bounds(x, y)) continue;
        auto tt = map_.at(x, y).type;
        if (tt != TileType::FLOOR_GRASS) continue;
        int crop = rng_.range(0, 15); // random plant
        Entity e = world_.create();
        world_.add<Position>(e, {x, y});
        world_.add<Renderable>(e, {SHEET_TILES, crop, 19, {255,255,255,255}, 0});
    }

    // Northern cold zone: sparse, icy-blue tinted plants
    for (int i = 0; i < 30; i++) {
        int x = rng_.range(100, 1900);
        int y = rng_.range(100, 400);
        if (!map_.in_bounds(x, y)) continue;
        auto tt = map_.at(x, y).type;
        if (tt != TileType::FLOOR_GRASS && tt != TileType::FLOOR_ICE) continue;
        Entity e = world_.create();
        world_.add<Position>(e, {x, y});
        // Frosty blue-tinted plants
        world_.add<Renderable>(e, {SHEET_TILES, rng_.range(0, 5), 19,
                                    {180, 200, 240, 255}, 0});
    }

    // Southern warm zone: warm-tinted plants, more variety
    for (int i = 0; i < 60; i++) {
        int x = rng_.range(100, 1900);
        int y = rng_.range(1000, 1400);
        if (!map_.in_bounds(x, y)) continue;
        auto tt = map_.at(x, y).type;
        if (tt != TileType::FLOOR_GRASS && tt != TileType::FLOOR_SAND
            && tt != TileType::FLOOR_DIRT) continue;
        int crop = rng_.range(0, 15);
        Entity e = world_.create();
        world_.add<Position>(e, {x, y});
        SDL_Color tint = (tt == TileType::FLOOR_SAND)
            ? SDL_Color{220, 200, 140, 255}  // sandy tint
            : SDL_Color{255, 255, 255, 255};
        world_.add<Renderable>(e, {SHEET_TILES, crop, 19, tint, 0});
    }

    // Mushrooms near dungeon entrances and in dark forest areas
    for (int i = 0; i < 25; i++) {
        int x = rng_.range(100, 1900);
        int y = rng_.range(100, 1400);
        if (!map_.in_bounds(x, y)) continue;
        // Only place near trees (forest areas)
        bool near_tree = false;
        for (int dy = -2; dy <= 2 && !near_tree; dy++)
            for (int dx = -2; dx <= 2 && !near_tree; dx++)
                if (map_.in_bounds(x+dx, y+dy) && map_.at(x+dx, y+dy).type == TileType::TREE)
                    near_tree = true;
        if (!near_tree) continue;
        if (!map_.is_walkable(x, y)) continue;
        Entity e = world_.create();
        world_.add<Position>(e, {x, y});
        world_.add<Renderable>(e, {SHEET_TILES, rng_.range(0, 1), 20, {255,255,255,255}, 0});
    }
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
        };
        log_.add(GENERIC[rng_.range(0, 4)], {120, 115, 110, 255});
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
            };
            log_.add(W[rng_.range(0, 4)], {140, 130, 100, 255});
        } else if (zone == "stonekeep") {
            static const char* S[] = {
                "Ancient mortar crumbles at your touch.", "The stonework here is older than the towns above.",
                "A cold wind blows through cracks in the wall.", "Iron sconces, long since empty.",
                "Your torch light catches old scratches on the walls.",
            };
            log_.add(S[rng_.range(0, 4)], {130, 130, 140, 255});
        } else if (zone == "catacombs") {
            static const char* C[] = {
                "Bones are stacked floor to ceiling.", "The dead are everywhere, but not all of them stay still.",
                "A faint moan from deeper in.", "The air tastes like dust and copper.",
                "Names are carved into every surface. Thousands of them.",
            };
            log_.add(C[rng_.range(0, 4)], {140, 120, 130, 255});
        } else if (zone == "molten") {
            static const char* M[] = {
                "The heat is almost unbearable.", "Lava glows in the cracks between stones.",
                "The rock walls radiate warmth.", "Sulphur stings your nostrils.",
                "The ground trembles slightly.",
            };
            log_.add(M[rng_.range(0, 4)], {160, 120, 80, 255});
        } else if (zone == "sunken") {
            static const char* SU[] = {
                "Water seeps through every crack.", "The walls are slick with moisture.",
                "Your boots splash in shallow water.", "The water here is unnaturally still.",
                "Something moves beneath the water.",
            };
            log_.add(SU[rng_.range(0, 4)], {100, 130, 150, 255});
        } else if (zone == "deep_halls") {
            static const char* D[] = {
                "The architecture here predates anything on the surface.",
                "The ceiling is so high your light doesn't reach it.",
                "Old banners hang in tatters from the walls.",
                "The stonework is precise beyond anything you've seen.",
                "Something about the proportions is wrong. Built for something larger.",
            };
            log_.add(D[rng_.range(0, 4)], {130, 125, 140, 255});
        }
    }
}

void Engine::render_victory() {
    // Darken screen
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, width_, height_};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer_, &overlay);

    if (!font_ || !font_title_) return;

    // God-specific ending text
    GodId god = GodId::NONE;
    if (world_.has<GodAlignment>(player_)) {
        god = world_.get<GodAlignment>(player_).god;
    }

    const char* title = "You hold the Reliquary.";
    const char* ending = nullptr;
    switch (god) {
        case GodId::VETHRIK:
            ending = "Vethrik claims the Reliquary.\n"
                     "The dead lie still. The undead crumble to dust.\n"
                     "The graveyards are quiet again.\n"
                     "It is done.";
            break;
        case GodId::THESSARKA:
            ending = "Thessarka takes the Reliquary.\n"
                     "Every secret in the world is laid bare.\n"
                     "The price is madness. You pay it gladly.";
            break;
        case GodId::MORRETH:
            ending = "Morreth takes the Reliquary.\n"
                     "The wars end. The strong rule.\n"
                     "You are the strongest. That is enough.";
            break;
        case GodId::YASHKHET:
            ending = "Yashkhet takes the Reliquary.\n"
                     "The blood price is paid in full.\n"
                     "Your hands will never stop shaking.";
            break;
        case GodId::KHAEL:
            ending = "Khael takes the Reliquary.\n"
                     "The forest reclaims the cities.\n"
                     "Mankind is no longer the dominant species.";
            break;
        case GodId::SOLETH:
            ending = "Soleth takes the Reliquary.\n"
                     "The Sepulchre burns. The old places burn.\n"
                     "Everything unclean burns.\n"
                     "There is a lot of burning.";
            break;
        case GodId::IXUUL:
            ending = "Ixuul takes the Reliquary.\n"
                     "It becomes something else. So does everything.\n"
                     "The world is unrecognizable by morning.";
            break;
        case GodId::ZHAVEK:
            ending = "Zhavek takes the Reliquary.\n"
                     "It disappears. The gods cannot find it.\n"
                     "Neither can you.";
            break;
        case GodId::THALARA:
            ending = "Thalara takes the Reliquary.\n"
                     "The sea rises. The lowlands flood.\n"
                     "The age of land is over.";
            break;
        case GodId::OSSREN:
            ending = "Ossren takes the Reliquary.\n"
                     "It is sealed in iron and stone.\n"
                     "No one will ever open it again.";
            break;
        case GodId::LETHIS:
            ending = "Lethis takes the Reliquary.\n"
                     "The world falls asleep.\n"
                     "Some of it wakes up. Most does not.";
            break;
        case GodId::GATHRUUN:
            ending = "Gathruun takes the Reliquary.\n"
                     "It sinks into the earth.\n"
                     "The mountains grow taller. The tunnels go deeper.";
            break;
        case GodId::SYTHARA:
            ending = "Sythara takes the Reliquary.\n"
                     "It decays. So does everything else.\n"
                     "This was always going to happen.";
            break;
        default:
            ending = "No god claims the Reliquary.\n"
                     "You hold it in faithless hands.\n"
                     "It is yours. You are not sure what that means.";
            break;
    }

    // Render title
    SDL_Color gold = {255, 220, 100, 255};
    SDL_Surface* title_surf = TTF_RenderText_Blended(font_title_, title, gold);
    if (title_surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, title_surf);
        SDL_Rect dst = {width_ / 2 - title_surf->w / 2, height_ / 4, title_surf->w, title_surf->h};
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(title_surf);
    }

    // Render ending text line by line
    SDL_Color text_col = {200, 190, 170, 255};
    int y_pos = height_ / 4 + 60;
    const char* p = ending;
    while (p && *p) {
        // Extract one line (up to \n)
        char line[256];
        int len = 0;
        while (*p && *p != '\n' && len < 255) {
            line[len++] = *p++;
        }
        line[len] = '\0';
        if (*p == '\n') p++;

        if (len > 0) {
            SDL_Surface* surf = TTF_RenderText_Blended(font_, line, text_col);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
                SDL_Rect dst = {width_ / 2 - surf->w / 2, y_pos, surf->w, surf->h};
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }
        y_pos += 24;
    }

    // Newly unlocked classes
    if (!newly_unlocked_.empty()) {
        int uy = y_pos + 20;
        SDL_Color unlock_gold = {255, 220, 100, 255};
        ui::draw_text_centered(renderer_, font_, "Class unlocked:", unlock_gold, width_ / 2, uy);
        uy += TTF_FontLineSkip(font_) + 4;
        for (auto& name : newly_unlocked_) {
            ui::draw_text_centered(renderer_, font_title_, name.c_str(), unlock_gold, width_ / 2, uy);
            uy += TTF_FontLineSkip(font_title_) + 4;
        }
    }

    // "Press any key"
    SDL_Color dim = {140, 130, 120, 255};
    SDL_Surface* prompt = TTF_RenderText_Blended(font_, "Press any key.", dim);
    if (prompt) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, prompt);
        SDL_Rect dst = {width_ / 2 - prompt->w / 2, height_ - 40, prompt->w, prompt->h};
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(prompt);
    }
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
                adjust_favor(1);
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

            // Pick a random monster from the depth-appropriate range
            int max_idx = std::min(17, 4 + dungeon_level_ * 2);
            int idx = rng_.range(0, max_idx);

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

            (void)idx;
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
            adjust_favor(-2);
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
                // Cursed items can't be unequipped
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
                // Unequip existing item in that slot (check curse)
                Entity prev = inv.get_equipped(item.slot);
                if (prev != NULL_ENTITY && world_.has<Item>(prev) &&
                    world_.get<Item>(prev).curse_state == 1) {
                    log_.add("You can't remove what's already equipped — it's cursed.", {200, 80, 80, 255});
                    world_.get<Item>(prev).identified = true;
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
                // Reveal curse on equip
                if (item.curse_state == 1) {
                    log_.add("A dark chill runs through you. The item is cursed!", {200, 80, 80, 255});
                    audio_.play(SfxId::CURSE);
                }
                // Profane item check on equip
                if (world_.has<GodAlignment>(player_) && item.tags != 0) {
                    auto& ga = world_.get<GodAlignment>(player_);
                    auto sp = get_sacred_profane(ga.god);
                    if (sp.profane && (item.tags & sp.profane)) {
                        auto& ginfo = get_god_info(ga.god);
                        adjust_favor(-2);
                        char pbuf[128];
                        snprintf(pbuf, sizeof(pbuf), "%s recoils. This item is profane.", ginfo.name);
                        log_.add(pbuf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        turn_actions_.equipped_profane = true;
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
                overworld_loaded_ = false;
                current_dungeon_idx_ = -1;
                build_traits_.clear();
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
                        // "Talk to" quests — the conversation IS the objective
                        journal_.set_state(qid, QuestState::COMPLETE);
                        journal_.set_state(qid, QuestState::FINISHED);
                        log_.add(qinfo.complete_text, {180, 170, 140, 255});
                        char buf[128];
                        snprintf(buf, sizeof(buf), "Quest complete: %s (+%dxp, +%dgold)",
                                 qinfo.name, qinfo.xp_reward, qinfo.gold_reward);
                        log_.add(buf, {120, 220, 120, 255});
                        audio_.play(SfxId::QUEST);
                        gold_ += qinfo.gold_reward;
                        if (world_.has<Stats>(player_) && qinfo.xp_reward > 0) {
                            if (world_.get<Stats>(player_).grant_xp(qinfo.xp_reward)) {
                                pending_levelup_ = true;
                                levelup_screen_.open(player_, rng_);
                                audio_.play(SfxId::LEVELUP);
                            }
                        }
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
                                default:
                                    particles_.spell_effect(sp.x, sp.y, 160, 140, 200);
                                    break;
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
                if (SDL_GetTicks() - end_screen_time_ < 1500) return; // ignore input briefly
                // Hardcore: delete save on death
                if (hardcore_) {
                    std::filesystem::remove(save::default_path());
                }
                // Save meta-progression
                if (dungeon_level_ >= 4) meta_.died_deep = true;
                update_meta_on_end();
                // Reset game state for new game
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
                // Clear all entities
                world_ = World();
                state_ = GameState::MAIN_MENU;
                main_menu_.set_can_continue(false);
                log_ = MessageLog();
                audio_.stop_all_ambient(800);
                audio_.play_music(MusicId::TITLE, 3000);
                audio_.play_ambient(AmbientId::FIRE_CRACKLE, 4000);
                audio_.play_ambient(AmbientId::FOREST_NIGHT_RAIN, 5000);
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
                world_ = World();
                state_ = GameState::MAIN_MENU;
                main_menu_.set_can_continue(false);
                log_ = MessageLog();
                audio_.stop_all_ambient(800);
                audio_.play_music(MusicId::TITLE, 3000);
                audio_.play_ambient(AmbientId::FIRE_CRACKLE, 4000);
                audio_.play_ambient(AmbientId::FOREST_NIGHT_RAIN, 5000);
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
                        generate_level(); // increments dungeon_level_
                        audio_.play(SfxId::STAIRS);
                    } else if (tile_type == TileType::STAIRS_UP &&
                               event.key.keysym.sym != SDLK_GREATER) {
                        if (dungeon_level_ > 1) {
                            // Go up one dungeon level: -2 because generate_level increments by 1
                            cache_current_floor(); // persist current floor before leaving
                            dungeon_level_ -= 2;
                            generate_level();
                            audio_.play(SfxId::STAIRS);
                        } else if (dungeon_level_ == 1) {
                            // Return to overworld from depth 1 — keep cache for re-entry
                            cache_current_floor();
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
    if (!world_.has<GodAlignment>(player_) || !world_.has<Position>(player_)) return;
    auto& ga = world_.get<GodAlignment>(player_);
    if (ga.god == GodId::NONE) return;
    auto& pos = world_.get<Position>(player_);
    auto& ginfo = get_god_info(ga.god);

    int TS = cam.tile_size;
    int px = (pos.x - cam.x) * TS;
    int py = (pos.y - cam.y) * TS + y_offset;
    int cx = px + TS / 2;
    int cy = py + TS / 2;

    Uint32 ticks = SDL_GetTicks();
    float t = ticks / 1000.0f; // time in seconds
    uint8_t r = ginfo.color.r, g = ginfo.color.g, b = ginfo.color.b;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    int ds = std::max(3, TS / 12); // dot/drip size scales with tile


    switch (ga.god) {
    case GodId::VETHRIK: {
        // Rising bone motes (matches creation screen)
        for (int i = 0; i < 4; i++) {
            float phase = std::fmod(t * 0.8f + i * 0.25f, 1.0f);
            int mx = cx + static_cast<int>(std::sin(t * 0.5f + i * 1.7f) * TS * 0.4f);
            int my = py + TS - static_cast<int>(phase * TS * 1.2f);
            int ma = static_cast<int>((1.0f - phase) * 180);
            SDL_SetRenderDrawColor(renderer_, 200, 200, 220, static_cast<Uint8>(ma));
            SDL_Rect mote = {mx - ds, my - ds, ds * 2, ds * 2};
            SDL_RenderFillRect(renderer_, &mote);
        }
        break;
    }
    case GodId::THESSARKA: {
        for (int i = 0; i < 4; i++) {
            float angle = t * 1.8f + i * 1.5708f;
            int ox = cx + static_cast<int>(std::cos(angle) * TS * 0.6f);
            int oy = cy + static_cast<int>(std::sin(angle) * TS * 0.4f);
            SDL_SetRenderDrawColor(renderer_, r, g, b, 170);
            SDL_Rect dot = {ox - ds * 2, oy - ds * 2, ds * 4, ds * 4};
            SDL_RenderFillRect(renderer_, &dot);
        }
        break;
    }
    case GodId::MORRETH: {
        for (int i = 0; i < 3; i++) {
            float phase = std::fmod(t * 2.0f + i * 0.33f, 1.0f);
            if (phase < 0.3f) {
                int spx = cx + static_cast<int>((phase - 0.15f) * TS * 3.0f * (i % 2 ? 1 : -1));
                int spy = py + TS - static_cast<int>(phase * TS * 0.5f);
                int sa = static_cast<int>((0.3f - phase) * 600);
                SDL_SetRenderDrawColor(renderer_, 220, 180, 120, static_cast<Uint8>(std::min(sa, 200)));
                SDL_Rect spark = {spx - ds, spy - ds, ds * 2, ds * 2};
                SDL_RenderFillRect(renderer_, &spark);
            }
        }
        break;
    }
    case GodId::YASHKHET: {
        for (int i = 0; i < 5; i++) {
            float phase = std::fmod(t * 0.6f + i * 0.2f, 1.0f);
            int dx2 = cx + static_cast<int>(std::sin(i * 2.3f) * TS * 0.4f);
            int dy2 = py + static_cast<int>(phase * TS * 1.4f);
            int da = static_cast<int>((1.0f - phase) * 180);
            SDL_SetRenderDrawColor(renderer_, 200, 40, 40, static_cast<Uint8>(da));
            SDL_Rect drip = {dx2 - ds / 2, dy2, ds, ds * 3};
            SDL_RenderFillRect(renderer_, &drip);
        }
        break;
    }
    case GodId::KHAEL: {
        for (int i = 0; i < 5; i++) {
            float phase = std::fmod(t * 0.4f + i * 0.2f, 1.0f);
            float angle = i * 1.257f + t * 0.3f;
            int lx = cx + static_cast<int>(std::cos(angle) * phase * TS * 0.8f);
            int ly = cy + static_cast<int>(std::sin(angle) * phase * TS * 0.5f);
            int la = static_cast<int>((1.0f - phase) * 150);
            SDL_SetRenderDrawColor(renderer_, 80, 180, 60, static_cast<Uint8>(la));
            SDL_Rect leaf = {lx - ds, ly - ds / 2, ds * 2, ds};
            SDL_RenderFillRect(renderer_, &leaf);
        }
        break;
    }
    case GodId::SOLETH: {
        int fl = static_cast<int>(45 + 25 * std::sin(t * 6.0f));
        SDL_SetRenderDrawColor(renderer_, 255, 220, 100, static_cast<Uint8>(fl));
        SDL_Rect halo = {cx - TS / 2, py - TS / 5, TS, TS / 5};
        SDL_RenderFillRect(renderer_, &halo);
        for (int i = 0; i < 4; i++) {
            float phase = std::fmod(t * 1.0f + i * 0.25f, 1.0f);
            int ex = cx + static_cast<int>(std::sin(t * 0.7f + i * 2.1f) * TS * 0.4f);
            int ey = py + TS - static_cast<int>(phase * TS * 1.3f);
            int ea = static_cast<int>((1.0f - phase) * 170);
            SDL_SetRenderDrawColor(renderer_, 255, 180, 60, static_cast<Uint8>(ea));
            SDL_Rect ember = {ex - ds, ey - ds, ds * 2, ds * 2};
            SDL_RenderFillRect(renderer_, &ember);
        }
        break;
    }
    case GodId::IXUUL: {
        if ((ticks / 50) % 4 == 0) {
            int off = (ticks / 25) % 9 - 4;
            SDL_SetRenderDrawColor(renderer_, r, g, b, 90);
            SDL_Rect tear = {px + off * 3, py + TS / 3, TS + 4, ds * 2};
            SDL_RenderFillRect(renderer_, &tear);
            SDL_Rect tear2 = {px - off * 4, py + TS * 2 / 3, TS + 6, ds + 1};
            SDL_RenderFillRect(renderer_, &tear2);
            SDL_Rect tear3 = {px + off * 2, py + TS / 6, TS / 2, ds};
            SDL_RenderFillRect(renderer_, &tear3);
        }
        break;
    }
    case GodId::ZHAVEK: {
        for (int i = 1; i <= 3; i++) {
            int off = i * TS / 8;
            int alpha = 45 - i * 12;
            SDL_SetRenderDrawColor(renderer_, 15, 15, 30, static_cast<Uint8>(alpha));
            SDL_Rect trail = {px + off, py + off, TS - off / 2, TS - off / 2};
            SDL_RenderFillRect(renderer_, &trail);
        }
        break;
    }
    case GodId::THALARA: {
        for (int i = 0; i < 3; i++) {
            float rp = std::fmod(t * 1.2f + i * 0.33f, 1.0f);
            int rs = static_cast<int>(rp * TS * 1.2f);
            int ra = static_cast<int>((1.0f - rp) * 60);
            SDL_SetRenderDrawColor(renderer_, 80, 180, 200, static_cast<Uint8>(ra));
            SDL_Rect ring = {cx - rs / 2, py + TS - rs / 3, rs, rs * 2 / 3};
            SDL_RenderDrawRect(renderer_, &ring);
            SDL_Rect ring2 = {cx - rs / 2 + 1, py + TS - rs / 3 + 1, rs - 2, rs * 2 / 3 - 2};
            SDL_RenderDrawRect(renderer_, &ring2);
        }
        break;
    }
    case GodId::OSSREN: {
        for (int i = 0; i < 3; i++) {
            float phase = std::fmod(t * 1.5f + i * 0.33f, 1.0f);
            if (phase < 0.4f) {
                int spx = cx + static_cast<int>(std::sin(i * 3.1f) * TS * 0.4f);
                int spy = py + TS - static_cast<int>(phase * TS * 0.7f);
                int sa = static_cast<int>((0.4f - phase) * 500);
                SDL_SetRenderDrawColor(renderer_, 255, 180, 60, static_cast<Uint8>(std::min(sa, 200)));
                SDL_Rect spark = {spx - ds, spy - ds, ds * 2, ds * 2};
                SDL_RenderFillRect(renderer_, &spark);
            }
        }
        break;
    }
    case GodId::LETHIS: {
        for (int ei = 0; ei < 2; ei++) {
            float ep = std::fmod(t * 0.6f + ei * 1.5f, 3.0f);
            if (ep < 0.6f) {
                int ea = static_cast<int>((0.6f - std::abs(ep - 0.3f)) * 500);
                SDL_SetRenderDrawColor(renderer_, 200, 160, 240, static_cast<Uint8>(std::min(ea, 220)));
                float eangle = t * 0.3f + ei * 3.14f;
                int ex2 = cx + static_cast<int>(std::cos(eangle) * TS * 0.5f);
                int ey2 = cy - TS / 6 + static_cast<int>(std::sin(eangle * 0.7f) * TS * 0.1f);
                SDL_Rect eye = {ex2 - ds * 2, ey2 - ds, ds * 4, ds * 2};
                SDL_RenderFillRect(renderer_, &eye);
            }
        }
        break;
    }
    case GodId::GATHRUUN: {
        for (int i = 0; i < 4; i++) {
            float angle = t * 1.2f + i * 1.5708f;
            int ox = cx + static_cast<int>(std::cos(angle) * TS * 0.55f);
            int oy = py + TS - ds * 2 + static_cast<int>(std::sin(angle) * TS * 0.08f);
            SDL_SetRenderDrawColor(renderer_, 160, 130, 90, 190);
            SDL_Rect peb = {ox - ds * 2, oy - ds, ds * 3, ds * 2};
            SDL_RenderFillRect(renderer_, &peb);
        }
        break;
    }
    case GodId::SYTHARA: {
        for (int i = 0; i < 6; i++) {
            float phase = std::fmod(t * 0.5f + i * 0.167f, 1.0f);
            float angle = i * 1.047f + t * 0.2f;
            int spx = cx + static_cast<int>(std::cos(angle) * phase * TS * 0.7f);
            int spy = cy + static_cast<int>(std::sin(angle) * phase * TS * 0.5f);
            int sa = static_cast<int>((1.0f - phase) * 150);
            SDL_SetRenderDrawColor(renderer_, 100, 160, 40, static_cast<Uint8>(sa));
            SDL_Rect spore = {spx - ds, spy - ds, ds * 2, ds * 2};
            SDL_RenderFillRect(renderer_, &spore);
        }
        break;
    }
    default: break;
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

    // God visual effects on player (rendered every frame)
    render_god_visuals(render_cam, HUD_HEIGHT);

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

    // Death overlay
    if (state_ == GameState::DEAD && font_) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_Rect overlay = {0, 0, width_, height_};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
        SDL_RenderFillRect(renderer_, &overlay);

        SDL_Color red = {200, 50, 50, 255};
        TTF_Font* death_font = font_title_ ? font_title_ : font_;
        SDL_Surface* surf = TTF_RenderText_Blended(death_font, "You have died.", red);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
            SDL_Rect dst = {width_ / 2 - surf->w / 2, height_ / 3, surf->w, surf->h};
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }

        // God-flavored death text
        const char* death_line = nullptr;
        GodId dgod = GodId::NONE;
        if (world_.has<GodAlignment>(player_))
            dgod = world_.get<GodAlignment>(player_).god;
        switch (dgod) {
            case GodId::VETHRIK:  death_line = "Vethrik adds your bones to the collection."; break;
            case GodId::THESSARKA: death_line = "Thessarka records your death. She forgets nothing."; break;
            case GodId::MORRETH:  death_line = "Morreth found you wanting."; break;
            case GodId::YASHKHET: death_line = "Yashkhet accepts the offering."; break;
            case GodId::KHAEL:    death_line = "Your body feeds the roots."; break;
            case GodId::SOLETH:   death_line = "The flame goes out."; break;
            case GodId::IXUUL:    death_line = "Ixuul is already making something new from the pieces."; break;
            case GodId::ZHAVEK:   death_line = "You vanish. No one notices."; break;
            case GodId::THALARA:  death_line = "The sea takes you back."; break;
            case GodId::OSSREN:   death_line = "You were not built to last."; break;
            case GodId::LETHIS:   death_line = "You fall asleep. You do not wake up."; break;
            case GodId::GATHRUUN: death_line = "The stone closes over you."; break;
            case GodId::SYTHARA:  death_line = "Rot takes you before you hit the ground."; break;
            default:              death_line = "You die alone."; break;
        }
        SDL_Color dim = {160, 120, 120, 255};
        SDL_Surface* dsurf = TTF_RenderText_Blended(font_, death_line, dim);
        if (dsurf) {
            SDL_Texture* dtex = SDL_CreateTextureFromSurface(renderer_, dsurf);
            SDL_Rect ddst = {width_ / 2 - dsurf->w / 2, height_ / 3 + 50, dsurf->w, dsurf->h};
            SDL_RenderCopy(renderer_, dtex, nullptr, &ddst);
            SDL_DestroyTexture(dtex);
            SDL_FreeSurface(dsurf);
        }

        // Newly unlocked classes
        if (!newly_unlocked_.empty()) {
            int uy = height_ / 2 + 20;
            SDL_Color gold = {255, 220, 100, 255};
            ui::draw_text_centered(renderer_, font_, "Class unlocked:", gold, width_ / 2, uy);
            uy += TTF_FontLineSkip(font_) + 4;
            for (auto& name : newly_unlocked_) {
                ui::draw_text_centered(renderer_, font_title_ ? font_title_ : font_,
                                        name.c_str(), gold, width_ / 2, uy);
                uy += (font_title_ ? TTF_FontLineSkip(font_title_) : TTF_FontLineSkip(font_)) + 4;
            }
        }

        ui::draw_text_centered(renderer_, font_, "Press any key.",
                                {100, 95, 90, 255}, width_ / 2, height_ - 40);
    }

    if (state_ == GameState::VICTORY) {
        render_victory();
    }

    // Particles
    if (!particles_.empty()) {
        particles_.render(renderer_, camera_.x, camera_.y, camera_.tile_size, HUD_HEIGHT);
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
        render();
        SDL_Delay(16); // ~60fps cap
    }
}
