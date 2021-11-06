#pragma once

#include <unordered_set>

#include "f4se/GameEvents.h"

namespace FormIDCache
{
    extern SimpleLock lock;
    extern std::unordered_set<UInt32> cells;

    void RegisterEventListener();
    void Clear();
}
