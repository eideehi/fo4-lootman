#include "papyrus_lootman_internal.h"

#include <cstdint>
#include <string>
#include <vector>

#include "injection_data.h"
#include "message_queue.h"
#include "properties.h"

namespace papyrus_lootman
{
	using namespace RE;

	std::uint32_t GetNotifyCategoryBit(ENUM_FORM_ID formType)
	{
		switch (formType)
		{
		case ENUM_FORM_ID::kALCH:
			return injection_data::notify_alch;
		case ENUM_FORM_ID::kAMMO:
			return injection_data::notify_ammo;
		case ENUM_FORM_ID::kARMO:
			return injection_data::notify_armo;
		case ENUM_FORM_ID::kBOOK:
			return injection_data::notify_book;
		case ENUM_FORM_ID::kINGR:
			return injection_data::notify_ingr;
		case ENUM_FORM_ID::kKEYM:
			return injection_data::notify_keym;
		case ENUM_FORM_ID::kMISC:
			return injection_data::notify_misc;
		case ENUM_FORM_ID::kWEAP:
			return injection_data::notify_weap;
		default:
			return 0;
		}
	}

	bool IsEquipmentFormType(ENUM_FORM_ID formType)
	{
		return formType == ENUM_FORM_ID::kARMO || formType == ENUM_FORM_ID::kWEAP;
	}

	bool ShouldNotifyLootItem(const TESForm* form, const InventoryItemInfo& info, MatchCache* matchCache = nullptr)
	{
		if (!form || !injection_data::HasNotifyFilters())
		{
			return false;
		}

		if (MatchesAnyCached(form, injection_data::notify_item, matchCache))
		{
			return true;
		}

		const auto formType = form->GetFormType();
		const auto categoryMask = injection_data::GetNotifyCategoryMask();
		if ((categoryMask & GetNotifyCategoryBit(formType)) != 0)
		{
			return true;
		}

		return injection_data::GetNotifyLegendaryEquipment() &&
		       IsEquipmentFormType(formType) &&
		       info.legendary;
	}

	void QueueLootItemNotification(
		TESForm* form,
		const std::string& itemName,
		std::int32_t count,
		const InventoryItemInfo& info,
		MatchCache* matchCache)
	{
		if (!ShouldNotifyLootItem(form, info, matchCache))
		{
			return;
		}

		message_queue::Enqueue(form ? form->formID : 0, itemName.empty() ? GetFormName(form) : itemName, count);
	}

	void ShowSystemMessage(std::monostate, std::int32_t messageId)
	{
		switch (messageId)
		{
		case 1:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_INSTALLED",
				"[LootMan] Installation is complete.");
			return;
		case 2:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_UNINSTALLED",
				"[LootMan] Uninstallation is complete.");
			return;
		case 3:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_ENABLED",
				"[LootMan] LootMan is enabled.");
			return;
		case 4:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_DISABLED",
				"[LootMan] LootMan is disabled.");
			return;
		case 5:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_HAS_OVERWEIGHT",
				"[LootMan] Maximum carry-weight has been exceeded.");
			return;
		case 6:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_REMIND_NOT_LOOTING_IN_SETTLEMENT",
				"[LootMan] Looting from settlements has been disabled.");
			return;
		case 7:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_WORKSHOP_NOT_FOUND",
				"[LootMan] Could not find the workshop.");
			return;
		case 8:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_LINKED_TO_WORKSHOP",
				"[LootMan] Linked to the workshop.");
			return;
		case 9:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_UNLINKED_TO_WORKSHOP",
				"[LootMan] Unlinked to the workshop.");
			return;
		case 10:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_NOT_HAVE_BOBBY_PIN",
				"[LootMan] Locked container ignored because there is no Bobby pin.");
			return;
		case 11:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_UTILITY_PROCESS_COMPLETE",
				"[LootMan] Utility processing is complete.");
			return;
		default:
			REX::WARN(
				"source=native component=message_queue event=invalid_system_message_id message_id={}",
				messageId);
			return;
		}
	}

	void ShowSystemMessageWithName(std::monostate, std::int32_t messageId, TESForm* nameSource)
	{
		const auto workshopName = GetFormName(nameSource);
		if (workshopName.empty())
		{
			ShowSystemMessage(std::monostate{}, messageId);
			return;
		}

		switch (messageId)
		{
		case 8:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_LINKED_TO_WORKSHOP_NAMED",
				"[LootMan] Workshop linked: {workshopName}.",
				{{ "{workshopName}", workshopName }});
			return;
		case 9:
			message_queue::EnqueueLocalizedText(
				"$LTMN_SYSTEM_MESSAGE_UNLINKED_TO_WORKSHOP_NAMED",
				"[LootMan] Workshop unlinked: {workshopName}.",
				{{ "{workshopName}", workshopName }});
			return;
		default:
			ShowSystemMessage(std::monostate{}, messageId);
			return;
		}
	}

	bool ShouldNotifyLootDestination(TESObjectREFR* dest)
	{
		if (!dest)
		{
			return false;
		}
		if (properties::GetBool(properties::looting_without_logs, true))
		{
			return false;
		}
		if (dest->IsPlayerRef())
		{
			return true;
		}

		return !properties::GetBool(properties::loot_is_deliver_to_player, false);
	}

	InventoryItemInfo BuildWorldReferenceNotificationInfo(
		TESObjectREFR* ref,
		TESBoundObject* object,
		std::int32_t count)
	{
		InventoryItemInfo info{};
		info.totalCount = count;
		if (!ref || !object || !IsEquipmentFormType(object->GetFormType()) || !ref->extraList)
		{
			return info;
		}

		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		EquipmentData equipmentData{};
		if (TryGetEquipmentDataSafe(ref->extraList.get(), &modBuffer, equipmentData))
		{
			info.legendary = equipmentData.isLegendary;
			info.featured = equipmentData.isFeaturedItem;
			info.unscrappable = equipmentData.isUnscrappable;
		}
		return info;
	}
}
