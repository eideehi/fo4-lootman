#include "papyrus_lootman_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <excpt.h>
#include <limits>
#include <vector>

#include "properties.h"

namespace papyrus_lootman
{
	using namespace RE;

	InventoryItemInfo GetInventoryItemInfo(
		const BGSInventoryItem& item,
		std::vector<BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags)
	{
		InventoryItemInfo result{};
		const bool includeEquipment = (infoFlags & inventory_info_equipment) != 0;
		const bool includeQuest = (infoFlags & inventory_info_quest) != 0;

		for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get())
		{
			result.totalCount += static_cast<std::int32_t>(stack->count);

			if (stack->flags.any(BGSInventoryItem::Stack::Flag::kTemporary))
			{
				result.dropped = true;
			}

			if (stack->flags.any(BGSInventoryItem::Stack::Flag::kSlotIndex1,
			                     BGSInventoryItem::Stack::Flag::kSlotIndex2,
			                     BGSInventoryItem::Stack::Flag::kSlotIndex3))
			{
				result.equipped = true;
			}

			if (includeEquipment && !(result.featured && result.unscrappable && result.legendary))
			{
				auto data = GetEquipmentData(stack->extra.get(), &buffer);
				if (data.isFeaturedItem) result.featured = true;
				if (data.isUnscrappable) result.unscrappable = true;
				if (data.isLegendary) result.legendary = true;
			}

			if (includeQuest && !result.questItem && IsQuestItem(stack->extra.get()))
			{
				result.questItem = true;
			}
		}

