#pragma once
#include <string>
#include <vector>

enum class QuestState : int {
    AVAILABLE,   // can be picked up
    ACTIVE,      // in progress
    COMPLETE,    // objectives met, turn in
    FINISHED,    // done and rewarded
};

enum class QuestId : int {
    // Main quest line
    MQ_BARROW_WIGHT = 0,   // Kill the wight in the Barrow near Thornwall
    MQ_SEEK_CATACOMBS,     // Descend to the Catacombs — something older stirs
    MQ_OSSUARY_KEY,        // Find the Ossuary Key in the Deep Halls
    MQ_THE_RELIQUARY,      // Reach the Reliquary Vault and claim the Reliquary

    // Side quests
    SQ_RAT_CELLAR,         // Clear rats from the shop cellar
    SQ_LOST_AMULET,        // Find a lost amulet in the Warrens
    SQ_UNDEAD_PATROL,      // Destroy 10 undead in the Catacombs

    COUNT
};

constexpr int QUEST_COUNT = static_cast<int>(QuestId::COUNT);

struct QuestInfo {
    const char* name;
    const char* description;     // shown in quest log
    const char* objective;       // short current objective text
    const char* complete_text;   // shown when turning in
    bool is_main;                // main quest line vs side quest
    int xp_reward;
    int gold_reward;
};

inline const QuestInfo& get_quest_info(QuestId id) {
    static const QuestInfo QUESTS[] = {
        // MQ_BARROW_WIGHT
        {"The Barrow Wight",
         "A wight has risen in the barrow east of Thornwall. People have died. "
         "The elder asked you to put it down.",
         "Enter the barrow east of town and slay the wight.",
         "The wight is dead. Thornwall sleeps easier tonight. But something "
         "deeper stirred when it fell — you felt it.",
         true, 50, 30},

        // MQ_SEEK_CATACOMBS
        {"What Stirs Below",
         "When the wight fell, you felt something deeper awaken. "
         "The scholar speaks of old catacombs beneath the hills, "
         "where things older than the barrow sleep.",
         "Find the entrance to the Catacombs.",
         "The catacombs stretch deeper than anyone knew. "
         "The bones here are arranged with purpose.",
         true, 100, 0},

        // MQ_OSSUARY_KEY
        {"The Ossuary Key",
         "The catacombs are sealed by a door that predates the stonework around it. "
         "A key exists somewhere in the Deep Halls — or so the inscriptions claim.",
         "Find the Ossuary Key in the Deep Halls.",
         "The key is warm in your hand. It shouldn't be.",
         true, 150, 0},

        // MQ_THE_RELIQUARY
        {"The Reliquary",
         "The deeper you go, the older things get. Every god wants what lies "
         "at the bottom. Your god sent you to claim it. "
         "Other paragons are down here too.",
         "Descend to the Reliquary Vault and claim the Reliquary.",
         "You hold the Reliquary. The world changes.",
         true, 0, 0},

        // SQ_RAT_CELLAR
        {"Rats in the Cellar",
         "The shopkeeper complains about rats in the cellar beneath the shop. "
         "Probably nothing dangerous.",
         "Kill 5 rats in the first dungeon level.",
         "The rats are dealt with. The shopkeeper is grateful.",
         false, 20, 15},

        // SQ_LOST_AMULET
        {"The Lost Amulet",
         "A farmer lost a family amulet somewhere in the Warrens. "
         "She'd like it back, if you happen to find it.",
         "Find the lost amulet in the Warrens.",
         "The farmer clutches the amulet. It's worthless, but she weeps anyway.",
         false, 25, 10},

        // SQ_UNDEAD_PATROL
        {"Undead Patrol",
         "The guard captain wants the undead numbers thinned in the Catacombs. "
         "Ten should make a difference. Maybe.",
         "Destroy 10 undead in the Catacombs.",
         "Ten fewer dead things walking. The captain nods.",
         false, 40, 25},
    };
    return QUESTS[static_cast<int>(id)];
}

// Player's quest journal
struct QuestJournal {
    struct Entry {
        QuestId id;
        QuestState state;
        int progress;    // kill count, items found, etc.
        int target;      // target count (0 = not count-based)
    };

    std::vector<Entry> entries;

    bool has_quest(QuestId id) const {
        for (auto& e : entries) if (e.id == id) return true;
        return false;
    }

    QuestState get_state(QuestId id) const {
        for (auto& e : entries) if (e.id == id) return e.state;
        return QuestState::AVAILABLE;
    }

    void add_quest(QuestId id, int target = 0) {
        if (!has_quest(id)) {
            entries.push_back({id, QuestState::ACTIVE, 0, target});
        }
    }

    void set_state(QuestId id, QuestState state) {
        for (auto& e : entries) {
            if (e.id == id) { e.state = state; return; }
        }
    }

    void add_progress(QuestId id, int amount = 1) {
        for (auto& e : entries) {
            if (e.id == id && e.state == QuestState::ACTIVE) {
                e.progress += amount;
                if (e.target > 0 && e.progress >= e.target) {
                    e.state = QuestState::COMPLETE;
                }
                return;
            }
        }
    }
};
