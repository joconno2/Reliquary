#pragma once
#include "components/quest.h"

// Marks an entity as a quest target. When killed, the associated quest progresses.
struct QuestTarget {
    QuestId quest_id;
    bool completes_quest = true; // true = killing this marks quest COMPLETE
};
