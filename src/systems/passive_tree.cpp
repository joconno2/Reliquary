#include "components/passive_tree.h"
#include <cmath>

// ══════════════════════════════════════════════════════════════════════
// PASSIVE TREE NODE DATA
//
// Layout: 8 sectors arranged in a ring. Center at (0,0).
// Sector centers at angle = sector_index * 45 degrees, radius ~6.
// Nodes within a sector fan out from the edge toward the center.
//
// Convention: x,y are abstract coordinates. UI scales to screen.
// Ring bottom = Might (angle 270 / -90), clockwise:
//   Might(270) -> Venom(225) -> Shadow(180) -> Finesse(135)
//   -> Arcane(90) -> Faith(45) -> Fortitude(0/360) -> Nature(315)
//
// IDs: 0-19 Center, 20-49 Might, 50-79 Finesse, 80-109 Arcane,
//       110-139 Faith, 140-169 Fortitude, 170-199 Nature,
//       200-229 Shadow, 230-259 Venom
// ══════════════════════════════════════════════════════════════════════

// Shorthand
#define N NodeType::SMALL
#define NT NodeType::NOTABLE
#define KS NodeType::KEYSTONE
#define CAP NodeType::CAPSTONE
#define NC NO_CONN
#define EFF(t, v) NodeEffect{EffectType::t, v}
#define NOEFF NodeEffect{EffectType::NONE, 0}

