#include "papyrus_lootman_internal.h"

#include <cstdint>
#include <vector>

namespace papyrus_lootman
{
	using namespace RE;

	std::vector<TESForm*> GetInventoryItemsWithItemType(
		std::monostate, TESObjectREFR* inventoryOwner, std::uint32_t itemType)
	{
		std::vector<TESForm*> result;

		if (!inventoryOwner || itemType > all_item) return result;
		EnsureItemTypeCache();

		bool isPlayer = inventoryOwner->IsPlayerRef();
		bool isDead = IsDeadForLooting(inventoryOwner);

		auto inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList) return result;

		MatchCache matchCache;
		matchCache.results.reserve(inventoryList->data.size());
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		ReadLockGuard guard(inventoryList->rwLock);
		result.reserve(inventoryList->data.size());

		for (auto& item : inventoryList->data)
		{
			auto form = item.object;
			if (!form) continue;

			if (!IsPlayable(form)) continue;
			if (!IsFormTypeMatchesItemType(form->GetFormType(), itemType)) continue;

			if (isPlayer)
			{
				if (form->formID == 0x0F) continue;
				if (IsFavorite(form)) continue;
			}

			auto info = GetInventoryItemInfo(item, modBuffer, inventory_info_quest);
			if ((info.questItem && !IsIncludedQuestItem(form, &matchCache)) ||
			    info.dropped ||
			    (info.equipped && !isDead))
			{
				continue;
			}

			result.push_back(form);
		}

		return result;
	}

	std::vector<TESForm*> GetLootableItems(
		std::monostate, TESObjectREFR* inventoryOwner, std::uint32_t itemType)
	{
		std::vector<TESForm*> result;

		if (!inventoryOwner || itemType > all_item) return result;
		EnsureItemTypeCache();

		auto inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList) return result;

		auto propsSnapshot = PropertiesSnapshot::Capture();
		MatchCache matchCache;
		matchCache.results.reserve(inventoryList->data.size() * 2);
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		ReadLockGuard guard(inventoryList->rwLock);
		result.reserve(inventoryList->data.size());
		modBuffer.reserve(8);
		for (auto& item : inventoryList->data)
		{
			auto form = item.object;
			if (!form) continue;

			if (!IsFormTypeMatchesItemType(form->GetFormType(), itemType)) continue;
			bool validForm = false;
			const bool gotValidForm = TryIsValidFormSafe(
				form,
				&propsSnapshot,
				&matchCache,
				validForm);
			if (!gotValidForm)
			{
				REX::WARN("GetLootableItems: skip {:08X}: valid-form-exception", form->formID);
				continue;
			}
			if (!validForm) continue;

			bool lootableForm = false;
			const bool gotLootableForm = TryIsLootableFormSafe(
				form,
				&propsSnapshot,
				&matchCache,
				lootableForm);
			if (!gotLootableForm)
			{
				REX::WARN("GetLootableItems: skip {:08X}: lootable-form-exception", form->formID);
				continue;
			}
			if (!lootableForm) continue;

			auto info = GetInventoryItemInfo(item, modBuffer, inventory_info_full);
			if (!IsValidInventoryItem(form, info, &matchCache) ||
			    !IsLootableInventoryItem(form, info, &propsSnapshot))
			{
				continue;
			}

			result.push_back(form);
		}

		return result;
	}
}
