#include "ui/levelup_screen.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
#include "components/stats.h"
#include <cstdio>
#include <algorithm>

// Universal choices — available to all classes
static const LevelUpChoice UNIVERSAL[] = {
    {"+2 Strength",     "Raw physical power. Increases melee damage.",
     static_cast<int>(Attr::STR), 2, 0, 0, 0, 0, 0, ClassId::COUNT},
    {"+2 Dexterity",    "Agility and precision. Improves ranged attacks and dodge.",
     static_cast<int>(Attr::DEX), 2, 0, 0, 0, 0, 0, ClassId::COUNT},
    {"+2 Constitution", "Toughness. Increases HP and disease resistance.",
     static_cast<int>(Attr::CON), 2, 0, 0, 0, 0, 0, ClassId::COUNT},
    {"+2 Intelligence", "Mental acuity. Increases spell damage and MP.",
     static_cast<int>(Attr::INT), 2, 0, 0, 0, 0, 0, ClassId::COUNT},
    {"+2 Willpower",    "Inner strength. Improves prayer power and focus.",
     static_cast<int>(Attr::WIL), 2, 0, 0, 0, 0, 0, ClassId::COUNT},
    {"+2 Perception",   "Awareness. Expands field of view.",
     static_cast<int>(Attr::PER), 2, 0, 0, 0, 0, 0, ClassId::COUNT},
    {"Tough: +8 Max HP", "More health to survive deeper dungeons.",
     -1, 0, 8, 0, 0, 0, 0, ClassId::COUNT},
    {"Arcane: +6 Max MP","More mana for spellcasting.",
     -1, 0, 0, 6, 0, 0, 0, ClassId::COUNT},
    {"Swift: +10 Speed", "Act more frequently in combat.",
     -1, 0, 0, 0, 10, 0, 0, ClassId::COUNT},
    {"Sharp: +1 Damage", "Increases base weapon damage.",
     -1, 0, 0, 0, 0, 1, 0, ClassId::COUNT},
};
static constexpr int UNIVERSAL_COUNT = sizeof(UNIVERSAL) / sizeof(UNIVERSAL[0]);

