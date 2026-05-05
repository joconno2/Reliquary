#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <string>
#include "core/spritesheet.h"
#include "components/god.h"
#include "components/class_def.h"
#include "components/background.h"
#include "components/traits.h"
#include "ui/background_select.h"
#include "ui/trait_select.h"
#include "core/rng.h"

enum class CreationPhase {
    CLASS_SELECT,
    NAME_ENTRY,       // kept for legacy (unused in new flow)
    GOD_SELECT,       // kept for legacy (unused in new flow)
    BACKGROUND_SELECT,// kept for legacy (unused in new flow)
    TRAIT_SELECT,     // kept for legacy (unused in new flow)
    HARDCORE_SELECT,  // kept for legacy (unused in new flow)
    BUILD_SCREEN,     // NEW: combined god + traits + background + name
    DONE
};

struct CharacterBuild {
    ClassId class_id = ClassId::FIGHTER;
    std::string name = "Aldric";
    GodId god = GodId::VETHRIK;
    BackgroundId background = BackgroundId::FARMER;
    std::vector<TraitId> traits;
    bool hardcore = false;
};

class CreationScreen {
public:
    CreationScreen() = default;

    void reset();
    bool is_done() const { return phase_ == CreationPhase::DONE; }
    bool is_cancelled() const { return cancelled_; }
    CharacterBuild get_build() const { return build_; }
    void set_unlocked(const bool* unlocks, int count); // called before rendering
    void set_unlock_progress(int class_idx, const char* progress); // "32/50 kills"
    void randomize(RNG& rng); // fill all fields randomly, skip to DONE

    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                const SpriteManager& sprites, int screen_w, int screen_h) const;

private:
    CreationPhase phase_ = CreationPhase::CLASS_SELECT;
    int selected_ = 0;
    bool cancelled_ = false;
    CharacterBuild build_;
    bool cursor_blink_ = false;
    int blink_timer_ = 0;

    BackgroundSelectScreen bg_screen_;
    TraitSelectScreen trait_screen_;
    bool class_unlocked_[CLASS_COUNT] = {}; // true = available
    // Cached grid layout for mouse hit-testing
    mutable int grid_x_ = 0, grid_y_ = 0;
    mutable int grid_cell_w_ = 0, grid_cell_h_ = 0;
    mutable int grid_cols_ = 6;
    // God select list rects for mouse
    mutable std::vector<SDL_Rect> god_rects_;
    std::string unlock_progress_[CLASS_COUNT];

    // BUILD_SCREEN state (combined god + traits + background + name)
    int build_column_ = 0;  // 0=god, 1=traits, 2=name/bg
    int build_god_cursor_ = 0;
    int build_trait_cursor_ = 0;
    int build_bg_cursor_ = 0;
    std::vector<TraitId> build_traits_selected_;

    void randomize_name();

    void render_class_select(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                              const SpriteManager& sprites, int w, int h) const;
    void render_name_entry(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                            const SpriteManager& sprites, int w, int h) const;
    void render_god_select(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                            const SpriteManager& sprites, int w, int h) const;
    void render_build_screen(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                              int w, int h) const;
    void render_character_preview(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                                   const SpriteManager& sprites, int w, int h) const;
};
