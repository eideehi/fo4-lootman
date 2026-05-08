#include "papyrus_lootman_internal.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "injection_data.h"
#include "properties.h"

namespace papyrus_lootman
{
	using namespace RE;

	std::mutex lootCapacityLock;

	LootPassBudget LootPassBudget::Capture()
	{
		LootPassBudget budget;
		budget.useTimeBudget = properties::GetBool(properties::use_looting_time_budget, false);
		budget.timeBudgetMs = std::clamp(
			static_cast<double>(properties::GetFloat(properties::looting_time_budget_ms, 4.0F)),
			0.1,
			100.0);

		const auto legacyLimit = properties::GetInt(properties::max_items_processed_per_thread, 32);
		budget.maxObjects = static_cast<std::size_t>(std::clamp(
			properties::GetInt(properties::max_lootable_objects_per_pass, legacyLimit),
			1,
			static_cast<int>(kMaxItemsProcessedPerThreadLimit)));
		budget.maxContainers = static_cast<std::size_t>(std::clamp(
			properties::GetInt(properties::max_containers_per_pass, 4),
			0,
			static_cast<int>(kMaxItemsProcessedPerThreadLimit)));
		budget.maxActors = static_cast<std::size_t>(std::clamp(
			properties::GetInt(properties::max_actors_per_pass, 4),
			0,
			static_cast<int>(kMaxItemsProcessedPerThreadLimit)));
		budget.maxActivationRefs = static_cast<std::size_t>(std::clamp(
			properties::GetInt(properties::max_activation_refs_per_pass, 8),
			0,
			static_cast<int>(kMaxItemsProcessedPerThreadLimit)));
		return budget;
	}

	bool LootPassBudget::ShouldStop()
	{
		if (processedObjects >= hardMaxObjects ||
			(!useTimeBudget && processedObjects >= maxObjects))
		{
			hitObjectLimit = true;
			return true;
		}
		if (useTimeBudget && processedObjects > 0 &&
			ElapsedMilliseconds(startedAt) >= timeBudgetMs)
		{
			hitTimeBudget = true;
			return true;
		}
		return false;
	}

	bool LootPassBudget::CanProcessCategory(ENUM_FORM_ID formType) const
	{
		if (useTimeBudget)
		{
			return true;
		}
		if (formType == ENUM_FORM_ID::kCONT)
		{
			return processedContainers < maxContainers;
		}
		if (formType == ENUM_FORM_ID::kNPC_)
		{
			return processedActors < maxActors;
		}
		if (formType == ENUM_FORM_ID::kACTI || formType == ENUM_FORM_ID::kFLOR)
		{
			return processedActivationRefs < maxActivationRefs;
		}
		return true;
	}

	void LootPassBudget::MarkProcessed(ENUM_FORM_ID formType)
	{
		++processedObjects;
		if (formType == ENUM_FORM_ID::kCONT)
		{
			++processedContainers;
		}
		else if (formType == ENUM_FORM_ID::kNPC_)
		{
			++processedActors;
		}
		else if (formType == ENUM_FORM_ID::kACTI || formType == ENUM_FORM_ID::kFLOR)
		{
			++processedActivationRefs;
		}
	}

	struct InventoryTransferRequest
	{
		TESBoundObject* object = nullptr;
		std::uint32_t stackIndex = 0;
		std::int32_t count = 0;
		float unitWeight = 0.0F;
		BSTSmartPointer<ExtraDataList> extra;
		bool preserveStackExtra = false;
		InventoryItemInfo info;
		std::string itemName;
	};

	struct InventoryFormTransferRequest
	{
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
		float unitWeight = 0.0F;
		std::optional<std::uint32_t> stackIndex;
		BSTSmartPointer<ExtraDataList> extra;
		bool preserveStackExtra = false;
		InventoryItemInfo info;
		std::string itemName;
	};

