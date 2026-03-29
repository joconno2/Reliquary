#pragma once
#include "core/spritesheet.h"

enum class PetId : int {
    RAT = 0,    // +5% item drop rate
    DOG,        // bark warns of enemies (+1 trap detect)
    CAT,        // +1 PER
    OWL,        // +1 FOV radius
    SNAKE,      // +5% poison resist
    BAT,        // echolocation (partial map reveal)
    IMP,        // +5% spell power
    CROW,       // scavenges gold from corpses
    PET_COUNT
};

constexpr int PET_TYPE_COUNT = static_cast<int>(PetId::PET_COUNT);

struct PetInfo {
    const char* name;
    const char* description;
    int sprite_sheet;
    int sprite_x, sprite_y;
    // Tint for sprites that need recoloring (crow = dark seagull)
    int tint_r, tint_g, tint_b;
};

inline const PetInfo& get_pet_info(PetId id) {
    // animals.txt corrected numbering (row = group - 1, col = letter index):
    // rat=7.i (row6,col8), dog=5.a (row4,col0), cat=4.a (row3,col0)
    // owl=12.b (row11,col1), snake=8.a (row7,col0)
    // bat=monsters 7.g (row6,col6), imp=monsters 12.b (row11,col1)
    // crow=animals 12.a seagull (row11,col0) with dark tint
    static const PetInfo INFO[] = {
        {"rat",   "A mangy rat. It scurries and sniffs. (+5% item drops)",
         SHEET_ANIMALS, 8, 6, 255, 255, 255},
        {"dog",   "A scrappy mutt with keen senses. (warns of traps)",
         SHEET_ANIMALS, 0, 4, 255, 255, 255},
        {"cat",   "A silent cat. It watches everything. (+1 PER)",
         SHEET_ANIMALS, 0, 3, 255, 255, 255},
        {"owl",   "A barn owl. Its pale face turns in the dark. (+1 FOV)",
         SHEET_ANIMALS, 1, 11, 255, 255, 255},
        {"snake", "A small, cold snake coiled at your feet. (+5% poison resist)",
         SHEET_ANIMALS, 0, 7, 255, 255, 255},
        {"bat",   "A chittering bat. It hangs upside down when you stop. (map reveal)",
         SHEET_MONSTERS, 6, 6, 255, 255, 255},
        {"imp",   "A tiny red imp. It giggles at inappropriate times. (+5% spell power)",
         SHEET_MONSTERS, 1, 11, 255, 255, 255},
        {"crow",  "A black crow. It picks at things. (scavenges gold)",
         SHEET_ANIMALS, 0, 11, 60, 60, 70},  // dark-tinted seagull
    };
    return INFO[static_cast<int>(id)];
}
