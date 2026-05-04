#include "papyrus_lootman_internal.h"

#include <algorithm>
#include <cstdint>
#include <excpt.h>

namespace papyrus_lootman
{
	using namespace RE;

	bool IsPlayable(const TESForm* form)
	{
		if (!form) return false;
		return (form->formFlags & (1 << 2)) == 0;
	}

	bool IsFavorite(const TESForm* form)
	{
		if (!form) return false;
		auto favMgr = FavoritesManager::GetSingleton();
		if (!favMgr) return false;
		for (int i = 0; i < 12; ++i)
		{
			TESBoundObject* fav = favMgr->storedFavTypes[i];
			if (fav && fav->formID == form->formID)
			{
				return true;
			}
		}
		return false;
	}

	bool TryIsInventoryStackFavoriteSafe(const BGSInventoryItem::Stack& stack, bool& outFavorite)
	{
		outFavorite = false;
		auto* extra = stack.extra.get();
		if (!extra)
		{
			return true;
		}

#if defined(_MSC_VER)
		__try
		{
			outFavorite = extra->HasType(EXTRA_DATA_TYPE::kFavorite);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outFavorite = extra->HasType(EXTRA_DATA_TYPE::kFavorite);
		return true;
#endif
	}

	bool HasInventoryFavoriteStack(const BGSInventoryItem& item)
	{
		for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get())
		{
			bool stackFavorite = false;
			if (TryIsInventoryStackFavoriteSafe(*stack, stackFavorite) && stackFavorite)
			{
				return true;
			}
		}
		return false;
	}

	std::int32_t GetPlayerProtectedStackCount(
		const TESForm* form,
		const BGSInventoryItem::Stack& stack,
		const InventoryItemInfo& stackInfo,
		bool ownerIsPlayer,
		bool ownerIsDead,
		bool formIsFavorite,
		bool hasFavoriteStack,
		bool& retainedFormFavorite)
	{
		if (!ownerIsPlayer || !form || stackInfo.totalCount <= 0)
		{
			return 0;
		}

		std::int32_t protectedCount = 0;
		if (!ownerIsDead && stackInfo.equipped)
		{
			protectedCount = form->GetFormType() == ENUM_FORM_ID::kAMMO ? stackInfo.totalCount : 1;
		}

		bool stackFavorite = false;
		if (TryIsInventoryStackFavoriteSafe(stack, stackFavorite) && stackFavorite)
		{
			protectedCount = std::max(protectedCount, 1);
		}
		if (formIsFavorite && !hasFavoriteStack && !retainedFormFavorite)
		{
			protectedCount = std::max(protectedCount, 1);
			retainedFormFavorite = true;
		}

		return std::min(protectedCount, stackInfo.totalCount);
	}

	bool ShouldProtectFullEquippedTransferStack(const TESForm* form)
	{
		if (!form)
		{
			return false;
		}

		const auto formType = form->GetFormType();
		if (formType == ENUM_FORM_ID::kAMMO)
		{
			return true;
		}
		if (formType != ENUM_FORM_ID::kWEAP)
		{
			return false;
		}

		const auto weaponType = GetWEAPType(form);
		return weaponType == WEAP::grenade || weaponType == WEAP::mine;
	}

	// Transfer treats equipped throwables like ammo; scrap keeps the baseline
	// player protection rules through GetPlayerProtectedStackCount.
	std::int32_t GetPlayerTransferProtectedStackCount(
		const TESForm* form,
		const BGSInventoryItem::Stack& stack,
		const InventoryItemInfo& stackInfo,
		bool ownerIsPlayer,
		bool ownerIsDead,
		bool formIsFavorite,
		bool hasFavoriteStack,
		bool& retainedFormFavorite)
	{
		auto protectedCount = GetPlayerProtectedStackCount(
			form,
			stack,
			stackInfo,
			ownerIsPlayer,
			ownerIsDead,
			formIsFavorite,
			hasFavoriteStack,
			retainedFormFavorite);

		if (ownerIsPlayer &&
			!ownerIsDead &&
			stackInfo.equipped &&
			stackInfo.totalCount > 0 &&
			ShouldProtectFullEquippedTransferStack(form))
		{
			protectedCount = std::max(protectedCount, stackInfo.totalCount);
		}

		return std::min(protectedCount, stackInfo.totalCount);
	}
}
