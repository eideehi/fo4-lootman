#include "papyrus_lootman_internal.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace papyrus_lootman
{
	using namespace RE;

	bool TryLootWorldReference(
		TESObjectREFR* ref,
		TESObjectREFR* dest,
		TESObjectREFR* player,
		bool playPickupSound,
		LootCapacityContext* capacity)
	{
		auto* object = ref ? ref->GetObjectReference() : nullptr;
		if (!ref || !dest || !object)
		{
			return false;
		}
		const bool notifyMovedItems = ShouldNotifyLootDestination(dest);
		const auto worldCount = GetWorldReferenceItemCount(ref);
		if (worldCount <= 0)
		{
			return false;
		}
		const auto itemName = notifyMovedItems ? GetFormName(ref) : std::string{};
		auto notificationInfo = notifyMovedItems
			? BuildWorldReferenceNotificationInfo(ref, object, worldCount)
			: InventoryItemInfo{};

		float acceptedWeight = 0.0F;
		float unitWeight = 0.0F;
		if (capacity && capacity->enabled)
		{
			if (!TryGetItemUnitWeightSafe(object, GetInstanceData(ref), unitWeight) ||
				!capacity->CanAccept(unitWeight, worldCount, acceptedWeight))
			{
				return false;
			}
		}

		std::int32_t beforeCount = 0;
		const bool gotBefore = TryGetReferenceItemCountSafe(dest, object, beforeCount);
		if (playPickupSound)
		{
			PlayPickUpSound(std::monostate{}, player, ref);
		}

		const auto moved = [&]()
		{
			if (player && player->IsPlayerRef())
			{
				PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
				return TryAddWorldReferenceToContainerSafe(dest, ref, worldCount);
			}
			return TryAddWorldReferenceToContainerSafe(dest, ref, worldCount);
		}();
		if (!moved)
		{
			REX::WARN(
				"source=native component=loot_nearby event=world_ref_add_failed ref={:08X} base={:08X} count={} dest={:08X}",
				ref->formID,
				object->formID,
				worldCount,
				dest->formID);
			return false;
		}

		std::int32_t afterCount = 0;
		const bool gotAfter = TryGetReferenceItemCountSafe(dest, object, afterCount);
		if (gotBefore && gotAfter && afterCount > beforeCount)
		{
			FinalizeWorldPickup(std::monostate{}, ref);
			const auto movedCount = GetObservedMovedCount(
				beforeCount,
				afterCount,
				gotBefore,
				gotAfter,
				worldCount);
			if (capacity)
			{
				capacity->Accept(acceptedWeight);
			}
			if (notifyMovedItems)
			{
				notificationInfo.totalCount = movedCount;
				QueueLootItemNotification(object, itemName, movedCount, notificationInfo);
			}
			return true;
		}

		REX::WARN(
			"source=native component=loot_nearby event=world_transfer_verification_failed ref={:08X} base={:08X} count={} before={} after={} got_before={} got_after={}",
			ref->formID,
			object->formID,
			worldCount,
			beforeCount,
			afterCount,
			gotBefore,
			gotAfter);
		return false;
	}

	bool TryLootDeferredActivationAmmoReference(
		TESObjectREFR* ref,
		TESObjectREFR* dest,
		TESObjectREFR* player,
		bool playPickupSound,
		LootCapacityContext* capacity)
	{
		auto* form = ref ? ref->GetObjectReference() : nullptr;
		auto* object = form ? form->As<TESBoundObject>() : nullptr;
		if (!ref || !dest || !player || !object)
		{
			return false;
		}

		const auto worldCount = GetWorldReferenceItemCount(ref);
		if (worldCount <= 0)
		{
			return false;
		}

		float acceptedWeight = 0.0F;
		float unitWeight = 0.0F;
		if (capacity && capacity->enabled)
		{
			if (!TryGetItemUnitWeightSafe(object, GetInstanceData(ref), unitWeight) ||
				!capacity->CanAccept(unitWeight, worldCount, acceptedWeight))
			{
				return false;
			}
		}

		std::int32_t playerBefore = 0;
		const bool gotPlayerBefore = TryGetReferenceItemCountSafe(player, object, playerBefore);
		const auto activated = [&]()
		{
			if (player->IsPlayerRef())
			{
				PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
				return TryActivateRefSafe(ref, player, false);
			}
			return TryActivateRefSafe(ref, player, false);
		}();
		if (!activated)
		{
			return false;
		}

		if (playPickupSound)
		{
			PlayPickUpSound(std::monostate{}, player, ref);
		}

		std::int32_t playerAfter = 0;
		const bool gotPlayerAfter = TryGetReferenceItemCountSafe(player, object, playerAfter);
		std::int32_t movedCount = 0;
		if (gotPlayerBefore && gotPlayerAfter && playerAfter > playerBefore)
		{
			movedCount = playerAfter - playerBefore;
		}
		else if (!gotPlayerBefore || !gotPlayerAfter)
		{
			movedCount = worldCount;
		}

		if (movedCount > 0 && dest != player)
		{
			auto remaining = movedCount;
			auto moveActivatedAmmo = [&]()
			{
				while (remaining > 0)
				{
					const auto chunk = std::min<std::int32_t>(remaining, 65535);
					if (!TryMoveInventoryItemSafe(player, dest, object, chunk))
					{
						REX::WARN(
							"source=native component=loot_nearby event=deferred_activation_transfer_failed player={:08X} dest={:08X} item={:08X} remaining={}",
							player->formID,
							dest->formID,
							object->formID,
							remaining);
						break;
					}
					remaining -= chunk;
				}
			};
			if (player->IsPlayerRef())
			{
				PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
				moveActivatedAmmo();
			}
			else
			{
				moveActivatedAmmo();
			}
			movedCount -= remaining;
		}

		if (movedCount > 0 && capacity)
		{
			capacity->Accept(acceptedWeight);
		}

		if (movedCount > 0 && ShouldNotifyLootDestination(dest))
		{
			auto notificationInfo = BuildWorldReferenceNotificationInfo(ref, object, movedCount);
			notificationInfo.totalCount = movedCount;
			QueueLootItemNotification(
				object,
				GetFormName(ref),
				movedCount,
				notificationInfo);
		}

		return movedCount > 0 || activated;
	}

	bool TryLootActivationReference(
		TESObjectREFR* ref,
		TESObjectREFR* actionRef,
		TESObjectREFR* player,
		bool playPickupSound,
		LootCapacityContext* capacity)
	{
		TESBoundObject* expectedItem = nullptr;
		auto* baseObject = ref ? ref->GetObjectReference() : nullptr;
		auto* flora = baseObject ? baseObject->As<TESFlora>() : nullptr;
		expectedItem = flora ? flora->produceItem : nullptr;
		const bool notifyMovedItems = expectedItem && ShouldNotifyLootDestination(actionRef);
		InventoryItemInfo notificationInfo{};
		notificationInfo.totalCount = 1;
		const auto itemName = notifyMovedItems ? GetFormName(expectedItem) : std::string{};

		float acceptedWeight = 0.0F;
		if (capacity && capacity->enabled)
		{
			float unitWeight = 0.0F;
			if (!expectedItem ||
				!TryGetItemUnitWeightSafe(expectedItem, nullptr, unitWeight) ||
				!capacity->CanAccept(unitWeight, 1, acceptedWeight))
			{
				return false;
			}
		}

		std::int32_t beforeCount = 0;
		const bool gotBefore = expectedItem && TryGetReferenceItemCountSafe(actionRef, expectedItem, beforeCount);
		const auto activated = [&]()
		{
			if (actionRef && actionRef->IsPlayerRef())
			{
				PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
				return TryActivateRefSafe(ref, actionRef, false);
			}
			return TryActivateRefSafe(ref, actionRef, false);
		}();
		if (!activated)
		{
			return false;
		}

		if (playPickupSound)
		{
			PlayPickUpSound(std::monostate{}, player, ref);
		}
		std::int32_t afterCount = 0;
		const bool gotAfter =
			expectedItem &&
			(capacity || notifyMovedItems) &&
			TryGetReferenceItemCountSafe(actionRef, expectedItem, afterCount);
		if (capacity && expectedItem)
		{
			if ((!gotBefore && !gotAfter) || (gotBefore && gotAfter && afterCount > beforeCount))
			{
				capacity->Accept(acceptedWeight);
			}
		}
		if (notifyMovedItems)
		{
			const auto movedCount = GetObservedMovedCount(beforeCount, afterCount, gotBefore, gotAfter, 1);
			notificationInfo.totalCount = movedCount;
			QueueLootItemNotification(
				expectedItem,
				itemName,
				movedCount,
				notificationInfo);
		}
		return true;
	}

	// Container animations call Activate; trap setups may keep activation/link
	// metadata on nearby refs instead of the container itself.
	inline constexpr float kNearbyContainerTrapProbeRadius = 768.0F;
	inline constexpr float kNearbyContainerTrapProbeRadiusSq =
		kNearbyContainerTrapProbeRadius * kNearbyContainerTrapProbeRadius;

	bool HasActivationOrLinkSideEffectExtras(TESObjectREFR* ref)
	{
		auto* extraList = ref ? ref->extraList.get() : nullptr;
		return extraList &&
		       (extraList->HasType(EXTRA_DATA_TYPE::kActivateRef) ||
		        extraList->HasType(EXTRA_DATA_TYPE::kActivateRefChildren) ||
		        extraList->HasType(EXTRA_DATA_TYPE::kOpenCloseActivateRef) ||
		        extraList->HasType(EXTRA_DATA_TYPE::kLinkedRef) ||
		        extraList->HasType(EXTRA_DATA_TYPE::kLinkedRefChildren));
	}

	bool HasNearbyActivationOrLinkSideEffectRef(TESObjectREFR* ref)
	{
		auto* cell = ref ? ref->GetParentCell() : nullptr;
		if (!cell || cell->cellState != TESObjectCELL::CELL_STATE::kAttached)
		{
			return false;
		}

		const auto origin = ref->GetPosition();
		std::uint32_t nearbyFormId = 0;
		std::uint32_t nearbyBaseFormId = 0;
		float nearbyDistanceSq = 0.0F;
		{
			BSAutoLock guard(cell->spinLock);
			for (auto& objPtr : cell->references)
			{
				auto* other = objPtr.get();
				if (!other || other == ref || !CheckPrecondition(other))
				{
					continue;
				}

				const auto pos = other->GetPosition();
				const auto dx = origin.x - pos.x;
				const auto dy = origin.y - pos.y;
				const auto dz = origin.z - pos.z;
				const auto distanceSq = dx * dx + dy * dy + dz * dz;
				if (distanceSq > kNearbyContainerTrapProbeRadiusSq)
				{
					continue;
				}

				auto* otherBase = other->GetObjectReference();
				if (otherBase && otherBase->GetFormType() == ENUM_FORM_ID::kCONT)
				{
					continue;
				}
				if (!HasActivationOrLinkSideEffectExtras(other))
				{
					continue;
				}

				nearbyFormId = other->formID;
				nearbyBaseFormId = otherBase ? otherBase->formID : 0;
				nearbyDistanceSq = distanceSq;
				break;
			}
		}

		if (nearbyFormId == 0)
		{
			return false;
		}

		REX::DEBUG(
			"source=native component=container_loot event=animation_skipped reason=nearby_activation_link_ref ref={:08X} nearby_ref={:08X} base={:08X} distance_sq={}",
			ref->formID,
			nearbyFormId,
			nearbyBaseFormId,
			nearbyDistanceSq);
		return true;
	}

	bool IsContainerAnimationCandidate(TESObjectREFR* ref)
	{
		if (!ref || ref->GetFullyLoaded3D() == nullptr)
		{
			return false;
		}
		if (HasActivationOrLinkSideEffectExtras(ref))
		{
			REX::DEBUG(
				"source=native component=container_loot event=animation_skipped reason=activation_link_extras ref={:08X}",
				ref->formID);
			return false;
		}
		return !HasNearbyActivationOrLinkSideEffectRef(ref);
	}
}
