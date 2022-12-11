#pragma once

class BGSKeyword;
class BGSListForm;
class BGSLocationRefType;
class TESFaction;

namespace form_cache
{
    namespace keyword
    {
        extern BGSKeyword* featuredItem;
        extern BGSKeyword* unscrappableObject;
        extern BGSKeyword* workshop;
        extern BGSKeyword* settlement;
        extern BGSKeyword* workshopSettlement;
    }

    namespace form_list
    {
        extern BGSListForm* uniqueItems;
    }

    namespace faction
    {
        extern TESFaction* playerFaction;
    }

    void Initialize();
}
