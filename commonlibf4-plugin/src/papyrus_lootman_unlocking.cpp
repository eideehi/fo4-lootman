#include "papyrus_lootman_internal.h"

#include <cstdint>

namespace papyrus_lootman
{
	using namespace RE;

	struct UnlockReferenceCallContext
	{
		TESObjectREFR* ref = nullptr;
		REFR_LOCK* lock = nullptr;
	};

	void InvokeUnlockReferenceCall(void* opaque)
	{
		auto* context = static_cast<UnlockReferenceCallContext*>(opaque);
		context->lock->SetLocked(false);
		context->ref->AddLockChange();
	}

	bool TrySetReferenceUnlockedSafe(TESObjectREFR* ref, REFR_LOCK* lock)
	{
		if (!ref || !lock)
		{
			return false;
		}

		UnlockReferenceCallContext context{
			ref,
			lock
		};
		return ExecuteSehCallSafe(&InvokeUnlockReferenceCall, &context);
	}

	bool IsReferenceLockedForLooting(TESObjectREFR* ref)
	{
		auto* lock = ref ? ref->GetLock() : nullptr;
		if (!lock)
		{
			return false;
		}

		return lock->GetLockLevel(ref) != LOCK_LEVEL::kUnlocked;
	}

	bool HasPerk(Actor* actor, BGSPerk* perk)
	{
		return actor && perk && actor->GetPerkRank(perk) > 0;
	}

	std::int32_t GetRequiredBobbyPinCount(
		LOCK_LEVEL level,
		Actor* player,
		BGSPerk* locksmith01,
		BGSPerk* locksmith02,
		BGSPerk* locksmith03,
		BGSPerk* locksmith04)
	{
		std::int32_t consumeCount = -1;
		switch (level)
		{
		case LOCK_LEVEL::kVeryHard:
			if (HasPerk(player, locksmith03)) consumeCount = 4;
			break;
		case LOCK_LEVEL::kHard:
			if (HasPerk(player, locksmith02)) consumeCount = 3;
			break;
		case LOCK_LEVEL::kAverage:
			if (HasPerk(player, locksmith01)) consumeCount = 2;
			break;
		case LOCK_LEVEL::kEasy:
			consumeCount = 1;
			break;
		case LOCK_LEVEL::kUnlocked:
			consumeCount = 0;
			break;
		default:
			break;
		}

		if (consumeCount > 0 && HasPerk(player, locksmith04))
		{
			consumeCount = 0;
		}

		return consumeCount;
	}

	bool TryUnlockContainerWithPins(
		TESObjectREFR* ref,
		TESObjectREFR* bobbyPinContainerRef,
		Actor* player,
		TESForm* bobbyPinForm,
		BGSPerk* locksmith01,
		BGSPerk* locksmith02,
		BGSPerk* locksmith03,
		BGSPerk* locksmith04,
		bool unlockLockedContainer)
	{
		if (!ref || !unlockLockedContainer)
		{
			return false;
		}

		auto* lock = ref->GetLock();
		if (!lock || lock->IsBroken())
		{
			return false;
		}

		const auto lockLevel = lock->GetLockLevel(ref);
		const auto consumeCount = GetRequiredBobbyPinCount(
			lockLevel,
			player,
			locksmith01,
			locksmith02,
			locksmith03,
			locksmith04);
		if (consumeCount < 0)
		{
			return false;
		}

		auto* bobbyPin = bobbyPinForm ? bobbyPinForm->As<TESBoundObject>() : nullptr;
		std::int32_t bobbyPinCount = 0;
		const bool gotBobbyPinCount = consumeCount == 0 ||
			(bobbyPinContainerRef && bobbyPin &&
			 TryGetReferenceItemCountSafe(bobbyPinContainerRef, bobbyPin, bobbyPinCount));
		if (!gotBobbyPinCount || bobbyPinCount < consumeCount)
		{
			return false;
		}

		if (consumeCount > 0 && !TryRemoveItemsSafe(bobbyPinContainerRef, bobbyPin, consumeCount))
		{
			return false;
		}

		return TrySetReferenceUnlockedSafe(ref, lock);
	}

	bool TryUnlockContainerForLooting(
		TESObjectREFR* ref,
		TESObjectREFR* playerRef,
		TESObjectREFR* workshopRef,
		TESForm* bobbyPin,
		BGSPerk* locksmith01,
		BGSPerk* locksmith02,
		BGSPerk* locksmith03,
		BGSPerk* locksmith04,
		bool unlockLockedContainer)
	{
		auto* player = playerRef ? playerRef->As<Actor>() : nullptr;
		return TryUnlockContainerWithPins(
				ref,
				playerRef,
				player,
				bobbyPin,
				locksmith01,
				locksmith02,
				locksmith03,
				locksmith04,
				unlockLockedContainer) ||
			TryUnlockContainerWithPins(
				ref,
				workshopRef,
				player,
				bobbyPin,
				locksmith01,
				locksmith02,
				locksmith03,
				locksmith04,
				unlockLockedContainer);
	}
}
