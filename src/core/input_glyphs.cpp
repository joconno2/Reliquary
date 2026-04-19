#include "core/input_glyphs.h"
#include <SDL2/SDL_image.h>
#include <cstring>
#include <cstdio>

InputGlyphs* InputGlyphs::instance_ = nullptr;

InputGlyphs::~InputGlyphs() {
    if (atlas_) SDL_DestroyTexture(atlas_);
}

void InputGlyphs::load(SDL_Renderer* renderer) {
    SDL_Surface* surf = IMG_Load("assets/32rogues/input_icons.png");
    if (!surf) {
        fprintf(stderr, "[input_glyphs] Failed to load input_icons.png: %s\n", IMG_GetError());
        return;
    }
    atlas_ = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (atlas_) SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
}

void InputGlyphs::update(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        mode_ = InputMode::KEYBOARD;
        return;
    }
    if (event.type == SDL_CONTROLLERBUTTONDOWN ||
        event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.type == SDL_CONTROLLERAXISMOTION &&
            std::abs(event.caxis.value) < 8000)
            return;
        if (mode_ == InputMode::KEYBOARD) {
            SDL_GameController* gc = SDL_GameControllerFromInstanceID(
                event.type == SDL_CONTROLLERBUTTONDOWN
                    ? event.cbutton.which : event.caxis.which);
            mode_ = detect_type(gc);
        }
    }
    if (event.type == SDL_CONTROLLERDEVICEADDED) {
        SDL_GameController* gc = SDL_GameControllerOpen(event.cdevice.which);
        if (gc) mode_ = detect_type(gc);
    }
}

InputMode InputGlyphs::detect_type(SDL_GameController* controller) {
    if (!controller) return InputMode::GAMEPAD_GENERIC;
    const char* name = SDL_GameControllerName(controller);
    if (!name) return InputMode::GAMEPAD_GENERIC;
    if (strstr(name, "PS3") || strstr(name, "PS4") || strstr(name, "PS5") ||
        strstr(name, "DualShock") || strstr(name, "DualSense") ||
        strstr(name, "Sony") || strstr(name, "PLAYSTATION"))
        return InputMode::GAMEPAD_PLAYSTATION;
    if (strstr(name, "Nintendo") || strstr(name, "Switch") ||
        strstr(name, "Joy-Con") || strstr(name, "Pro Controller"))
        return InputMode::GAMEPAD_SWITCH;
    return InputMode::GAMEPAD_XBOX;
}

int InputGlyphs::gamepad_row() const {
    switch (mode_) {
        case InputMode::GAMEPAD_PLAYSTATION: return 1;
        case InputMode::GAMEPAD_SWITCH: return 2;
        default: return 0; // xbox / generic
    }
}

BtnIcon InputGlyphs::action_to_btn(Action action) const {
    switch (action) {
        case Action::INTERACT:
        case Action::PICKUP:
        case Action::STAIRS_DOWN:
        case Action::STAIRS_UP:
        case Action::STAIRS_ENTER:
            return BtnIcon::FACE_SOUTH;
        case Action::EXAMINE:
            return BtnIcon::FACE_WEST;
        case Action::INVENTORY:
            return BtnIcon::FACE_NORTH;
        case Action::SPELLBOOK:
        case Action::QUICK_CAST:
            return BtnIcon::LB;
        case Action::FIRE_RANGED:
            return BtnIcon::RB;
        case Action::PRAY:
            return BtnIcon::LT;
        case Action::REST:
            return BtnIcon::RT;
        case Action::SNEAK_TOGGLE:
            return BtnIcon::L3;
        case Action::WAIT:
            return BtnIcon::R3;
        case Action::WORLD_MAP:
            return BtnIcon::SELECT;
        case Action::HELP:
            return BtnIcon::START;
        case Action::MOVE_UP:
            return BtnIcon::DPAD_UP;
        case Action::MOVE_DOWN:
            return BtnIcon::DPAD_DOWN;
        case Action::MOVE_LEFT:
            return BtnIcon::DPAD_LEFT;
        case Action::MOVE_RIGHT:
            return BtnIcon::DPAD_RIGHT;
        case Action::CHARACTER:
            return BtnIcon::DPAD_UP; // LB+Up (shown as Up, caller adds LB context)
        case Action::QUEST_LOG:
            return BtnIcon::DPAD_DOWN;
        case Action::PASSIVE_TREE:
            return BtnIcon::DPAD_LEFT;
        case Action::BESTIARY:
            return BtnIcon::DPAD_RIGHT;
        default:
            return BtnIcon::FACE_SOUTH;
    }
}

