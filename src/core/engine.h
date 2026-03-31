#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/spritesheet.h"
#include "core/rng.h"
#include "systems/render.h"
#include "ui/message_log.h"
#include "generation/dungeon.h"
#include "generation/village.h"
#include "generation/mapfile.h"
#include "ui/inventory_screen.h"
#include "ui/creation_screen.h"
#include "ui/spell_screen.h"
#include "ui/character_sheet.h"
#include "ui/quest_log.h"
#include "ui/quest_offer.h"
#include "components/quest.h"
#include "save/save.h"
#include "save/meta.h"
#include "ui/main_menu.h"
#include "ui/settings_screen.h"
#include "ui/pause_menu.h"
#include "ui/levelup_screen.h"
#include "ui/shop_screen.h"
#include "ui/help_screen.h"
#include "ui/world_map.h"
#include "systems/particles.h"
#include "components/tenet.h"
#include "components/traits.h"
#include "components/stats.h"
#include "components/ai.h"
#include "components/item.h"
#include "components/status_effect.h"
#include "components/god.h"
#include "components/container.h"
#include "core/audio.h"
#include <vector>
#include <string>
#include <map>

struct BestiaryEntry {
    std::string name;
    int hp = 0, damage = 0, armor = 0, speed = 0;
    int kills = 0;
};

struct DungeonEntry {
    std::string name;
    int x = 0, y = 0;
    std::string zone;
    std::string quest; // empty = generic dungeon
    int zone_difficulty = 0; // 0-8, based on distance from Thornwall
    int max_depth = 3;       // how many floors this dungeon has
};

enum class GameState {
    MAIN_MENU,
    SETTINGS,
    CREATING,
    PLAYING,
    DEAD,
    VICTORY,
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
    TTF_Font* font_ = nullptr;             // Press Start 2P — body text, UI, messages
    TTF_Font* font_title_ = nullptr;     // Jacquard 12 — titles, god names, decorative
    TTF_Font* font_title_large_ = nullptr; // Jacquard 12 — main menu title, huge

    // Core systems
    World world_;
    TileMap map_;
    SpriteManager sprites_;
    RNG rng_;
    Audio audio_;
    ParticleSystem particles_;
    Camera camera_;
    MessageLog log_;
    GameState state_ = GameState::MAIN_MENU;

    // Level data
    std::vector<Room> rooms_;
    int dungeon_level_ = -1; // incremented to 0 for village, 1+ for dungeons
    int overworld_return_x_ = 0; // position to return to on overworld when ascending
    int overworld_return_y_ = 0;
    std::vector<DungeonEntry> dungeon_registry_;
    int current_dungeon_idx_ = -1; // which dungeon we're in (-1 = none/overworld)

    // Overworld cache — loaded once, reused on return from dungeons
    bool overworld_loaded_ = false;
    MapFileResult overworld_cache_;

    // Floor persistence — cache floor state when changing levels
    struct CachedEntity {
        int x, y;
        int sheet, sprite_x, sprite_y;
        uint8_t tint_r, tint_g, tint_b, tint_a;
        int z_order;
        bool flip_h;
        // Monster fields (valid if has_stats)
        bool has_stats = false;
        Stats stats;
        bool has_ai = false;
        AI ai;
        int energy_current = 0, energy_speed = 100;
        bool has_status = false;
        StatusEffects status_fx;
        bool has_god = false;
        GodAlignment god_align;
        // Item fields (valid if has_item)
        bool has_item = false;
        Item item;
        bool has_container = false;
        Container container;
    };
    struct FloorState {
        TileMap map;
        std::vector<Room> rooms;
        std::vector<CachedEntity> entities;
        int player_x = 0, player_y = 0; // where player was on this floor
    };
    std::map<int, FloorState> floor_cache_;
    void cache_current_floor();
    bool restore_floor(int level);

    // Player
    Entity player_ = NULL_ENTITY;
    Entity pet_entity_ = NULL_ENTITY; // visual pet that follows player
    int game_turn_ = 0;
    int gold_ = 0;
    bool player_acted_ = false;
    MetaSave meta_; // persistent cross-run progression
    int run_kills_ = 0; // kills this run (for tracking)
    std::vector<TraitId> build_traits_; // player's chosen traits (for runtime checks)
    bool hardcore_ = false; // permadeath mode
    PlayerActions turn_actions_; // action flags for tenet checking
    bool rested_this_floor_ = false; // Lethis tenet tracking
    Uint32 end_screen_time_ = 0; // SDL_GetTicks when death/victory screen appeared
    bool pet_naming_ = false; // currently naming a pet
    std::string pet_name_buf_; // pet name being typed
    Entity pet_naming_item_ = NULL_ENTITY; // the pet item being named
    std::vector<std::string> newly_unlocked_; // classes unlocked this run (for display)

    // UI
    InventoryScreen inventory_screen_;
    CreationScreen creation_screen_;
    SpellScreen spell_screen_;
    CharacterSheet char_sheet_;
    QuestLog quest_log_;
    QuestOffer quest_offer_;
    QuestJournal journal_;
    Entity pending_quest_npc_ = NULL_ENTITY; // NPC that offered the current quest
    MainMenu main_menu_;
    SettingsScreen settings_;
    PauseMenu pause_menu_;
    LevelUpScreen levelup_screen_;
    bool pending_levelup_ = false;
    bool prayer_mode_ = false;
    bool look_mode_ = false;
    int look_x_ = 0, look_y_ = 0;
    std::map<std::string, BestiaryEntry> bestiary_;
    ShopScreen shop_screen_;
    HelpScreen help_screen_;
    WorldMap world_map_;

    // Track where Settings should return to
    GameState return_from_settings_ = GameState::MAIN_MENU;

    // Window config — dynamic resolution
    int width_ = 1280;
    int height_ = 800;
    bool fullscreen_ = false;
    float ui_scale_ = 1.0f;
    int LOG_HEIGHT = 180;
    int HUD_HEIGHT = 32;

    // Methods
    void handle_input();
    void reload_fonts();
    void try_move_player(int dx, int dy);
    void open_door(int x, int y);
    void process_turn();
    void try_pickup();
    void handle_inventory_action(InvAction action);
    void render();
    void render_hud();
    void generate_level();
    void clear_entities_except_player();
    void try_rest();
    void process_npc_wander();
    void try_spawn_overworld_enemy();
    void execute_prayer(int prayer_idx);
    void adjust_favor(int amount);
    void check_tenets();
    void render_god_visuals(const Camera& cam, int y_offset);
    void fire_ranged();
    void describe_tile(int x, int y);
    void process_status_effects();
    void sepulchre_ambient();
    void spawn_pet_visual(int pet_id);
    void despawn_pet_visual();
    void populate_overworld();
    bool is_class_unlocked(ClassId id) const;
    void update_meta_on_end();
    void update_music_for_location();
    void render_victory();
    void do_save();
    void do_load();
};
