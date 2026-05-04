#include "papyrus_lootman_internal.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

#include "properties.h"

namespace papyrus_lootman
{
	using namespace RE;

	struct ObjectEntry
	{
		NiPointer<TESObjectREFR> obj;
		float distanceSquared = 0.0f;
	};

	PropertiesSnapshot PropertiesSnapshot::Capture()
	{
		PropertiesSnapshot s;
		s.notLootingFromSettlement = properties::GetBool(properties::not_looting_from_settlement);
		s.lootingLegendaryOnly = properties::GetBool(properties::looting_legendary_only);
		s.alwaysLootingExplosives = properties::GetBool(properties::always_looting_explosives);
		s.lootableAlchItemType = properties::GetInt(properties::lootable_alch_item_type);
		s.lootableBookItemType = properties::GetInt(properties::lootable_book_item_type);
		s.lootableMiscItemType = properties::GetInt(properties::lootable_misc_item_type);
		s.lootableWeapItemType = properties::GetInt(properties::lootable_weap_item_type);
		s.lootableInventoryItemType = properties::GetInt(properties::lootable_inventory_item_type);
		return s;
	}

	inline constexpr std::size_t kLootPassBucketCount = 12;
	inline constexpr std::size_t kLootPassResultProcessedObjects = 0;
	inline constexpr std::size_t kLootPassResultSuccessfulObjects = 1;
	inline constexpr std::size_t kLootPassResultMovedStacks = 2;
	inline constexpr std::size_t kLootPassResultHitObjectLimit = 3;
	inline constexpr std::size_t kLootPassResultHitTimeBudget = 4;
	inline constexpr std::size_t kLootPassResultCandidateObjects = 5;
	inline constexpr std::size_t kLootPassResultBucketOffset = 6;
	inline constexpr std::size_t kLootPassResultSize = kLootPassResultBucketOffset + kLootPassBucketCount;

	using ReferenceStateFlagGetter = bool (*)(TESObjectREFR*, std::uint32_t);
	using ReferenceStateFlagSetter = void (*)(TESObjectREFR*, std::uint32_t);

	using ObjectEmptyStateSetter = void (*)(TESForm*, bool);

	struct GetOpenStateCallContext
	{
		const TESObjectREFR* ref = nullptr;
		BGSOpenCloseForm::OPEN_STATE state = BGSOpenCloseForm::OPEN_STATE::kNone;
	};

	struct OpenCloseBoolQueryCallContext
	{
		const TESObjectREFR* ref = nullptr;
		bool result = false;
	};

	// ---- Cell collection ----

	template <typename FilterFn>
	void CollectFromCells(
		TESObjectREFR* actorRef,
		const NiPoint3& origin,
		float maxDistanceSq,
		bool notLootingFromSettlement,
		FilterFn&& filter,
		std::vector<ObjectEntry>& buffer)
	{
		auto collect = [&](TESObjectCELL* cell)
		{
			if (!cell) return;
			if (cell->cellState != TESObjectCELL::CELL_STATE::kAttached) return;
			if (!IsOwnerEmptyOrFriend(cell->GetOwner()))
			{
				return;
			}

			if (notLootingFromSettlement)
			{
				auto cellEZ = cell->GetEncounterZone();
				if (IsSettlement(cellEZ))
				{
					return;
				}
			}

			BSAutoLock guard(cell->spinLock);
			for (auto& objPtr : cell->references)
			{
				auto obj = objPtr.get();
				if (!obj)
				{
					continue;
				}
				auto* baseObj = obj->GetObjectReference();
				if (!baseObj)
				{
					continue;
				}
				if (!filter(baseObj->GetFormType()))
				{
					continue;
				}
				if (!CheckPrecondition(obj))
				{
					continue;
				}

				auto pos = obj->GetPosition();
				auto dx = origin.x - pos.x;
				auto dy = origin.y - pos.y;
				auto dz = origin.z - pos.z;
				auto distSq = dx * dx + dy * dy + dz * dz;
				if (distSq > 0.0f && distSq <= maxDistanceSq)
				{
					buffer.push_back({ objPtr, distSq });
				}
			}
		};

		auto parentCell = actorRef ? actorRef->GetParentCell() : nullptr;
		if (parentCell && parentCell->IsInterior())
		{
			collect(parentCell);
		}
		else
		{
			auto* tes = TES::GetSingleton();
			auto* gridCells = tes ? tes->gridCells : nullptr;
			bool collectedAnyCell = false;

			if (gridCells && gridCells->dimension > 0)
			{
				auto dim = gridCells->dimension;
				for (std::uint32_t y = 0; y < dim; ++y)
				{
					for (std::uint32_t x = 0; x < dim; ++x)
					{
						auto* gridCell = gridCells->Get(x, y);
						auto* loadedCell = gridCell ? gridCell->cell : nullptr;
						if (!loadedCell) continue;

						collect(loadedCell);
						collectedAnyCell = true;
					}
				}
			}

			if (!collectedAnyCell && parentCell)
			{
				collect(parentCell);
			}
		}
	}

