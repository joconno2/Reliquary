#include "systems/npc_interaction.h"
#include "systems/god_system.h"
#include "core/ecs.h"
#include "core/rng.h"
#include "core/audio.h"
#include "ui/message_log.h"
#include "ui/shop_screen.h"
#include "ui/quest_offer.h"
#include "ui/levelup_screen.h"
#include "systems/particles.h"
#include "components/npc.h"
#include "components/god.h"
#include "components/player.h"
#include "components/stats.h"
#include "components/position.h"
#include "components/dynamic_quest.h"
#include "components/passive_tree.h"
#include "data/world_data.h"
#include "components/quest.h"
#include "components/status_effect.h"
#include "core/audio.h"
#include "save/meta.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace npc_interaction {

bool interact(Context& ctx, Entity target, int target_x, int target_y) {
    if (!ctx.world.has<NPC>(target)) return false;
    auto& npc = ctx.world.get<NPC>(target);

    // Check if bumping this NPC satisfies any delivery quest's target town
    {
        auto& dq_pool = ctx.world.pool<DynamicQuest>();
        for (size_t di = 0; di < dq_pool.size(); di++) {
            auto& dq = dq_pool.at_index(di);
            if (dq.accepted && !dq.completed && dq.target_town_x >= 0 && !dq.reached_target) {
                if (std::abs(target_x - dq.target_town_x) < 30 &&
                    std::abs(target_y - dq.target_town_y) < 30) {
                    dq.reached_target = true;
                }
            }
        }
    }

    // Shop difficulty from town distance to Thornwall
    auto calc_shop_diff = [&]() -> int {
        if (ctx.dungeon_level > 0) return 0;
        if (!ctx.world.has<Position>(target)) return 0;
        auto& sp = ctx.world.get<Position>(target);
        float d = std::sqrt(static_cast<float>((sp.x - 1000) * (sp.x - 1000) +
                                                (sp.y - 750) * (sp.y - 750)));
        return std::min(8, static_cast<int>(d / 80.0f));
    };

    // Province god for shop stock variety
    auto calc_province_god = [&]() -> GodId {
        if (!ctx.world.has<Position>(target)) return GodId::NONE;
        auto& sp = ctx.world.get<Position>(target);
        return get_town_god(sp.x, sp.y);
    };

    // Check if player has a trait (via Player component)
    auto has_trait = [&](TraitId t) -> bool {
        if (!ctx.world.has<Player>(ctx.player)) return false;
        for (auto tid : ctx.world.get<Player>(ctx.player).traits)
            if (tid == t) return true;
        return false;
    };

    // Shop pricing: excommunication, god faction, charisma, traits
    auto calc_shop_price_mult = [&]() -> int {
        int mult = 100;
        if (ctx.world.has<GodAlignment>(ctx.player)) {
            auto& a = ctx.world.get<GodAlignment>(ctx.player);
            if (a.god != GodId::NONE && a.favor <= -100) return 200;
            // Ixuul: shops charge 2x (god of chaos, mistrusted)
            if (a.god == GodId::IXUUL) mult = 200;
            // Sythara: towns charge 3x (feared plague-bringer)
            else if (a.god == GodId::SYTHARA) mult = 300;
            else if (npc.god_affiliation != GodId::NONE && a.god != GodId::NONE) {
                if (a.god == npc.god_affiliation) mult = 85;
                else mult = 125;
            }
        }
        // CHA affects prices: Charming trait gives CHA bonus → cheaper; Ill-Favored → expensive
        if (ctx.world.has<Stats>(ctx.player)) {
            int cha = ctx.world.get<Stats>(ctx.player).attr(Attr::CHA);
            if (cha >= 2) mult = mult * 90 / 100;       // high CHA: 10% discount
            else if (cha <= -2) mult = mult * 110 / 100; // low CHA: 10% markup
        }
        return mult;
    };

    // Innkeeper — rest and heal for gold
    if (npc.role == NPCRole::INNKEEPER && ctx.world.has<Stats>(ctx.player)) {
        int cost = 10;
        auto& ps = ctx.world.get<Stats>(ctx.player);
        bool full_hp = (ps.hp >= ps.hp_max && ps.mp >= ps.mp_max);
        bool night = ctx.dungeon_level == 0 && (ctx.game_turn % 200) >= 140;

        if (full_hp && !night) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s: \"You look well enough. Come back when you need a bed.\"",
                     npc.name.c_str());
            ctx.log.add(buf, {180, 170, 140, 255});
            return true;
        }
        if (ctx.gold < cost) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s: \"A room costs %d gold. You don't have it.\"",
                     npc.name.c_str(), cost);
            ctx.log.add(buf, {180, 120, 120, 255});
            return true;
        }
        // Pay and rest
        ctx.gold -= cost;
        int healed = ps.hp_max - ps.hp;
        int mp_restored = ps.mp_max - ps.mp;
        ps.hp = ps.hp_max;
        ps.mp = ps.mp_max;
        // Clear status effects
        if (ctx.world.has<StatusEffects>(ctx.player))
            ctx.world.get<StatusEffects>(ctx.player).effects.clear();

        // Advance to morning if night
        if (night) {
            int turns_left = 200 - (ctx.game_turn % 200);
            ctx.game_turn += turns_left;
        }

        char buf[128];
        if (night)
            snprintf(buf, sizeof(buf),
                     "%s: \"Sleep well.\" (-%dg, +%d HP, +%d MP, rested until morning)",
                     npc.name.c_str(), cost, healed, mp_restored);
        else
            snprintf(buf, sizeof(buf),
                     "%s: \"Take your time.\" (-%dg, +%d HP, +%d MP)",
                     npc.name.c_str(), cost, healed, mp_restored);
        ctx.log.add(buf, {140, 200, 160, 255});
        ctx.audio.play(SfxId::REST);
        ctx.meta.total_hp_healed += healed;
        return true;
    }

    // Cannibal trait: shops refuse to trade with you
    if (npc.role == NPCRole::SHOPKEEPER && has_trait(TraitId::CANNIBAL)) {
        ctx.log.add("The merchant recoils. \"Get away from me, flesh-eater.\"", {200, 120, 80, 255});
        ctx.log.add("[Cannibal] Shops refuse to trade with you.", {180, 140, 100, 255});
        return true;
    }

    // Shopkeeper — open shop screen (unless they have a dynamic quest to offer)
    if (npc.role == NPCRole::SHOPKEEPER && !ctx.world.has<DynamicQuest>(target)) {
        // Shops closed at night (overworld only)
        bool night = ctx.dungeon_level == 0 && (ctx.game_turn % 200) >= 140;
        if (night) {
            char nbuf[128];
            snprintf(nbuf, sizeof(nbuf), "%s: \"Shop's closed. Come back in the morning.\"", npc.name.c_str());
            ctx.log.add(nbuf, {160, 150, 130, 255});
            return true;
        }
        int pm = calc_shop_price_mult();
        if (pm >= 300)
            ctx.log.add("[Sythara] The merchant backs away in fear. Prices tripled.", {120, 180, 60, 255});
        else if (pm >= 200)
            ctx.log.add("[Ixuul] The merchant mistrusts you. Prices doubled.", {180, 100, 255, 255});
        else if (pm > 100)
            ctx.log.add("The merchant seems wary of your faith. Prices are higher.", {200, 180, 120, 255});
        else if (pm < 100)
            ctx.log.add("The merchant gives you a knowing nod. A fellow believer.", {140, 200, 160, 255});
        ctx.shop_screen.open(ctx.player, ctx.world, ctx.rng, &ctx.gold, calc_shop_diff(), pm, calc_province_god());
        return true;
    }
    // Merchant with dynamic quest: show dialogue + quest, then can shop next time
    if (npc.role == NPCRole::SHOPKEEPER && ctx.world.has<DynamicQuest>(target)) {
        auto& dq = ctx.world.get<DynamicQuest>(target);
        if (dq.completed) {
            int pm = calc_shop_price_mult();
            if (pm >= 300)
                ctx.log.add("[Sythara] The merchant backs away in fear. Prices tripled.", {120, 180, 60, 255});
            else if (pm >= 200)
                ctx.log.add("[Ixuul] The merchant mistrusts you. Prices doubled.", {180, 100, 255, 255});
            else if (pm > 100)
                ctx.log.add("The merchant seems wary of your faith. Prices are higher.", {200, 180, 120, 255});
            else if (pm < 100)
                ctx.log.add("The merchant gives you a knowing nod. A fellow believer.", {140, 200, 160, 255});
            ctx.shop_screen.open(ctx.player, ctx.world, ctx.rng, &ctx.gold, calc_shop_diff(), pm, calc_province_god());
            return true;
        }
    }

    // World-reactive dialogue: NPCs comment on quest progress (30% chance per bump)
    if (ctx.rng.chance(30)) {
        const char* reaction = nullptr;
        auto qs = [&](QuestId q) { return ctx.journal.get_state(q); };

        // Late game: fragments and Sepulchre
        if (qs(QuestId::MQ_09_CLAIM_RELIQUARY) == QuestState::FINISHED)
            reaction = "You did it. I can feel the difference. The air is lighter.";
        else if (qs(QuestId::MQ_08_ENTER_SEPULCHRE) == QuestState::ACTIVE)
            reaction = "You're going into the Sepulchre? Gods help you.";
        else if (qs(QuestId::MQ_07_BREAK_SEAL) == QuestState::FINISHED)
            reaction = "Word is the seal at Hollowgate broke open. People are scared.";
        else if (qs(QuestId::MQ_06_THIRD_FRAGMENT) == QuestState::FINISHED)
            reaction = "Three fragments. I can see it in your eyes. You're changing.";
        else if (qs(QuestId::MQ_06_THIRD_FRAGMENT) == QuestState::FINISHED)
            reaction = "Two fragments. The ground trembles less now. Or more. I can't tell.";
        else if (qs(QuestId::MQ_05_SECOND_FRAGMENT) == QuestState::FINISHED)
            reaction = "I heard you found something in the Catacombs. Something old.";
        else if (qs(QuestId::MQ_05_SECOND_FRAGMENT) == QuestState::FINISHED)
            reaction = "The Catacombs gate is open? That's been sealed since before I was born.";
        else if (qs(QuestId::MQ_05_SECOND_FRAGMENT) == QuestState::FINISHED)
            reaction = "A key from the ice? The old stories are true, then.";
        else if (qs(QuestId::MQ_03_FIRST_FRAGMENT) == QuestState::FINISHED)
            reaction = "Stonekeep. I heard the inscription drove the last person who read it mad.";
        else if (qs(QuestId::MQ_04_SAGE_COUNSEL) == QuestState::FINISHED)
            reaction = "Captain Voss sent word. Whatever you told him has the garrison on alert.";
        else if (qs(QuestId::MQ_03_FIRST_FRAGMENT) == QuestState::FINISHED)
            reaction = "You found the tablet? The scholars will want to hear about that.";
        else if (qs(QuestId::MQ_01_BARROW_WIGHT) == QuestState::FINISHED)
            reaction = "You cleared the Barrow? We've been hearing less noise from that direction.";

        // God-aware reactions (if no quest reaction fired)
        if (!reaction && ctx.world.has<GodAlignment>(ctx.player)) {
            auto& ga = ctx.world.get<GodAlignment>(ctx.player);
            if (ga.favor >= 75) {
                auto& gi = get_god_info(ga.god);
                static char gbuf[128];
                snprintf(gbuf, sizeof(gbuf), "There's something about you. %s's mark is strong.", gi.name);
                reaction = gbuf;
            } else if (ga.favor <= -50 && ga.god != GodId::NONE) {
                reaction = "Something's wrong with your brand. It's dimmer than it should be.";
            }
        }

        if (reaction) {
            char rbuf[256];
            snprintf(rbuf, sizeof(rbuf), "%s: \"%s\"", npc.name.c_str(), reaction);
            ctx.log.add(rbuf, {200, 190, 150, 255});
        }
    }

    // Show NPC dialogue — cycle through lines on repeat visits
    const std::string& line = npc.next_line();
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: \"%s\"", npc.name.c_str(), line.c_str());
    ctx.log.add(buf, {180, 180, 140, 255});

    // God-aware NPC reactions (priests/scholars react to player's god)
    if (npc.role == NPCRole::PRIEST && ctx.world.has<GodAlignment>(ctx.player)) {
        auto& align = ctx.world.get<GodAlignment>(ctx.player);
        if (align.favor <= -100) {
            auto& ginfo = get_god_info(align.god);
            char gbuf[128];
            snprintf(gbuf, sizeof(gbuf),
                "The priest recoils. \"Begone, excommunicate! %s has forsaken you!\"", ginfo.name);
            ctx.log.add(gbuf, {220, 60, 60, 255});
            return true;
        }
        // Hostile faction: Soleth priests attack Ixuul followers on sight
        if (npc.god_affiliation == GodId::SOLETH && align.god == GodId::IXUUL) {
            ctx.log.add("\"Servant of the Formless! The Pale Flame will purge you!\"",
                     {255, 220, 100, 255});
            return true;
        }
        // Same-god priest: free healing
        if (npc.god_affiliation != GodId::NONE && align.god == npc.god_affiliation) {
            auto& ginfo = get_god_info(align.god);
            auto& ps = ctx.world.get<Stats>(ctx.player);
            int heal = std::min(10, ps.hp_max - ps.hp);
            ps.hp += heal;
            char gbuf[128];
            if (heal > 0)
                snprintf(gbuf, sizeof(gbuf),
                    "\"Welcome, child of %s. Be restored.\" (+%d HP)", ginfo.name, heal);
            else
                snprintf(gbuf, sizeof(gbuf),
                    "\"Walk in %s's light, faithful one.\"", ginfo.name);
            ctx.log.add(gbuf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
        }
        // Rival-god priest: wary
        else if (npc.god_affiliation != GodId::NONE && align.god != GodId::NONE
                 && align.god != npc.god_affiliation) {
            auto& ninfo = get_god_info(npc.god_affiliation);
            auto& pinfo = get_god_info(align.god);
            char gbuf[128];
            snprintf(gbuf, sizeof(gbuf),
                "\"This is %s's domain. Followers of %s are... tolerated.\"", ninfo.name, pinfo.name);
            ctx.log.add(gbuf, {180, 140, 140, 255});
        }
        // Specific god warnings (flavor)
        else if (align.god == GodId::IXUUL) {
            ctx.log.add("The priest eyes you warily. \"Your god is... unwelcome here.\"",
                     {180, 140, 140, 255});
        } else if (align.god == GodId::YASHKHET) {
            ctx.log.add("The priest flinches at your scars. \"The Wound's followers unsettle me.\"",
                     {180, 140, 140, 255});
        } else if (align.god == GodId::NONE) {
            ctx.log.add("The priest looks at you with pity. \"No god watches over you.\"",
                     {160, 150, 140, 255});
        } else if (align.favor > 50) {
            auto& ginfo = get_god_info(align.god);
            char gbuf[128];
            snprintf(gbuf, sizeof(gbuf),
                "The priest nods respectfully. \"%s's favor is strong in you.\"", ginfo.name);
            ctx.log.add(gbuf, {160, 180, 200, 255});
        }
    }

    // Show direction hint if quest is active
    if (npc.quest_id >= 0) {
        auto hint_qid = static_cast<QuestId>(npc.quest_id);
        if (ctx.journal.has_quest(hint_qid) &&
            ctx.journal.get_state(hint_qid) == QuestState::ACTIVE) {
            const char* hint = get_quest_hint(hint_qid);
            if (hint) {
                char hbuf[256];
                snprintf(hbuf, sizeof(hbuf), "%s: \"%s\"", npc.name.c_str(), hint);
                ctx.log.add(hbuf, {200, 190, 150, 255});
            }
        }
    }

    // Quest giving — static quests with prerequisite chaining
    if (npc.quest_id >= 0) {
        auto qid = static_cast<QuestId>(npc.quest_id);

        // Check prerequisite: for main quests, the previous quest must be FINISHED
        auto quest_prereq = [](QuestId id) -> QuestId {
            int idx = static_cast<int>(id);
            if (idx <= 0 || idx > static_cast<int>(QuestId::MQ_09_CLAIM_RELIQUARY))
                return QuestId::COUNT;
            return static_cast<QuestId>(idx - 1);
        };
        auto prereq = quest_prereq(qid);
        bool prereq_met = (prereq == QuestId::COUNT) ||
                          (ctx.journal.has_quest(prereq) &&
                           ctx.journal.get_state(prereq) == QuestState::FINISHED);

        if (prereq_met && !ctx.journal.has_quest(qid)) {
            // Show quest offer modal
            ctx.quest_offer.show(qid, npc.name);
            ctx.pending_quest_npc = target;
        } else if (ctx.journal.get_state(qid) == QuestState::COMPLETE) {
            // Turn in: COMPLETE -> FINISHED
            ctx.journal.set_state(qid, QuestState::FINISHED);
            auto& qinfo = get_quest_info(qid);
            char qbuf[128];
            snprintf(qbuf, sizeof(qbuf), "Quest complete: %s (+%dxp, +%dgold)",
                     qinfo.name, qinfo.xp_reward, qinfo.gold_reward);
            ctx.log.add(qbuf, {120, 220, 120, 255});
            // Show completion text
            if (qinfo.complete_text)
                ctx.log.add(qinfo.complete_text, {180, 200, 160, 255});
            ctx.audio.play(SfxId::QUEST);
            ctx.gold += qinfo.gold_reward;
            god_system::adjust_favor(ctx.world, ctx.player, ctx.log, 5);
            if (ctx.world.has<Stats>(ctx.player) && qinfo.xp_reward > 0) {
                if (ctx.world.get<Stats>(ctx.player).grant_xp(qinfo.xp_reward)) {
                    if (ctx.world.has<PassiveTreeState>(ctx.player))
                        ctx.world.get<PassiveTreeState>(ctx.player).grant_point();
                    ctx.audio.play(SfxId::LEVELUP);
                    { auto& lp = ctx.world.get<Position>(ctx.player); ctx.particles.levelup_effect(lp.x, lp.y); }
                }
            }
            // Clear quest marker (set quest_id to -1 so NPC stops offering)
            npc.quest_id = -1;
        } else if (ctx.journal.has_quest(qid) &&
                   ctx.journal.get_state(qid) == QuestState::ACTIVE &&
                   is_auto_complete_quest(qid)) {
            // "Talk to NPC" quests auto-complete on interaction
            auto& qinfo = get_quest_info(qid);
            ctx.journal.set_state(qid, QuestState::FINISHED);
            char qbuf[128];
            snprintf(qbuf, sizeof(qbuf), "Quest complete: %s (+%dxp, +%dgold)",
                     qinfo.name, qinfo.xp_reward, qinfo.gold_reward);
            ctx.log.add(qbuf, {120, 220, 120, 255});
            if (qinfo.complete_text)
                ctx.log.add(qinfo.complete_text, {180, 200, 160, 255});
            ctx.audio.play(SfxId::QUEST);
            ctx.gold += qinfo.gold_reward;
            god_system::adjust_favor(ctx.world, ctx.player, ctx.log, 5);
            if (ctx.world.has<Stats>(ctx.player) && qinfo.xp_reward > 0)
                ctx.world.get<Stats>(ctx.player).grant_xp(qinfo.xp_reward);
            npc.quest_id = -1;
        } else if (ctx.journal.has_quest(qid) &&
                   ctx.journal.get_state(qid) == QuestState::ACTIVE) {
            // Active non-auto quest: NPC reminds you of the objective
            auto& qinfo = get_quest_info(qid);
            ctx.log.add(qinfo.objective, {180, 175, 150, 255});
        } else if (ctx.journal.has_quest(qid) &&
                   ctx.journal.get_state(qid) == QuestState::FINISHED) {
            // Already finished this quest, give post-completion line
            auto& qinfo = get_quest_info(qid);
            if (qinfo.complete_text)
                ctx.log.add(qinfo.complete_text, {160, 160, 140, 255});
            else
                ctx.log.add("You've done well. The road ahead is long.", {160, 160, 140, 255});
        }
    }

    // Dynamic quest handling
    if (ctx.world.has<DynamicQuest>(target)) {
        auto& dq = ctx.world.get<DynamicQuest>(target);
        if (!dq.accepted) {
            // Offer the quest
            char qbuf[256];
            snprintf(qbuf, sizeof(qbuf), "[Quest] %s", dq.name.c_str());
            ctx.log.add(qbuf, {220, 200, 100, 255});
            ctx.log.add(dq.description.c_str(), {180, 170, 140, 255});
            snprintf(qbuf, sizeof(qbuf), "Objective: %s", dq.objective.c_str());
            ctx.log.add(qbuf, {160, 160, 130, 255});
            snprintf(qbuf, sizeof(qbuf), "Reward: %d XP, %d gold", dq.xp_reward, dq.gold_reward);
            ctx.log.add(qbuf, {160, 160, 130, 255});
            dq.accepted = true;
            dq.accepted_turn = ctx.game_turn;
            ctx.log.add("Quest accepted.", {120, 220, 120, 255});
        } else if (!dq.completed) {
            if (dq.conditions_met(ctx.game_turn)) {
                // Conditions satisfied — complete the quest
                dq.completed = true;
                ctx.meta.total_quests_completed++;
                char qbuf[256];
                snprintf(qbuf, sizeof(qbuf), "Quest complete: %s (+%dxp, +%dgold)",
                         dq.name.c_str(), dq.xp_reward, dq.gold_reward);
                ctx.log.add(qbuf, {120, 220, 120, 255});
                ctx.audio.play(SfxId::QUEST);
                ctx.log.add(dq.complete_text.c_str(), {180, 170, 140, 255});
                ctx.gold += dq.gold_reward;
                if (ctx.world.has<Stats>(ctx.player) && dq.xp_reward > 0) {
                    if (ctx.world.get<Stats>(ctx.player).grant_xp(dq.xp_reward)) {
                        ctx.pending_levelup = true;
                        ctx.levelup_screen.open(ctx.player, ctx.rng);
                        ctx.audio.play(SfxId::LEVELUP);
                    }
                }
            } else {
                // Tell the player what's still needed
                if (dq.requires_dungeon && !dq.visited_dungeon)
                    ctx.log.add("\"Come back when you've actually been down there.\"", {180, 170, 140, 255});
                else if (dq.target_town_x >= 0 && !dq.reached_target)
                    ctx.log.add("\"You haven't made the journey yet. Go.\"", {180, 170, 140, 255});
                else
                    ctx.log.add("\"It hasn't been long enough. These things take time.\"", {180, 170, 140, 255});
            }
        }
    }
    return true;
}

} // namespace npc_interaction
