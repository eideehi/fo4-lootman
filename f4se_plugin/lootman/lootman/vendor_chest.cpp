#include "vendor_chest.hpp"

#include <unordered_set>

#include "f4se/GameData.h"
#include "f4se/GameForms.h"
#include "f4se/GameObjects.h"
#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"

#include "fallout4.hpp"

namespace vendor_chest
{
    SimpleLock vendorChestsLock;
    std::unordered_set<UInt32> vendorChests;

    void Initialize()
    {
        const auto prefix = "| INITIALIZE |";
        _MESSAGE("%s   [ Start caching vendor chest IDs ]", prefix);

        const auto allFaction = (*g_dataHandler)->arrFACT;
        for (UInt32 i = 0; i < allFaction.count; ++i)
        {
            TESForm* form = nullptr;
            if (!allFaction.GetNthItem(i, form))
            {
                continue;
            }

            const auto faction = reinterpret_cast<TESFaction*>(Runtime_DynamicCast(form, RTTI_TESForm, RTTI_TESFaction));
            if (!faction || (faction->factionData.flags & TESFaction::FACTION_DATA::vendor) == 0)
            {
                continue;
            }

            if (faction->vendorData.vendorChest)
            {
                SimpleLocker locker(&vendorChestsLock);
                vendorChests.emplace(faction->vendorData.vendorChest->baseForm->formID);
            }
        }

        for (const auto& formID : vendorChests)
        {
            _MESSAGE("%s     %08X", prefix, formID);
        }
    }

    bool IsVendorChest(const UInt32 formId)
    {
        SimpleLocker locker(&vendorChestsLock);
        return vendorChests.find(formId) != vendorChests.end();
    }
}
