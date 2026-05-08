#pragma once
#include <string>

enum class BackgroundId : int {
    GRAVEDIGGER = 0,
    ALCHEMIST,
    DESERTER,
    PIT_FIGHTER,
    HEDGE_WITCH,
    TOMB_ROBBER,
    HERETIC_PRIEST,
    SHIPWRECK_SURVIVOR,
    RATCATCHER,
    BLACKSMITH,
    SCHOLAR,
    PLAGUE_DOCTOR,
    EXECUTIONER,
    FARMER,
    GLADIATOR,
    COUNT
};

constexpr int BACKGROUND_COUNT = static_cast<int>(BackgroundId::COUNT);

struct BackgroundInfo {
    const char* name;
    const char* description;
    const char* passive_name;
    const char* passive_desc;
    // Attribute bonuses
    int str_bonus, dex_bonus, con_bonus, int_bonus, wil_bonus, per_bonus, cha_bonus;
    // Vital bonuses
    int bonus_hp, bonus_damage;
};

inline const BackgroundInfo& get_background_info(BackgroundId id) {
    static const BackgroundInfo BACKGROUNDS[] = {
        {"Gravedigger",
         "Dug graves for years.",
         "Bone Reader",
         "+2 damage vs undead. Undead visible on minimap.",
         1, 0, 1, 0, 0, 0, -1,
         0, 0},
        {"Alchemist",
         "Trained in herbalism.",
         "Reagent Mastery",
         "All potions auto-identified. Poison lasts half as long on you.",
         0, 0, 0, 2, 0, 0, 0,
         0, 0},
        {"Deserter",
         "Deserted from the garrison.",
         "Combat Drill",
         "+1 base damage. +10 speed on first floor of each dungeon.",
         1, 1, 0, 0, 0, 0, -1,
         0, 1},
        {"Pit Fighter",
         "Underground fighting rings.",
         "Iron Jaw",
         "Take 1 less damage from everything. Stuns last 1 turn less.",
         2, 0, 1, 0, 0, 0, -1,
         2, 1},
        {"Hedge Witch",
         "Village healer.",
         "Hedge Craft",
         "Start with Minor Heal. Healing spells are 25% stronger.",
         0, 0, 0, 1, 2, 0, 0,
         0, 0},
        {"Tomb Robber",
         "Raided tombs for a living.",
         "Trap Sense",
         "Traps revealed at +2 range. Chests drop better loot.",
         0, 2, 0, 0, 0, 1, 0,
         0, 0},
        {"Heretic Priest",
         "Expelled from the temple.",
         "Dark Litany",
         "Start with one random Dark Arts spell. Favor decays 25% faster.",
         0, 0, 0, 1, 1, 0, 0,
         0, 0},
        {"Shipwreck Survivor",
         "Sole survivor of a wreck.",
         "Last Breath",
         "Below 25% HP: -2 damage taken. Cannot be one-shot from above half.",
         0, 0, 2, 0, 1, 0, 0,
         2, 0},
        {"Ratcatcher",
         "Pest control.",
         "Vermin Slayer",
         "Rats, bats, spiders, kobolds deal half damage. +2 PER in dungeons.",
         0, 1, 0, 0, 0, 2, 0,
         0, 0},
        {"Blacksmith",
         "Worked a forge.",
         "Forge Trained",
         "+1 unarmed damage. Equipment never degrades. Repair on rest.",
         2, 0, 1, 0, 0, 0, 0,
         0, 1},
        {"Scholar",
         "Read everything. Understood too much.",
         "Arcane Index",
         "Spell tomes always succeed. Start with Identify spell.",
         0, 0, 0, 2, 0, 1, 0,
         0, 0},
        {"Plague Doctor",
         "Treated the dying. Immune to what killed them.",
         "Immunity",
         "Disease immune. 50% poison resist. -2 CHA (mask scares people).",
         0, 0, 1, 1, 0, 0, -2,
         0, 0},
        {"Executioner",
         "Professional killer.",
         "Clean Kill",
         "Kills below 20% HP restore 3 HP. +1 base damage.",
         2, 0, 0, 0, 1, 0, -2,
         0, 1},
        {"Farmer",
         "Farm laborer.",
         "Endurance",
         "+3 max HP. Rest heals 10% more. Poison resist +25%.",
         1, 0, 2, 0, 0, 0, 0,
         3, 0},
        {"Gladiator",
         "Arena veteran.",
         "Showmanship",
         "+50% XP from kills. +5 max HP. Start with +1 damage.",
         2, 1, 0, 0, 0, 0, 1,
         5, 1},
    };
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= BACKGROUND_COUNT) {
        static const BackgroundInfo NONE_BG = {
            "None", "No background.", "None", "No passive.", 0,0,0,0,0,0,0,0,0
        };
        return NONE_BG;
    }
    return BACKGROUNDS[idx];
}
