#include "papyrus_lootman_internal.h"

#include "runtime_probe.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

namespace papyrus_lootman
{
	using namespace RE;

	namespace
	{
		std::atomic<std::uint32_t> inventoryProbeRecords{ 0 };
		constexpr std::uint32_t kInventoryProbeRecordLimit = 512;
	}

	struct ExtraCountData : BSExtraData
	{
		static constexpr auto TYPE{ EXTRA_DATA_TYPE::kCount };

		std::uint16_t count;  // 18
	};
	static_assert(sizeof(BSExtraData) == 0x18);

	struct WorldReferenceAddCallContext
	{
		TESObjectREFR* dest = nullptr;
		TESBoundObject* object = nullptr;
		BSTSmartPointer<ExtraDataList> extra;
		TESObjectREFR* oldContainer = nullptr;
		std::int32_t count = 1;
		// Owned by the caller's frame: an SEH fault inside the guarded call
		// skips destructors under /EHsc, so the copy target must not be a
		// local of the guarded frame or its allocation leaks on every fault.
		BSTSmartPointer<ExtraDataList> extraCopy;
	};

	void InvokeWorldReferenceAddCall(void* opaque)
	{
		auto* context = static_cast<WorldReferenceAddCallContext*>(opaque);
		// Hand the destination a deep copy of the world reference's extra data
		// instead of the reference's own list. AddObjectToContainer stores the
		// passed list in the new inventory stack while the world reference still
		// points at it, and the pickup finalization (MarkAsDeleted) then tears
		// the reference down; with a shared list that teardown can cost the
		// looted item its object-instance (weapon/armor mod) data.
		if (context->extra && context->extraCopy)
		{
			context->extraCopy->CopyList(context->extra.get());
		}
		context->dest->AddObjectToContainer(
			context->object,
			context->extraCopy,
			context->count,
			context->oldContainer,
			ITEM_REMOVE_REASON::kStoreContainer);
	}

	std::int32_t GetWorldReferenceItemCount(TESObjectREFR* ref)
	{
		auto* extraList = ref ? ref->extraList.get() : nullptr;
		if (!extraList)
		{
			return ref ? 1 : 0;
		}

		auto* extraCount = static_cast<ExtraCountData*>(
			extraList->GetByType(EXTRA_DATA_TYPE::kCount));
		if (!extraCount || extraCount->count == 0)
		{
			return 1;
		}

		return static_cast<std::int32_t>(extraCount->count);
	}

	bool TryAddWorldReferenceToContainerSafe(TESObjectREFR* dest, TESObjectREFR* ref, std::int32_t count)
	{
		if (!dest || !ref || dest == ref || count <= 0)
		{
			return false;
		}

		auto* object = ref->GetObjectReference();
		if (!object)
		{
			return false;
		}

		WorldReferenceAddCallContext context{
			dest,
			object,
			ref->extraList,
			ref,
			count
		};
		if (context.extra)
		{
			context.extraCopy = BSTSmartPointer<ExtraDataList>(new ExtraDataList());
		}
		return ExecuteSehCallSafe(&InvokeWorldReferenceAddCall, &context);
	}

	struct AddInventoryItemCallContext
	{
		TESObjectREFR* dest = nullptr;
		TESBoundObject* object = nullptr;
		BSTSmartPointer<ExtraDataList> extra;
		std::uint32_t count = 0;
	};

	void InvokeAddInventoryItemCall(void* opaque)
	{
		auto* context = static_cast<AddInventoryItemCallContext*>(opaque);
		context->dest->AddInventoryItem(
			context->object,
			context->extra,
			context->count,
			nullptr,
			nullptr,
			nullptr);
	}

	bool TryAddInventoryItemSafe(
		TESObjectREFR* dest,
		TESBoundObject* object,
		std::uint32_t count,
		BSTSmartPointer<ExtraDataList> extra)
	{
		if (!dest || !object || count == 0)
		{
			return count == 0;
		}

		AddInventoryItemCallContext context{
			dest,
			object,
			std::move(extra),
			count
		};
		return ExecuteSehCallSafe(&InvokeAddInventoryItemCall, &context);
	}