// Class-specific perks — only appear for matching class
static const LevelUpChoice CLASS_PERKS[] = {
    // Fighter
    {"Cleave: +2 STR, +1 Damage", "Overwhelming force. Hit harder, hit more.",
     static_cast<int>(Attr::STR), 2, 0, 0, 0, 1, 0, ClassId::FIGHTER},
    {"Iron Skin: +1 Armor, +2 CON", "Toughen your hide through combat experience.",
     static_cast<int>(Attr::CON), 2, 0, 0, 0, 0, 1, ClassId::FIGHTER},

    // Rogue
    {"Precision: +2 DEX, +1 Damage", "Surgical strikes. Every hit counts.",
     static_cast<int>(Attr::DEX), 2, 0, 0, 0, 1, 0, ClassId::ROGUE},
    {"Shadow Step: +15 Speed", "Move before they see you coming.",
     -1, 0, 0, 0, 15, 0, 0, ClassId::ROGUE},

    // Wizard
    {"Arcane Focus: +3 INT, +4 MP", "Deeper understanding of the arcane arts.",
     static_cast<int>(Attr::INT), 3, 0, 4, 0, 0, 0, ClassId::WIZARD},
    {"Mana Well: +12 Max MP", "A vast reservoir of magical energy.",
     -1, 0, 0, 12, 0, 0, 0, ClassId::WIZARD},

    // Ranger
    {"Marksman: +2 DEX, +1 Damage", "Deadly precision at range.",
     static_cast<int>(Attr::DEX), 2, 0, 0, 0, 1, 0, ClassId::RANGER},
    {"Keen Eye: +3 PER", "Nothing escapes your notice. Expanded field of view.",
     static_cast<int>(Attr::PER), 3, 0, 0, 0, 0, 0, ClassId::RANGER},

    // Barbarian
    {"Berserker: +3 STR, +10 HP", "Rage fuels your body.",
     static_cast<int>(Attr::STR), 3, 10, 0, 0, 0, 0, ClassId::BARBARIAN},
    {"Thick Hide: +2 Armor, +2 CON", "Scars become armor.",
     static_cast<int>(Attr::CON), 2, 0, 0, 0, 0, 2, ClassId::BARBARIAN},

    // Knight
    {"Shield Mastery: +2 Armor, +6 HP", "Your shield is an extension of your body.",
     -1, 0, 6, 0, 0, 0, 2, ClassId::KNIGHT},
    {"Stalwart: +2 CON, +2 WIL", "Unshakeable resolve.",
     static_cast<int>(Attr::CON), 2, 0, 0, 0, 0, 0, ClassId::KNIGHT},

    // Monk
    {"Inner Focus: +2 DEX, +2 WIL", "Mind and body as one.",
     static_cast<int>(Attr::DEX), 2, 0, 0, 0, 0, 0, ClassId::MONK},
    {"Iron Fist: +2 Damage, +10 Speed", "Your hands are weapons.",
     -1, 0, 0, 0, 10, 2, 0, ClassId::MONK},

    // Templar
    {"Holy Fortitude: +8 HP, +1 Armor", "Faith made manifest.",
     -1, 0, 8, 0, 0, 0, 1, ClassId::TEMPLAR},
    {"Righteous Fury: +2 STR, +2 WIL", "Wrath in service of the divine.",
     static_cast<int>(Attr::STR), 2, 0, 0, 0, 0, 0, ClassId::TEMPLAR},

    // Druid
    {"Nature's Gift: +3 INT, +6 HP", "The forest strengthens you.",
     static_cast<int>(Attr::INT), 3, 6, 0, 0, 0, 0, ClassId::DRUID},
    {"Wild Shape: +2 CON, +10 Speed", "Your body adapts to the wild.",
     static_cast<int>(Attr::CON), 2, 0, 0, 10, 0, 0, ClassId::DRUID},

    // War Cleric
    {"Battle Prayer: +2 WIL, +1 Damage", "Strike with divine authority.",
     static_cast<int>(Attr::WIL), 2, 0, 0, 0, 1, 0, ClassId::WAR_CLERIC},
    {"Divine Aegis: +6 HP, +6 MP", "Protection of body and spirit.",
     -1, 0, 6, 6, 0, 0, 0, ClassId::WAR_CLERIC},

    // Warlock
    {"Dark Pact: +4 INT, -4 HP", "Power at a price.",
     static_cast<int>(Attr::INT), 4, -4, 0, 0, 0, 0, ClassId::WARLOCK},
    {"Soul Siphon: +2 Damage, +4 MP", "Feed on what you destroy.",
     -1, 0, 0, 4, 0, 2, 0, ClassId::WARLOCK},

    // Necromancer
    {"Death's Embrace: +3 INT, +4 MP", "The dead whisper their secrets.",
     static_cast<int>(Attr::INT), 3, 0, 4, 0, 0, 0, ClassId::NECROMANCER},
    {"Bone Armor: +2 Armor, +2 CON", "The dead protect their master.",
     static_cast<int>(Attr::CON), 2, 0, 0, 0, 0, 2, ClassId::NECROMANCER},

    // Dwarf
    {"Stone Blood: +2 CON, +1 Armor", "Mountain-born resilience.",
     static_cast<int>(Attr::CON), 2, 0, 0, 0, 0, 1, ClassId::DWARF},
    {"Forge-Hardened: +10 HP, +1 Damage", "Tempered in the deep fires.",
     -1, 0, 10, 0, 0, 1, 0, ClassId::DWARF},

    // Elf
    {"Elven Grace: +2 DEX, +2 PER", "Preternatural awareness.",
     static_cast<int>(Attr::DEX), 2, 0, 0, 0, 0, 0, ClassId::ELF},
    {"Arcane Heritage: +2 INT, +4 MP", "Magic runs in your blood.",
     static_cast<int>(Attr::INT), 2, 0, 4, 0, 0, 0, ClassId::ELF},

    // Bandit
    {"Cutthroat: +2 DEX, +2 Damage", "No rules. No mercy.",
     static_cast<int>(Attr::DEX), 2, 0, 0, 0, 2, 0, ClassId::BANDIT},
    {"Slippery: +15 Speed, +2 PER", "Always one step ahead.",
     static_cast<int>(Attr::PER), 2, 0, 0, 15, 0, 0, ClassId::BANDIT},

    // Wyrmkin
    {"Dragon Fire: +3 CON, +1 Damage", "The flame burns hotter within you.",
     static_cast<int>(Attr::CON), 3, 0, 0, 0, 1, 0, ClassId::WYRMKIN},
    {"Scaled Hide: +2 Armor, +8 HP", "Your skin hardens to scale.",
     -1, 0, 8, 0, 0, 0, 2, ClassId::WYRMKIN},

    // Revenant
    {"Undying Will: +3 WIL, +6 HP", "You refuse to stay dead.",
     static_cast<int>(Attr::WIL), 3, 6, 0, 0, 0, 0, ClassId::REVENANT},
    {"Death's Reach: +2 Damage, +4 MP", "The grave lends its strength.",
     -1, 0, 0, 4, 0, 2, 0, ClassId::REVENANT},

    // Serpentine
    {"Venom Strike: +3 DEX, +1 Damage", "Every cut carries poison.",
     static_cast<int>(Attr::DEX), 3, 0, 0, 0, 1, 0, ClassId::SERPENTINE},
    {"Serpent's Grace: +15 Speed, +2 PER", "Strike before they blink.",
     static_cast<int>(Attr::PER), 2, 0, 0, 15, 0, 0, ClassId::SERPENTINE},

    // Trollblood
    {"Regenerate: +15 HP, +2 CON", "Your wounds knit shut before your eyes.",
     static_cast<int>(Attr::CON), 2, 15, 0, 0, 0, 0, ClassId::TROLLBLOOD},
    {"Thick Skull: +2 Armor, +2 STR", "Built like a mountain that punches back.",
     static_cast<int>(Attr::STR), 2, 0, 0, 0, 0, 2, ClassId::TROLLBLOOD},
};
static constexpr int CLASS_PERK_COUNT = sizeof(CLASS_PERKS) / sizeof(CLASS_PERKS[0]);

