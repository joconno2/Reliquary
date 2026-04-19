#include "core/gamepad.h"
#include <cmath>
#include <cstdio>

Gamepad::Gamepad() {}

Gamepad::~Gamepad() {
    if (controller_) {
        SDL_GameControllerClose(controller_);
        controller_ = nullptr;
    }
}

void Gamepad::init() {
    // Open first available controller
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller_ = SDL_GameControllerOpen(i);
            if (controller_) {
                fprintf(stderr, "[gamepad] Connected: %s\n",
                        SDL_GameControllerName(controller_));
                break;
            }
        }
    }
}

void Gamepad::clear_flags() {
    last_confirm_ = false;
    last_cancel_ = false;
    last_up_ = false;
    last_down_ = false;
    last_left_ = false;
    last_right_ = false;
}

Action Gamepad::dpad_to_movement(int dx, int dy) const {
    if (dx == 0 && dy == -1) return Action::MOVE_UP;
    if (dx == 0 && dy == 1)  return Action::MOVE_DOWN;
    if (dx == -1 && dy == 0) return Action::MOVE_LEFT;
    if (dx == 1 && dy == 0)  return Action::MOVE_RIGHT;
    if (dx == -1 && dy == -1) return Action::MOVE_NW;
    if (dx == 1 && dy == -1)  return Action::MOVE_NE;
    if (dx == -1 && dy == 1)  return Action::MOVE_SW;
    if (dx == 1 && dy == 1)   return Action::MOVE_SE;
    return Action::COUNT;
}

Action Gamepad::translate(const SDL_Event& event) {
    clear_flags();

    // Handle controller connect/disconnect
    if (event.type == SDL_CONTROLLERDEVICEADDED) {
        if (!controller_) {
            controller_ = SDL_GameControllerOpen(event.cdevice.which);
            if (controller_)
                fprintf(stderr, "[gamepad] Connected: %s\n",
                        SDL_GameControllerName(controller_));
        }
        return Action::COUNT;
    }
    if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        if (controller_ && event.cdevice.which ==
            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller_))) {
            SDL_GameControllerClose(controller_);
            controller_ = nullptr;
            fprintf(stderr, "[gamepad] Disconnected\n");
        }
        return Action::COUNT;
    }

    if (!controller_) return Action::COUNT;

    // Track LB held state
    if (event.type == SDL_CONTROLLERBUTTONDOWN &&
        event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        lb_held_ = true;
    if (event.type == SDL_CONTROLLERBUTTONUP &&
        event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        lb_held_ = false;

    // Button presses
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        auto btn = event.cbutton.button;

        // D-pad with LB modifier for screen shortcuts
        if (lb_held_) {
            switch (btn) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    last_up_ = true;
                    return Action::CHARACTER;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    last_down_ = true;
                    return Action::QUEST_LOG;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    last_left_ = true;
                    return Action::PASSIVE_TREE;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    last_right_ = true;
                    return Action::BESTIARY;
                default: break;
            }
        }

        // D-pad movement
        switch (btn) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                last_up_ = true;
                return Action::MOVE_UP;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                last_down_ = true;
                return Action::MOVE_DOWN;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                last_left_ = true;
                return Action::MOVE_LEFT;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                last_right_ = true;
                return Action::MOVE_RIGHT;
            default: break;
        }

        // Face buttons
        switch (btn) {
            case SDL_CONTROLLER_BUTTON_A:
                last_confirm_ = true;
                return Action::INTERACT;
            case SDL_CONTROLLER_BUTTON_B:
                last_cancel_ = true;
                return Action::COUNT; // handled by screens as cancel
            case SDL_CONTROLLER_BUTTON_X:
                return Action::EXAMINE;
            case SDL_CONTROLLER_BUTTON_Y:
                return Action::INVENTORY;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                return Action::SPELLBOOK;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                return Action::FIRE_RANGED;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK:
                return Action::SNEAK_TOGGLE;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
                return Action::WAIT;
            case SDL_CONTROLLER_BUTTON_START:
                return Action::COUNT; // engine handles as pause
            case SDL_CONTROLLER_BUTTON_BACK:
                return Action::WORLD_MAP;
            default: break;
        }
    }

    // Trigger axes (LT = pray, RT = rest)
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        // Triggers go 0-32767
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            if (event.caxis.value > 16000) return Action::PRAY;
        }
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            if (event.caxis.value > 16000) return Action::REST;
        }

        // Left stick to movement
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            float lx = SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            float ly = SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;

            int new_dx = 0, new_dy = 0;
            if (lx < -DEADZONE) new_dx = -1;
            else if (lx > DEADZONE) new_dx = 1;
            if (ly < -DEADZONE) new_dy = -1;
            else if (ly > DEADZONE) new_dy = 1;

            // Only fire on state change or repeat timer
            if (new_dx != stick_dx_ || new_dy != stick_dy_) {
                stick_dx_ = new_dx;
                stick_dy_ = new_dy;
                stick_repeat_time_ = SDL_GetTicks() + STICK_REPEAT_DELAY;
                if (new_dx != 0 || new_dy != 0) {
                    if (new_dy == -1) last_up_ = true;
                    if (new_dy == 1) last_down_ = true;
                    if (new_dx == -1) last_left_ = true;
                    if (new_dx == 1) last_right_ = true;
                    return dpad_to_movement(new_dx, new_dy);
                }
            } else if ((new_dx != 0 || new_dy != 0) &&
                       SDL_GetTicks() >= stick_repeat_time_) {
                stick_repeat_time_ = SDL_GetTicks() + STICK_REPEAT_RATE;
                if (new_dy == -1) last_up_ = true;
                if (new_dy == 1) last_down_ = true;
                if (new_dx == -1) last_left_ = true;
                if (new_dx == 1) last_right_ = true;
                return dpad_to_movement(new_dx, new_dy);
            }
        }
    }

    return Action::COUNT;
}