	bool IsExcludedByMiscSubtype(TESForm* form, std::int32_t subType, BGSKeyword* looseModKeyword)
	{
		if (!form || subType < 0 || form->GetFormType() != ENUM_FORM_ID::kMISC)
		{
			return false;
		}

		const auto* miscForm = form->As<TESObjectMISC>();
		const bool isLooseModItem = looseModKeyword ?
			HasKeyword(form, looseModKeyword) :
			(miscForm && miscForm->IsLooseMod());
		return (subType == 0 && isLooseModItem) || (subType == 1 && !isLooseModItem);
	}

	std::int32_t TransferInventoryItemsImpl(
		TESObjectREFR* src,
		TESObjectREFR* dest,
		std::uint32_t itemType,
		std::int32_t subType,
		BGSKeyword* looseModKeyword,
		LootCapacityContext* capacity,
		bool notifyMovedItems)
	{
		if (!src || !dest || src == dest || itemType > all_item)
		{
			return 0;
		}
		EnsureItemTypeCache();

		auto inventoryList = src->inventoryList;
		if (!inventoryList)
		{
			return 0;
		}

		const bool sourceIsPlayer = src->IsPlayerRef();
		const bool sourceIsDead = IsDeadForLooting(src);
		MatchCache matchCache;
		matchCache.results.reserve(inventoryList->data.size());
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		std::vector<InventoryFormTransferRequest> requests;
		requests.reserve(inventoryList->data.size());
		const auto requestInfoFlags = notifyMovedItems ? inventory_info_full : inventory_info_quest;

		{
			ReadLockGuard guard(inventoryList->rwLock);
			for (auto& item : inventoryList->data)
			{
				auto* form = item.object;
				if (!form)
				{
					continue;
				}

				if (!IsPlayable(form) ||
					!IsFormTypeMatchesItemType(form->GetFormType(), itemType) ||
					IsExcludedByMiscSubtype(form, subType, looseModKeyword))
				{
					continue;
				}

				if (sourceIsPlayer)
				{
					if (form->formID == 0x0F)
					{
						continue;
					}

					const bool formIsFavorite = IsFavorite(form);
					const bool hasFavoriteStack = HasInventoryFavoriteStack(item);
					bool retainedFormFavorite = false;
					std::vector<InventoryFormTransferRequest> itemRequests;
					itemRequests.reserve(4);

					std::uint32_t stackIndex = 0;
					for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex)
					{
						InventoryItemInfo stackInfo{};
						if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, requestInfoFlags, stackInfo))
						{
							REX::WARN(
								"source=native component=inventory_transfer event=stack_skipped reason=stack_info_exception operation=transfer_inventory_items item={:08X}",
								form->formID);
							continue;
						}

						if ((stackInfo.questItem && !MatchesAnyCached(form, injection_data::include_quest_item, &matchCache)) ||
							stackInfo.dropped ||
							stackInfo.totalCount <= 0)
						{
							continue;
						}

						const auto protectedCount = GetPlayerTransferProtectedStackCount(
							form,
							*stack,
							stackInfo,
							sourceIsPlayer,
							sourceIsDead,
							formIsFavorite,
							hasFavoriteStack,
							retainedFormFavorite);
						const auto movableCount = stackInfo.totalCount - protectedCount;
						if (movableCount <= 0)
						{
							continue;
						}

						float unitWeight = 0.0F;
						if (capacity && capacity->enabled &&
							!TryGetItemUnitWeightSafe(form, GetInstanceData(stack->extra.get()), unitWeight))
						{
							continue;
						}

						itemRequests.push_back(InventoryFormTransferRequest{
							form,
							movableCount,
							unitWeight,
							stackIndex,
							stack->extra,
							ShouldPreserveStackExtraForTransfer(
								form,
								*stack,
								movableCount,
								stackInfo.totalCount),
							stackInfo,
							notifyMovedItems ? GetInventoryItemDisplayNameSafe(item, form, stackIndex) : std::string{}
						});
					}

					for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
					{
						requests.push_back(*it);
					}
					continue;
				}