	struct ActivateRefCallContext
	{
		TESObjectREFR* ref = nullptr;
		TESObjectREFR* actionRef = nullptr;
		bool defaultProcessingOnly = false;
		bool result = false;
	};

	void InvokeActivateRefCall(void* opaque)
	{
		auto* context = static_cast<ActivateRefCallContext*>(opaque);
		context->result = context->ref->ActivateRef(
			context->actionRef,
			nullptr,
			1,
			context->defaultProcessingOnly,
			true,
			false);
	}

	bool TryActivateRefSafe(TESObjectREFR* ref, TESObjectREFR* actionRef, bool defaultProcessingOnly)
	{
		if (!ref || !actionRef)
		{
			return false;
		}

		ActivateRefCallContext context{
			ref,
			actionRef,
			defaultProcessingOnly,
			false
		};
		return ExecuteSehCallSafe(&InvokeActivateRefCall, &context) && context.result;
	}

	struct RemoveItemsCallContext
	{
		TESObjectREFR* ref = nullptr;
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
	};

	void InvokeRemoveItemsCall(void* opaque)
	{
		auto* context = static_cast<RemoveItemsCallContext*>(opaque);
		TESObjectREFR::RemoveItemData removeData(context->object, context->count);
		context->ref->RemoveItem(removeData);
	}

	bool TryRemoveItemsSafe(TESObjectREFR* ref, TESBoundObject* object, std::int32_t count)
	{
		if (!ref || !object || count <= 0)
		{
			return count == 0;
		}

		RemoveItemsCallContext context{
			ref,
			object,
			count
		};
		return ExecuteSehCallSafe(&InvokeRemoveItemsCall, &context);
	}

	struct MoveInventoryItemCallContext
	{
		TESObjectREFR* src = nullptr;
		TESObjectREFR* dest = nullptr;
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
		std::optional<std::uint32_t> stackIndex;
	};

	void InvokeMoveInventoryItemCall(void* opaque)
	{
		auto* context = static_cast<MoveInventoryItemCallContext*>(opaque);
		TESObjectREFR::RemoveItemData removeData(context->object, context->count);
		removeData.reason = ITEM_REMOVE_REASON::kStoreContainer;
		removeData.otherContainer = context->dest;
		if (context->stackIndex)
		{
			removeData.stackData.push_back(*context->stackIndex);
		}
		context->src->RemoveItem(removeData);
	}

	bool TryMoveInventoryItemSafe(
		TESObjectREFR* src,
		TESObjectREFR* dest,
		TESBoundObject* object,
		std::int32_t count,
		std::optional<std::uint32_t> stackIndex)
	{
		if (!src || !dest || !object || count <= 0)
		{
			return count == 0;
		}

		MoveInventoryItemCallContext context{
			src,
			dest,
			object,
			count,
			stackIndex
		};
		return ExecuteSehCallSafe(&InvokeMoveInventoryItemCall, &context);
	}

	struct RemoveScrapSourceCallContext
	{
		TESObjectREFR* owner = nullptr;
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
		std::optional<std::uint32_t> stackIndex;
	};

	void InvokeRemoveScrapSourceCall(void* opaque)
	{
		auto* context = static_cast<RemoveScrapSourceCallContext*>(opaque);
		TESObjectREFR::RemoveItemData removeData(context->object, context->count);
		if (context->stackIndex)
		{
			removeData.stackData.push_back(*context->stackIndex);
		}
		context->owner->RemoveItem(removeData);
	}

	bool TryRemoveScrapSourceSafe(
		TESObjectREFR* owner,
		TESBoundObject* object,
		std::int32_t count,
		std::optional<std::uint32_t> stackIndex)
	{
		if (!owner || !object || count <= 0)
		{
			return false;
		}

		RemoveScrapSourceCallContext context{
			owner,
			object,
			count,
			stackIndex
		};
		return ExecuteSehCallSafe(&InvokeRemoveScrapSourceCall, &context);
	}