		return result;
	}

	InventoryItemInfo GetInventoryStackInfo(
		const BGSInventoryItem::Stack& stack,
		std::vector<BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags)
	{
		InventoryItemInfo result{};
		const bool includeEquipment = (infoFlags & inventory_info_equipment) != 0;
		const bool includeQuest = (infoFlags & inventory_info_quest) != 0;

		result.totalCount = static_cast<std::int32_t>(stack.count);
		if (stack.flags.any(BGSInventoryItem::Stack::Flag::kTemporary))
		{
			result.dropped = true;
		}
		if (stack.flags.any(BGSInventoryItem::Stack::Flag::kSlotIndex1,
		                    BGSInventoryItem::Stack::Flag::kSlotIndex2,
		                    BGSInventoryItem::Stack::Flag::kSlotIndex3))
		{
			result.equipped = true;
		}
		if (includeEquipment)
		{
			auto data = GetEquipmentData(stack.extra.get(), &buffer);
			result.featured = data.isFeaturedItem;
			result.unscrappable = data.isUnscrappable;
			result.legendary = data.isLegendary;
		}
		if (includeQuest)
		{
			result.questItem = IsQuestItem(stack.extra.get());
		}

		return result;
	}

	InventoryItemInfo BuildFallbackStackInfo(const BGSInventoryItem::Stack& stack)
	{
		InventoryItemInfo result{};
		result.totalCount = static_cast<std::int32_t>(stack.count);
		if (stack.flags.any(BGSInventoryItem::Stack::Flag::kTemporary))
		{
			result.dropped = true;
		}
		if (stack.flags.any(BGSInventoryItem::Stack::Flag::kSlotIndex1,
		                    BGSInventoryItem::Stack::Flag::kSlotIndex2,
		                    BGSInventoryItem::Stack::Flag::kSlotIndex3))
		{
			result.equipped = true;
		}
		return result;
	}

	bool TryGetInventoryItemInfoSafe(
		const BGSInventoryItem& item,
		std::vector<BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags,
		InventoryItemInfo& outInfo)
	{
#if defined(_MSC_VER)
		__try
		{
			outInfo = GetInventoryItemInfo(item, buffer, infoFlags);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outInfo = GetInventoryItemInfo(item, buffer, infoFlags);
		return true;
#endif
	}

	bool TryGetInventoryStackInfoSafe(
		const BGSInventoryItem::Stack& stack,
		std::vector<BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags,
		InventoryItemInfo& outInfo)
	{
#if defined(_MSC_VER)
		__try
		{
			outInfo = GetInventoryStackInfo(stack, buffer, infoFlags);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outInfo = GetInventoryStackInfo(stack, buffer, infoFlags);
		return true;
#endif
	}

	bool TryCreateInventoryListSafe(TESObjectREFR* ref, const TESContainer* container)
	{
		if (!ref || !container)
		{
			return false;
		}

#if defined(_MSC_VER)
		__try
		{
			ref->CreateInventoryList(container);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		ref->CreateInventoryList(container);
		return true;
#endif
	}

	bool EnsureContainerInventoryListForLootScan(TESObjectREFR* ref, TESForm* baseForm)
	{
		if (!ref || !baseForm || baseForm->GetFormType() != ENUM_FORM_ID::kCONT)
		{
			return false;
		}

		if (ref->inventoryList)
		{
			return true;
		}

		auto* containerForm = baseForm->As<TESObjectCONT>();
		if (!containerForm)
		{
			return false;
		}

		const auto* container = static_cast<const TESContainer*>(containerForm);
		if (!TryCreateInventoryListSafe(ref, container))
		{
			REX::WARN(
				"ContainerLoot scan: failed to create inventory list for ref={:08X}, base={:08X}",
				ref->formID,
				baseForm->formID);
			return false;
		}

		return ref->inventoryList != nullptr;
	}

	struct ReferenceItemCountCallContext
	{
		TESObjectREFR* ref = nullptr;
		TESBoundObject* object = nullptr;
		std::uint32_t count = 0;
		bool result = false;
	};

	void InvokeGetReferenceItemCountCall(void* opaque)
	{
		auto* context = static_cast<ReferenceItemCountCallContext*>(opaque);
		context->result = context->ref->GetItemCount(context->count, context->object, false);
	}

	bool TryGetReferenceItemCountSafe(
		TESObjectREFR* ref,
		TESBoundObject* object,
		std::int32_t& outCount)
	{
		if (!ref || !object)
		{
			return false;
		}

		ReferenceItemCountCallContext context{
			ref,
			object,
			0,
			false
		};
		if (!ExecuteSehCallSafe(&InvokeGetReferenceItemCountCall, &context) || !context.result)
		{
			return false;
		}

		outCount = static_cast<std::int32_t>(std::min<std::uint32_t>(
			context.count,
			static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())));
		return true;
	}

	std::int32_t GetObservedMovedCount(
		std::int32_t beforeCount,
		std::int32_t afterCount,
		bool gotBefore,
		bool gotAfter,
		std::int32_t fallbackCount)
	{
		if (gotBefore && gotAfter && afterCount > beforeCount)
		{
			return afterCount - beforeCount;
		}
		return std::max<std::int32_t>(fallbackCount, 1);
	}

	std::int32_t GetObservedTransferCount(
		std::int32_t srcBefore,
		std::int32_t srcAfter,
		bool gotSrcBefore,
		bool gotSrcAfter,
		std::int32_t destBefore,
		std::int32_t destAfter,
		bool gotDestBefore,
		bool gotDestAfter,
		std::int32_t fallbackCount)
	{
		if (gotDestBefore && gotDestAfter && destAfter > destBefore)
		{
			return destAfter - destBefore;
		}
		if (gotSrcBefore && gotSrcAfter && srcAfter < srcBefore)
		{
			return srcBefore - srcAfter;
		}
		return std::max<std::int32_t>(fallbackCount, 1);
	}

	struct ContainerWeightCallContext
	{
		TESObjectREFR* ref = nullptr;
		float weight = 0.0F;
	};

	void InvokeContainerWeightCall(void* opaque)
	{
		auto* context = static_cast<ContainerWeightCallContext*>(opaque);
		context->weight = context->ref->GetWeightInContainer();
	}

	bool TryGetContainerWeightSafe(TESObjectREFR* ref, float& outWeight)
	{
		if (!ref)
		{
			return false;
		}

		ContainerWeightCallContext context{ ref, 0.0F };
		if (!ExecuteSehCallSafe(&InvokeContainerWeightCall, &context) ||
			!std::isfinite(context.weight) ||
			context.weight < 0.0F)
		{
			return false;
		}

		outWeight = context.weight;
		return true;
	}

	struct FormWeightCallContext
	{
		TESBoundObject* object = nullptr;
		TBO_InstanceData* instanceData = nullptr;
		float weight = 0.0F;
	};

	void InvokeFormWeightCall(void* opaque)
	{
		auto* context = static_cast<FormWeightCallContext*>(opaque);
		context->weight = TESWeightForm::GetFormWeight(context->object, context->instanceData);
	}

	bool TryGetItemUnitWeightSafe(TESBoundObject* object, TBO_InstanceData* instanceData, float& outWeight)
	{
		if (!object)
		{
			return false;
		}

		FormWeightCallContext context{ object, instanceData, 0.0F };
		if (!ExecuteSehCallSafe(&InvokeFormWeightCall, &context) ||
			!std::isfinite(context.weight) ||
			context.weight < 0.0F)
		{
			return false;
		}

		outWeight = context.weight;
		return true;
	}

	float GetActorCarryWeight(TESObjectREFR* actorRef)
	{
		auto* actor = actorRef ? actorRef->As<Actor>() : nullptr;
		auto* actorValues = ActorValue::GetSingleton();
		if (!actor || !actorValues || !actorValues->carryWeight)
		{
			return 0.0F;
		}

		return actor->GetActorValue(*actorValues->carryWeight);
	}

	bool LootCapacityContext::CanAccept(float unitWeight, std::int32_t count, float& outWeight) const
	{
		outWeight = 0.0F;
		if (!enabled)
		{
			return true;
		}
		if (!valid || count <= 0 || !std::isfinite(unitWeight) || unitWeight < 0.0F)
		{
			return false;
		}

		outWeight = unitWeight * static_cast<float>(count);
		if (!std::isfinite(outWeight) || outWeight < 0.0F)
		{
			return false;
		}

		return projectedWeight + outWeight <= limit;
	}

	void LootCapacityContext::Accept(float weight)
	{
		if (enabled && valid)
		{
			projectedWeight += weight;
		}
	}

	LootCapacityContext BuildLootCapacityContext(
		TESObjectREFR* player,
		TESObjectREFR* pendingContainer,
		TESObjectREFR* workshop)
	{
		LootCapacityContext context;
		if (properties::GetBool(properties::ignore_overweight, true))
		{
			return context;
		}

		context.enabled = true;
		const bool deliverToPlayer = properties::GetBool(properties::loot_is_deliver_to_player, false);
		TESObjectREFR* target = deliverToPlayer ? player : workshop;
		context.limit = deliverToPlayer
			? GetActorCarryWeight(player)
			: static_cast<float>(properties::GetInt(properties::carry_weight, 1000));

		float targetWeight = 0.0F;
		float pendingWeight = 0.0F;
		context.valid =
			target &&
			context.limit > 0.0F &&
			TryGetContainerWeightSafe(target, targetWeight) &&
			TryGetContainerWeightSafe(pendingContainer, pendingWeight);
		context.projectedWeight = targetWeight + pendingWeight;
		return context;
	}

	LootCapacityContext BuildDirectTransferCapacityContext(TESObjectREFR* dest)
	{
		LootCapacityContext context;
		if (properties::GetBool(properties::ignore_overweight, true))
		{
			return context;
		}

		const bool deliverToPlayer = properties::GetBool(properties::loot_is_deliver_to_player, false);
		if (deliverToPlayer != (dest && dest->IsPlayerRef()))
		{
			return context;
		}

		context.enabled = true;
		context.limit = deliverToPlayer
			? GetActorCarryWeight(dest)
			: static_cast<float>(properties::GetInt(properties::carry_weight, 1000));

		float targetWeight = 0.0F;
		context.valid =
			dest &&
			context.limit > 0.0F &&
			TryGetContainerWeightSafe(dest, targetWeight);
		context.projectedWeight = targetWeight;
		return context;
	}
}
