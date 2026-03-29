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
    // Main quest line (17 steps)
    MQ_01_BARROW_WIGHT = 0,      // Kill the wight in the barrow near Thornwall
    MQ_02_SCHOLAR_CLUE,           // Speak to the scholar in Thornwall about what stirred
    MQ_03_ASHFORD_TABLET,         // Travel to Ashford — find a stone tablet in the Warrens nearby
    MQ_04_GREYWATCH_WARNING,      // Deliver the tablet to the captain in Greywatch
    MQ_05_STONEKEEP_DEPTHS,       // Descend Stonekeep to find the First Inscription
    MQ_06_FROSTMERE_SAGE,         // Travel north to Frostmere — consult the ice sage
    MQ_07_FROZEN_KEY,             // Retrieve the Frozen Key from the ice dungeon near Frostmere
    MQ_08_CATACOMBS_GATE,         // Use the Frozen Key to open the Catacombs gate
    MQ_09_OSSUARY_FRAGMENT,       // Find the first Reliquary Fragment in the Catacombs
    MQ_10_IRONHEARTH_FORGE,       // Travel to Ironhearth — have the fragment analyzed
    MQ_11_MOLTEN_TRIAL,           // Descend the Molten Depths to find the second fragment
    MQ_12_CANDLEMERE_RITUAL,      // Travel to Candlemere — the Soleth priests know the binding ritual
    MQ_13_SUNKEN_FRAGMENT,        // Retrieve the third fragment from the Sunken Halls
    MQ_14_HOLLOWGATE_SEAL,        // Travel to Hollowgate — break the final seal
    MQ_15_THE_BARROW,             // Enter The Barrow — the deepest mega-dungeon (6 levels)
    MQ_16_RELIQUARY_VAULT,        // Reach the Reliquary Vault at the bottom of The Barrow
    MQ_17_CLAIM_RELIQUARY,        // Claim the Reliquary — the game's climax

    // Side quests
    SQ_RAT_CELLAR,         // Clear rats from the shop cellar
    SQ_LOST_AMULET,        // Find a lost amulet in the Warrens
    SQ_UNDEAD_PATROL,      // Destroy 10 undead in the Catacombs
    SQ_KILL_BEAR,          // Hunter asks to kill a dangerous bear in the wilderness
    SQ_DELIVER_WEAPON,     // Blacksmith asks to deliver a sword to the guard captain in Greywatch
    SQ_HERB_GATHERING,     // Herbalist needs 3 herbs from the wilderness
    SQ_MISSING_PERSON,     // Farmer's son went into the Warrens and hasn't returned

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
        // MQ_01_BARROW_WIGHT
        {"The Barrow Wight",
         "A wight has risen in the barrow east of Thornwall. People have died. "
         "The elder asked you to put it down.",
         "Enter the barrow east of town and slay the wight.",
         "The wight is dead. Thornwall sleeps easier tonight. But something "
         "deeper stirred when it fell — you felt it.",
         true, 50, 30},

        // MQ_02_SCHOLAR_CLUE
        {"What Stirs Below",
         "When the wight fell, something answered from deeper down. "
         "The scholar in Thornwall spends his nights reading texts no one else can. "
         "He may know what woke.",
         "Speak to the scholar in Thornwall.",
         "The scholar's hands trembled as he spoke. 'The old texts mention a tablet "
         "in Ashford — a warning carved before the gods had names.'",
         true, 60, 0},

        // MQ_03_ASHFORD_TABLET
        {"The Ashford Tablet",
         "A stone tablet lies somewhere in the Warrens near Ashford. "
         "The scholar believes it predates the barrow by centuries. "
         "Whatever is written there, the dead didn't want it found.",
         "Find the stone tablet in the Warrens near Ashford.",
         "The tablet is heavier than stone should be. The inscriptions seem to shift "
         "when you aren't looking directly at them.",
         true, 80, 20},

        // MQ_04_GREYWATCH_WARNING
        {"The Captain's Warning",
         "The tablet's inscriptions speak of a seal weakening beneath the hills. "
         "The guard captain in Greywatch commands the largest garrison in the region. "
         "He needs to see this.",
         "Deliver the tablet to the captain in Greywatch.",
         "The captain read the tablet in silence. Then he said one word: 'Stonekeep.' "
         "Something in the deep has been scratching at the walls for a long time.",
         true, 100, 40},

        // MQ_05_STONEKEEP_DEPTHS
        {"The First Inscription",
         "Stonekeep was sealed generations ago. The captain unsealed it. "
         "Deep in the lowest level, there is said to be an inscription — "
         "the first thing ever written in this land. Or the last.",
         "Descend Stonekeep to find the First Inscription.",
         "The inscription is not carved. It is burned into the stone, as though "
         "the words themselves were too heavy for the rock to hold.",
         true, 120, 50},

        // MQ_06_FROSTMERE_SAGE
        {"The Ice Sage",
         "The First Inscription mentions a name: Vehlkyr. No one in the south knows it. "
         "But in Frostmere, far to the north, an old sage studies things that should "
         "stay frozen.",
         "Travel to Frostmere and consult the ice sage.",
         "The sage stared at your transcription for a long time. "
         "'Vehlkyr was the first to find the Reliquary. He did not survive the finding.'",
         true, 140, 30},

        // MQ_07_FROZEN_KEY
        {"The Frozen Key",
         "The sage spoke of a key — forged in cold so deep that fire cannot touch it. "
         "It lies in the ice dungeon north of Frostmere, guarded by things that "
         "stopped being human long ago.",
         "Retrieve the Frozen Key from the ice dungeon near Frostmere.",
         "The key burns your hand with cold. It is shaped like no lock you've ever seen.",
         true, 160, 0},

        // MQ_08_CATACOMBS_GATE
        {"The Sealed Gate",
         "The Catacombs gate has stood sealed since before Thornwall was built. "
         "The Frozen Key is the only thing that opens it. "
         "What waits beyond has been patient for a very long time.",
         "Use the Frozen Key to open the Catacombs gate.",
         "The gate screamed when it opened. Not metal on stone. Something else.",
         true, 180, 0},

        // MQ_09_OSSUARY_FRAGMENT
        {"The First Fragment",
         "Beyond the gate, the Catacombs stretch into places the maps don't show. "
         "Somewhere in the deepest ossuary lies the first fragment of the Reliquary — "
         "a piece of something that should never have been broken apart.",
         "Find the first Reliquary Fragment in the Catacombs.",
         "The fragment hums in your pack. It is warm. It should not be warm.",
         true, 200, 60},

        // MQ_10_IRONHEARTH_FORGE
        {"The Master Smith",
         "The fragment is unlike any material you've seen. The master smith in Ironhearth "
         "works metals that others won't touch. If anyone can tell you what this is, "
         "it's her.",
         "Travel to Ironhearth and have the fragment analyzed.",
         "'This isn't metal,' she said. 'It's memory. Solidified memory. "
         "There are two more pieces. Without all three, you hold nothing.'",
         true, 220, 50},

        // MQ_11_MOLTEN_TRIAL
        {"The Molten Trial",
         "The smith's analysis points to the Molten Depths — volcanic tunnels beneath "
         "Ironhearth where the earth itself bleeds. The second fragment lies in the "
         "deepest chamber, where the heat would kill anything that isn't already dead.",
         "Descend the Molten Depths to find the second fragment.",
         "The second fragment is cold, even in the heart of the furnace. "
         "Two of three. The pieces want to be together.",
         true, 260, 80},

        // MQ_12_CANDLEMERE_RITUAL
        {"The Binding Ritual",
         "Two fragments are not enough. They must be bound. "
         "The Soleth priests in Candlemere have preserved the old rituals — "
         "the ones the gods tried to make everyone forget.",
         "Travel to Candlemere and learn the binding ritual from the priests.",
         "The priests taught you the words. They also taught you the price. "
         "Nothing this old comes free.",
         true, 280, 40},

        // MQ_13_SUNKEN_FRAGMENT
        {"The Drowned Shard",
         "The third fragment lies in the Sunken Halls — flooded ruins that sink "
         "deeper with each passing year. The water there doesn't behave like water. "
         "It remembers.",
         "Retrieve the third fragment from the Sunken Halls.",
         "Three fragments. They pull toward each other in your pack. "
         "The Reliquary is almost whole.",
         true, 320, 100},

        // MQ_14_HOLLOWGATE_SEAL
        {"The Final Seal",
         "Hollowgate marks the entrance to the oldest place in the world. "
         "A seal bars the way — the last thing standing between you and The Barrow. "
         "The fragments resonate when you hold them near the gate.",
         "Travel to Hollowgate and break the final seal.",
         "The seal shattered. The fragments fused into a key that fits no door "
         "you can see. But the way is open. Down.",
         true, 360, 0},

        // MQ_15_THE_BARROW
        {"Into the Barrow",
         "The Barrow is not a tomb. It is older than death. Six levels deep, "
         "it descends through ages the world has forgotten. "
         "At the bottom lies the Reliquary Vault — and whatever guards it.",
         "Enter The Barrow and descend to its depths.",
         "Each level is older than the last. The walls stop being stone. "
         "The floor stops being solid. But you keep going down.",
         true, 400, 0},

        // MQ_16_RELIQUARY_VAULT
        {"The Vault",
         "The Reliquary Vault. You can feel it before you see it. "
         "The air tastes like iron and endings. "
         "Every god is watching. Every paragon who ever lived stands between you "
         "and what waits inside.",
         "Reach the Reliquary Vault at the bottom of The Barrow.",
         "The Vault is open. The Reliquary is here. It has been waiting.",
         true, 450, 0},

        // MQ_17_CLAIM_RELIQUARY
        {"The Reliquary",
         "You stand before the Reliquary. It is not what you expected. "
         "It is not what anyone expected. The gods are silent for the first time "
         "since the world was made. Your hand reaches out.",
         "Claim the Reliquary.",
         "You hold the Reliquary. The world changes.",
         true, 500, 0},

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

        // SQ_KILL_BEAR
        {"The Beast of the East",
         "A dangerous bear has been killing livestock east of Thornwall. "
         "The guard wants it dealt with before someone gets hurt.",
         "Kill the bear east of Thornwall.",
         "The bear is dead. The livestock are safe. For now.",
         false, 35, 20},

        // SQ_DELIVER_WEAPON
        {"Special Delivery",
         "The blacksmith has forged a sword for the guard captain in Greywatch. "
         "He needs someone to deliver it.",
         "Deliver the sword to the guard captain in Greywatch.",
         "The captain tests the blade and nods. Fine work.",
         false, 30, 25},

        // SQ_HERB_GATHERING
        {"Bitter Remedies",
         "The herbalist needs rare herbs that grow in the wilderness. "
         "Three bundles should be enough for a season's remedies.",
         "Gather 3 herb bundles from the wilderness.",
         "The herbalist smells each bundle carefully. These will do.",
         false, 30, 15},

        // SQ_MISSING_PERSON
        {"The Farmer's Son",
         "A farmer's son went into the Warrens looking for adventure. "
         "He hasn't come back. The farmer fears the worst.",
         "Find the farmer's son in the Warrens.",
         "The boy is shaken but alive. His father weeps with relief.",
         false, 50, 30},
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