// Map keyboard actions to atlas cells (rows 3-5)
// Row 3: A(0) B(1) C(2) D(3) E(4) F(5) G(6) H(7) I(8) J(9) K(10) L(11) M(12) N(13) O(14) P(15)
// Row 4: Q(0) R(1) S(2) T(3) U(4) V(5) W(6) X(7) Y(8) Z(9) 1(10) 2(11) 3(12) 4(13) Tab(14) Enter(15)
// Row 5: Esc(0) Space(1) Up(2) Down(3) Left(4) Right(5) .(6) /(7) F5(8) F6(9) F11(10) F12(11) Shift(12) Ctrl(13)
void InputGlyphs::action_to_kb_cell(Action action, int& row, int& col) const {
    switch (action) {
        // Letters (row 3-4)
        case Action::PICKUP:       row = 3; col = 6; return;  // G
        case Action::FIRE_RANGED:  row = 3; col = 5; return;  // F
        case Action::REST:         row = 4; col = 1; return;  // R
        case Action::PRAY:         row = 3; col = 15; return; // P
        case Action::EXAMINE:      row = 4; col = 7; return;  // X
        case Action::SNEAK_TOGGLE: row = 3; col = 14; return; // O
        case Action::INVENTORY:    row = 3; col = 8; return;  // I
        case Action::SPELLBOOK:    row = 4; col = 9; return;  // Z
        case Action::CHARACTER:    row = 3; col = 2; return;  // C
        case Action::PASSIVE_TREE: row = 4; col = 3; return;  // T
        case Action::QUEST_LOG:    row = 4; col = 0; return;  // Q
        case Action::WORLD_MAP:    row = 3; col = 12; return; // M
        case Action::QUICK_CAST:   row = 4; col = 5; return;  // V
        case Action::HELP:         row = 5; col = 7; return;  // / (?)

        // Special keys
        case Action::INTERACT:
        case Action::STAIRS_ENTER:
        case Action::STAIRS_DOWN:
        case Action::STAIRS_UP:    row = 4; col = 15; return; // Enter
        case Action::WAIT:         row = 5; col = 6; return;  // .

        // Arrows
        case Action::MOVE_UP:      row = 5; col = 2; return;
        case Action::MOVE_DOWN:    row = 5; col = 3; return;
        case Action::MOVE_LEFT:    row = 5; col = 4; return;
        case Action::MOVE_RIGHT:   row = 5; col = 5; return;

        // Diagonals (no icon, fall back to text)
        case Action::MOVE_NW:
        case Action::MOVE_NE:
        case Action::MOVE_SW:
        case Action::MOVE_SE:      row = -1; col = -1; return;

        case Action::BESTIARY:     row = 4; col = 14; return; // Tab
        case Action::QUICKSAVE:    row = 5; col = 8; return;  // F5
        case Action::QUICKLOAD:    row = 5; col = 9; return;  // F6
        case Action::SCREENSHOT:   row = 5; col = 11; return; // F12

        case Action::ABILITY_1:    row = 4; col = 10; return; // 1
        case Action::ABILITY_2:    row = 4; col = 11; return; // 2
        case Action::ABILITY_3:    row = 4; col = 12; return; // 3
        case Action::ABILITY_4:    row = 4; col = 13; return; // 4

        default: row = -1; col = -1; return;
    }
}

int InputGlyphs::draw_icon(SDL_Renderer* renderer, BtnIcon icon, int x, int y, int size) const {
    if (!atlas_) return 0;
    int col = static_cast<int>(icon);
    int row = gamepad_row();
    SDL_Rect src = {col * 16, row * 16, 16, 16};
    SDL_Rect dst = {x, y, size, size};
    SDL_RenderCopy(renderer, atlas_, &src, &dst);
    return size;
}

int InputGlyphs::draw_action(SDL_Renderer* renderer, Action action, int x, int y, int size) const {
    if (!atlas_) return 0;
    int col, row;
    if (using_gamepad()) {
        BtnIcon icon = action_to_btn(action);
        col = static_cast<int>(icon);
        row = gamepad_row();
    } else {
        action_to_kb_cell(action, row, col);
        if (row < 0) return 0; // no icon for this action
    }
    SDL_Rect src = {col * 16, row * 16, 16, 16};
    SDL_Rect dst = {x, y, size, size};
    SDL_RenderCopy(renderer, atlas_, &src, &dst);
    return size;
}