void LevelUpScreen::open(Entity player, RNG& rng, ClassId cls) {
    open_ = true;
    selected_ = 0;
    player_ = player;
    choices_.clear();

    // Build pool: all universal + class-matching perks
    std::vector<int> universal_indices;
    for (int i = 0; i < UNIVERSAL_COUNT; i++) universal_indices.push_back(i);
    rng.shuffle(universal_indices);

    std::vector<int> class_indices;
    for (int i = 0; i < CLASS_PERK_COUNT; i++) {
        if (CLASS_PERKS[i].class_req == cls)
            class_indices.push_back(i);
    }
    rng.shuffle(class_indices);

    // Always offer 1 class perk (if available) + 2 universal
    if (!class_indices.empty()) {
        choices_.push_back(CLASS_PERKS[class_indices[0]]);
    }
    for (int i = 0; i < UNIVERSAL_COUNT && static_cast<int>(choices_.size()) < 3; i++) {
        choices_.push_back(UNIVERSAL[universal_indices[i]]);
    }
}

LevelUpAction LevelUpScreen::handle_input(SDL_Event& event) {
    if (!open_) return LevelUpAction::NONE;

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        return LevelUpAction::CHOSEN;
    }

    if (event.type != SDL_KEYDOWN) return LevelUpAction::NONE;

    int count = static_cast<int>(choices_.size());
    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_w:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return LevelUpAction::NONE;
        case SDLK_DOWN:
        case SDLK_s:
        case SDLK_j:
            if (selected_ < count - 1) selected_++;
            return LevelUpAction::NONE;
        case SDLK_RETURN:
        case SDLK_SPACE:
            return LevelUpAction::CHOSEN;
        case SDLK_1:
            if (count > 0) { selected_ = 0; return LevelUpAction::CHOSEN; }
            return LevelUpAction::NONE;
        case SDLK_2:
            if (count > 1) { selected_ = 1; return LevelUpAction::CHOSEN; }
            return LevelUpAction::NONE;
        case SDLK_3:
            if (count > 2) { selected_ = 2; return LevelUpAction::CHOSEN; }
            return LevelUpAction::NONE;
        default:
            return LevelUpAction::NONE;
    }
}

