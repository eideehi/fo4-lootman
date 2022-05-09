#include "form_cache.hpp"

#include "f4se/GameRTTI.h"

#include "utility.hpp"

namespace form_cache
{
    namespace keyword
    {
        BGSKeyword* bobblehead;
        BGSKeyword* featuredItem;
        BGSKeyword* perkMagazine;
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
        keyword::bobblehead = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|135E6C"), TESForm, BGSKeyword);
        keyword::featuredItem = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|1B3FAC"), TESForm, BGSKeyword);
        keyword::perkMagazine = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|1D4A70"), TESForm, BGSKeyword);
        keyword::unscrappableObject = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|1CC46A"), TESForm, BGSKeyword);
        keyword::workshop = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|54BA7"), TESForm, BGSKeyword);
        keyword::lootingMarker = DYNAMIC_CAST(utility::LookupForm("LootMan.esp|2686"), TESForm, BGSKeyword);
        keyword::settlement = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|22611"), TESForm, BGSKeyword);
        keyword::workshopSettlement = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|83C9A"), TESForm, BGSKeyword);

        form_list::uniqueItems = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|17C668"), TESForm, BGSListForm);

        faction::playerFaction = DYNAMIC_CAST(utility::LookupForm("Fallout4.esm|1C21C"), TESForm, TESFaction);
    }
}
