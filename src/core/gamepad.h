#pragma once
#include <SDL2/SDL.h>
#include "core/keybinds.h"

// Translates SDL GameController events into Actions.
// Steam Deck layout:
//   D-pad/stick: movement    A: interact/confirm    B: back/cancel
//   X: examine               Y: inventory
//   LB: spellbook            RB: fire ranged
//   LT: pray                 RT: rest
//   Start: pause             Select: world map
//   L3: sneak toggle         R3: wait
//   Hold LB + D-pad: character(U), quests(D), tree(L), bestiary(R)

class Gamepad {
public:
    Gamepad();
    ~Gamepad();

    // Call once at startup (after SDL_Init with GAMECONTROLLER flag)
    void init();

    // Process SDL event. Returns Action if a gameplay action was triggered,
    // or Action::COUNT if the event was not a gamepad event.
    Action translate(const SDL_Event& event);

    // For UI screens: simplified queries on last event
    bool is_confirm() const { return last_confirm_; }
    bool is_cancel() const { return last_cancel_; }
    bool is_up() const { return last_up_; }
    bool is_down() const { return last_down_; }
    bool is_left() const { return last_left_; }
    bool is_right() const { return last_right_; }

    bool connected() const { return controller_ != nullptr; }

    // Stick deadzone (0.0 - 1.0)
    static constexpr float DEADZONE = 0.3f;

private:
    SDL_GameController* controller_ = nullptr;
    bool lb_held_ = false;

    // D-pad held state (for diagonal combinations)
    bool dpad_up_ = false, dpad_down_ = false;
    bool dpad_left_ = false, dpad_right_ = false;

    // Trigger edge detection (fire once on press, not continuously)
    bool lt_held_ = false, rt_held_ = false;

    // Stick-to-dpad state (prevents repeated movement)
    int stick_dx_ = 0, stick_dy_ = 0;
    Uint32 stick_repeat_time_ = 0;
    static constexpr Uint32 STICK_REPEAT_DELAY = 200; // ms before first repeat
    static constexpr Uint32 STICK_REPEAT_RATE = 100;  // ms between repeats

    // Flags set per translate() call for UI screen queries
    bool last_confirm_ = false;
    bool last_cancel_ = false;
    bool last_up_ = false;
    bool last_down_ = false;
    bool last_left_ = false;
    bool last_right_ = false;

    void clear_flags();
    Action dpad_to_movement(int dx, int dy) const;
};
