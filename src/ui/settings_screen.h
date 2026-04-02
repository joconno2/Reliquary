#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

class Audio;

class SettingsScreen {
public:
    SettingsScreen() = default;

    void set_audio(Audio* audio) { audio_ = audio; }

    bool handle_input(SDL_Event& event, SDL_Window* window);

    void render(SDL_Renderer* renderer, TTF_Font* font, int w, int h) const;

    bool should_close() const { return should_close_; }
    void reset() { should_close_ = false; keybinds_open_ = false; }

    // UI scale
    float get_ui_scale() const { return SCALES[scale_index_]; }
    bool scale_changed() const { return scale_changed_; }
    void clear_scale_changed() { scale_changed_ = false; }

private:
    int selected_ = 0;
    int resolution_index_ = 0;
    int sfx_volume_ = 50;
    int music_volume_ = 35;
    Audio* audio_ = nullptr;
    int scale_index_ = 0;
    bool should_close_ = false;
    bool scale_changed_ = false;
    bool keybinds_open_ = false; // sub-screen showing keybind reference

    static constexpr int OPTION_COUNT = 6; // Resolution, SFX Vol, Music Vol, UI Scale, Keybinds, Back

    struct Resolution {
        int w, h;
        const char* label;
    };
    static constexpr int RES_COUNT = 5;
    static constexpr Resolution RESOLUTIONS[RES_COUNT] = {
        {1280, 800,  "1280x800"},
        {1440, 900,  "1440x900"},
        {1600, 900,  "1600x900"},
        {1920, 1080, "1920x1080"},
        {0,    0,    "Fullscreen"},
    };

    static constexpr int SCALE_COUNT = 4;
    static constexpr float SCALES[SCALE_COUNT] = {1.0f, 1.25f, 1.5f, 2.0f};
    static constexpr const char* SCALE_LABELS[SCALE_COUNT] = {
        "1.0x", "1.25x", "1.5x", "2.0x"
    };

    void render_keybinds(SDL_Renderer* renderer, TTF_Font* font, int w, int h) const;
};
