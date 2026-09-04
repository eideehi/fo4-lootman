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
		if (useTimeBudget)
		{
			// Time-stop once at least one object has been looted (so a pass always
			// makes progress), or, when a dense cell keeps rejecting every
			// candidate (processedObjects stays 0), on a bounded scan cadence so
			// the elapsed-time guard cannot be starved into an unbounded
			// main-thread scan. The cadence caps the worst-case overrun at ~64
			// candidate evaluations past the budget.
			const bool timeCheckDue =
				processedObjects > 0 || (scannedObjects > 0 && (scannedObjects & 0x3F) == 0);
			if (timeCheckDue && ElapsedMilliseconds(startedAt) >= timeBudgetMs)
			{
				hitTimeBudget = true;
				return true;
			}
		}
		++scannedObjects;
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
					for (auto stack = item.stackData.get(); stack;)
					{
						auto* currentStack = stack;
						const auto currentStackIndex = stackIndex;
						// Advance eagerly through the SEH guard so the skip paths below never re-read
						// a suspect link raw; a faulting link ends the chain after the current stack.
						BGSInventoryItem::Stack* nextStack = nullptr;
						stack = TryGetNextStackSafe(currentStack, nextStack) ? nextStack : nullptr;
						++stackIndex;

						InventoryItemInfo stackInfo{};
						if (!TryGetInventoryStackInfoSafe(*currentStack, modBuffer, requestInfoFlags, stackInfo))
						{
							REX::WARN(
								"source=native component=inventory_transfer event=stack_skipped reason=stack_info_exception operation=transfer_inventory_items item={:08X}",
								form->formID);
							continue;
						}

						bool includedQuestItem = false;
						if ((stackInfo.questItem &&
							 (!TryMatchesAnyCachedSafe(
								  form,
								  injection_data::include_quest_item,
								  &matchCache,
								  includedQuestItem) ||
							  !includedQuestItem)) ||
							stackInfo.dropped ||
							stackInfo.totalCount <= 0)
						{
							continue;
						}

						// Transfer protection classifies the WEAP subtype, which can re-raise a
						// recoverable match-probe fault. Catch it inside the helper's own SEH frame
						// so the fault cannot unwind past this scope's ReadLockGuard, and fail closed
						// by skipping the suspect stack instead of moving it unprotected.
						std::int32_t protectedCount = 0;
						if (!TryGetPlayerTransferProtectedStackCountSafe(
								form,
								*currentStack,
								stackInfo,
								sourceIsPlayer,
								sourceIsDead,
								formIsFavorite,
								hasFavoriteStack,
								retainedFormFavorite,
								protectedCount))
						{
							REX::WARN(
								"source=native component=inventory_transfer event=stack_skipped reason=protected_count_exception operation=transfer_inventory_items item={:08X}",
								form->formID);
							continue;
						}
						const auto movableCount = stackInfo.totalCount - protectedCount;
						if (movableCount <= 0)
						{
							continue;
						}

						float unitWeight = 0.0F;
						if (capacity && capacity->enabled)
						{
							// Mirror the lootable path: extra-list contents are
							// runtime-mutated engine data, so resolve the instance
							// data behind an SEH guard instead of walking it raw.
							TBO_InstanceData* instanceData = nullptr;
							if (!TryGetInstanceDataSafe(currentStack->extra.get(), instanceData) ||
							    !TryGetItemUnitWeightSafe(form, instanceData, unitWeight))
							{
								continue;
							}
						}

						itemRequests.push_back(InventoryFormTransferRequest{
							form,
							movableCount,
							unitWeight,
							currentStackIndex,
							currentStack->extra,
							ShouldPreserveStackExtraForTransfer(
								form,
								currentStack->extra.get(),
								movableCount,
								stackInfo.totalCount),
							stackInfo,
							notifyMovedItems ? GetInventoryItemDisplayNameSafe(item, form, currentStackIndex) : std::string{}
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
				for (auto stack = item.stackData.get(); stack;)
				{
					auto* currentStack = stack;
					const auto currentStackIndex = stackIndex;
					// Advance eagerly through the SEH guard so the skip paths below never re-read
					// a suspect link raw; a faulting link ends the chain after the current stack.
					BGSInventoryItem::Stack* nextStack = nullptr;
					stack = TryGetNextStackSafe(currentStack, nextStack) ? nextStack : nullptr;
					++stackIndex;

					InventoryItemInfo stackInfo{};
					if (!TryGetInventoryStackInfoSafe(*currentStack, modBuffer, requestInfoFlags, stackInfo))
					{
						REX::WARN(
							"source=native component=inventory_transfer event=stack_skipped reason=stack_info_exception operation=transfer_inventory_items item={:08X}",
							form->formID);
						continue;
					}
					bool includedQuestItem = false;
					if ((stackInfo.questItem &&
						 (!TryMatchesAnyCachedSafe(
							  form,
							  injection_data::include_quest_item,
							  &matchCache,
							  includedQuestItem) ||
						  !includedQuestItem)) ||
						stackInfo.dropped ||
						(stackInfo.equipped && !sourceIsDead) ||
						stackInfo.totalCount <= 0)
					{
						continue;
					}

					float unitWeight = 0.0F;
					if (capacity && capacity->enabled)
					{
						// Mirror the lootable path: extra-list contents are
						// runtime-mutated engine data, so resolve the instance
						// data behind an SEH guard instead of walking it raw.
						TBO_InstanceData* instanceData = nullptr;
						if (!TryGetInstanceDataSafe(currentStack->extra.get(), instanceData) ||
						    !TryGetItemUnitWeightSafe(form, instanceData, unitWeight))
						{
							continue;
						}
					}

					itemRequests.push_back(InventoryFormTransferRequest{
						form,
						stackInfo.totalCount,
						unitWeight,
						currentStackIndex,
						currentStack->extra,
						ShouldPreserveStackExtraForTransfer(
							form,
							currentStack->extra.get(),
							stackInfo.totalCount,
							stackInfo.totalCount),
						stackInfo,
						notifyMovedItems ? GetInventoryItemDisplayNameSafe(item, form, currentStackIndex) : std::string{}
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
		const PropertiesSnapshot* props,
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
		// Reuse the caller's pass-invariant snapshot when provided (mirrors HasLootableItem) so a nearby-loot
		// pass does not re-capture the properties snapshot for every container/corpse it transfers from.
		PropertiesSnapshot localProps;
		if (!props)
		{
			localProps = PropertiesSnapshot::Capture();
			props = &localProps;
		}
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
			// Hold the inventory read lock for the whole scan, mirroring HasLootableItem: every engine-memory
			// access below is routed through an SEH-guarded Try...Safe helper so a corrupt entry becomes a
			// skipped stack instead of an unwind past this frame. Under /EHsc an SEH unwind skips C++
			// destructors, so an unguarded fault here would leak the read lock and deadlock the next writer.
			ReadLockGuard guard(inventoryList->rwLock);

			std::uint32_t inventoryItemCount = 0;
			if (!TryGetInventoryItemCountSafe(inventoryList, inventoryItemCount))
			{
				inventoryItemCount = 0;
			}
			for (std::uint32_t itemIndex = 0; itemIndex < inventoryItemCount; ++itemIndex)
			{
				if (passBudget && passBudget->ShouldStop())
				{
					break;
				}

				TESForm* entryForm = nullptr;
				BGSInventoryItem::Stack* firstStack = nullptr;
				if (!TryGetInventoryEntrySafe(inventoryList, itemIndex, entryForm, firstStack) || !entryForm)
				{
					continue;
				}
				// BGSInventoryItem::object is declared TESBoundObject*, so this static downcast
				// only restores the pointer's original type.
				auto* form = static_cast<TESBoundObject*>(entryForm);

				ENUM_FORM_ID formType{};
				if (!TryGetFormTypeSafe(form, formType))
				{
					continue;
				}
				if (!IsFormTypeMatchesItemType(formType, itemType))
				{
					continue;
				}

				bool validForm = false;
				const bool gotValidForm = TryIsValidFormSafe(
					form,
					props,
					&matchCache,
					validForm);
				if (!gotValidForm || !validForm)
				{
					continue;
				}

				bool lootableForm = false;
				const bool gotLootableForm = TryIsLootableFormSafe(
					form,
					props,
					&matchCache,
					lootableForm);
				if (!gotLootableForm || !lootableForm)
				{
					continue;
				}

				std::vector<InventoryTransferRequest> itemRequests;
				std::uint32_t stackIndex = 0;
				for (auto stack = firstStack; stack;)
				{
					if (passBudget && passBudget->ShouldStop())
					{
						break;
					}

					auto* currentStack = stack;
					const auto currentStackIndex = stackIndex;
					// Advance eagerly through the SEH guard so the skip paths below never re-read a
					// suspect link raw; a faulting link ends the chain after the current stack.
					BGSInventoryItem::Stack* nextStack = nullptr;
					stack = TryGetNextStackSafe(currentStack, nextStack) ? nextStack : nullptr;
					++stackIndex;

					InventoryItemInfo stackInfo{};
					BSTSmartPointer<ExtraDataList> stackExtra;
					if (TryGetInventoryStackInfoSafe(
							*currentStack,
							modBuffer,
							inventory_info_full,
							stackInfo))
					{
						// The guarded full-info read already walked this stack, so copying its
						// extra pointer raw cannot fault.
						stackExtra = currentStack->extra;
					}
					// The stack-info read above already faulted on this stack; every further read of
					// the same memory must stay behind an SEH guard too (BuildFallbackStackInfo and
					// the extra copy dereference the same suspect stack).
					else if (!TryBuildFallbackStackInfoSafe(*currentStack, stackInfo) ||
					         !TryGetStackExtraSafe(*currentStack, stackExtra))
					{
						REX::WARN(
							"source=native component=inventory_transfer event=stack_skipped reason=stack_info_exception operation=transfer_lootable_inventory_items item={:08X}",
							form->formID);
						continue;
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
					bool lootableStack = false;
					if (!TryIsLootableInventoryItemSafe(form, stackInfo, props, &matchCache, lootableStack) ||
					    !lootableStack)
					{
						continue;
					}

					float unitWeight = 0.0F;
					if (capacity && capacity->enabled)
					{
						TBO_InstanceData* instanceData = nullptr;
						if (!TryGetInstanceDataSafe(stackExtra.get(), instanceData) ||
						    !TryGetItemUnitWeightSafe(form, instanceData, unitWeight))
						{
							continue;
						}
					}

					const auto preservationStackCount = stackInfo.totalCount > 0 ?
						stackInfo.totalCount :
						resolvedCount;
					itemRequests.push_back(InventoryTransferRequest{
						form,
						currentStackIndex,
						resolvedCount,
						unitWeight,
						stackExtra,
						ShouldPreserveStackExtraForTransfer(
							form,
							stackExtra.get(),
							resolvedCount,
							preservationStackCount),
						stackInfo,
						notifyMovedItems ?
							GetInventoryItemDisplayNameSafe(inventoryList->data[itemIndex], form, currentStackIndex) :
							std::string{}
					});
				}

				for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
				{
					requests.push_back(std::move(*it));
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
		// Read enable_carry_weight_limit once so the lock decision and the capacity
		// context's enabled state cannot disagree when the MCM flips the property
		// between the two reads mid-transfer.
		const bool trackCapacity = properties::GetBool(properties::enable_carry_weight_limit, false);
		std::unique_lock<std::mutex> capacityGuard;
		if (trackCapacity)
		{
			capacityGuard = std::unique_lock<std::mutex>(lootCapacityLock);
		}
		auto capacity = BuildDirectTransferCapacityContext(dest, trackCapacity);
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
					for (auto stack = inventoryItem.stackData.get(); stack && remainingRequested > 0;)
					{
						auto* currentStack = stack;
						const auto currentStackIndex = stackIndex;
						// Advance eagerly through the SEH guard so the skip paths below never re-read
						// a suspect link raw; a faulting link ends the chain after the current stack.
						BGSInventoryItem::Stack* nextStack = nullptr;
						stack = TryGetNextStackSafe(currentStack, nextStack) ? nextStack : nullptr;
						++stackIndex;

						InventoryItemInfo stackInfo{};
						if (!TryGetInventoryStackInfoSafe(*currentStack, modBuffer, inventory_info_basic, stackInfo))
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

						// Transfer protection classifies the WEAP subtype, which can re-raise a
						// recoverable match-probe fault. Catch it inside the helper's own SEH frame
						// so the fault cannot unwind past this scope's ReadLockGuard, and fail closed
						// by skipping the suspect stack instead of moving it unprotected.
						std::int32_t protectedCount = 0;
						if (!TryGetPlayerTransferProtectedStackCountSafe(
								object,
								*currentStack,
								stackInfo,
								true,
								false,
								formIsFavorite,
								hasFavoriteStack,
								retainedFormFavorite,
								protectedCount))
						{
							REX::WARN(
								"source=native component=inventory_transfer event=stack_skipped reason=protected_count_exception operation=move_inventory_item item={:08X}",
								object->formID);
							continue;
						}
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
							currentStackIndex,
							currentStack->extra,
							ShouldPreserveStackExtraForTransfer(
								object,
								currentStack->extra.get(),
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
					for (auto stack = inventoryItem.stackData.get(); stack && remainingRequested > 0;)
					{
						auto* currentStack = stack;
						const auto currentStackIndex = stackIndex;
						// Advance eagerly through the SEH guard so the skip paths below never re-read
						// a suspect link raw; a faulting link ends the chain after the current stack.
						BGSInventoryItem::Stack* nextStack = nullptr;
						stack = TryGetNextStackSafe(currentStack, nextStack) ? nextStack : nullptr;
						++stackIndex;

						InventoryItemInfo stackInfo{};
						std::int32_t stackCount = 0;
						BSTSmartPointer<ExtraDataList> stackExtra;
						if (TryGetInventoryStackInfoSafe(*currentStack, modBuffer, inventory_info_basic, stackInfo))
						{
							stackCount = stackInfo.totalCount;
							// The guarded info read already walked this stack, so copying its extra
							// pointer raw cannot fault.
							stackExtra = currentStack->extra;
						}
						else
						{
							// The info read above already faulted on this stack; the count and extra
							// reads touch the same suspect memory, so keep them behind SEH guards too.
							InventoryItemInfo fallbackInfo{};
							if (!TryBuildFallbackStackInfoSafe(*currentStack, fallbackInfo) ||
							    !TryGetStackExtraSafe(*currentStack, stackExtra))
							{
								REX::WARN(
									"source=native component=inventory_transfer event=stack_skipped reason=stack_info_exception operation=move_inventory_item item={:08X}",
									object->formID);
								continue;
							}
							stackCount = fallbackInfo.totalCount;
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
							currentStackIndex,
							stackExtra,
							ShouldPreserveStackExtraForTransfer(
								object,
								stackExtra.get(),
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