	void TraceInventoryStackSnapshot(
		TESObjectREFR* owner,
		TESBoundObject* object,
		std::optional<std::uint32_t> stackIndex,
		std::uintptr_t expectedStackIdentity,
		std::int32_t snapshotCount,
		std::int32_t requestedCount,
		const char* operation,
		const char* mode)
	{
		if (!runtime_probe::IsEnabled())
		{
			return;
		}

		if (!runtime_probe::TryReserve(inventoryProbeRecords, kInventoryProbeRecordLimit))
		{
			return;
		}

		const char* outcome = "invalid_request";
		std::int32_t observedCount = 0;
		bool identityMatch = false;
		bool entryProbeFailed = false;
		if (owner && object && stackIndex && expectedStackIdentity != 0)
		{
			auto* inventoryList = owner->inventoryList;
			if (!inventoryList)
			{
				outcome = "inventory_missing";
			}
			else
			{
				ReadLockGuard guard(inventoryList->rwLock);
				std::uint32_t itemCount = 0;
				if (!TryGetInventoryItemCountSafe(inventoryList, itemCount))
				{
					outcome = "item_count_probe_failed";
				}
				else
				{
					outcome = "item_missing";
					for (std::uint32_t itemIndex = 0; itemIndex < itemCount; ++itemIndex)
					{
						TESForm* entryForm = nullptr;
						BGSInventoryItem::Stack* stack = nullptr;
						if (!TryGetInventoryEntrySafe(inventoryList, itemIndex, entryForm, stack))
						{
							entryProbeFailed = true;
							continue;
						}
						if (entryForm != object)
						{
							continue;
						}

						std::uint32_t currentIndex = 0;
						while (stack && currentIndex < *stackIndex)
						{
							BGSInventoryItem::Stack* nextStack = nullptr;
							if (!TryGetNextStackSafe(stack, nextStack))
							{
								outcome = "stack_link_probe_failed";
								stack = nullptr;
								break;
							}
							stack = nextStack;
							++currentIndex;
						}

						if (!stack)
						{
							if (outcome != "stack_link_probe_failed"sv)
							{
								outcome = "stack_missing";
							}
							break;
						}

						identityMatch = reinterpret_cast<std::uintptr_t>(stack) == expectedStackIdentity;
						InventoryItemInfo currentInfo{};
						if (!TryBuildFallbackStackInfoSafe(*stack, currentInfo))
						{
							outcome = "stack_info_probe_failed";
						}
						else
						{
							observedCount = currentInfo.totalCount;
							outcome = identityMatch ?
								(observedCount == snapshotCount ? "match" : "count_changed") :
								"identity_mismatch";
						}
						break;
					}
					if (outcome == "item_missing"sv && entryProbeFailed)
					{
						outcome = "entry_probe_failed";
					}
				}
			}
		}

		const auto sequence = runtime_probe::NextSequence();
		REX::TRACE(
			"source=native component=runtime_probe event=stack_snapshot_recheck probe_schema=1 ordering=reservation_only seq={} thread_id={} operation={} mode={} owner={:08X} item={:08X} stack={} snapshot_count={} requested_count={} observed_count={} identity_match={} outcome={}",
			sequence,
			REX::W32::GetCurrentThreadId(),
			operation ? operation : "unknown",
			mode ? mode : "unknown",
			owner ? owner->formID : 0,
			object ? object->formID : 0,
			stackIndex ? static_cast<std::int32_t>(*stackIndex) : -1,
			snapshotCount,
			requestedCount,
			observedCount,
			identityMatch,
			outcome);
	}

	struct TransferExtraPresenceCallContext
	{
		ExtraDataList* extra = nullptr;
		bool hasObjectInstance = false;
		bool hasInstanceData = false;
	};

	void InvokeTransferExtraPresenceCall(void* opaque)
	{
		auto* context = static_cast<TransferExtraPresenceCallContext*>(opaque);
		if (!context->extra)
		{
			return;
		}

		context->hasObjectInstance = context->extra->HasType(EXTRA_DATA_TYPE::kObjectInstance);
		context->hasInstanceData = context->extra->HasType(EXTRA_DATA_TYPE::kInstanceData);
	}