std::string InputGlyphs::gamepad_label(Action action) const {
    bool is_ps = (mode_ == InputMode::GAMEPAD_PLAYSTATION);
    bool is_sw = (mode_ == InputMode::GAMEPAD_SWITCH);

    const char* btn_a = is_ps ? "X" : is_sw ? "B" : "A";
    const char* btn_x = is_ps ? "[]" : is_sw ? "Y" : "X";
    const char* btn_y = is_ps ? "/\\" : is_sw ? "X" : "Y";
    const char* lb = is_ps ? "L1" : is_sw ? "L" : "LB";
    const char* rb = is_ps ? "R1" : is_sw ? "R" : "RB";
    const char* lt = is_ps ? "L2" : is_sw ? "ZL" : "LT";
    const char* rt = is_ps ? "R2" : is_sw ? "ZR" : "RT";

    switch (action) {
        case Action::INTERACT:
        case Action::PICKUP:
        case Action::STAIRS_DOWN:
        case Action::STAIRS_UP:
        case Action::STAIRS_ENTER: return std::string("(") + btn_a + ")";
        case Action::EXAMINE:      return std::string("(") + btn_x + ")";
        case Action::INVENTORY:    return std::string("(") + btn_y + ")";
        case Action::SPELLBOOK:
        case Action::QUICK_CAST:   return std::string("(") + lb + ")";
        case Action::FIRE_RANGED:  return std::string("(") + rb + ")";
        case Action::PRAY:         return std::string("(") + lt + ")";
        case Action::REST:         return std::string("(") + rt + ")";
        case Action::SNEAK_TOGGLE: return "(L3)";
        case Action::WAIT:         return "(R3)";
        case Action::WORLD_MAP:    return "(Select)";
        case Action::MOVE_UP:      return "(Up)";
        case Action::MOVE_DOWN:    return "(Dn)";
        case Action::MOVE_LEFT:    return "(Lt)";
        case Action::MOVE_RIGHT:   return "(Rt)";
        case Action::CHARACTER:    return std::string("(") + lb + "+Up)";
        case Action::QUEST_LOG:    return std::string("(") + lb + "+Dn)";
        case Action::PASSIVE_TREE: return std::string("(") + lb + "+Lt)";
        case Action::BESTIARY:     return std::string("(") + lb + "+Rt)";
        case Action::HELP:         return "(Start)";
        default:                   return "(??)";
    }
}

std::string InputGlyphs::label(Action action) const {
    if (mode_ != InputMode::KEYBOARD) return gamepad_label(action);
    switch (action) {
        case Action::MOVE_UP:      return "Up";
        case Action::MOVE_DOWN:    return "Down";
        case Action::MOVE_LEFT:    return "Left";
        case Action::MOVE_RIGHT:   return "Right";
        case Action::MOVE_NW:      return "Y";
        case Action::MOVE_NE:      return "U";
        case Action::MOVE_SW:      return "B";
        case Action::MOVE_SE:      return "N";
        case Action::WAIT:         return ".";
        case Action::INTERACT:     return "Enter";
        case Action::PICKUP:       return "G";
        case Action::STAIRS_DOWN:  return ">";
        case Action::STAIRS_UP:    return "<";
        case Action::STAIRS_ENTER: return "Enter";
        case Action::FIRE_RANGED:  return "F";
        case Action::REST:         return "R";
        case Action::PRAY:         return "P";
        case Action::EXAMINE:      return "X";
        case Action::SNEAK_TOGGLE: return "O";
        case Action::QUICK_CAST:   return "V";
        case Action::INVENTORY:    return "I";
        case Action::SPELLBOOK:    return "Z";
        case Action::CHARACTER:    return "C";
        case Action::PASSIVE_TREE: return "T";
        case Action::QUEST_LOG:    return "Q";
        case Action::WORLD_MAP:    return "M";
        case Action::BESTIARY:     return "Tab";
        case Action::HELP:         return "?";
        case Action::ABILITY_1:    return "1";
        case Action::ABILITY_2:    return "2";
        case Action::ABILITY_3:    return "3";
        case Action::ABILITY_4:    return "4";
        case Action::QUICKSAVE:    return "F5";
        case Action::QUICKLOAD:    return "F6";
        case Action::SCREENSHOT:   return "F12";
        default: return "??";
    }
}

std::string InputGlyphs::confirm() const {
    return label(Action::INTERACT);
}

std::string InputGlyphs::cancel() const {
    if (using_gamepad()) {
        bool is_ps = (mode_ == InputMode::GAMEPAD_PLAYSTATION);
        bool is_sw = (mode_ == InputMode::GAMEPAD_SWITCH);
        const char* btn = is_ps ? "O" : is_sw ? "A" : "B";
        return std::string("(") + btn + ")";
    }
    return "Esc";
}

std::string InputGlyphs::hint(Action a1, const char* desc1,
                               Action a2, const char* desc2,
                               Action a3, const char* desc3) const {
    std::string result;
    auto add = [&](Action a, const char* desc) {
        if (!result.empty()) result += "   ";
        result += "[" + label(a) + "] " + desc;
    };
    add(a1, desc1);
    if (a2 != Action::COUNT && desc2) add(a2, desc2);
    if (a3 != Action::COUNT && desc3) add(a3, desc3);
    return result;
}
