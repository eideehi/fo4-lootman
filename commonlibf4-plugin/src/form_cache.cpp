#include "form_cache.h"

namespace form_cache
{
	// Frequently queried vanilla forms are cached once per game load.
	template <class T>
	void LogMissing(const char* label, T* form)
	{
		if (!form)
		{
			REX::WARN("Failed to resolve {}", label);
		}
	}

	namespace keyword
	{
		RE::BGSKeyword* featuredItem = nullptr;
		RE::BGSKeyword* unscrappableObject = nullptr;
		RE::BGSKeyword* workshop = nullptr;
		RE::BGSKeyword* settlement = nullptr;
		RE::BGSKeyword* workshopSettlement = nullptr;
	}

	namespace faction
	{
		RE::TESFaction* playerFaction = nullptr;
	}

	namespace form_list
	{
		RE::BGSListForm* uniqueItems = nullptr;
		std::unordered_set<RE::TESFormID> uniqueItemIds;

		bool IsUniqueItem(RE::TESFormID formID)
		{
			return uniqueItemIds.find(formID) != uniqueItemIds.end();
		}
	}

	void Initialize()
	{
		const auto dh = RE::TESDataHandler::GetSingleton();

		keyword::featuredItem = dh->LookupForm<RE::BGSKeyword>(0x1B3FAC, "Fallout4.esm"sv);
		keyword::unscrappableObject = dh->LookupForm<RE::BGSKeyword>(0x1CC46A, "Fallout4.esm"sv);
		keyword::workshop = dh->LookupForm<RE::BGSKeyword>(0x54BA7, "Fallout4.esm"sv);
		keyword::settlement = dh->LookupForm<RE::BGSKeyword>(0x22611, "Fallout4.esm"sv);
		keyword::workshopSettlement = dh->LookupForm<RE::BGSKeyword>(0x83C9A, "Fallout4.esm"sv);
		LogMissing("keyword::featuredItem", keyword::featuredItem);
		LogMissing("keyword::unscrappableObject", keyword::unscrappableObject);
		LogMissing("keyword::workshop", keyword::workshop);
		LogMissing("keyword::settlement", keyword::settlement);
		LogMissing("keyword::workshopSettlement", keyword::workshopSettlement);

		form_list::uniqueItems = dh->LookupForm<RE::BGSListForm>(0x17C668, "Fallout4.esm"sv);
		LogMissing("form_list::uniqueItems", form_list::uniqueItems);
		form_list::uniqueItemIds.clear();
		if (form_list::uniqueItems)
		{
			// Precompute IDs so hot-path checks avoid list traversal.
			form_list::uniqueItemIds.reserve(form_list::uniqueItems->arrayOfForms.size());
			for (auto* form : form_list::uniqueItems->arrayOfForms)
			{
				if (form)
				{
					form_list::uniqueItemIds.emplace(form->formID);
				}
			}
		}

		faction::playerFaction = dh->LookupForm<RE::TESFaction>(0x1C21C, "Fallout4.esm"sv);
		LogMissing("faction::playerFaction", faction::playerFaction);
	}
}