				std::vector<InventoryFormTransferRequest> itemRequests;
				itemRequests.reserve(4);
				std::uint32_t stackIndex = 0;
				for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex)
				{
					InventoryItemInfo stackInfo{};
					if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, requestInfoFlags, stackInfo))
					{
						REX::WARN(
							"source=native component=inventory_transfer event=stack_skipped reason=stack_info_exception operation=transfer_inventory_items item={:08X}",
							form->formID);
						continue;
					}
					if ((stackInfo.questItem && !MatchesAnyCached(form, injection_data::include_quest_item, &matchCache)) ||
						stackInfo.dropped ||
						(stackInfo.equipped && !sourceIsDead) ||
						stackInfo.totalCount <= 0)
					{
						continue;
					}

					float unitWeight = 0.0F;
					if (capacity && capacity->enabled &&
						!TryGetItemUnitWeightSafe(form, GetInstanceData(stack->extra.get()), unitWeight))
					{
						continue;
					}

					itemRequests.push_back(InventoryFormTransferRequest{
						form,
						stackInfo.totalCount,
						unitWeight,
						stackIndex,
						stack->extra,
						ShouldPreserveStackExtraForTransfer(
							form,
							*stack,
							stackInfo.totalCount,
							stackInfo.totalCount),
						stackInfo,
						notifyMovedItems ? GetInventoryItemDisplayNameSafe(item, form, stackIndex) : std::string{}
					});
				}

				for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
				{
					requests.push_back(*it);
				}
			}
		}

		std::int32_t movedItems = 0;
		for (const auto& request : requests)
		{
			if (!request.object || request.count <= 0)
			{
				continue;
			}

			float acceptedWeight = 0.0F;
			if (capacity && !capacity->CanAccept(request.unitWeight, request.count, acceptedWeight))
			{
				continue;
			}

			std::int32_t srcBefore = 0;
			std::int32_t destBefore = 0;
			const bool gotSrcBefore = TryGetReferenceItemCountSafe(src, request.object, srcBefore);
			const bool gotDestBefore = TryGetReferenceItemCountSafe(dest, request.object, destBefore);

			auto remaining = request.count;
			bool transferFailed = false;
			if (request.preserveStackExtra)
			{
				if (!TryMoveInventoryItemPreservingStackExtraSafe(
						src,
						dest,
						request.object,
						request.count,
						request.stackIndex,
						request.extra))
				{
					REX::WARN(
						"source=native component=inventory_transfer event=transfer_failed reason=instance_preserving_transfer_failed operation=transfer_inventory_items src={:08X} dest={:08X} item={:08X} count={} stack={}",
						src->formID,
						dest->formID,
						request.object->formID,
						request.count,
						request.stackIndex ? static_cast<std::int32_t>(*request.stackIndex) : -1);
					transferFailed = true;
				}
				else
				{
					remaining = 0;
				}
			}
			while (remaining > 0 && !request.preserveStackExtra)
			{
				const auto chunk = std::min<std::int32_t>(remaining, 65535);
				if (!TryMoveInventoryItemSafe(
						src,
						dest,
						request.object,
						chunk,
						request.stackIndex))
				{
					REX::WARN(
						"source=native component=inventory_transfer event=transfer_failed reason=move_failed operation=transfer_inventory_items src={:08X} dest={:08X} item={:08X} remaining={} stack={}",
						src->formID,
						dest->formID,
						request.object->formID,
						remaining,
						request.stackIndex ? static_cast<std::int32_t>(*request.stackIndex) : -1);
					break;
				}
				remaining -= chunk;
			}
			if (transferFailed)
			{
				continue;
			}
			const auto movedCount = request.count - remaining;

			std::int32_t srcAfter = 0;
			std::int32_t destAfter = 0;
			const bool gotSrcAfter = TryGetReferenceItemCountSafe(src, request.object, srcAfter);
			const bool gotDestAfter = TryGetReferenceItemCountSafe(dest, request.object, destAfter);
			const bool observedSourceReduction =
				gotSrcBefore && gotSrcAfter && srcAfter < srcBefore;
			const bool observedDestIncrease =
				gotDestBefore && gotDestAfter && destAfter > destBefore;
			const bool countUnavailable =
				!gotSrcBefore && !gotSrcAfter && !gotDestBefore && !gotDestAfter;

			if (movedCount > 0 && (observedSourceReduction || observedDestIncrease || countUnavailable))
			{
				++movedItems;
				const auto observedMovedCount = GetObservedTransferCount(
					srcBefore,
					srcAfter,
					gotSrcBefore,
					gotSrcAfter,
					destBefore,
					destAfter,
					gotDestBefore,
					gotDestAfter,
					movedCount);
				if (capacity)
				{
					capacity->Accept(acceptedWeight);
				}
				if (notifyMovedItems)
				{
					auto notificationInfo = request.info;
					notificationInfo.totalCount = observedMovedCount;
					QueueLootItemNotification(
						request.object,
						request.itemName,
						observedMovedCount,
						notificationInfo,
						&matchCache);
				}
			}
			else
			{
				REX::WARN(
					"source=native component=inventory_transfer event=transfer_verification_failed reason=no_observed_transfer operation=transfer_inventory_items item={:08X} requested_count={} src_before={} src_after={} dest_before={} dest_after={} got_src_before={} got_src_after={} got_dest_before={} got_dest_after={}",
					request.object->formID,
					request.count,
					srcBefore,
					srcAfter,
					destBefore,
					destAfter,
					gotSrcBefore,
					gotSrcAfter,
					gotDestBefore,
					gotDestAfter);
			}
		}

		return movedItems;
	}

	std::int32_t TransferLootableInventoryItemsImpl(
		TESObjectREFR* src,
		TESObjectREFR* dest,
		std::uint32_t itemType,
		LootCapacityContext* capacity,
		LootPassBudget* passBudget)
	{
		if (!src || !dest || src == dest || itemType > all_item)
		{
			return 0;
		}
		EnsureItemTypeCache();

		auto inventoryList = src->inventoryList;
		if (!inventoryList)
		{
			return 0;
		}
		if (passBudget && passBudget->ShouldStop())
		{
			return 0;
		}
		auto propsSnapshot = PropertiesSnapshot::Capture();
		auto* sourceBase = src->GetObjectReference();
		const bool sourceIsNpc = sourceBase && sourceBase->GetFormType() == ENUM_FORM_ID::kNPC_;
		const bool sourceIsDead = IsDeadForLooting(src);
		if (sourceIsNpc && !sourceIsDead)
		{
			return 0;
		}

		MatchCache matchCache;
		matchCache.results.reserve(inventoryList->data.size() * 2);
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		modBuffer.reserve(8);
		std::vector<InventoryTransferRequest> requests;
		requests.reserve(inventoryList->data.size());
		const bool notifyMovedItems = ShouldNotifyLootDestination(dest);

		{
			ReadLockGuard guard(inventoryList->rwLock);
			for (auto& item : inventoryList->data)
			{
				if (passBudget && passBudget->ShouldStop())
				{
					break;
				}

				auto* form = item.object;
				if (!form)
				{
					continue;
				}

				if (!IsFormTypeMatchesItemType(form->GetFormType(), itemType))
				{
					continue;
				}
				const auto formType = form->GetFormType();

				bool validForm = false;
				const bool gotValidForm = TryIsValidFormSafe(
					form,
					&propsSnapshot,
					&matchCache,
					validForm);
				if (!gotValidForm || !validForm)
				{
					continue;
				}

				bool lootableForm = false;
				const bool gotLootableForm = TryIsLootableFormSafe(
					form,
					&propsSnapshot,
					&matchCache,
					lootableForm);
				if (!gotLootableForm || !lootableForm)
				{
					continue;
				}

				std::vector<InventoryTransferRequest> itemRequests;
				std::uint32_t stackIndex = 0;
				for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex)
				{
					if (passBudget && passBudget->ShouldStop())
					{
						break;
					}

					InventoryItemInfo stackInfo{};
					bool gotStackInfo = TryGetInventoryStackInfoSafe(
						*stack,
						modBuffer,
						inventory_info_full,
						stackInfo);
					if (!gotStackInfo)
					{
						stackInfo = BuildFallbackStackInfo(*stack);
					}

					auto resolvedCount = stackInfo.totalCount;
					if (resolvedCount <= 0 && stackInfo.equipped)
					{
						resolvedCount = 1;
					}
					if (resolvedCount <= 0 && sourceIsDead && formType == ENUM_FORM_ID::kWEAP)
					{
						resolvedCount = 1;
					}
					if (resolvedCount <= 0)
					{
						continue;
					}
					if (!IsValidInventoryItem(form, stackInfo, &matchCache) ||
					    !IsLootableInventoryItem(form, stackInfo, &propsSnapshot))
					{
						continue;
					}

					float unitWeight = 0.0F;
					if (capacity && capacity->enabled &&
						!TryGetItemUnitWeightSafe(form, GetInstanceData(stack->extra.get()), unitWeight))
					{
						continue;
					}

					const auto preservationStackCount = stackInfo.totalCount > 0 ?
						stackInfo.totalCount :
						resolvedCount;
					itemRequests.push_back(InventoryTransferRequest{
						form,
						stackIndex,
						resolvedCount,
						unitWeight,
						stack->extra,
						ShouldPreserveStackExtraForTransfer(
							form,
							*stack,
							resolvedCount,
							preservationStackCount),
						stackInfo,
						notifyMovedItems ? GetInventoryItemDisplayNameSafe(item, form, stackIndex) : std::string{}
					});
				}

				for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
				{
					requests.push_back(*it);
				}
			}
		}

		std::int32_t movedStacks = 0;
		for (const auto& request : requests)
		{
			if (passBudget && passBudget->ShouldStop())
			{
				break;
			}

			if (!request.object || request.count <= 0)
			{
				continue;
			}

			float acceptedWeight = 0.0F;
			if (capacity && !capacity->CanAccept(request.unitWeight, request.count, acceptedWeight))
			{
				continue;
			}

			std::int32_t srcBefore = 0;
			std::int32_t destBefore = 0;
			const bool gotSrcBefore = TryGetReferenceItemCountSafe(src, request.object, srcBefore);
			const bool gotDestBefore = TryGetReferenceItemCountSafe(dest, request.object, destBefore);

			auto remaining = request.count;
			bool transferFailed = false;
			auto moveRequest = [&]()
			{
				if (request.preserveStackExtra)
				{
					if (!TryMoveInventoryItemPreservingStackExtraSafe(
							src,
							dest,
							request.object,
							request.count,
							request.stackIndex,
							request.extra))
					{
						REX::WARN(
							"source=native component=inventory_transfer event=transfer_failed reason=instance_preserving_transfer_failed operation=transfer_lootable_inventory_items src={:08X} dest={:08X} item={:08X} count={} stack={}",
							src->formID,
							dest->formID,
							request.object->formID,
							request.count,
							request.stackIndex);
						transferFailed = true;
					}
					else
					{
						remaining = 0;
					}
				}
				while (remaining > 0 && !request.preserveStackExtra)
				{
					if (passBudget && passBudget->ShouldStop())
					{
						break;
					}
					const auto chunk = std::min<std::int32_t>(remaining, 65535);
					if (!TryMoveInventoryItemSafe(
							src,
							dest,
							request.object,
							chunk,
							request.stackIndex))
					{
						REX::WARN(
							"source=native component=inventory_transfer event=transfer_failed reason=move_failed operation=transfer_lootable_inventory_items src={:08X} dest={:08X} item={:08X} remaining={} stack={}",
							src->formID,
							dest->formID,
							request.object->formID,
							remaining,
							request.stackIndex);
						break;
					}
					remaining -= chunk;
				}
			};
			if (dest->IsPlayerRef())
			{
				PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
				moveRequest();
			}
			else
			{
				moveRequest();
			}
			if (transferFailed)
			{
				continue;
			}
			const auto movedCount = request.count - remaining;

			std::int32_t srcAfter = 0;
			std::int32_t destAfter = 0;
			const bool gotSrcAfter = TryGetReferenceItemCountSafe(src, request.object, srcAfter);
			const bool gotDestAfter = TryGetReferenceItemCountSafe(dest, request.object, destAfter);
			const bool observedSourceReduction =
				gotSrcBefore && gotSrcAfter && srcAfter < srcBefore;
			const bool observedDestIncrease =
				gotDestBefore && gotDestAfter && destAfter > destBefore;
			const bool countUnavailable =
				!gotSrcBefore && !gotSrcAfter && !gotDestBefore && !gotDestAfter;

			if (movedCount > 0 && (observedSourceReduction || observedDestIncrease || countUnavailable))
			{
				++movedStacks;
				const auto observedMovedCount = GetObservedTransferCount(
					srcBefore,
					srcAfter,
					gotSrcBefore,
					gotSrcAfter,
					destBefore,
					destAfter,
					gotDestBefore,
					gotDestAfter,
					movedCount);
				if (capacity)
				{
					capacity->Accept(acceptedWeight);
				}
				if (notifyMovedItems)
				{
					auto notificationInfo = request.info;
					notificationInfo.totalCount = observedMovedCount;
					QueueLootItemNotification(
						request.object,
						request.itemName,
						observedMovedCount,
						notificationInfo,
						&matchCache);
				}
			}
			else
			{
				REX::WARN(
					"source=native component=inventory_transfer event=transfer_verification_failed reason=no_observed_transfer operation=transfer_lootable_inventory_items item={:08X} requested_count={} src_before={} src_after={} dest_before={} dest_after={} got_src_before={} got_src_after={} got_dest_before={} got_dest_after={}",
					request.object->formID,
					request.count,
					srcBefore,
					srcAfter,
					destBefore,
					destAfter,
					gotSrcBefore,
					gotSrcAfter,
					gotDestBefore,
					gotDestAfter);
			}
		}

		return movedStacks;
	}

	std::int32_t TransferLootableInventoryItems(
		std::monostate, TESObjectREFR* src, TESObjectREFR* dest, std::uint32_t itemType)
	{
		return TransferLootableInventoryItemsImpl(src, dest, itemType);
	}

	std::int32_t TransferInventoryItems(
		std::monostate,
		TESObjectREFR* src,
		TESObjectREFR* dest,
		std::uint32_t itemType,
		std::int32_t subType,
		BGSKeyword* looseModKeyword,
		bool suppressPlayerMessages)
	{
		std::unique_lock<std::mutex> capacityGuard;
		if (!properties::GetBool(properties::ignore_overweight, true))
		{
			capacityGuard = std::unique_lock<std::mutex>(lootCapacityLock);
		}
		auto capacity = BuildDirectTransferCapacityContext(dest);
		const bool notifyMovedItems = dest && dest->IsPlayerRef() && !suppressPlayerMessages;
		if (dest && dest->IsPlayerRef())
		{
			PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
			return TransferInventoryItemsImpl(
				src,
				dest,
				itemType,
				subType,
				looseModKeyword,
				&capacity,
				notifyMovedItems);
		}

		return TransferInventoryItemsImpl(
			src,
			dest,
			itemType,
			subType,
			looseModKeyword,
			&capacity,
			notifyMovedItems);
	}

	bool IsLootingSafe(std::monostate)
	{
		auto* ui = UI::GetSingleton();
		if (ui && ui->menuMode > 0)
		{
			return false;
		}

		auto* vats = VATS::GetSingleton();
		if (vats && vats->mode == VATS::VATS_MODE_ENUM::kPlayback)
		{
			return false;
		}

		return true;
	}

	void MoveInventoryItem(
		std::monostate,
		TESObjectREFR* src,
		TESObjectREFR* dest,
		TESForm* item,
		std::int32_t count,
		bool silent)
	{
		if (!src || !dest || src == dest || !item)
		{
			return;
		}

		auto* object = item->As<TESBoundObject>();
		if (!object)
		{
			return;
		}

		std::int32_t resolvedCount = count;
		if (resolvedCount < 0 && !TryGetReferenceItemCountSafe(src, object, resolvedCount))
		{
			return;
		}
		if (resolvedCount <= 0)
		{
			return;
		}
		// Single-item moves still need WEAP subtype classification for transfer
		// protection.
		EnsureItemTypeCache();

		std::vector<InventoryFormTransferRequest> requests;
		if (src->IsPlayerRef())
		{
			auto inventoryList = src->inventoryList;
			if (!inventoryList)
			{
				return;
			}

			std::vector<BGSMod::Attachment::Mod*> modBuffer;
			std::int32_t remainingRequested = resolvedCount;
			{
				ReadLockGuard guard(inventoryList->rwLock);
				for (auto& inventoryItem : inventoryList->data)
				{
					if (!inventoryItem.object || inventoryItem.object->formID != object->formID)
					{
						continue;
					}

					const bool formIsFavorite = IsFavorite(object);
					const bool hasFavoriteStack = HasInventoryFavoriteStack(inventoryItem);
					bool retainedFormFavorite = false;
					std::vector<InventoryFormTransferRequest> itemRequests;
					itemRequests.reserve(4);

					std::uint32_t stackIndex = 0;
					for (auto stack = inventoryItem.stackData.get();
					     stack && remainingRequested > 0;
					     stack = stack->nextStack.get(), ++stackIndex)
					{
						InventoryItemInfo stackInfo{};
						if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, inventory_info_basic, stackInfo))
						{
							REX::WARN(
								"source=native component=inventory_transfer event=stack_skipped reason=stack_info_exception operation=move_inventory_item item={:08X}",
								object->formID);
							continue;
						}
						if (stackInfo.totalCount <= 0)
						{
							continue;
						}

						const auto protectedCount = GetPlayerTransferProtectedStackCount(
							object,
							*stack,
							stackInfo,
							true,
							false,
							formIsFavorite,
							hasFavoriteStack,
							retainedFormFavorite);
						const auto movableCount = stackInfo.totalCount - protectedCount;
						if (movableCount <= 0)
						{
							continue;
						}

						const auto requestCount = std::min(movableCount, remainingRequested);
						itemRequests.push_back(InventoryFormTransferRequest{
							object,
							requestCount,
							0.0F,
							stackIndex,
							stack->extra,
							ShouldPreserveStackExtraForTransfer(
								object,
								*stack,
								requestCount,
								stackInfo.totalCount)
						});
						remainingRequested -= requestCount;
					}

					for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
					{
						requests.push_back(*it);
					}
					break;
				}
			}
		}
		else
		{
			auto inventoryList = src->inventoryList;
			if (!inventoryList)
			{
				return;
			}

			std::vector<BGSMod::Attachment::Mod*> modBuffer;
			std::int32_t remainingRequested = resolvedCount;
			{
				ReadLockGuard guard(inventoryList->rwLock);
				for (auto& inventoryItem : inventoryList->data)
				{
					if (!inventoryItem.object || inventoryItem.object->formID != object->formID)
					{
						continue;
					}

					std::vector<InventoryFormTransferRequest> itemRequests;
					itemRequests.reserve(4);
					std::uint32_t stackIndex = 0;
					for (auto stack = inventoryItem.stackData.get();
					     stack && remainingRequested > 0;
					     stack = stack->nextStack.get(), ++stackIndex)
					{
						InventoryItemInfo stackInfo{};
						std::int32_t stackCount = 0;
						if (TryGetInventoryStackInfoSafe(*stack, modBuffer, inventory_info_basic, stackInfo))
						{
							stackCount = stackInfo.totalCount;
						}
						else
						{
							stackCount = static_cast<std::int32_t>(std::min<std::uint32_t>(
								stack->count,
								static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())));
						}
						if (stackCount <= 0)
						{
							continue;
						}

						const auto requestCount = std::min(stackCount, remainingRequested);
						itemRequests.push_back(InventoryFormTransferRequest{
							object,
							requestCount,
							0.0F,
							stackIndex,
							stack->extra,
							ShouldPreserveStackExtraForTransfer(
								object,
								*stack,
								requestCount,
								stackCount)
						});
						remainingRequested -= requestCount;
					}

					for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
					{
						requests.push_back(*it);
					}
					break;
				}
			}
		}

		if (requests.empty())
		{
			return;
		}

		auto move = [&]()
		{
			for (const auto& request : requests)
			{
				auto remaining = request.count;
				if (request.preserveStackExtra)
				{
					if (!TryMoveInventoryItemPreservingStackExtraSafe(
							src,
							dest,
							request.object,
							request.count,
							request.stackIndex,
							request.extra))
					{
						REX::WARN(
							"source=native component=inventory_transfer event=transfer_failed reason=instance_preserving_transfer_failed operation=move_inventory_item src={:08X} dest={:08X} item={:08X} count={} stack={}",
							src->formID,
							dest->formID,
							request.object->formID,
							request.count,
							request.stackIndex ? static_cast<std::int32_t>(*request.stackIndex) : -1);
						remaining = request.count;
					}
					else
					{
						remaining = 0;
					}
				}
				while (remaining > 0 && !request.preserveStackExtra)
				{
					const auto chunk = std::min<std::int32_t>(remaining, 65535);
					if (!TryMoveInventoryItemSafe(
							src,
							dest,
							request.object,
							chunk,
							request.stackIndex))
					{
						REX::WARN(
							"source=native component=inventory_transfer event=transfer_failed reason=move_failed operation=move_inventory_item src={:08X} dest={:08X} item={:08X} remaining={} stack={}",
							src->formID,
							dest->formID,
							item->formID,
							remaining,
							request.stackIndex ? static_cast<std::int32_t>(*request.stackIndex) : -1);
						return;
					}
					remaining -= chunk;
				}
			}
		};

		if ((src->IsPlayerRef() || dest->IsPlayerRef()) && silent)
		{
			PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
			move();
			return;
		}

		move();
	}

	void MoveInventoryItems(
		std::monostate,
		TESObjectREFR* src,
		TESObjectREFR* dest,
		std::uint32_t itemType,
		std::int32_t subType,
		bool silent)
	{
		const auto startedAt = Clock::now();
		if (!src || !dest || src == dest || itemType > all_item)
		{
			return;
		}

		std::int32_t movedItems = 0;
		if ((src->IsPlayerRef() || dest->IsPlayerRef()) && silent)
		{
			PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
			movedItems = TransferInventoryItemsImpl(src, dest, itemType, subType, nullptr);
		}
		else
		{
			movedItems = TransferInventoryItemsImpl(src, dest, itemType, subType, nullptr);
		}

		if (movedItems > 0)
		{
			REX::DEBUG(
				"source=native component=inventory_transfer event=move_inventory_items_summary src={:08X} dest={:08X} item_type={} sub_type={} moved={} elapsed_ms={:.3f}",
				src->formID,
				dest->formID,
				itemType,
				subType,
				movedItems,
				ElapsedMilliseconds(startedAt));
		}
	}
}