static const PassiveNode TREE[] = {

// ── CENTER HUB (id 0-14) ────────────────────────────────────────────
// Small ring of utility nodes in the middle. Connects to inner nodes
// of each sector.
{0, N, Sector::CENTER, nullptr, "+2 HP",
 0.0f, 0.0f, {EFF(BONUS_HP, 2), NOEFF, NOEFF, NOEFF},
 {1, 2, 3, 4, NC, NC}},

{1, N, Sector::CENTER, nullptr, "+5% XP gain",
 -0.8f, -0.8f, {EFF(XP_GAIN_BONUS, 5), NOEFF, NOEFF, NOEFF},
 {0, 5, 6, NC, NC, NC}},

{2, N, Sector::CENTER, nullptr, "+1 trap detection",
 0.8f, -0.8f, {EFF(TRAP_DETECTION, 1), NOEFF, NOEFF, NOEFF},
 {0, 7, 8, NC, NC, NC}},

{3, N, Sector::CENTER, nullptr, "+5% rest efficiency",
 0.8f, 0.8f, {EFF(REST_EFFICIENCY, 5), NOEFF, NOEFF, NOEFF},
 {0, 9, 10, NC, NC, NC}},

{4, N, Sector::CENTER, nullptr, "+5% potion effectiveness",
 -0.8f, 0.8f, {EFF(POTION_EFFECTIVENESS, 5), NOEFF, NOEFF, NOEFF},
 {0, 11, 12, NC, NC, NC}},

// Center notable: Wanderer
{5, NT, Sector::CENTER, "Wanderer", "+1 FOV, +5% XP gain",
 -1.5f, -1.5f, {EFF(BONUS_FOV, 1), EFF(XP_GAIN_BONUS, 5), NOEFF, NOEFF},
 {1, 80, 200, NC, NC, NC}},  // connects to Arcane(80) and Shadow(200)

// Center notable: Scavenger
{6, NT, Sector::CENTER, "Scavenger", "10% chance to identify items on pickup",
 -1.5f, 0.0f, {EFF(IDENTIFY_ON_PICKUP_PCT, 10), NOEFF, NOEFF, NOEFF},
 {1, 50, NC, NC, NC, NC}},   // connects to Finesse(50)

// Center notable: Survivor
{7, NT, Sector::CENTER, "Survivor", "+4 HP, +10% rest efficiency",
 1.5f, -1.5f, {EFF(BONUS_HP, 4), EFF(REST_EFFICIENCY, 10), NOEFF, NOEFF},
 {2, 110, 140, NC, NC, NC}}, // connects to Faith(110) and Fortitude(140)

{8, N, Sector::CENTER, nullptr, "+1 PER",
 1.5f, 0.0f, {EFF(BONUS_PER, 1), NOEFF, NOEFF, NOEFF},
 {2, 140, NC, NC, NC, NC}},  // connects to Fortitude(140)

{9, N, Sector::CENTER, nullptr, "+2 MP",
 1.2f, 1.5f, {EFF(BONUS_MP, 2), NOEFF, NOEFF, NOEFF},
 {3, 170, NC, NC, NC, NC}},  // connects to Nature(170)

{10, N, Sector::CENTER, nullptr, "+1 STR",
 0.0f, 1.5f, {EFF(BONUS_STR, 1), NOEFF, NOEFF, NOEFF},
 {3, 20, NC, NC, NC, NC}},   // connects to Might(20)

{11, N, Sector::CENTER, nullptr, "+1 DEX",
 -1.2f, 1.5f, {EFF(BONUS_DEX, 1), NOEFF, NOEFF, NOEFF},
 {4, 230, NC, NC, NC, NC}},  // connects to Venom(230)

{12, N, Sector::CENTER, nullptr, "+2 HP",
 -0.3f, 1.2f, {EFF(BONUS_HP, 2), NOEFF, NOEFF, NOEFF},
 {4, 20, NC, NC, NC, NC}},   // connects to Might(20)

// ── MIGHT SECTOR (id 20-39) ─────────────────────────────────────────
// Bottom of ring. STR, melee damage, HP, cleave.
// Entry from center at top, fans downward.

// Inner ring (connect to center)
{20, N, Sector::MIGHT, nullptr, "+1 STR",
 0.0f, 3.0f, {EFF(BONUS_STR, 1), NOEFF, NOEFF, NOEFF},
 {10, 12, 21, 22, NC, NC}},

{21, N, Sector::MIGHT, nullptr, "+3 HP",
 -0.8f, 3.5f, {EFF(BONUS_HP, 3), NOEFF, NOEFF, NOEFF},
 {20, 23, 24, NC, NC, NC}},

{22, N, Sector::MIGHT, nullptr, "+1 damage",
 0.8f, 3.5f, {EFF(BONUS_DAMAGE, 1), NOEFF, NOEFF, NOEFF},
 {20, 25, 26, NC, NC, NC}},

// Mid ring
{23, N, Sector::MIGHT, nullptr, "+1 STR",
 -1.2f, 4.2f, {EFF(BONUS_STR, 1), NOEFF, NOEFF, NOEFF},
 {21, 27, NC, NC, NC, NC}},

{24, NT, Sector::MIGHT, "Power Attack", "+2 melee damage, +5% melee damage",
 -0.4f, 4.5f, {EFF(BONUS_DAMAGE, 2), EFF(BONUS_MELEE_DAMAGE_PCT, 5), NOEFF, NOEFF},
 {21, 28, 29, NC, NC, NC}},

{25, NT, Sector::MIGHT, "Bloodlust", "Kills restore 5% max HP",
 0.4f, 4.5f, {EFF(ON_KILL_HEAL_PCT, 5), NOEFF, NOEFF, NOEFF},
 {22, 28, 30, NC, NC, NC}},

{26, N, Sector::MIGHT, nullptr, "+3 HP",
 1.2f, 4.2f, {EFF(BONUS_HP, 3), NOEFF, NOEFF, NOEFF},
 {22, 30, 31, NC, NC, NC}},

// Bridge to Venom (left) and Fortitude (right)
{27, N, Sector::MIGHT, nullptr, "+1 CON",
 -2.0f, 4.5f, {EFF(BONUS_CON, 1), NOEFF, NOEFF, NOEFF},
 {23, 230, NC, NC, NC, NC}},  // connects to Venom sector entry

{31, N, Sector::MIGHT, nullptr, "+1 CON",
 2.0f, 4.5f, {EFF(BONUS_CON, 1), NOEFF, NOEFF, NOEFF},
 {26, 170, NC, NC, NC, NC}},  // connects to Nature sector entry

// Deep ring
{28, N, Sector::MIGHT, nullptr, "+1 STR",
 0.0f, 5.2f, {EFF(BONUS_STR, 1), NOEFF, NOEFF, NOEFF},
 {24, 25, 32, NC, NC, NC}},

{29, NT, Sector::MIGHT, "Executioner", "+25% damage vs enemies below 30% HP",
 -0.8f, 5.5f, {EFF(DAMAGE_VS_LOW_HP, 25), NOEFF, NOEFF, NOEFF},
 {24, 33, NC, NC, NC, NC},
 static_cast<int>(SkillId::BLADES), 25},

{30, NT, Sector::MIGHT, "Iron Skin", "+2 armor, +1 CON, +15% counter dmg",
 0.8f, 5.5f, {EFF(BONUS_ARMOR, 2), EFF(BONUS_CON, 1), EFF(COUNTER_DAMAGE_BONUS, 15), NOEFF},
 {25, 26, 33, NC, NC, NC}},

// Pre-capstone
{32, N, Sector::MIGHT, nullptr, "+5% melee damage",
 0.0f, 6.0f, {EFF(BONUS_MELEE_DAMAGE_PCT, 5), NOEFF, NOEFF, NOEFF},
 {28, 33, NC, NC, NC, NC}},

{33, NT, Sector::MIGHT, "Berserker", "+15% melee dmg when low HP, +1 fury chain",
 0.0f, 6.8f, {EFF(LOW_HP_DAMAGE_BONUS, 15), EFF(FURY_CHAIN_BONUS, 1), NOEFF, NOEFF},
 {29, 30, 32, 34, NC, NC}},

// Capstone
{34, CAP, Sector::MIGHT, "Whirlwind",
 "Active: Attack all adjacent enemies. 15 turn cooldown.",
 0.0f, 7.5f, {EFF(CAP_WHIRLWIND, 15), NOEFF, NOEFF, NOEFF},
 {33, NC, NC, NC, NC, NC}},

// ── FINESSE SECTOR (id 50-69) ───────────────────────────────────────
// Upper-left of ring. DEX, crit, speed, dodge.

// Inner ring (connect to center)
{50, N, Sector::FINESSE, nullptr, "+1 DEX",
 -3.0f, 0.0f, {EFF(BONUS_DEX, 1), NOEFF, NOEFF, NOEFF},
 {6, 51, 52, NC, NC, NC}},

{51, N, Sector::FINESSE, nullptr, "+5 speed",
 -3.5f, -0.8f, {EFF(BONUS_SPEED, 5), NOEFF, NOEFF, NOEFF},
 {50, 53, 54, NC, NC, NC}},

{52, N, Sector::FINESSE, nullptr, "+2% crit chance",
 -3.5f, 0.8f, {EFF(BONUS_CRIT_CHANCE, 2), NOEFF, NOEFF, NOEFF},
 {50, 55, 56, NC, NC, NC}},

// Mid ring
{53, N, Sector::FINESSE, nullptr, "+1 DEX",
 -4.2f, -1.2f, {EFF(BONUS_DEX, 1), NOEFF, NOEFF, NOEFF},
 {51, 57, NC, NC, NC, NC}},

{54, NT, Sector::FINESSE, "Precision", "+4% crit chance, +1 damage on crit",
 -4.5f, -0.4f, {EFF(BONUS_CRIT_CHANCE, 4), EFF(ON_CRIT_BONUS_DAMAGE, 1), NOEFF, NOEFF},
 {51, 58, 59, NC, NC, NC}},

{55, NT, Sector::FINESSE, "Fleet of Foot", "+15 speed",
 -4.5f, 0.4f, {EFF(BONUS_SPEED, 15), NOEFF, NOEFF, NOEFF},
 {52, 58, 60, NC, NC, NC}},

{56, N, Sector::FINESSE, nullptr, "+2% dodge chance",
 -4.2f, 1.2f, {EFF(BONUS_DODGE_CHANCE, 2), NOEFF, NOEFF, NOEFF},
 {52, 60, 57, NC, NC, NC}},

// Bridge to Shadow (up) and Venom (down)
{57, N, Sector::FINESSE, nullptr, "+1 PER",
 -4.5f, -2.0f, {EFF(BONUS_PER, 1), NOEFF, NOEFF, NOEFF},
 {53, 56, 200, NC, NC, NC}},  // connects to Shadow sector

// Deep ring
{58, N, Sector::FINESSE, nullptr, "+1 DEX",
 -5.2f, 0.0f, {EFF(BONUS_DEX, 1), NOEFF, NOEFF, NOEFF},
 {54, 55, 61, NC, NC, NC}},

{59, NT, Sector::FINESSE, "Riposte", "Counter-attack on successful dodge (once per turn)",
 -5.5f, -0.8f, {EFF(RIPOSTE, 1), EFF(BONUS_DODGE_CHANCE, 3), NOEFF, NOEFF},
 {54, 62, NC, NC, NC, NC},
 static_cast<int>(SkillId::DODGE), 25},

{60, NT, Sector::FINESSE, "Evasion", "+5% dodge, +2 PER",
 -5.5f, 0.8f, {EFF(BONUS_DODGE_CHANCE, 5), EFF(BONUS_PER, 2), NOEFF, NOEFF},
 {55, 56, 62, NC, NC, NC}},

// Pre-capstone
{61, N, Sector::FINESSE, nullptr, "+3% crit chance",
 -6.0f, 0.0f, {EFF(BONUS_CRIT_CHANCE, 3), NOEFF, NOEFF, NOEFF},
 {58, 62, NC, NC, NC, NC}},

{62, NT, Sector::FINESSE, "Lethal Precision", "+2 damage on crit, +5% crit",
 -6.8f, 0.0f, {EFF(ON_CRIT_BONUS_DAMAGE, 2), EFF(BONUS_CRIT_CHANCE, 5), NOEFF, NOEFF},
 {59, 60, 61, 63, NC, NC}},

// Capstone
{63, CAP, Sector::FINESSE, "Time Slip",
 "Active: Take 3 actions in 1 turn. 30 turn cooldown.",
 -7.5f, 0.0f, {EFF(CAP_TIME_SLIP, 30), NOEFF, NOEFF, NOEFF},
 {62, NC, NC, NC, NC, NC}},

// ── ARCANE SECTOR (id 80-99) ────────────────────────────────────────
// Top of ring. INT, spell power, MP, school mastery.

{80, N, Sector::ARCANE, nullptr, "+1 INT",
 0.0f, -3.0f, {EFF(BONUS_INT, 1), NOEFF, NOEFF, NOEFF},
 {5, 81, 82, NC, NC, NC}},

{81, N, Sector::ARCANE, nullptr, "+3 MP",
 -0.8f, -3.5f, {EFF(BONUS_MP, 3), NOEFF, NOEFF, NOEFF},
 {80, 83, 84, NC, NC, NC}},

{82, N, Sector::ARCANE, nullptr, "+5% spell damage",
 0.8f, -3.5f, {EFF(BONUS_SPELL_DAMAGE_PCT, 5), NOEFF, NOEFF, NOEFF},
 {80, 85, 86, NC, NC, NC}},

{83, N, Sector::ARCANE, nullptr, "+1 INT",
 -1.2f, -4.2f, {EFF(BONUS_INT, 1), NOEFF, NOEFF, NOEFF},
 {81, 87, NC, NC, NC, NC}},

{84, NT, Sector::ARCANE, "Mana Siphon", "Kills restore 15% MP, +10% siphon",
 -0.4f, -4.5f, {EFF(MANA_SIPHON, 15), EFF(BONUS_MP, 2), EFF(SIPHON_BONUS, 10), NOEFF},
 {81, 88, NC, NC, NC, NC}},

{85, NT, Sector::ARCANE, "Spell Pierce", "Spells ignore 2 armor, +5 explode dmg",
 0.4f, -4.5f, {EFF(SPELL_PIERCE, 2), EFF(BONUS_SPELL_DAMAGE_PCT, 5), EFF(EXPLODE_DAMAGE_BONUS, 5), NOEFF},
 {82, 88, NC, NC, NC, NC},
 static_cast<int>(SkillId::CONJURATION), 25},

{86, N, Sector::ARCANE, nullptr, "+3 MP",
 1.2f, -4.2f, {EFF(BONUS_MP, 3), NOEFF, NOEFF, NOEFF},
 {82, 87, NC, NC, NC, NC}},

// Bridge to Shadow (left) and Faith (right)
{87, N, Sector::ARCANE, nullptr, "+1 WIL",
 -2.0f, -4.5f, {EFF(BONUS_WIL, 1), NOEFF, NOEFF, NOEFF},
 {83, 86, 200, NC, NC, NC}},

// Deep
{88, N, Sector::ARCANE, nullptr, "-5% spell cost",
 0.0f, -5.2f, {EFF(SPELL_COST_REDUCE_PCT, 5), NOEFF, NOEFF, NOEFF},
 {84, 85, 89, NC, NC, NC}},

{89, NT, Sector::ARCANE, "Arcane Mastery", "+10% spell damage, -10% spell cost",
 0.0f, -6.0f, {EFF(BONUS_SPELL_DAMAGE_PCT, 10), EFF(SPELL_COST_REDUCE_PCT, 10), NOEFF, NOEFF},
 {88, 90, NC, NC, NC, NC}},

{90, CAP, Sector::ARCANE, "Arcane Overload",
 "Active: Next spell costs 0 MP and deals 2x damage. 20 turn cooldown.",
 0.0f, -7.0f, {EFF(CAP_ARCANE_OVERLOAD, 20), NOEFF, NOEFF, NOEFF},
 {89, NC, NC, NC, NC, NC}},

// Arcane side branches
{91, N, Sector::ARCANE, nullptr, "+2 MP",
 1.5f, -4.0f, {EFF(BONUS_MP, 2), NOEFF, NOEFF, NOEFF},
 {86, 92, NC, NC, NC, NC}},

{92, NT, Sector::ARCANE, "Deep Focus", "+1 INT, -8% spell cost",
 2.0f, -4.8f, {EFF(BONUS_INT, 1), EFF(SPELL_COST_REDUCE_PCT, 8), NOEFF, NOEFF},
 {91, NC, NC, NC, NC, NC}},

{93, N, Sector::ARCANE, nullptr, "+3 MP",
 -1.5f, -4.0f, {EFF(BONUS_MP, 3), NOEFF, NOEFF, NOEFF},
 {83, 94, NC, NC, NC, NC}},

{94, NT, Sector::ARCANE, "Overcharge", "+10% spell damage, +1 INT",
 -2.0f, -4.8f, {EFF(BONUS_SPELL_DAMAGE_PCT, 10), EFF(BONUS_INT, 1), NOEFF, NOEFF},
 {93, NC, NC, NC, NC, NC}},

// ── FAITH SECTOR (id 110-129) ────────────────────────────────────────
// Upper-right. WIL, prayer power, favor gain, divine abilities.

{110, N, Sector::FAITH, nullptr, "+1 WIL",
 2.2f, -2.2f, {EFF(BONUS_WIL, 1), NOEFF, NOEFF, NOEFF},
 {7, 111, 112, NC, NC, NC}},

{111, N, Sector::FAITH, nullptr, "+2 HP",
 2.8f, -2.8f, {EFF(BONUS_HP, 2), NOEFF, NOEFF, NOEFF},
 {110, 113, 114, NC, NC, NC}},

{112, N, Sector::FAITH, nullptr, "+1 WIL",
 3.2f, -2.0f, {EFF(BONUS_WIL, 1), NOEFF, NOEFF, NOEFF},
 {110, 115, 116, NC, NC, NC}},

{113, N, Sector::FAITH, nullptr, "+2 MP",
 2.5f, -3.5f, {EFF(BONUS_MP, 2), NOEFF, NOEFF, NOEFF},
 {111, 117, NC, NC, NC, NC}},

{114, NT, Sector::FAITH, "Devotion", "-10% prayer favor cost, +1 WIL",
 3.0f, -3.5f, {EFF(PRAYER_COST_REDUCE_PCT, 10), EFF(BONUS_WIL, 1), NOEFF, NOEFF},
 {111, 118, NC, NC, NC, NC},
 static_cast<int>(SkillId::PRAYER), 25},

{115, NT, Sector::FAITH, "Righteous Fury", "+10% melee damage, +2 HP",
 3.8f, -2.5f, {EFF(BONUS_MELEE_DAMAGE_PCT, 10), EFF(BONUS_HP, 2), NOEFF, NOEFF},
 {112, 118, NC, NC, NC, NC}},

{116, N, Sector::FAITH, nullptr, "+1 CON",
 3.8f, -1.5f, {EFF(BONUS_CON, 1), NOEFF, NOEFF, NOEFF},
 {112, 140, NC, NC, NC, NC}},  // bridge to Fortitude

// Bridge to Arcane
{117, N, Sector::FAITH, nullptr, "+1 INT",
 2.0f, -4.0f, {EFF(BONUS_INT, 1), NOEFF, NOEFF, NOEFF},
 {113, 86, NC, NC, NC, NC}},   // connects to Arcane(86)

// Deep
{118, N, Sector::FAITH, nullptr, "+1 WIL",
 3.5f, -3.8f, {EFF(BONUS_WIL, 1), NOEFF, NOEFF, NOEFF},
 {114, 115, 119, NC, NC, NC}},

{119, NT, Sector::FAITH, "Divine Shield", "+3 armor, +5% all resist",
 3.5f, -4.5f, {EFF(BONUS_ARMOR, 3), EFF(BONUS_ALL_RESIST, 5), NOEFF, NOEFF},
 {118, 120, NC, NC, NC, NC}},

{120, CAP, Sector::FAITH, "Divine Intervention",
 "Active: Full heal + cleanse all effects. Once per floor.",
 3.5f, -5.2f, {EFF(CAP_DIVINE_INTERVENTION, 0), NOEFF, NOEFF, NOEFF},
 {119, NC, NC, NC, NC, NC}},

// Faith side branches
{121, N, Sector::FAITH, nullptr, "+2 HP",
 2.0f, -3.0f, {EFF(BONUS_HP, 2), NOEFF, NOEFF, NOEFF},
 {111, 122, NC, NC, NC, NC}},

{122, NT, Sector::FAITH, "Martyr", "When below 25% HP, prayer cost halved",
 1.5f, -3.8f, {EFF(PRAYER_COST_REDUCE_PCT, 15), EFF(BONUS_WIL, 1), NOEFF, NOEFF},
 {121, NC, NC, NC, NC, NC}},

{123, N, Sector::FAITH, nullptr, "+1 CON",
 4.0f, -3.0f, {EFF(BONUS_CON, 1), NOEFF, NOEFF, NOEFF},
 {115, 124, NC, NC, NC, NC}},

{124, NT, Sector::FAITH, "Zealous Might", "+2 damage, +1 WIL",
 4.5f, -3.5f, {EFF(BONUS_DAMAGE, 2), EFF(BONUS_WIL, 1), NOEFF, NOEFF},
 {123, NC, NC, NC, NC, NC}},

// ── FORTITUDE SECTOR (id 140-159) ───────────────────────────────────
// Right side. CON, HP, armor, resistances, disease immunity.

{140, N, Sector::FORTITUDE, nullptr, "+1 CON",
 3.0f, 0.0f, {EFF(BONUS_CON, 1), NOEFF, NOEFF, NOEFF},
 {7, 8, 116, 141, 142, NC}},

{141, N, Sector::FORTITUDE, nullptr, "+4 HP",
 3.5f, -0.5f, {EFF(BONUS_HP, 4), NOEFF, NOEFF, NOEFF},
 {140, 143, 144, NC, NC, NC}},

{142, N, Sector::FORTITUDE, nullptr, "+1 armor",
 3.5f, 0.5f, {EFF(BONUS_ARMOR, 1), NOEFF, NOEFF, NOEFF},
 {140, 145, 146, NC, NC, NC}},

{143, N, Sector::FORTITUDE, nullptr, "+1 CON",
 4.2f, -1.0f, {EFF(BONUS_CON, 1), NOEFF, NOEFF, NOEFF},
 {141, 147, NC, NC, NC, NC}},

{144, NT, Sector::FORTITUDE, "Thick Skin", "+3 armor, +5 HP, +5 breath dmg",
 4.5f, -0.2f, {EFF(BONUS_ARMOR, 3), EFF(BONUS_HP, 5), EFF(BREATH_DAMAGE_BONUS, 5), NOEFF},
 {141, 148, NC, NC, NC, NC}},

{145, NT, Sector::FORTITUDE, "Resist All", "+10% fire, poison, bleed resist",
 4.5f, 0.2f, {EFF(BONUS_ALL_RESIST, 10), NOEFF, NOEFF, NOEFF},
 {142, 148, NC, NC, NC, NC}},

{146, N, Sector::FORTITUDE, nullptr, "+4 HP",
 4.2f, 1.0f, {EFF(BONUS_HP, 4), NOEFF, NOEFF, NOEFF},
 {142, 170, NC, NC, NC, NC}},  // bridge to Nature

// Bridge up toward Faith already via 116
{147, N, Sector::FORTITUDE, nullptr, "+5% poison resist",
 4.5f, -1.5f, {EFF(BONUS_POISON_RESIST, 5), NOEFF, NOEFF, NOEFF},
 {143, NC, NC, NC, NC, NC}},

// Deep
{148, N, Sector::FORTITUDE, nullptr, "+1 CON",
 5.2f, 0.0f, {EFF(BONUS_CON, 1), NOEFF, NOEFF, NOEFF},
 {144, 145, 149, NC, NC, NC}},

{149, NT, Sector::FORTITUDE, "Last Stand", "Survive lethal hit at 1 HP (once per floor)",
 6.0f, 0.0f, {EFF(LAST_STAND, 1), EFF(BONUS_HP, 5), NOEFF, NOEFF},
 {148, 150, NC, NC, NC, NC},
 static_cast<int>(SkillId::HEAVY_ARMOR), 25},

{150, CAP, Sector::FORTITUDE, "Unbreakable",
 "Active: Halve all incoming damage for 8 turns. 40 turn cooldown.",
 6.8f, 0.0f, {EFF(CAP_UNBREAKABLE, 40), NOEFF, NOEFF, NOEFF},
 {149, NC, NC, NC, NC, NC}},

// Fortitude side branches
{151, N, Sector::FORTITUDE, nullptr, "+3 HP",
 4.0f, -0.8f, {EFF(BONUS_HP, 3), NOEFF, NOEFF, NOEFF},
 {141, 152, NC, NC, NC, NC}},

{152, NT, Sector::FORTITUDE, "Stone Bones", "+10% bleed resist, +1 CON",
 4.5f, -1.3f, {EFF(BONUS_BLEED_RESIST, 10), EFF(BONUS_CON, 1), NOEFF, NOEFF},
 {151, NC, NC, NC, NC, NC}},

{153, N, Sector::FORTITUDE, nullptr, "+3 HP",
 4.0f, 0.8f, {EFF(BONUS_HP, 3), NOEFF, NOEFF, NOEFF},
 {142, 154, NC, NC, NC, NC}},

{154, NT, Sector::FORTITUDE, "Endure", "+10% fire resist, +2 armor",
 4.5f, 1.3f, {EFF(BONUS_FIRE_RESIST, 10), EFF(BONUS_ARMOR, 2), NOEFF, NOEFF},
 {153, NC, NC, NC, NC, NC}},

// ── NATURE SECTOR (id 170-189) ──────────────────────────────────────
// Lower-right. Healing, summoning, animal affinity, regen.

{170, N, Sector::NATURE, nullptr, "+1 WIL",
 2.2f, 2.2f, {EFF(BONUS_WIL, 1), NOEFF, NOEFF, NOEFF},
 {9, 31, 146, 171, 172, NC}},

{171, N, Sector::NATURE, nullptr, "+3 HP",
 2.8f, 2.8f, {EFF(BONUS_HP, 3), NOEFF, NOEFF, NOEFF},
 {170, 173, 174, NC, NC, NC}},

{172, N, Sector::NATURE, nullptr, "+2 MP",
 2.0f, 3.0f, {EFF(BONUS_MP, 2), NOEFF, NOEFF, NOEFF},
 {170, 175, NC, NC, NC, NC}},

{173, NT, Sector::NATURE, "Regrowth", "+15% rest efficiency, +3 HP",
 3.2f, 3.2f, {EFF(REST_EFFICIENCY, 15), EFF(BONUS_HP, 3), NOEFF, NOEFF},
 {171, 176, NC, NC, NC, NC}},

{174, NT, Sector::NATURE, "Herbalist", "+20% potion effectiveness",
 2.5f, 3.5f, {EFF(POTION_EFFECTIVENESS, 20), NOEFF, NOEFF, NOEFF},
 {171, 176, NC, NC, NC, NC}},

{175, N, Sector::NATURE, nullptr, "+1 WIL",
 1.5f, 3.5f, {EFF(BONUS_WIL, 1), NOEFF, NOEFF, NOEFF},
 {172, NC, NC, NC, NC, NC}},

// Deep
{176, N, Sector::NATURE, nullptr, "+2 HP",
 3.0f, 4.0f, {EFF(BONUS_HP, 2), NOEFF, NOEFF, NOEFF},
 {173, 174, 177, NC, NC, NC}},

{177, NT, Sector::NATURE, "Spirit Bond", "Kill heals 8% HP, +3 shapeshift dmg",
 3.0f, 4.8f, {EFF(ON_KILL_HEAL_PCT, 8), EFF(SHAPESHIFT_DAMAGE_BONUS, 3), NOEFF, NOEFF},
 {176, 178, NC, NC, NC, NC}},

{178, CAP, Sector::NATURE, "Aspect of the Beast",
 "Active: Transform +5 all stats, natural attacks. 15 turns. 50 turn cooldown.",
 3.0f, 5.5f, {EFF(CAP_ASPECT_OF_BEAST, 50), NOEFF, NOEFF, NOEFF},
 {177, NC, NC, NC, NC, NC}},

// Nature side branches
{179, N, Sector::NATURE, nullptr, "+1 WIL",
 1.5f, 3.0f, {EFF(BONUS_WIL, 1), NOEFF, NOEFF, NOEFF},
 {172, 180, NC, NC, NC, NC}},

{180, NT, Sector::NATURE, "Deep Roots", "+5 HP, +10% poison resist",
 1.0f, 3.8f, {EFF(BONUS_HP, 5), EFF(BONUS_POISON_RESIST, 10), NOEFF, NOEFF},
 {179, NC, NC, NC, NC, NC}},

{181, N, Sector::NATURE, nullptr, "+2 MP",
 3.5f, 2.5f, {EFF(BONUS_MP, 2), NOEFF, NOEFF, NOEFF},
 {171, 182, NC, NC, NC, NC}},

{182, NT, Sector::NATURE, "Verdant Growth", "+10% rest efficiency, +3 HP",
 4.0f, 3.0f, {EFF(REST_EFFICIENCY, 10), EFF(BONUS_HP, 3), NOEFF, NOEFF},
 {181, NC, NC, NC, NC, NC}},

// ── SHADOW SECTOR (id 200-219) ──────────────────────────────────────
// Left side. Stealth, backstab, evasion, deception.

{200, N, Sector::SHADOW, nullptr, "+1 DEX",
 -2.2f, -2.2f, {EFF(BONUS_DEX, 1), NOEFF, NOEFF, NOEFF},
 {5, 57, 87, 201, 202, NC}},

{201, N, Sector::SHADOW, nullptr, "+5 speed",
 -2.8f, -2.8f, {EFF(BONUS_SPEED, 5), NOEFF, NOEFF, NOEFF},
 {200, 203, 204, NC, NC, NC}},

{202, N, Sector::SHADOW, nullptr, "+2% dodge",
 -3.2f, -2.0f, {EFF(BONUS_DODGE_CHANCE, 2), NOEFF, NOEFF, NOEFF},
 {200, 205, NC, NC, NC, NC}},

{203, NT, Sector::SHADOW, "Patient Hunter", "+50% dmg to unaware, +20% opener dmg",
 -3.0f, -3.5f, {EFF(PATIENT_HUNTER, 50), EFF(STEALTH_OPENER_BONUS, 20), NOEFF, NOEFF},
 {201, 206, NC, NC, NC, NC},
 static_cast<int>(SkillId::STEALTH), 25},

{204, NT, Sector::SHADOW, "Shadowstep", "+15 speed, +3% dodge",
 -2.5f, -3.5f, {EFF(BONUS_SPEED, 15), EFF(BONUS_DODGE_CHANCE, 3), NOEFF, NOEFF},
 {201, 206, NC, NC, NC, NC}},

{205, N, Sector::SHADOW, nullptr, "+1 PER",
 -3.5f, -1.5f, {EFF(BONUS_PER, 1), NOEFF, NOEFF, NOEFF},
 {202, 230, NC, NC, NC, NC}},  // bridge to Venom

// Deep
{206, N, Sector::SHADOW, nullptr, "+1 DEX",
 -3.0f, -4.2f, {EFF(BONUS_DEX, 1), NOEFF, NOEFF, NOEFF},
 {203, 204, 207, NC, NC, NC}},

{207, NT, Sector::SHADOW, "Assassinate", "+3 damage on crit, +5% crit",
 -3.0f, -5.0f, {EFF(ON_CRIT_BONUS_DAMAGE, 3), EFF(BONUS_CRIT_CHANCE, 5), NOEFF, NOEFF},
 {206, 208, NC, NC, NC, NC}},

{208, CAP, Sector::SHADOW, "Death Mark",
 "Active: Mark target. Next hit is guaranteed crit, 3x damage. 20 turn cooldown.",
 -3.0f, -5.8f, {EFF(CAP_DEATH_MARK, 20), NOEFF, NOEFF, NOEFF},
 {207, NC, NC, NC, NC, NC}},

// Shadow side branches
{209, N, Sector::SHADOW, nullptr, "+5 speed",
 -2.5f, -3.0f, {EFF(BONUS_SPEED, 5), NOEFF, NOEFF, NOEFF},
 {201, 210, NC, NC, NC, NC}},

{210, NT, Sector::SHADOW, "Ambush", "+3% crit, +5 speed",
 -2.0f, -3.8f, {EFF(BONUS_CRIT_CHANCE, 3), EFF(BONUS_SPEED, 5), NOEFF, NOEFF},
 {209, NC, NC, NC, NC, NC}},

{211, N, Sector::SHADOW, nullptr, "+2% dodge",
 -3.5f, -2.5f, {EFF(BONUS_DODGE_CHANCE, 2), NOEFF, NOEFF, NOEFF},
 {202, 212, NC, NC, NC, NC}},

{212, NT, Sector::SHADOW, "Smoke Screen", "+5% dodge, +1 PER",
 -4.0f, -3.0f, {EFF(BONUS_DODGE_CHANCE, 5), EFF(BONUS_PER, 1), NOEFF, NOEFF},
 {211, NC, NC, NC, NC, NC}},

// ── VENOM SECTOR (id 230-249) ───────────────────────────────────────
// Lower-left. Poison, bleed, DoTs, debuffs, curses.

{230, N, Sector::VENOM, nullptr, "+1 DEX",
 -2.2f, 2.2f, {EFF(BONUS_DEX, 1), NOEFF, NOEFF, NOEFF},
 {11, 27, 205, 231, 232, NC}},

{231, N, Sector::VENOM, nullptr, "+5% poison resist",
 -2.8f, 2.8f, {EFF(BONUS_POISON_RESIST, 5), NOEFF, NOEFF, NOEFF},
 {230, 233, 234, NC, NC, NC}},

{232, N, Sector::VENOM, nullptr, "+1 damage",
 -3.0f, 2.0f, {EFF(BONUS_DAMAGE, 1), NOEFF, NOEFF, NOEFF},
 {230, 235, NC, NC, NC, NC}},

{233, NT, Sector::VENOM, "Envenom", "15% poison on hit, +1 status duration",
 -3.2f, 3.2f, {EFF(ON_HIT_POISON_CHANCE, 15), EFF(STATUS_DURATION_BONUS, 1), NOEFF, NOEFF},
 {231, 236, NC, NC, NC, NC}},

{234, NT, Sector::VENOM, "Hemorrhage", "10% chance to bleed on melee hit",
 -2.5f, 3.5f, {EFF(ON_HIT_BLEED_CHANCE, 10), NOEFF, NOEFF, NOEFF},
 {231, 236, NC, NC, NC, NC}},

{235, N, Sector::VENOM, nullptr, "+1 STR",
 -3.5f, 1.5f, {EFF(BONUS_STR, 1), NOEFF, NOEFF, NOEFF},
 {232, NC, NC, NC, NC, NC}},

// Deep
{236, N, Sector::VENOM, nullptr, "+1 DEX",
 -3.5f, 3.5f, {EFF(BONUS_DEX, 1), NOEFF, NOEFF, NOEFF},
 {233, 234, 237, NC, NC, NC}},

{237, NT, Sector::VENOM, "Wither", "+20% poison/bleed resist, +15% on-hit poison",
 -4.0f, 4.0f, {EFF(ON_HIT_POISON_CHANCE, 15), EFF(BONUS_POISON_RESIST, 20), NOEFF, NOEFF},
 {236, 238, NC, NC, NC, NC}},

{238, CAP, Sector::VENOM, "Pandemic",
 "Active: All your DoTs spread to enemies within 3 tiles. 25 turn cooldown.",
 -4.5f, 4.5f, {EFF(CAP_PANDEMIC, 25), NOEFF, NOEFF, NOEFF},
 {237, NC, NC, NC, NC, NC}},

// Venom side branches
{239, N, Sector::VENOM, nullptr, "+1 DEX",
 -3.2f, 2.5f, {EFF(BONUS_DEX, 1), NOEFF, NOEFF, NOEFF},
 {232, 240, NC, NC, NC, NC}},

{240, NT, Sector::VENOM, "Toxic Blood", "+10% poison resist, +8% on-hit poison",
 -3.8f, 2.0f, {EFF(BONUS_POISON_RESIST, 10), EFF(ON_HIT_POISON_CHANCE, 8), NOEFF, NOEFF},
 {239, NC, NC, NC, NC, NC}},

{241, N, Sector::VENOM, nullptr, "+1 STR",
 -2.5f, 3.5f, {EFF(BONUS_STR, 1), NOEFF, NOEFF, NOEFF},
 {231, 242, NC, NC, NC, NC}},

{242, NT, Sector::VENOM, "Festering Wounds", "+8% on-hit bleed, +1 damage",
 -2.0f, 4.0f, {EFF(ON_HIT_BLEED_CHANCE, 8), EFF(BONUS_DAMAGE, 1), NOEFF, NOEFF},
 {241, NC, NC, NC, NC, NC}},

// ── CROSS-SECTOR CONNECTORS ──────────────────────────────────────────
// Small nodes between adjacent sectors for hybrid pathing.

// Arcane <-> Faith connector
{270, N, Sector::ARCANE, nullptr, "+1 INT",
 1.5f, -3.0f, {EFF(BONUS_INT, 1), NOEFF, NOEFF, NOEFF},
 {91, 113, NC, NC, NC, NC}},

// Faith <-> Fortitude (already connected via 116)
// Fortitude <-> Nature (already connected via 146)

// Nature <-> Might connector
{271, N, Sector::NATURE, nullptr, "+1 STR",
 1.5f, 2.8f, {EFF(BONUS_STR, 1), NOEFF, NOEFF, NOEFF},
 {181, 26, NC, NC, NC, NC}},  // connects Nature(181) to Might(26)

// Shadow <-> Arcane (already connected via 87/200)
// Shadow <-> Finesse (already connected via 57)

// Venom <-> Might connector
{272, N, Sector::VENOM, nullptr, "+1 CON",
 -1.5f, 2.8f, {EFF(BONUS_CON, 1), NOEFF, NOEFF, NOEFF},
 {241, 23, NC, NC, NC, NC}},  // connects Venom(241) to Might(23)

// Finesse <-> Shadow extra path
{273, N, Sector::FINESSE, nullptr, "+1 DEX",
 -3.0f, -1.5f, {EFF(BONUS_DEX, 1), NOEFF, NOEFF, NOEFF},
 {56, 211, NC, NC, NC, NC}},  // connects Finesse(56) to Shadow(211)

// Center notable: Experienced
{13, NT, Sector::CENTER, "Experienced", "+8% XP gain, +2 HP",
 0.0f, -0.5f, {EFF(XP_GAIN_BONUS, 8), EFF(BONUS_HP, 2), NOEFF, NOEFF},
 {0, 2, NC, NC, NC, NC}},

// Center notable: Prepared
{14, NT, Sector::CENTER, "Prepared", "+5% potion effectiveness, +2 trap detection",
 0.0f, 0.5f, {EFF(POTION_EFFECTIVENESS, 5), EFF(TRAP_DETECTION, 2), NOEFF, NOEFF},
 {0, 3, NC, NC, NC, NC}},

// ── KEYSTONES (at sector boundaries) ────────────────────────────────
// These sit between sectors and have trade-off effects.

// Between Arcane and Shadow: Blood Magic
{260, KS, Sector::ARCANE, "Blood Magic",
 "Spells cost HP instead of MP. +30% spell power.",
 -1.5f, -3.5f, {EFF(KS_BLOOD_MAGIC, 1), NOEFF, NOEFF, NOEFF},
 {87, 200, NC, NC, NC, NC}},

// Between Finesse and Arcane: Ghost Blade
{261, KS, Sector::FINESSE, "Ghost Blade",
 "Melee attacks scale with INT instead of STR. Deal magic damage.",
 -3.5f, -1.5f, {EFF(KS_GHOST_BLADE, 1), NOEFF, NOEFF, NOEFF},
 {57, 87, NC, NC, NC, NC}},

// Between Faith and Fortitude: Iron Reflexes
{262, KS, Sector::FORTITUDE, "Iron Reflexes",
 "All dodge chance converted to armor. Can't dodge, big armor.",
 3.5f, -1.0f, {EFF(KS_IRON_REFLEXES, 1), NOEFF, NOEFF, NOEFF},
 {116, 143, NC, NC, NC, NC}},

// Between Venom and Might: Chaos Inoculation
{263, KS, Sector::VENOM, "Chaos Inoculation",
 "Immune to poison and disease. Max HP halved.",
 -1.5f, 3.5f, {EFF(KS_CHAOS_INOCULATION, 1), NOEFF, NOEFF, NOEFF},
 {27, 231, NC, NC, NC, NC}},

// Between Might and Nature: Vampiric Pact
{264, KS, Sector::MIGHT, "Vampiric Pact",
 "All healing comes from damage dealt. Potions and rest don't heal.",
 1.5f, 3.5f, {EFF(KS_VAMPIRIC_PACT, 1), NOEFF, NOEFF, NOEFF},
 {31, 170, NC, NC, NC, NC}},

// Between Shadow and Venom: Point Blank
{265, KS, Sector::SHADOW, "Point Blank",
 "+50% damage at range 1. -50% damage at range 5+.",
 -3.5f, 0.0f, {EFF(KS_POINT_BLANK, 1), NOEFF, NOEFF, NOEFF},
 {205, 232, NC, NC, NC, NC}},

// Between Faith and Arcane: Zealot
{266, KS, Sector::FAITH, "Zealot",
 "Prayer power doubled. Favor decays twice as fast.",
 1.5f, -2.5f, {EFF(KS_ZEALOT, 1), NOEFF, NOEFF, NOEFF},
 {113, 270, NC, NC, NC, NC}},

// Between Nature and Fortitude: Avatar of the Wild
{267, KS, Sector::NATURE, "Avatar of the Wild",
 "Summons inherit your stats. Cannot wear armor.",
 3.5f, 1.5f, {EFF(KS_AVATAR_OF_WILD, 1), NOEFF, NOEFF, NOEFF},
 {146, 181, NC, NC, NC, NC}},

// ── CROSS-SECTOR HYBRID NOTABLES ─────────────────────────────────────
// Reward multi-sector pathing with unique combined effects.

// Arcane + Venom: Toxic Sorcery (poison + spell synergy)
{280, NT, Sector::ARCANE, "Toxic Sorcery",
 "+10% spell dmg, +10% on-hit poison chance",
 -2.0f, -1.5f, {EFF(BONUS_SPELL_DAMAGE_PCT, 10), EFF(ON_HIT_POISON_CHANCE, 10), NOEFF, NOEFF},
 {87, 230, NC, NC, NC, NC}},

// Might + Finesse: Controlled Fury
{281, NT, Sector::MIGHT, "Controlled Fury",
 "+5% melee dmg, +3% crit chance, +5 speed",
 -0.5f, 3.0f, {EFF(BONUS_MELEE_DAMAGE_PCT, 5), EFF(BONUS_CRIT_CHANCE, 3), EFF(BONUS_SPEED, 5), NOEFF},
 {21, 57, NC, NC, NC, NC}},

// Faith + Shadow: Dark Devotion
{282, NT, Sector::FAITH, "Dark Devotion",
 "-8% prayer cost, +3% dodge, +1 spell pierce",
 2.0f, -2.5f, {EFF(PRAYER_COST_REDUCE_PCT, 8), EFF(BONUS_DODGE_CHANCE, 3), EFF(SPELL_PIERCE, 1), NOEFF},
 {113, 200, NC, NC, NC, NC}},

// Fortitude + Might: War Machine
{283, NT, Sector::FORTITUDE, "War Machine",
 "+2 armor, +2 damage, +3 HP",
 2.5f, 1.5f, {EFF(BONUS_ARMOR, 2), EFF(BONUS_DAMAGE, 2), EFF(BONUS_HP, 3), NOEFF},
 {31, 146, NC, NC, NC, NC}},

// Nature + Venom: Festering Growth
{284, NT, Sector::NATURE, "Festering Growth",
 "Kill heals 5% HP, +10% on-hit poison, +1 status duration",
 2.0f, 2.5f, {EFF(ON_KILL_HEAL_PCT, 5), EFF(ON_HIT_POISON_CHANCE, 10), EFF(STATUS_DURATION_BONUS, 1), NOEFF},
 {179, 231, NC, NC, NC, NC}},

// Finesse + Fortitude: Armored Agility
{285, NT, Sector::FINESSE, "Armored Agility",
 "+3% dodge, +2 armor, +5 HP",
 3.0f, 0.0f, {EFF(BONUS_DODGE_CHANCE, 3), EFF(BONUS_ARMOR, 2), EFF(BONUS_HP, 5), NOEFF},
 {56, 140, NC, NC, NC, NC}},

};

