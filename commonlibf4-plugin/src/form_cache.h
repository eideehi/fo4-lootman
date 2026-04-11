#pragma once

namespace form_cache
{
	// Vanilla and plugin forms resolved after `kGameLoaded`. These pointers are treated as stable
	// runtime singletons and are shared across validation, looting, and scan logic.
	namespace keyword
	{
		extern RE::BGSKeyword* featuredItem;
		extern RE::BGSKeyword* unscrappableObject;
		extern RE::BGSKeyword* workshop;
		extern RE::BGSKeyword* settlement;
		extern RE::BGSKeyword* workshopSettlement;
	}

	namespace form_list
	{
		extern RE::BGSListForm* uniqueItems;
		bool IsUniqueItem(RE::TESFormID formID);
	}

	namespace faction
	{
		extern RE::TESFaction* playerFaction;
	}

	// Safe to call repeatedly; later calls refresh the cached pointers.
	void Initialize();
}
