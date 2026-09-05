#include "form_cache.h"

#include <shared_mutex>

namespace form_cache
{
	// Frequently queried vanilla forms are cached once per game load.
	template <class T>
	void LogMissing(const char* label, T* form)
	{
		if (!form)
		{
			REX::WARN("source=native component=form_cache event=form_resolution_failed label=\"{}\"", label);
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
		// uniqueItemIds is built by Initialize() on the F4SE message thread (kGameLoaded, which fires once
		// per process and not on a save load) but read per-form from VM worker threads on the
		// loot-validation hot path. A shared_mutex
		// plus build-local-then-swap keeps a reader from traversing the set mid-clear()/rehash (data race /
		// use-after-free), matching the sibling constructible_object / vendor_chest caches.
		std::shared_mutex uniqueItemIdsMutex;
		std::unordered_set<RE::TESFormID> uniqueItemIds;

		bool IsUniqueItem(RE::TESFormID formID)
		{
			std::shared_lock<std::shared_mutex> guard(uniqueItemIdsMutex);
			return uniqueItemIds.find(formID) != uniqueItemIds.end();
		}
	}

	void Initialize()
	{
		const auto dh = RE::TESDataHandler::GetSingleton();
		if (!dh)
		{
			REX::ERROR("source=native component=form_cache event=initialize_failed reason=data_handler_unavailable");
			return;
		}

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
		// Build into a local set, then publish under the exclusive lock so a concurrent reader never
		// observes a half-rebuilt / mid-rehash set.
		std::unordered_set<RE::TESFormID> rebuiltUniqueItemIds;
		if (form_list::uniqueItems)
		{
			// Precompute IDs so hot-path checks avoid list traversal.
			rebuiltUniqueItemIds.reserve(form_list::uniqueItems->arrayOfForms.size());
			for (auto* form : form_list::uniqueItems->arrayOfForms)
			{
				if (form)
				{
					rebuiltUniqueItemIds.emplace(form->formID);
				}
			}
		}
		{
			std::unique_lock<std::shared_mutex> guard(form_list::uniqueItemIdsMutex);
			form_list::uniqueItemIds = std::move(rebuiltUniqueItemIds);
		}

		faction::playerFaction = dh->LookupForm<RE::TESFaction>(0x1C21C, "Fallout4.esm"sv);
		LogMissing("faction::playerFaction", faction::playerFaction);
	}
}
