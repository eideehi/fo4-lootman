#include "papyrus_lootman_internal.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace papyrus_lootman
{
	using namespace RE;

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
	};

	void InvokeWorldReferenceAddCall(void* opaque)
	{
		auto* context = static_cast<WorldReferenceAddCallContext*>(opaque);
		context->dest->AddObjectToContainer(
			context->object,
			context->extra,
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
		removeData.a_otherContainer = context->dest;
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
		const BGSInventoryItem::Stack& stack,
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
		return TryHasTransferRelevantExtraSafe(stack.extra.get(), hasRelevantExtra) && hasRelevantExtra;
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

		if (!TryAddInventoryItemSafe(dest, object, static_cast<std::uint32_t>(count), std::move(extra)))
		{
			return false;
		}

		if (!TryRemoveScrapSourceSafe(src, object, count, stackIndex))
		{
			REX::WARN(
				"Inventory transfer: source removal failed after instance-preserving add, src={:08X}, dest={:08X}, item={:08X}, count={}, stack={}",
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
