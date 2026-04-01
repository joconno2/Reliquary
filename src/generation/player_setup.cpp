#include "generation/player_setup.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/player.h"
#include "components/stats.h"
#include "components/energy.h"
#include "components/inventory.h"
#include "components/god.h"
#include "components/class_def.h"
#include "components/spellbook.h"
#include "components/status_effect.h"
#include "components/buff.h"
#include "components/disease.h"
#include "components/background.h"
#include "components/traits.h"
#include "components/item.h"
#include "core/spritesheet.h"

namespace player_setup {

PlayerResult create_player(World& world, const CharacterBuild& build,
                           int start_x, int start_y) {
    auto& cls = get_class_info(build.class_id);
    auto& god = get_god_info(build.god);
    auto& bg  = get_background_info(build.background);

    Entity player = world.create();
    world.add<Player>(player);
    world.add<Position>(player, {start_x, start_y});

    // Subtle god-colored sprite tint
    SDL_Color player_tint = {255, 255, 255, 255};
    if (build.god != GodId::NONE) {
        auto& gi = get_god_info(build.god);
        // Blend toward god color ~20% (subtle tint, not a full recolor)
        player_tint.r = static_cast<Uint8>(255 - (255 - gi.color.r) / 5);
        player_tint.g = static_cast<Uint8>(255 - (255 - gi.color.g) / 5);
        player_tint.b = static_cast<Uint8>(255 - (255 - gi.color.b) / 5);
    }
    world.add<Renderable>(player, {SHEET_ROGUES, cls.sprite_x, cls.sprite_y,
                                     player_tint, 10});
    world.add<Energy>(player, {0, 100});
    world.add<Inventory>(player);
    world.add<GodAlignment>(player, {build.god, 0});
    world.add<StatusEffects>(player);
    world.add<Diseases>(player);
    world.add<Buffs>(player);

    // --- Stats ---
    Stats player_stats;
    player_stats.name = build.name;
    player_stats.hp = cls.hp + god.bonus_hp + bg.bonus_hp;
    player_stats.hp_max = cls.hp + god.bonus_hp + bg.bonus_hp;
    player_stats.mp = cls.mp + god.bonus_mp;
    player_stats.mp_max = cls.mp + god.bonus_mp;
    player_stats.set_attr(Attr::STR, cls.str   + god.str_bonus + bg.str_bonus);
    player_stats.set_attr(Attr::DEX, cls.dex   + god.dex_bonus + bg.dex_bonus);
    player_stats.set_attr(Attr::CON, cls.con   + god.con_bonus + bg.con_bonus);
    player_stats.set_attr(Attr::INT, cls.intel + god.int_bonus + bg.int_bonus);
    player_stats.set_attr(Attr::WIL, cls.wil   + god.wil_bonus + bg.wil_bonus);
    player_stats.set_attr(Attr::PER, cls.per   + god.per_bonus + bg.per_bonus);
    player_stats.set_attr(Attr::CHA, cls.cha   + god.cha_bonus + bg.cha_bonus);
    player_stats.base_damage = cls.base_damage + bg.bonus_damage;
    player_stats.base_speed = 100;

    // Apply trait stat + gameplay modifiers
    for (TraitId tid : build.traits) {
        const TraitInfo& tr = get_trait_info(tid);
        player_stats.set_attr(Attr::STR, player_stats.attr(Attr::STR) + tr.str_mod);
        player_stats.set_attr(Attr::DEX, player_stats.attr(Attr::DEX) + tr.dex_mod);
        player_stats.set_attr(Attr::CON, player_stats.attr(Attr::CON) + tr.con_mod);
        player_stats.set_attr(Attr::INT, player_stats.attr(Attr::INT) + tr.int_mod);
        player_stats.set_attr(Attr::WIL, player_stats.attr(Attr::WIL) + tr.wil_mod);
        player_stats.set_attr(Attr::PER, player_stats.attr(Attr::PER) + tr.per_mod);
        player_stats.set_attr(Attr::CHA, player_stats.attr(Attr::CHA) + tr.cha_mod);
        player_stats.hp_max += tr.bonus_hp;
        player_stats.hp += tr.bonus_hp;
        player_stats.natural_armor += tr.bonus_natural_armor;
        player_stats.base_speed += tr.bonus_speed;
        player_stats.fire_resist += tr.fire_resist;
        player_stats.poison_resist += tr.poison_resist;
        player_stats.bleed_resist += tr.bleed_resist;
    }

    // God-specific resistances and passive stat mods
    switch (build.god) {
        case GodId::SOLETH:
            player_stats.fire_resist = 30;
            break;
        case GodId::KHAEL:
            player_stats.poison_resist = 25;
            break;
        case GodId::THALARA:
            player_stats.poison_resist = 100; // immune
            player_stats.fire_resist = -20;   // weakness
            break;
        case GodId::SYTHARA:
            player_stats.poison_resist = 100; // immune -- you ARE the plague
            break;
        case GodId::GATHRUUN:
            player_stats.natural_armor += 3; // stone's endurance
            break;
        default: break;
    }

    world.add<Stats>(player, std::move(player_stats));

    // --- Starting spells based on class ---
    Spellbook book;
    switch (build.class_id) {
        case ClassId::WIZARD:
            book.learn(SpellId::SPARK);
            book.learn(SpellId::FORCE_BOLT);
            book.learn(SpellId::IDENTIFY);
            break;
        case ClassId::RANGER:
            book.learn(SpellId::DETECT_MONSTERS);
            break;
        case ClassId::DRUID:
            book.learn(SpellId::ENTANGLE);
            book.learn(SpellId::MINOR_HEAL);
            break;
        case ClassId::WAR_CLERIC:
            book.learn(SpellId::MINOR_HEAL);
            book.learn(SpellId::MAJOR_HEAL);
            break;
        case ClassId::WARLOCK:
            book.learn(SpellId::DRAIN_LIFE);
            book.learn(SpellId::FEAR);
            break;
        case ClassId::NECROMANCER:
            book.learn(SpellId::DRAIN_LIFE);
            book.learn(SpellId::FEAR);
            book.learn(SpellId::RAISE_DEAD);
            break;
        case ClassId::SCHEMA_MONK:
            book.learn(SpellId::HARDEN_SKIN);
            break;
        case ClassId::TEMPLAR:
            book.learn(SpellId::MINOR_HEAL);
            break;
        default:
            book.learn(SpellId::MINOR_HEAL); // everyone gets minor heal
            break;
    }
    world.add<Spellbook>(player, std::move(book));

    // --- Starting gear per class ---
    auto give_item = [&](const char* name, const char* desc, ItemType type, EquipSlot slot,
                          int sx, int sy, int dmg, int arm, int atk, int dge, int range_val) {
        Entity ie = world.create();
        Item item;
        item.name = name; item.description = desc;
        item.type = type; item.slot = slot;
        item.damage_bonus = dmg; item.armor_bonus = arm;
        item.attack_bonus = atk; item.dodge_bonus = dge;
        item.range = range_val;
        item.identified = true; item.gold_value = 0;
        world.add<Item>(ie, std::move(item));
        world.add<Renderable>(ie, {SHEET_ITEMS, sx, sy, {255,255,255,255}, 1});
        auto& inv = world.get<Inventory>(player);
        inv.add(ie);
        inv.equip(world.get<Item>(ie).slot, ie);
    };

    switch (build.class_id) {
        case ClassId::FIGHTER:
            give_item("short sword", "A reliable sidearm.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 0, 3, 0, 0, 0, 0);
            give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
            break;
        case ClassId::ROGUE:
            give_item("dagger", "A short, sharp blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0, 2, 0, 1, 1, 0);
            give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
            break;
        case ClassId::WIZARD:
            give_item("dagger", "A short, sharp blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0, 2, 0, 0, 0, 0);
            break;
        case ClassId::RANGER:
            give_item("short bow", "Light and quick.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 9, 3, 0, 1, 0, 6);
            give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
            break;
        case ClassId::BARBARIAN:
            give_item("battle axe", "Heavy. Splits bone.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 3, 6, 0, -1, 0, 0);
            break;
        case ClassId::KNIGHT:
            give_item("long sword", "A well-balanced blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0, 5, 0, 0, 0, 0);
            give_item("buckler", "A small, round shield.", ItemType::SHIELD, EquipSlot::OFF_HAND, 0, 11, 0, 2, 0, 1, 0);
            give_item("chain mail", "Rings of iron.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 3, 12, 0, 4, 0, -1, 0);
            break;
        case ClassId::MONK:
        case ClassId::SCHEMA_MONK:
            // Unarmed specialists -- no gear
            break;
        case ClassId::TEMPLAR:
            give_item("mace", "Blunt and merciless.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5, 4, 0, 0, 0, 0);
            give_item("buckler", "A small, round shield.", ItemType::SHIELD, EquipSlot::OFF_HAND, 0, 11, 0, 2, 0, 1, 0);
            give_item("chain mail", "Rings of iron.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 3, 12, 0, 4, 0, -1, 0);
            break;
        case ClassId::DRUID:
            give_item("spear", "Long reach.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 6, 4, 0, 1, 0, 0);
            give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
            break;
        case ClassId::WAR_CLERIC:
            give_item("mace", "Blunt and merciless.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 5, 4, 0, 0, 0, 0);
            give_item("chain mail", "Rings of iron.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 3, 12, 0, 4, 0, -1, 0);
            break;
        case ClassId::WARLOCK:
        case ClassId::NECROMANCER:
            give_item("dagger", "A short, sharp blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0, 2, 0, 0, 0, 0);
            break;
        case ClassId::DWARF:
            give_item("war hammer", "Crushes plate.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 1, 4, 8, 0, -1, 0, 0);
            give_item("chain mail", "Rings of iron.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 3, 12, 0, 4, 0, -1, 0);
            break;
        case ClassId::ELF:
            give_item("long sword", "A well-balanced blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 3, 0, 5, 0, 0, 0, 0);
            give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
            break;
        case ClassId::BANDIT:
            give_item("dagger", "A short, sharp blade.", ItemType::WEAPON, EquipSlot::MAIN_HAND, 0, 0, 2, 0, 1, 1, 0);
            give_item("leather armor", "Supple hide.", ItemType::ARMOR_CHEST, EquipSlot::CHEST, 1, 12, 0, 2, 0, 0, 0);
            break;
        case ClassId::HERETIC:
            // Godless -- starts with nothing
            break;
        default: break;
    }

    return {player, build.traits};
}

} // namespace player_setup