static constexpr int TREE_NODE_COUNT = sizeof(TREE) / sizeof(TREE[0]);

// ══════════════════════════════════════════════════════════════════════
// CLASS START NODES
// ══════════════════════════════════════════════════════════════════════

static const uint16_t CLASS_START_NODES[] = {
    // Base classes
    20,  // FIGHTER      -> Might
    50,  // ROGUE        -> Finesse
    80,  // WIZARD       -> Arcane
    50,  // RANGER       -> Finesse (Finesse/Arcane bridge)
    // Unlockable
    20,  // BARBARIAN    -> Might
    20,  // KNIGHT       -> Might/Finesse bridge
    110, // MONK         -> Faith/Nature bridge
    110, // TEMPLAR      -> Faith
    170, // DRUID        -> Nature
    110, // WAR_CLERIC   -> Faith
    200, // WARLOCK      -> Shadow (Arcane/Shadow bridge)
    140, // DWARF        -> Fortitude
    50,  // ELF          -> Finesse/Arcane bridge
    50,  // BANDIT       -> Finesse (Might/Finesse bridge)
    200, // NECROMANCER  -> Shadow (Arcane/Shadow)
    80,  // SCHEMA_MONK  -> Arcane
    230, // HERETIC      -> Venom (Shadow/Venom)
    // Monster-themed
    140, // WYRMKIN      -> Fortitude
    200, // REVENANT     -> Shadow/Venom
    50,  // SERPENTINE   -> Finesse
    20,  // TROLLBLOOD   -> Might (Might/Fortitude)
};

