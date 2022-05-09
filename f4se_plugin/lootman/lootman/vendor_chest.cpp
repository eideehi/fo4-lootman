#include "vendor_chest.hpp"

#include <unordered_set>

#include "f4se/GameData.h"
#include "f4se/GameForms.h"
#include "f4se/GameObjects.h"
#include "f4se/GameReferences.h"

#include "fallout4.hpp"

namespace vendor_chest
{
    SimpleLock vendorChestsLock;
    std::unordered_set<UInt32> vendorChests;

    void Initialize()
    {
        const auto allNpc = (*g_dataHandler)->arrNPC_;
        for (UInt32 i = 0; i < allNpc.count; ++i)
        {
            TESNPC* npc = nullptr;
            if (!allNpc.GetNthItem(i, npc))
            {
                continue;
            }

            const auto actorData = reinterpret_cast<TESActorBaseDataAlt*>(&npc->actorData);
            if (actorData && actorData->factions.count)
            {
                for (UInt32 j = 0; j < actorData->factions.count; ++j)
                {
                    TESActorBaseDataAlt::FACTION_DATA factionData = {};
                    if (!actorData->factions.GetNthItem(j, factionData))
                    {
                        continue;
                    }

                    const auto faction = factionData.faction;
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
            }
        }
    }

    bool IsVendorChest(const UInt32 formId)
    {
        SimpleLocker locker(&vendorChestsLock);
        return vendorChests.find(formId) != vendorChests.end();
    }
}
