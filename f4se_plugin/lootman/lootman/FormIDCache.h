#pragma once

#include <unordered_set>

#include "f4se/GameTypes.h"

namespace FormIDCache
{
    extern SimpleLock lock;
    extern std::unordered_set<UInt32> cells;
    extern UInt32 lastLootingTimestamp;
    extern UInt32 lootingMarker;

    void Initialize();
    void Clear();
}
