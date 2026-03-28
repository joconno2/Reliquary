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
#include "ui/main_menu.h"
#include "ui/settings_screen.h"
#include "ui/pause_menu.h"
#include "ui/levelup_screen.h"
#include "ui/shop_screen.h"
#include <vector>

enum class GameState {
    MAIN_MENU,
    SETTINGS,
    CREATING,
    PLAYING,
    DEAD,
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
    Camera camera_;
    MessageLog log_;
    GameState state_ = GameState::MAIN_MENU;

    // Level data
    std::vector<Room> rooms_;
    int dungeon_level_ = -1; // incremented to 0 for village, 1+ for dungeons

    // Player
    Entity player_ = NULL_ENTITY;
    int game_turn_ = 0;
    int gold_ = 0;
    bool player_acted_ = false;

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
    ShopScreen shop_screen_;

    // Track where Settings should return to
    GameState return_from_settings_ = GameState::MAIN_MENU;

    // Window config — dynamic resolution
    int width_ = 1280;
    int height_ = 800;
    bool fullscreen_ = false;
    float ui_scale_ = 1.0f;
    static constexpr int LOG_HEIGHT = 180;
    static constexpr int HUD_HEIGHT = 32;

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
    void do_save();
    void do_load();
};