	// ================================================================
	// Papyrus native functions (10 v2.2.0 functions)
	// ================================================================

	std::vector<TESObjectREFR*> FindNearbyReferencesWithFormTypeImpl(
		TESObjectREFR* ref, std::uint32_t formType, bool requirePapyrusObjectHandle)
	{
		std::vector<TESObjectREFR*> result;

		auto ui = UI::GetSingleton();
		if (ui && ui->pauseMenuDisableCt.load_unchecked() > 0)
		{
			return result;
		}

		if (!ref)
		{
			return result;
		}

		EnsureItemTypeCache();
		auto propsSnapshot = PropertiesSnapshot::Capture();
		auto origin = ref->GetPosition();
		auto lootingRange = std::clamp(properties::GetFloat(properties::looting_range), 0.0f, 200.0f);
		auto maxDistance = lootingRange * 100.0f;
		auto maxDistanceSq = maxDistance * maxDistance;
		auto notLootingFromSettlement = propsSnapshot.notLootingFromSettlement;
		auto maxItemsRaw = properties::GetInt(properties::max_items_processed_per_thread);
		auto maxItemsClamped = std::clamp(
			maxItemsRaw,
			1,
			static_cast<int>(kMaxItemsProcessedPerThreadLimit));
		auto maxItemsProcessedPerThread = static_cast<std::size_t>(maxItemsClamped);

		if (maxDistance < 1.0f)
		{
			return result;
		}

		auto matchType = static_cast<ENUM_FORM_ID>(formType);
		std::vector<ObjectEntry> buffer;
		buffer.reserve(1024);
		CollectFromCells(ref, origin, maxDistanceSq, notLootingFromSettlement,
			[matchType](ENUM_FORM_ID ft)
			{
				return IsFormTypeMatch(ft, matchType);
			},
			buffer);
		std::sort(buffer.begin(), buffer.end(),
			[](const ObjectEntry& lhs, const ObjectEntry& rhs)
			{
				return lhs.distanceSquared < rhs.distanceSquared;
			});

		std::vector<TESObjectREFR*> tmp;
		tmp.reserve(maxItemsProcessedPerThread);
		std::vector<BGSMod::Attachment::Mod*> equipmentBuffer;
		MatchCache matchCache;
		matchCache.results.reserve(buffer.size() * 2);
		for (const auto& entry : buffer)
		{
			auto* obj = entry.obj.get();
			if (!obj) continue;

			auto* baseObj = obj->GetObjectReference();
			if (!baseObj) continue;
			if (UsesWorldReferenceTransfer(matchType) && IsRecentlyLootedWorldRef(obj))
			{
				continue;
			}

			bool validForm = false;
			const bool gotValidForm = TryIsValidFormSafe(
				baseObj, &propsSnapshot, &matchCache, validForm);
			if (!gotValidForm)
			{
				continue;
			}
			if (!validForm)
			{
				continue;
			}

			bool lootableForm = false;
			const bool gotLootableForm = TryIsLootableFormSafe(
				baseObj, &propsSnapshot, &matchCache, lootableForm);
			if (!gotLootableForm)
			{
				continue;
			}
			if (!lootableForm)
			{
				continue;
			}

			bool validObject = false;
			const bool gotValidObject = TryIsValidObjectSafe(
				obj, &propsSnapshot, baseObj, &matchCache, validObject);
			if (!gotValidObject)
			{
				continue;
			}
			if (!validObject)
			{
				continue;
			}

			bool lootableObject = false;
			const bool gotLootableObject = TryIsLootableObjectSafe(
				obj,
				&propsSnapshot,
				baseObj,
				&equipmentBuffer,
				&matchCache,
				lootableObject);
			if (!gotLootableObject)
			{
				continue;
			}
			if (!lootableObject)
			{
				continue;
			}

			if (requirePapyrusObjectHandle && !IsPapyrusObjectHandleAvailable(obj))
			{
				continue;
			}

			if (TryLockObject(obj))
			{
				tmp.push_back(obj);
				if (tmp.size() >= maxItemsProcessedPerThread)
				{
					break;
				}
			}
		}

		// Reverse for Papyrus loop ordering (closest items processed last = first in Papyrus reverse loop)
		std::reverse(tmp.begin(), tmp.end());
		result = std::move(tmp);

		return result;
	}