// ══════════════════════════════════════════════════════════════════════
// IMPLEMENTATION
// ══════════════════════════════════════════════════════════════════════

namespace passive_tree {

const PassiveNode* nodes() { return TREE; }
int node_count() { return TREE_NODE_COUNT; }

const PassiveNode* find_node(uint16_t id) {
    for (int i = 0; i < TREE_NODE_COUNT; i++) {
        if (TREE[i].id == id) return &TREE[i];
    }
    return nullptr;
}

bool is_connected(const PassiveTreeState& state, uint16_t node_id) {
    if (node_id == state.start_node) return true;

    const PassiveNode* node = find_node(node_id);
    if (!node) return false;

    for (int i = 0; i < 6; i++) {
        uint16_t conn = node->connections[i];
        if (conn == NO_CONN) continue;
        if (state.is_allocated(conn)) return true;
    }
    return false;
}

bool can_allocate(const PassiveTreeState& state, uint16_t node_id) {
    if (state.points_available <= 0) return false;
    if (state.is_allocated(node_id)) return false;
    if (find_node(node_id) == nullptr) return false;
    return is_connected(state, node_id);
}

bool skill_requirement_met(uint16_t node_id, const Skills& skills) {
    auto* node = find_node(node_id);
    if (!node) return true;
    if (node->required_skill >= SKILL_COUNT) return true; // no requirement
    return skills.get_level(static_cast<SkillId>(node->required_skill)) >= node->required_skill_level;
}

uint16_t start_node_for_class(ClassId cls) {
    int idx = static_cast<int>(cls);
    if (idx < 0 || idx >= static_cast<int>(ClassId::COUNT)) return 0;
    return CLASS_START_NODES[idx];
}

TreeBonuses compute_bonuses(const PassiveTreeState& state) {
    TreeBonuses b{};

    for (int i = 0; i < TREE_NODE_COUNT; i++) {
        if (!state.is_allocated(TREE[i].id)) continue;

        for (int e = 0; e < 4; e++) {
            auto& eff = TREE[i].effects[e];
            if (eff.type == EffectType::NONE) continue;

            switch (eff.type) {
                case EffectType::BONUS_STR: b.str += eff.value; break;
                case EffectType::BONUS_DEX: b.dex += eff.value; break;
                case EffectType::BONUS_CON: b.con += eff.value; break;
                case EffectType::BONUS_INT: b.intel += eff.value; break;
                case EffectType::BONUS_WIL: b.wil += eff.value; break;
                case EffectType::BONUS_PER: b.per += eff.value; break;
                case EffectType::BONUS_CHA: b.cha += eff.value; break;
                case EffectType::BONUS_HP: b.hp += eff.value; break;
                case EffectType::BONUS_MP: b.mp += eff.value; break;
                case EffectType::BONUS_SPEED: b.speed += eff.value; break;
                case EffectType::BONUS_DAMAGE: b.damage += eff.value; break;
                case EffectType::BONUS_ARMOR: b.armor += eff.value; break;
                case EffectType::BONUS_CRIT_CHANCE: b.crit_chance += eff.value; break;
                case EffectType::BONUS_DODGE_CHANCE: b.dodge_chance += eff.value; break;
                case EffectType::BONUS_MELEE_DAMAGE_PCT: b.melee_dmg_pct += eff.value; break;
                case EffectType::BONUS_SPELL_DAMAGE_PCT: b.spell_dmg_pct += eff.value; break;
                case EffectType::BONUS_FIRE_RESIST: b.fire_resist += eff.value; break;
                case EffectType::BONUS_POISON_RESIST: b.poison_resist += eff.value; break;
                case EffectType::BONUS_BLEED_RESIST: b.bleed_resist += eff.value; break;
                case EffectType::BONUS_ALL_RESIST:
                    b.fire_resist += eff.value;
                    b.poison_resist += eff.value;
                    b.bleed_resist += eff.value;
                    break;
                case EffectType::ON_KILL_HEAL_PCT: b.on_kill_heal_pct += eff.value; break;
                case EffectType::ON_HIT_BLEED_CHANCE: b.on_hit_bleed_chance += eff.value; break;
                case EffectType::ON_HIT_POISON_CHANCE: b.on_hit_poison_chance += eff.value; break;
                case EffectType::ON_CRIT_BONUS_DAMAGE: b.on_crit_bonus_dmg += eff.value; break;
                case EffectType::LOW_HP_DAMAGE_BONUS: b.low_hp_dmg_bonus += eff.value; break;
                case EffectType::DAMAGE_VS_LOW_HP: b.dmg_vs_low_hp += eff.value; break;
                case EffectType::SPELL_COST_REDUCE_PCT: b.spell_cost_reduce += eff.value; break;
                case EffectType::PRAYER_COST_REDUCE_PCT: b.prayer_cost_reduce += eff.value; break;
                case EffectType::REST_EFFICIENCY: b.rest_efficiency += eff.value; break;
                case EffectType::POTION_EFFECTIVENESS: b.potion_effectiveness += eff.value; break;
                case EffectType::XP_GAIN_BONUS: b.xp_gain_bonus += eff.value; break;
                case EffectType::TRAP_DETECTION: b.trap_detection += eff.value; break;
                case EffectType::IDENTIFY_ON_PICKUP_PCT: b.identify_on_pickup += eff.value; break;
                case EffectType::BONUS_FOV: b.fov_bonus += eff.value; break;
                // Class verb amplifiers
                case EffectType::STATUS_DURATION_BONUS: b.status_duration_bonus += eff.value; break;
                case EffectType::SHAPESHIFT_DAMAGE_BONUS: b.shapeshift_dmg_bonus += eff.value; break;
                case EffectType::FURY_CHAIN_BONUS: b.fury_chain_bonus += eff.value; break;
                case EffectType::SIPHON_BONUS: b.siphon_bonus_pct += eff.value; break;
                case EffectType::EXPLODE_DAMAGE_BONUS: b.explode_dmg_bonus += eff.value; break;
                case EffectType::COUNTER_DAMAGE_BONUS: b.counter_dmg_bonus_pct += eff.value; break;
                case EffectType::STEALTH_OPENER_BONUS: b.stealth_opener_bonus_pct += eff.value; break;
                case EffectType::BREATH_DAMAGE_BONUS: b.breath_dmg_bonus += eff.value; break;
                // Notable mechanics
                case EffectType::RIPOSTE: b.riposte = true; break;
                case EffectType::LAST_STAND: b.last_stand = true; break;
                case EffectType::MANA_SIPHON: b.mana_siphon_pct += eff.value; break;
                case EffectType::SPELL_PIERCE: b.spell_pierce += eff.value; break;
                case EffectType::PATIENT_HUNTER: b.patient_hunter_pct += eff.value; break;
                // Keystones
                case EffectType::KS_BLOOD_MAGIC: b.blood_magic = true; break;
                case EffectType::KS_GHOST_BLADE: b.ghost_blade = true; break;
                case EffectType::KS_ZEALOT: b.zealot = true; break;
                case EffectType::KS_IRON_REFLEXES: b.iron_reflexes = true; break;
                case EffectType::KS_CHAOS_INOCULATION: b.chaos_inoculation = true; break;
                case EffectType::KS_POINT_BLANK: b.point_blank = true; break;
                case EffectType::KS_AVATAR_OF_WILD: b.avatar_of_wild = true; break;
                case EffectType::KS_VAMPIRIC_PACT: b.vampiric_pact = true; break;
                // Capstones
                case EffectType::CAP_WHIRLWIND: b.cap_whirlwind_cd = eff.value; break;
                case EffectType::CAP_TIME_SLIP: b.cap_time_slip_cd = eff.value; break;
                case EffectType::CAP_ARCANE_OVERLOAD: b.cap_arcane_overload_cd = eff.value; break;
                case EffectType::CAP_DIVINE_INTERVENTION: b.cap_divine_intervention_cd = eff.value; break;
                case EffectType::CAP_UNBREAKABLE: b.cap_unbreakable_cd = eff.value; break;
                case EffectType::CAP_ASPECT_OF_BEAST: b.cap_aspect_of_beast_cd = eff.value; break;
                case EffectType::CAP_DEATH_MARK: b.cap_death_mark_cd = eff.value; break;
                case EffectType::CAP_PANDEMIC: b.cap_pandemic_cd = eff.value; break;
                default: break;
            }
        }
    }
    return b;
}

} // namespace passive_tree
