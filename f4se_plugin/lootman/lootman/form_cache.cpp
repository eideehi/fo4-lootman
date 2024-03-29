﻿#include "form_cache.hpp"

#include "f4se/GameRTTI.h"

#include "utility.hpp"

namespace form_cache
{
    namespace keyword
    {
        BGSKeyword* featuredItem;
        BGSKeyword* unscrappableObject;
        BGSKeyword* workshop;
        BGSKeyword* lootingMarker;
        BGSKeyword* settlement;
        BGSKeyword* workshopSettlement;
    }

    namespace form_list
    {
        BGSListForm* uniqueItems;
    }

    namespace faction
    {
        TESFaction* playerFaction;
    }

    void Initialize()
    {
        keyword::featuredItem = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|1B3FAC"), TESForm, BGSKeyword);
        keyword::unscrappableObject = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|1CC46A"), TESForm, BGSKeyword);
        keyword::workshop = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|54BA7"), TESForm, BGSKeyword);
        keyword::settlement = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|22611"), TESForm, BGSKeyword);
        keyword::workshopSettlement = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|83C9A"), TESForm, BGSKeyword);

        form_list::uniqueItems = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|17C668"), TESForm, BGSListForm);

        faction::playerFaction = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|1C21C"), TESForm, TESFaction);
    }
}