void LevelUpScreen::apply_choice(World& world) const {
    if (selected_ < 0 || selected_ >= static_cast<int>(choices_.size())) return;
    if (!world.has<Stats>(player_)) return;

    auto& stats = world.get<Stats>(player_);
    auto& choice = choices_[selected_];

    if (choice.attr_index >= 0 && choice.attr_index < ATTR_COUNT) {
        auto attr = static_cast<Attr>(choice.attr_index);
        stats.set_attr(attr, stats.attr(attr) + choice.attr_bonus);
        // WIL bonus also adds WIL to the secondary attr if present
    }
    if (choice.hp_bonus != 0) {
        stats.base_hp_max += choice.hp_bonus;
        stats.hp_max += choice.hp_bonus;
        stats.hp += choice.hp_bonus;
        if (stats.base_hp_max < 1) stats.base_hp_max = 1;
        if (stats.hp_max < 1) stats.hp_max = 1;
        if (stats.hp < 1) stats.hp = 1;
    }
    if (choice.mp_bonus > 0) {
        stats.base_mp_max += choice.mp_bonus;
        stats.mp_max += choice.mp_bonus;
        stats.mp += choice.mp_bonus;
    }
    if (choice.speed_bonus > 0) {
        stats.base_speed += choice.speed_bonus;
    }
    if (choice.damage_bonus > 0) {
        stats.base_damage += choice.damage_bonus;
    }
    if (choice.armor_bonus > 0) {
        stats.natural_armor += choice.armor_bonus;
    }
}

void LevelUpScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                            int screen_w, int screen_h) const {
    if (!open_ || !font) return;

    int line_h = TTF_FontLineSkip(font);
    int num_choices = static_cast<int>(choices_.size());

    SDL_Color title_col = {255, 220, 100, 255};
    SDL_Color normal_col = {180, 175, 170, 255};
    SDL_Color sel_col = {255, 240, 180, 255};
    SDL_Color desc_col = {140, 150, 130, 255};
    SDL_Color class_col = {180, 200, 255, 255};
    SDL_Color hint_col = {120, 110, 100, 255};

    // Darken background
    ui::draw_overlay(renderer, screen_w, screen_h);

    // Panel sized by fractions: ~55% width, height dynamic from content
    auto screen = ui::Layout::from_screen(screen_w, screen_h, line_h);
    int choice_h = line_h * 2 + screen.gap + 2; // label + desc + spacing
    int content_h = line_h * 2 + screen.gap * 2 + choice_h * num_choices
                  + screen.gap + line_h + ui::Layout::PANEL_INSET * 2;
    ui::Rect outer = screen.bounds.center(
        screen.bounds.w * 11 / 20, content_h);
    auto layout = ui::draw_panel_in(renderer, outer, line_h);

    // Title
    ui::draw_text_in(renderer, font, "Level Up!", title_col,
                     layout.row(), ui::Align::CENTER);
    layout.skip(layout.gap);

    // Subtitle
    ui::draw_text_in(renderer, font, "Choose a bonus:", hint_col,
                     layout.row(), ui::Align::CENTER);
    layout.skip(layout.gap);

    for (int i = 0; i < num_choices; i++) {
        bool is_sel = (i == selected_);
        auto& c = choices_[i];
        bool is_class = (c.class_req != ClassId::COUNT);
        SDL_Color col = is_sel ? sel_col : (is_class ? class_col : normal_col);

        // Highlight background for selected choice
        ui::Rect choice_area = layout.cursor.top(line_h * 2 + 4);
        if (is_sel) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 60, 50, 30, 100);
            SDL_Rect sel_rect = choice_area.sdl();
            SDL_RenderFillRect(renderer, &sel_rect);
        }

        // Label row
        char buf[128];
        snprintf(buf, sizeof(buf), "%s%d) %s", is_sel ? "> " : "  ", i + 1,
                 c.label.c_str());
        ui::draw_text_in(renderer, font, buf, col,
                         layout.row().inset(layout.pad, 0), ui::Align::LEFT);
        layout.skip(2);

        // Description row (indented a bit more)
        if (!c.description.empty()) {
            ui::Rect desc_row = layout.row();
            ui::draw_text_in(renderer, font, c.description.c_str(), desc_col,
                             desc_row.inset(layout.pad * 2, 0), ui::Align::LEFT);
        }
        layout.skip(layout.gap);
    }

    // Hint at bottom
    layout.skip(layout.gap / 2);
    { auto* ig = InputGlyphs::get();
      char hbuf[128];
      if (ig && ig->using_gamepad())
          snprintf(hbuf, sizeof(hbuf), "D-Pad + %s to choose", ig->confirm().c_str());
      else
          snprintf(hbuf, sizeof(hbuf), "[1-3] or Up/Down + Enter");
      ui::draw_text_in(renderer, font, hbuf, hint_col, layout.row(), ui::Align::CENTER); }
}