	// 1. FindNearbyReferencesWithFormType
	std::vector<TESObjectREFR*> FindNearbyReferencesWithFormType(
		std::monostate, TESObjectREFR* ref, std::uint32_t formType)
	{
		return FindNearbyReferencesWithFormTypeImpl(ref, formType, true);
	}

	std::vector<std::int32_t> FindNearbyReferenceIdsWithFormType(
		std::monostate, TESObjectREFR* ref, std::uint32_t formType)
	{
		auto refs = FindNearbyReferencesWithFormTypeImpl(ref, formType, false);
		std::vector<std::int32_t> ids;
		ids.reserve(refs.size());
		for (auto* obj : refs)
		{
			if (!obj)
			{
				continue;
			}

			ids.push_back(static_cast<std::int32_t>(obj->formID));
		}
		return ids;
	}

	std::int32_t FindNearestValidWorkshopId(std::monostate, TESObjectREFR* ref)
	{
		auto* workshop = TryFindNearestValidWorkshop(ref);
		return workshop ? static_cast<std::int32_t>(workshop->formID) : 0;
	}

	std::int32_t LootNearbyReferences(
		std::monostate,
		TESObjectREFR* player,
		TESObjectREFR* dest,
		TESObjectREFR* activator,
		TESObjectREFR* workshop,
		std::uint32_t formType,
		std::uint32_t itemType,
		bool playPickupSound,
		bool playContainerAnimation,
		bool unlockLockedContainer,
		TESForm* bobbyPin,
		BGSPerk* locksmith01,
		BGSPerk* locksmith02,
		BGSPerk* locksmith03,
		BGSPerk* locksmith04)
	{
		if (!player || !dest)
		{
			return 0;
		}

		auto refs = FindNearbyReferencesWithFormTypeImpl(player, formType, false);
		if (refs.empty())
		{
			return 0;
		}

		// `FindNearbyReferencesWithFormTypeImpl` reverses the result for the
		// old Papyrus reverse loop. Native processing should preserve nearest
		// first ordering directly.
		std::reverse(refs.begin(), refs.end());

		const auto candidateCount = refs.size();
		const auto matchType = static_cast<ENUM_FORM_ID>(formType);
		std::unique_lock<std::mutex> capacityGuard;
		if (!properties::GetBool(properties::ignore_overweight, true))
		{
			capacityGuard = std::unique_lock<std::mutex>(lootCapacityLock);
		}
		auto capacity = BuildLootCapacityContext(player, dest, workshop);
		std::int32_t successfulObjects = 0;
		std::int32_t movedStacks = 0;

		for (auto* ref : refs)
		{
			const auto lockedFormId = ref ? ref->formID : 0;
			struct ReleaseGuard
			{
				std::uint32_t formId = 0;
				~ReleaseGuard()
				{
					if (formId != 0)
					{
						UnlockObject(formId);
					}
				}
			} releaseGuard{ lockedFormId };

			if (!ref)
			{
				continue;
			}

			auto* baseForm = ref->GetObjectReference();
			if (!baseForm)
			{
				continue;
			}

			const auto actualFormType = baseForm->GetFormType();
			if (actualFormType == ENUM_FORM_ID::kCONT)
			{
				if (IsReferenceLockedForLooting(ref) &&
					!TryUnlockContainerForLooting(
						ref,
						player,
						workshop,
						bobbyPin,
						locksmith01,
						locksmith02,
						locksmith03,
						locksmith04,
						unlockLockedContainer))
				{
					continue;
				}

				const auto moved = TransferLootableInventoryItemsImpl(
					ref,
					dest,
					itemType,
					&capacity);
				if (moved > 0)
				{
					++successfulObjects;
					movedStacks += moved;
					if (playContainerAnimation && activator && IsContainerAnimationCandidate(ref))
					{
						(void)TryActivateRefSafe(ref, activator, true);
					}
				}
				continue;
			}

			if (actualFormType == ENUM_FORM_ID::kNPC_)
			{
				const auto moved = TransferLootableInventoryItemsImpl(
					ref,
					dest,
					itemType,
					&capacity);
				if (moved > 0)
				{
					++successfulObjects;
					movedStacks += moved;
					if (playPickupSound)
					{
						PlayPickUpSound(std::monostate{}, player, ref);
					}
				}
				continue;
			}

			if (actualFormType == ENUM_FORM_ID::kACTI || actualFormType == ENUM_FORM_ID::kFLOR)
			{
				if (TryLootActivationReference(ref, dest, player, playPickupSound, &capacity))
				{
					++successfulObjects;
				}
				continue;
			}

			if (UsesWorldReferenceTransfer(matchType))
			{
				if (TryLootWorldReference(ref, dest, player, playPickupSound, &capacity))
				{
					++successfulObjects;
				}
			}
		}

		REX::DEBUG(
			"LootNearbyReferences: formType={}, candidates={}, successfulObjects={}, movedStacks={}",
			formType,
			candidateCount,
			successfulObjects,
			movedStacks);

		return static_cast<std::int32_t>(
			std::min<std::size_t>(
				candidateCount,
				static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));
	}