	bool TryHasTransferRelevantExtraSafe(ExtraDataList* extra, bool& outResult)
	{
		outResult = false;
		if (!extra)
		{
			return true;
		}

		TransferExtraPresenceCallContext context{ extra };
		if (!ExecuteSehCallSafe(&InvokeTransferExtraPresenceCall, &context))
		{
			return false;
		}

		outResult = context.hasObjectInstance || context.hasInstanceData;
		return true;
	}

	bool ShouldPreserveStackExtraForTransfer(
		TESBoundObject* object,
		ExtraDataList* extra,
		std::int32_t movingCount,
		std::int32_t stackCount)
	{
		if (!object || movingCount <= 0 || movingCount != stackCount)
		{
			return false;
		}

		const auto formType = object->GetFormType();
		if (formType != ENUM_FORM_ID::kWEAP && formType != ENUM_FORM_ID::kARMO)
		{
			return false;
		}

		bool hasRelevantExtra = false;
		return TryHasTransferRelevantExtraSafe(extra, hasRelevantExtra) && hasRelevantExtra;
	}

	bool TryMoveInventoryItemPreservingStackExtraSafe(
		TESObjectREFR* src,
		TESObjectREFR* dest,
		TESBoundObject* object,
		std::int32_t count,
		std::optional<std::uint32_t> stackIndex,
		BSTSmartPointer<ExtraDataList> extra)
	{
		if (!src || !dest || !object || count <= 0 || !extra)
		{
			return false;
		}

		std::int32_t destBefore = 0;
		const bool gotDestBefore = TryGetReferenceItemCountSafe(dest, object, destBefore);

		// Remove from the source first. The destination add and the source removal
		// cannot be made atomic, so order them for the safe failure mode: if the
		// removal faults (TryRemoveScrapSourceSafe returns false under SEH), the
		// item is still in the source and nothing was added to the destination.
		// Adding first and then failing to remove would leave the instance-bearing
		// stack in BOTH containers, i.e. a genuine item duplication.
		if (!TryRemoveScrapSourceSafe(src, object, count, stackIndex))
		{
			return false;
		}

		if (!TryAddInventoryItemSafe(dest, object, static_cast<std::uint32_t>(count), extra))
		{
			// An SEH return does not prove whether AddInventoryItem committed its side
			// effect. Only when the destination count verifiably did not change is the
			// add known to be uncommitted; then re-adding the same instance extra to
			// the source cannot create two owners for one extra. In every other case
			// (count increased, or either count query failed) the extra may already
			// be owned by the destination, so never reuse it.
			std::int32_t destAfter = 0;
			const bool gotDestAfter = TryGetReferenceItemCountSafe(dest, object, destAfter);
			if (gotDestBefore && gotDestAfter && destAfter == destBefore)
			{
				if (TryAddInventoryItemSafe(src, object, static_cast<std::uint32_t>(count), std::move(extra)))
				{
					REX::WARN(
						"source=native component=inventory_transfer event=instance_preserving_move_rolled_back reason=dest_add_failed src={:08X} dest={:08X} item={:08X} count={} stack={}",
						src->formID,
						dest->formID,
						object->formID,
						count,
						stackIndex ? static_cast<std::int32_t>(*stackIndex) : -1);
					return false;
				}
				REX::WARN(
					"source=native component=inventory_transfer event=instance_preserving_move_unrecoverable reason=dest_add_and_source_restore_failed src={:08X} dest={:08X} item={:08X} count={} stack={}",
					src->formID,
					dest->formID,
					object->formID,
					count,
					stackIndex ? static_cast<std::int32_t>(*stackIndex) : -1);
				return false;
			}
			REX::WARN(
				"source=native component=inventory_transfer event=instance_preserving_move_unrecoverable reason=dest_add_outcome_unknown src={:08X} dest={:08X} item={:08X} count={} stack={}",
				src->formID,
				dest->formID,
				object->formID,
				count,
				stackIndex ? static_cast<std::int32_t>(*stackIndex) : -1);
			return false;
		}

		return true;
	}
}
