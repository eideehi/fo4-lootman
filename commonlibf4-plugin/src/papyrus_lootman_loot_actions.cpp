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

		// Resolve the display name / notification payload only after the capacity gate passes, so a
		// destination at capacity does not pay name resolution for every rejected item.
		const auto itemName = notifyMovedItems ? GetFormName(ref) : std::string{};
		auto notificationInfo = notifyMovedItems
			? BuildWorldReferenceNotificationInfo(ref, object, worldCount)
			: InventoryItemInfo{};

		std::int32_t beforeCount = 0;
		const bool gotBefore = TryGetReferenceItemCountSafe(dest, object, beforeCount);

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
		const bool observedDestIncrease = gotBefore && gotAfter && afterCount > beforeCount;
		// The add already committed at this point. Any failed count read makes the
		// verification inconclusive, and treating "inconclusive" as failure would
		// leave the world reference live and unsuppressed while the destination may
		// keep the item, so the next pass would loot it again and duplicate it. Only a
		// conclusive no-increase reading or an available zero post-count may reject.
		// A missing pre-add count is still inconclusive when the post-add probe sees
		// at least one item: the item may have pre-existed, but finalizing avoids a
		// possible duplicate after a void add. A successful zero post-count is
		// different: it affirmatively proves the destination has none of the item,
		// so keep the world reference instead of turning uncertainty into item loss.
		const bool verificationInconclusive =
			!gotAfter || (!gotBefore && afterCount > 0);
		if (verificationInconclusive)
		{
			REX::WARN(
				"source=native component=loot_nearby event=world_transfer_verification_inconclusive ref={:08X} base={:08X} count={} got_before={} got_after={}",
				ref->formID,
				object->formID,
				worldCount,
				gotBefore,
				gotAfter);
		}
		if (observedDestIncrease || verificationInconclusive)
		{
			// Play the pickup cue only now that the transfer is confirmed, so the
			// player never hears a pickup sound for an item that failed to move.
			if (playPickupSound)
			{
				PlayPickUpSound(std::monostate{}, player, ref);
			}
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

		std::int32_t playerAfter = 0;
		const bool gotPlayerAfter = TryGetReferenceItemCountSafe(player, object, playerAfter);
		std::int32_t movedCount = 0;
		bool observedPlayerDelta = false;
		if (gotPlayerBefore && gotPlayerAfter && playerAfter > playerBefore)
		{
			movedCount = playerAfter - playerBefore;
			observedPlayerDelta = true;
		}
		else if (!gotPlayerBefore || !gotPlayerAfter)
		{
			// A count read failed under SEH. Assume the activation deposited the
			// world count for capacity/notification purposes, but never relay an
			// unverified amount out of the player inventory below: if the activation
			// actually deposited less, the relay would siphon the player's own
			// pre-existing ammo of this type into the loot destination.
			movedCount = worldCount;
		}

		if (playPickupSound && (observedPlayerDelta || !gotPlayerBefore || !gotPlayerAfter))
		{
			// Play the pickup cue only once the player-side delta confirms the
			// activation deposited something (mirrors TryLootWorldReference). An
			// unreadable count stays inconclusive and still plays, so a probe
			// failure never silences a pickup that really happened.
			PlayPickUpSound(std::monostate{}, player, ref);
		}

		if (movedCount > 0 && dest != player && !observedPlayerDelta)
		{
			REX::WARN(
				"source=native component=loot_nearby event=deferred_activation_relay_skipped reason=unverified_player_delta ref={:08X} item={:08X} assumed_count={} got_before={} got_after={}",
				ref->formID,
				object->formID,
				movedCount,
				gotPlayerBefore,
				gotPlayerAfter);
			// The activation may have deposited the ammo into the player, but without
			// an observed delta we cannot relay any amount safely. Do not charge or
			// notify the configured non-player destination for items it did not receive.
			movedCount = 0;
		}

		if (movedCount > 0 && dest != player && observedPlayerDelta)
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
			// Charge the capacity budget for what actually moved, not the full
			// world count: a partial activation delta would otherwise over-debit
			// the shared budget and wrongly reject later lootable items.
			const float chargedWeight = (capacity->enabled && movedCount < worldCount)
				? unitWeight * static_cast<float>(movedCount)
				: acceptedWeight;
			capacity->Accept(chargedWeight);
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
		LootCapacityContext* capacity,
		ActivationOutcome* outOutcome)
	{
		// Default to kNotAttempted so every gate ahead of the activation reports the
		// outcome that neither strikes nor clears the reference, without each early
		// return having to remember to say so.
		const auto reportOutcome = [&](ActivationOutcome outcome)
		{
			if (outOutcome)
			{
				*outOutcome = outcome;
			}
		};
		reportOutcome(ActivationOutcome::kNotAttempted);

		TESBoundObject* expectedItem = nullptr;
		auto* baseObject = ref ? ref->GetObjectReference() : nullptr;
		auto* flora = baseObject ? baseObject->As<TESFlora>() : nullptr;
		expectedItem = flora ? flora->produceItem : nullptr;
		const bool notifyMovedItems = expectedItem && ShouldNotifyLootDestination(actionRef);
		InventoryItemInfo notificationInfo{};
		notificationInfo.totalCount = 1;

		float acceptedWeight = 0.0F;
		if (capacity && capacity->enabled)
		{
			// Log each capacity rejection separately: activation refs (ACTI/FLOR)
			// have no other per-reference diagnostics, so a silently failing gate
			// here is indistinguishable from a scan-level skip in field reports.
			float unitWeight = 0.0F;
			if (!expectedItem)
			{
				REX::DEBUG(
					"source=native component=loot_nearby event=activation_skipped reason=no_produce_item ref={:08X} base={:08X}",
					ref ? ref->formID : 0,
					baseObject ? baseObject->formID : 0);
				return false;
			}
			if (!TryGetItemUnitWeightSafe(expectedItem, nullptr, unitWeight))
			{
				REX::DEBUG(
					"source=native component=loot_nearby event=activation_skipped reason=unit_weight_unavailable ref={:08X} produce={:08X}",
					ref ? ref->formID : 0,
					expectedItem->formID);
				return false;
			}
			if (!capacity->CanAccept(unitWeight, 1, acceptedWeight))
			{
				REX::DEBUG(
					"source=native component=loot_nearby event=activation_skipped reason=capacity_rejected ref={:08X} produce={:08X} unit_weight={:.3f}",
					ref ? ref->formID : 0,
					expectedItem->formID,
					unitWeight);
				return false;
			}
		}

		// Resolve the display name only after the capacity gate passes (mirrors TryLootWorldReference).
		const auto itemName = notifyMovedItems ? GetFormName(expectedItem) : std::string{};

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
			// A refusal is evidence about the reference, not about the destination, so
			// it feeds strike accounting like an activation that produced nothing.
			reportOutcome(ActivationOutcome::kActivatedNoYield);
			REX::DEBUG(
				"source=native component=loot_nearby event=activation_skipped reason=activation_failed ref={:08X} base={:08X}",
				ref ? ref->formID : 0,
				baseObject ? baseObject->formID : 0);
			return false;
		}

		// The produce probe runs for every activation that has a produce item, whatever
		// the caller asked for: it is the sole evidence that a flora activation
		// produced anything, so it cannot be conditional on a capacity context or a
		// notification destination being present. Flora is the one synchronously
		// verifiable case, because the engine adds TESFlora::produceItem inside the
		// activation call itself.
		std::int32_t afterCount = 0;
		const bool gotAfter =
			expectedItem && TryGetReferenceItemCountSafe(actionRef, expectedItem, afterCount);
		// A plain activator (no produce item) starts from "yielded": it hands its items
		// out from an OnActivate script, and the VM dispatches that event asynchronously,
		// so nothing readable at this point could observe the delivery. Guessing "no
		// yield" here would silence the pickup cue and undercount successful objects for
		// every legitimate activator pickup, so the verdict is deferred instead - the
		// caller marks the reference and the next pass that re-collects it settles the
		// question (see MarkActivationAwaitingYieldEvidence).
		// For flora an unreadable probe stays inconclusive and counts as a yield, for
		// the same reason TryLootWorldReference treats inconclusive verification as
		// success: uncertainty must never be turned into a suppression of a reference
		// that really did hand something over.
		bool activationYielded = true;
		if (expectedItem)
		{
			const bool observedProduceIncrease = gotBefore && gotAfter && afterCount > beforeCount;
			const bool produceProbeInconclusive = !gotAfter || (!gotBefore && afterCount > 0);
			activationYielded = observedProduceIncrease || produceProbeInconclusive;
			reportOutcome(
				activationYielded ? ActivationOutcome::kYielded : ActivationOutcome::kActivatedNoYield);
		}
		else
		{
			reportOutcome(ActivationOutcome::kAwaitingEvidence);
		}

		if (playPickupSound && activationYielded)
		{
			// Flora gates the cue on the produce probe, so a harvest the engine accepted
			// without producing anything stays silent. A plain activator always reaches
			// here with activationYielded still true, which keeps its cue exactly as
			// unconditional as the engine's own pickup is.
			PlayPickUpSound(std::monostate{}, player, ref);
		}
		if (capacity && expectedItem)
		{
			if ((!gotBefore && !gotAfter) || (gotBefore && gotAfter && afterCount > beforeCount))
			{
				capacity->Accept(acceptedWeight);
			}
		}
		if (notifyMovedItems && activationYielded)
		{
			const auto movedCount = GetObservedMovedCount(beforeCount, afterCount, gotBefore, gotAfter, 1);
			notificationInfo.totalCount = movedCount;
			QueueLootItemNotification(
				expectedItem,
				itemName,
				movedCount,
				notificationInfo);
		}
		if (!activationYielded)
		{
			REX::DEBUG(
				"source=native component=loot_nearby event=activation_no_yield ref={:08X} base={:08X} produce={:08X}",
				ref ? ref->formID : 0,
				baseObject ? baseObject->formID : 0,
				expectedItem ? expectedItem->formID : 0);
		}
		return activationYielded;
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
