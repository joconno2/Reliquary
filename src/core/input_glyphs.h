#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include "core/keybinds.h"

// Tracks whether the player is using keyboard or gamepad,
// and provides button display names/glyphs for UI hints.
//
// Atlas layout (input_icons.png, 16x16 cells, 16 cols x 6 rows):
//   Row 0: Xbox      (A B X Y LB RB LT RT Start Select L3 R3 DU DD DL DR)
//   Row 1: PS        (X O [] /\ L1 R1 L2 R2 Opt Share L3 R3 DU DD DL DR)
//   Row 2: Switch    (B A Y X L R ZL ZR + - LS RS DU DD DL DR)
//   Row 3: Keyboard  (A B C D E F G H I J K L M N O P)
//   Row 4: Keyboard  (Q R S T U V W X Y Z 1 2 3 4 Tab Enter)
//   Row 5: Keyboard  (Esc Space Up Down Left Right . / F5 F6 F11 F12 Shift Ctrl)

enum class InputMode {
    KEYBOARD,
    GAMEPAD_XBOX,
    GAMEPAD_PLAYSTATION,
    GAMEPAD_SWITCH,
    GAMEPAD_GENERIC
};

// Button IDs matching atlas column positions
enum class BtnIcon {
    FACE_SOUTH = 0,  // A / Cross / B
    FACE_EAST,       // B / Circle / A
    FACE_WEST,       // X / Square / Y
    FACE_NORTH,      // Y / Triangle / X
    LB, RB, LT, RT,
    START, SELECT,
    L3, R3,
    DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
    COUNT
};

class InputGlyphs {
public:
    InputGlyphs() : mode_(InputMode::KEYBOARD) { instance_ = this; }
    ~InputGlyphs();

    // Global accessor (there's only one)
    static InputGlyphs* get() { return instance_; }

    // Load atlas texture (call after renderer is created)
    void load(SDL_Renderer* renderer);

    // Call on every SDL event to track input mode switches
    void update(const SDL_Event& event);

    InputMode mode() const { return mode_; }
    bool using_gamepad() const { return mode_ != InputMode::KEYBOARD; }

    // Get display string for an action in the current input mode
    std::string label(Action action) const;

    // Get display string for common UI actions
    std::string confirm() const;
    std::string cancel() const;

    // Draw a button icon sprite at (x, y) with given size.
    // Returns width consumed (icon_size).
    int draw_icon(SDL_Renderer* renderer, BtnIcon icon, int x, int y, int size = 16) const;

    // Draw the correct icon for an Action. Uses gamepad icon or keyboard icon
    // depending on current mode. Returns width consumed.
    int draw_action(SDL_Renderer* renderer, Action action, int x, int y, int size = 16) const;

    // Build a hint string like "(A) select  (B) back" or "[Enter] select  [Esc] back"
    // Caller can use this for simple text-only hints
    std::string hint(Action a1, const char* desc1,
                     Action a2 = Action::COUNT, const char* desc2 = nullptr,
                     Action a3 = Action::COUNT, const char* desc3 = nullptr) const;

private:
    InputMode mode_;
    SDL_Texture* atlas_ = nullptr;

    static InputMode detect_type(SDL_GameController* controller);
    std::string gamepad_label(Action action) const;

    // Map an Action to a BtnIcon (gamepad) or keyboard atlas position (row, col)
    BtnIcon action_to_btn(Action action) const;
    void action_to_kb_cell(Action action, int& row, int& col) const;

    // Atlas row for current gamepad type
    int gamepad_row() const;

    static InputGlyphs* instance_;
};