	std::vector<std::int32_t> LootNearbyEnabledReferences(
		std::monostate,
		TESObjectREFR* player,
		TESObjectREFR* dest,
		TESObjectREFR* activator,
		TESObjectREFR* workshop,
		std::uint32_t enabledFormTypeMask,
		std::uint32_t itemType,
		bool playPickupSound,
		bool playContainerAnimation,
		bool unlockLockedContainer,
		TESForm* bobbyPin,
		BGSPerk* locksmith01,
		BGSPerk* locksmith02,
		BGSPerk* locksmith03,
		BGSPerk* locksmith04)
	{
		std::vector<std::int32_t> result(kLootPassResultSize, 0);
		if (!player || !dest || enabledFormTypeMask == 0)
		{
			return result;
		}

		auto ui = UI::GetSingleton();
		if (ui && ui->pauseMenuDisableCt.load_unchecked() > 0)
		{
			return result;
		}

		EnsureItemTypeCache();
		auto propsSnapshot = PropertiesSnapshot::Capture();
		auto origin = player->GetPosition();
		auto lootingRange = std::clamp(properties::GetFloat(properties::looting_range), 0.0F, 200.0F);
		auto maxDistance = lootingRange * 100.0F;
		auto maxDistanceSq = maxDistance * maxDistance;
		if (maxDistance < 1.0F)
		{
			return result;
		}

		std::vector<ObjectEntry> buffer;
		buffer.reserve(1024);
		CollectFromCells(
			player,
			origin,
			maxDistanceSq,
			propsSnapshot.notLootingFromSettlement,
			[enabledFormTypeMask](ENUM_FORM_ID formType)
			{
				return (FormTypeToEnabledBit(formType) & enabledFormTypeMask) != 0;
			},
			buffer);
		std::sort(
			buffer.begin(),
			buffer.end(),
			[](const ObjectEntry& lhs, const ObjectEntry& rhs)
			{
				return lhs.distanceSquared < rhs.distanceSquared;
			});

		result[kLootPassResultCandidateObjects] = static_cast<std::int32_t>(
			std::min<std::size_t>(
				buffer.size(),
				static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));

		std::unique_lock<std::mutex> capacityGuard;
		if (!properties::GetBool(properties::ignore_overweight, true))
		{
			capacityGuard = std::unique_lock<std::mutex>(lootCapacityLock);
		}
		auto capacity = BuildLootCapacityContext(player, dest, workshop);
		auto budget = LootPassBudget::Capture();
		MatchCache matchCache;
		matchCache.results.reserve(buffer.size() * 2);
		std::vector<BGSMod::Attachment::Mod*> equipmentBuffer;

		for (const auto& entry : buffer)
		{
			if (budget.ShouldStop())
			{
				break;
			}

			auto* ref = entry.obj.get();
			if (!ref)
			{
				continue;
			}

			auto* baseForm = ref->GetObjectReference();
			if (!baseForm)
			{
				continue;
			}

			const auto actualFormType = baseForm->GetFormType();
			const auto enabledBit = FormTypeToEnabledBit(actualFormType);
			if ((enabledBit & enabledFormTypeMask) == 0)
			{
				continue;
			}
			if (!budget.CanProcessCategory(actualFormType))
			{
				continue;
			}
			if (UsesWorldReferenceTransfer(actualFormType) && IsRecentlyLootedWorldRef(ref))
			{
				continue;
			}

			bool validForm = false;
			const bool gotValidForm = TryIsValidFormSafe(
				baseForm,
				&propsSnapshot,
				&matchCache,
				validForm);
			if (!gotValidForm || !validForm)
			{
				continue;
			}

			bool lootableForm = false;
			const bool gotLootableForm = TryIsLootableFormSafe(
				baseForm,
				&propsSnapshot,
				&matchCache,
				lootableForm);
			if (!gotLootableForm || !lootableForm)
			{
				continue;
			}

			bool validObject = false;
			const bool gotValidObject = TryIsValidObjectSafe(
				ref,
				&propsSnapshot,
				baseForm,
				&matchCache,
				validObject);
			if (!gotValidObject || !validObject)
			{
				continue;
			}

			bool lootableObject = false;
			const bool gotLootableObject = TryIsLootableObjectSafe(
				ref,
				&propsSnapshot,
				baseForm,
				&equipmentBuffer,
				&matchCache,
				lootableObject);
			if (!gotLootableObject || !lootableObject)
			{
				continue;
			}

			if (!TryLockObject(ref))
			{
				continue;
			}

			struct ReleaseGuard
			{
				std::uint32_t formId = 0;
				~ReleaseGuard()
				{
					if (formId != 0)
					{
						UnlockObject(formId);
					}
				}
			} releaseGuard{ ref->formID };

			budget.MarkProcessed(actualFormType);
			result[kLootPassResultProcessedObjects] = static_cast<std::int32_t>(
				std::min<std::size_t>(
					budget.processedObjects,
					static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));
			const auto bucketIndex = FormTypeToBucketIndex(actualFormType);
			if (bucketIndex >= 0)
			{
				++result[kLootPassResultBucketOffset + static_cast<std::size_t>(bucketIndex)];
			}

			bool successful = false;
			std::int32_t movedStacks = 0;
			if (actualFormType == ENUM_FORM_ID::kCONT)
			{
				if (IsReferenceLockedForLooting(ref) &&
					!TryUnlockContainerForLooting(
						ref,
						player,
						workshop,
						bobbyPin,
						locksmith01,
						locksmith02,
						locksmith03,
						locksmith04,
						unlockLockedContainer))
				{
					continue;
				}

				movedStacks = TransferLootableInventoryItemsImpl(
					ref,
					dest,
					itemType,
					&capacity,
					&budget);
				successful = movedStacks > 0;
				if (successful && playContainerAnimation && activator && IsContainerAnimationCandidate(ref))
				{
					(void)TryActivateRefSafe(ref, activator, true);
				}
			}
			else if (actualFormType == ENUM_FORM_ID::kNPC_)
			{
				movedStacks = TransferLootableInventoryItemsImpl(
					ref,
					dest,
					itemType,
					&capacity,
					&budget);
				successful = movedStacks > 0;
				if (successful && playPickupSound)
				{
					PlayPickUpSound(std::monostate{}, player, ref);
				}
			}
			else if (actualFormType == ENUM_FORM_ID::kACTI || actualFormType == ENUM_FORM_ID::kFLOR)
			{
				successful = TryLootActivationReference(ref, dest, player, playPickupSound, &capacity);
			}
			else if (UsesWorldReferenceTransfer(actualFormType))
			{
				successful = IsDeferredActivationAmmoCandidate(ref, baseForm)
					? TryLootDeferredActivationAmmoReference(ref, dest, player, playPickupSound, &capacity)
					: TryLootWorldReference(ref, dest, player, playPickupSound, &capacity);
			}

			if (successful)
			{
				++result[kLootPassResultSuccessfulObjects];
				result[kLootPassResultMovedStacks] += movedStacks;
			}
		}

		if (budget.processedObjects >= budget.hardMaxObjects ||
			(!budget.useTimeBudget && budget.processedObjects >= budget.maxObjects))
		{
			budget.hitObjectLimit = true;
		}
		if (budget.useTimeBudget && budget.processedObjects > 0 &&
			ElapsedMilliseconds(budget.startedAt) >= budget.timeBudgetMs)
		{
			budget.hitTimeBudget = true;
		}

		result[kLootPassResultHitObjectLimit] = budget.hitObjectLimit ? 1 : 0;
		result[kLootPassResultHitTimeBudget] = budget.hitTimeBudget ? 1 : 0;
		REX::DEBUG(
			"LootNearbyEnabledReferences: enabledMask={}, candidates={}, processed={}, successfulObjects={}, movedStacks={}, hitObjectLimit={}, hitTimeBudget={}, elapsed_ms={:.3f}",
			enabledFormTypeMask,
			result[kLootPassResultCandidateObjects],
			result[kLootPassResultProcessedObjects],
			result[kLootPassResultSuccessfulObjects],
			result[kLootPassResultMovedStacks],
			result[kLootPassResultHitObjectLimit],
			result[kLootPassResultHitTimeBudget],
			ElapsedMilliseconds(budget.startedAt));
		return result;
	}

}
