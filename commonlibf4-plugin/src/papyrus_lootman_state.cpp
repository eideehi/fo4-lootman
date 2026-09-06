#include "papyrus_lootman_internal.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace papyrus_lootman
{
	using namespace RE;

	std::mutex objectsLock;
	std::mutex recentWorldLootLock;
	// A dedicated lock rather than a reuse of recentWorldLootLock: the two maps have
	// opposite lifetimes (one marker is permanent until save load, the other expires
	// on its own), and sharing a lock would tie the activation bookkeeping to the
	// world-pickup finalize path it has nothing to do with.
	std::mutex activationAttemptLock;

	struct LockedObjectEntry
	{
		NiPointer<TESObjectREFR> ref;
		Clock::time_point lockedAt;
	};
	struct RecentlyLootedWorldRefEntry
	{
		const TESObjectREFR* ref = nullptr;
		TESFormID formID = 0;
	};

	// Activation refs (ACTI/FLOR) are never finalized the way world refs are, so a
	// reference that keeps accepting activations without handing anything over stays
	// lootable forever and burns a slot of every pass. Count the activations attempted
	// per reference instead of marking the reference permanently: unlike
	// recentlyLootedWorldRefs this must expire, because mirelurk eggs and harvested
	// plants legitimately become lootable again within the same session.
	struct ActivationAttemptEntry
	{
		TESFormID formID = 0;
		std::uint32_t attempts = 0;
		// How many times the hold has already re-armed on an attempt made after an
		// earlier hold had expired. It scales the hold, so a reference that never hands
		// anything over costs ever fewer activations instead of one per hold forever.
		std::uint32_t backoffLevel = 0;
		Clock::time_point updatedAt{};
	};

	std::unordered_map<std::uint32_t, LockedObjectEntry> lockedObjects;
	std::unordered_map<std::uint64_t, RecentlyLootedWorldRefEntry> recentlyLootedWorldRefs;
	std::unordered_map<std::uint64_t, ActivationAttemptEntry> activationAttempts;
	Clock::time_point lastLockedObjectCleanupAt{};
	Clock::time_point lastActivationAttemptCleanupAt{};
	// Cleanup resume cursor: an unordered_map has no ordered traversal, so the pass
	// bound is expressed in buckets and picked up again where the previous pass left
	// off. Iterators cannot be stored across calls (an insert can rehash), a bucket
	// index can.
	std::size_t activationAttemptCleanupBucket = 0;

	// Created refs are keyed by their 32-bit handle value, persistent refs by
	// their formID. Those two 32-bit ranges overlap, so tag handle-derived keys
	// in the high word to keep the two key spaces disjoint within one set.
	inline constexpr std::uint64_t kRecentLootHandleTag = std::uint64_t{ 1 } << 32;

	inline constexpr auto kLockedObjectStaleTimeout = std::chrono::minutes(5);
	inline constexpr auto kLockedObjectCleanupInterval = std::chrono::seconds(1);
	inline constexpr std::size_t kLockedObjectCleanupMaxPerPass = 32;

	// Flora is the observable class: the engine adds TESFlora::produceItem inside the
	// activation call, so the produce probe really does say whether that activation
	// delivered. Three misses before the hold arms, because a single miss is routinely
	// a transient engine state (the produce is still being spawned, the destination
	// briefly refuses the add), while a reference that misses three passes in a row is
	// not going to produce on the fourth either.
	inline constexpr std::uint32_t kFloraActivationAttemptLimit = 3;
	// The hold is the self-healing part: a held reference is activated again once the
	// hold expires, so a permanently unproductive one costs one activation per hold
	// instead of one per pass, while a plant that respawns mid-session is harvested
	// again on the first activation after it becomes harvestable.
	inline constexpr auto kFloraActivationHold = std::chrono::seconds(45);
	// A single fixed interval is not enough on its own: references are visited
	// nearest-first, so several staggered plants that never produce would each keep
	// spending one activation of the shared per-pass budget every 45s and starve the
	// loot behind them. Each re-arm therefore doubles the wait: 45s, 90s, 180s, 360s,
	// then the ceiling. Ten minutes is the longest a plant that becomes harvestable
	// again may have to wait before it is activated once more, which is what trades
	// harvest responsiveness against the cost of a permanently dead reference.
	inline constexpr auto kFloraActivationHoldMax = std::chrono::minutes(10);
	// A plain activator gets exactly one attempt, because nothing about it can be
	// observed and so there is no such thing as a miss to count. Every base form on the
	// shipped /include/activator list hands its entire payload out inside the first
	// accepted OnActivate and then guards, destroys or disables itself, so a second
	// activation cannot add loot; the one entry with no script-side guard at all
	// (TrapFloraThistle, which only damages its own destruction data) would duplicate
	// its item if a second activation landed before the destroyed flag did.
	inline constexpr std::uint32_t kPlainActivationAttemptLimit = 1;
	// Far above the 10s maximum pass interval and far above any plausible Papyrus
	// dispatch latency, so the hold never expires merely because the VM has not run the
	// OnActivate handler yet.
	inline constexpr auto kPlainActivationHold = std::chrono::seconds(300);
	// A single fixed interval is not enough on its own. References are visited
	// nearest-first, so several staggered dead references each keep spending one
	// activation of the shared per-pass budget every interval, and farther loot behind
	// them can be starved indefinitely. Each re-arm therefore doubles the wait:
	// 300s, 600s, 1200s, then the ceiling. The ceiling bounds a permanently stuck
	// reference to roughly two activations an hour while capping what a misclassified
	// one costs the player at half an hour.
	inline constexpr auto kPlainActivationHoldMax = std::chrono::minutes(30);
	inline constexpr std::uint32_t kActivationBackoffLevelMax = 4;
	// Deliberately longer than the longest hold of either policy: an entry sitting out
	// its longest hold must not be culled as stale, because that would reset its
	// attempts and hand the reference back the full per-pass activation rate.
	inline constexpr auto kActivationAttemptStaleTimeout = std::chrono::minutes(60);
	inline constexpr auto kActivationAttemptCleanupInterval = std::chrono::seconds(1);
	// Work bound per cleanup pass: at most kActivationAttemptCleanupMaxPerPassBuckets
	// buckets and kActivationAttemptCleanupMaxPerPassExamined entries are *examined*
	// (not merely removed), and the bucket cursor resumes on the next pass, so the map
	// is covered over successive passes without the lock ever being held across a full
	// traversal.
	inline constexpr std::size_t kActivationAttemptCleanupMaxPerPassExamined = 64;
	inline constexpr std::size_t kActivationAttemptCleanupMaxPerPassBuckets = 64;
	// Absolute ceiling on tracked references. The stale timeout alone cannot bound the
	// map, because a player crossing a dense cell can insert faster than the timeout
	// retires: past this many entries the least recently updated one is evicted to make
	// room, which costs one pass over at most this many elements and only while full.
	inline constexpr std::size_t kActivationAttemptMaxEntries = 512;

	std::uint32_t GetActivationAttemptLimit(ActivationPolicy policy)
	{
		return policy == ActivationPolicy::kFloraProbe
			? kFloraActivationAttemptLimit
			: kPlainActivationAttemptLimit;
	}

	std::chrono::seconds GetActivationHold(ActivationPolicy policy, std::uint32_t backoffLevel)
	{
		const auto level = backoffLevel < kActivationBackoffLevelMax
			? backoffLevel
			: kActivationBackoffLevelMax;
		const bool floraProbe = policy == ActivationPolicy::kFloraProbe;
		const auto scaled = (floraProbe ? kFloraActivationHold : kPlainActivationHold) *
			(std::int64_t{ 1 } << level);
		const auto ceiling = std::chrono::duration_cast<std::chrono::seconds>(
			floraProbe ? kFloraActivationHoldMax : kPlainActivationHoldMax);
		return scaled < ceiling ? scaled : ceiling;
	}

	std::uint64_t GetRecentlyLootedWorldRefKey(const TESObjectREFR* ref)
	{
		if (!ref)
		{
			return 0;
		}

		if (!ref->IsCreated())
		{
			return static_cast<std::uint64_t>(ref->formID);
		}

		auto* mutableRef = const_cast<TESObjectREFR*>(ref);
		auto handle = mutableRef->GetHandle();
		auto handleKey = handle.get_handle();
		if (handleKey != 0)
		{
			return kRecentLootHandleTag | static_cast<std::uint64_t>(handleKey);
		}
		return static_cast<std::uint64_t>(ref->formID);
	}

	bool IsRecentlyLootedWorldRef(std::uint64_t key, const TESObjectREFR* ref)
	{
		std::lock_guard<std::mutex> guard(recentWorldLootLock);
		auto it = recentlyLootedWorldRefs.find(key);
		if (it == recentlyLootedWorldRefs.end())
		{
			return false;
		}
		if (it->second.formID != ref->formID)
		{
			recentlyLootedWorldRefs.erase(it);
			return false;
		}
		if (!ref->IsCreated())
		{
			// A persistent placed ref keeps its formID for the whole session even
			// when the engine destroys and recreates the TESObjectREFR on cell
			// reload, so the formID hit is authoritative regardless of the stored
			// pointer. Refresh the pointer so diagnostics stay current.
			it->second.ref = ref;
			return true;
		}
		if (it->second.ref == ref)
		{
			return true;
		}

		// Created-ref handles (and 0xFF formIDs, for the handle==0 fallback key)
		// can be recycled. A key hit for a different pointer is stale identity,
		// not evidence that the new reference was already looted.
		recentlyLootedWorldRefs.erase(it);
		return false;
	}

	bool TryMarkRecentlyLootedWorldRef(std::uint64_t key, const TESObjectREFR* ref)
	{
		std::lock_guard<std::mutex> guard(recentWorldLootLock);
		// Finalization can fail after the item has already moved. Keep the suppression
		// marker until the next save load so a still-enabled reference cannot be looted
		// again merely because an elapsed-time TTL expired.
		auto [it, inserted] = recentlyLootedWorldRefs.insert_or_assign(
			key, RecentlyLootedWorldRefEntry{ ref, ref->formID });
		(void)it;
		return inserted;
	}

	bool IsRecentlyLootedWorldRef(const TESObjectREFR* ref)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0)
		{
			return false;
		}
		return IsRecentlyLootedWorldRef(key, ref);
	}

	bool TryMarkRecentlyLootedWorldRef(TESObjectREFR* ref)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0)
		{
			return false;
		}
		return TryMarkRecentlyLootedWorldRef(key, ref);
	}

	// Caller must hold activationAttemptLock. An interval gate plus a bound on the
	// entries *examined* (not just the ones removed) keeps every call cheap: a map
	// full of young entries costs one bounded slice of work per pass instead of a
	// full traversal that removes nothing. The bucket cursor resumes where the
	// previous pass stopped, so the whole map is still covered over successive passes.
	std::size_t CleanupStaleActivationAttemptsLocked(const Clock::time_point& now)
	{
		if (lastActivationAttemptCleanupAt.time_since_epoch().count() != 0 &&
			(now - lastActivationAttemptCleanupAt) < kActivationAttemptCleanupInterval)
		{
			return 0;
		}
		lastActivationAttemptCleanupAt = now;

		const auto bucketCount = activationAttempts.bucket_count();
		if (bucketCount == 0)
		{
			return 0;
		}
		if (activationAttemptCleanupBucket >= bucketCount)
		{
			activationAttemptCleanupBucket = 0;
		}

		// Collect first, erase after: erase() cannot take a bucket-local iterator, and
		// erasing during the walk would invalidate the one being held.
		std::vector<std::uint64_t> staleKeys;
		std::size_t examinedCount = 0;
		std::size_t visitedBuckets = 0;
		while (visitedBuckets < bucketCount &&
			   visitedBuckets < kActivationAttemptCleanupMaxPerPassBuckets &&
			   examinedCount < kActivationAttemptCleanupMaxPerPassExamined)
		{
			const auto bucket = activationAttemptCleanupBucket;
			for (auto it = activationAttempts.begin(bucket); it != activationAttempts.end(bucket); ++it)
			{
				++examinedCount;
				if ((now - it->second.updatedAt) >= kActivationAttemptStaleTimeout)
				{
					staleKeys.push_back(it->first);
				}
			}
			activationAttemptCleanupBucket = (bucket + 1) % bucketCount;
			++visitedBuckets;
		}

		std::size_t removedCount = 0;
		for (const auto staleKey : staleKeys)
		{
			removedCount += activationAttempts.erase(staleKey);
		}
		return removedCount;
	}

	// Caller must hold activationAttemptLock. The stale timeout retires entries by age,
	// which cannot bound the map on its own when a player crosses a dense cell faster
	// than the timeout retires. This is the hard ceiling: while the map is full, drop
	// the least recently updated entry so a new reference can still be tracked. The
	// scan is one pass over at most kActivationAttemptMaxEntries elements and only runs
	// while the map is at that ceiling.
	void EvictOldestActivationAttemptsLocked()
	{
		while (activationAttempts.size() >= kActivationAttemptMaxEntries)
		{
			auto oldest = activationAttempts.begin();
			for (auto it = activationAttempts.begin(); it != activationAttempts.end(); ++it)
			{
				if (it->second.updatedAt < oldest->second.updatedAt)
				{
					oldest = it;
				}
			}
			activationAttempts.erase(oldest);
		}
	}

	// Caller must hold activationAttemptLock. Returns the entry for key, resetting it
	// first when the key was recycled onto a different reference, and enforcing the
	// entry ceiling before a genuinely new entry is inserted.
	ActivationAttemptEntry& GetOrCreateActivationAttemptEntryLocked(std::uint64_t key, TESFormID formId)
	{
		if (activationAttempts.find(key) == activationAttempts.end())
		{
			EvictOldestActivationAttemptsLocked();
		}

		auto& entry = activationAttempts[key];
		if (entry.formID != formId)
		{
			entry = ActivationAttemptEntry{};
			entry.formID = formId;
		}
		return entry;
	}

	bool IsActivationHeld(const TESObjectREFR* ref, ActivationPolicy policy)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0)
		{
			return false;
		}

		// Read the untrusted reference before taking the lock: an access violation
		// inside the lock's scope would skip the guard's destructor under /EHsc.
		const auto formId = ref->formID;
		const auto now = Clock::now();
		std::lock_guard<std::mutex> guard(activationAttemptLock);
		// Bind the map through a const reference so the compiler enforces what this
		// query promises. A hold query that also mutated the entry is the defect this
		// replaced: it handed back a retry that a gate further down the same pass could
		// then consume without ever activating anything.
		const auto& attempts = activationAttempts;
		const auto it = attempts.find(key);
		if (it == attempts.end())
		{
			return false;
		}
		if (it->second.formID != formId)
		{
			// Handle keys are recycled, so a hit for a different formID is stale identity
			// and not history this reference earned. The entry is left for the cleanup
			// sweep and for the next recorded attempt to reset, because erasing it here
			// would make a query change what a later query sees.
			return false;
		}
		if (it->second.attempts < GetActivationAttemptLimit(policy))
		{
			// Below the limit the reference keeps its normal per-pass activations. For
			// flora that is what stops a single transient miss from holding a plant that
			// was about to produce.
			return false;
		}
		return (now - it->second.updatedAt) < GetActivationHold(policy, it->second.backoffLevel);
	}

	bool RecordActivationAttempt(TESObjectREFR* ref, ActivationPolicy policy)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0)
		{
			return false;
		}

		// Read the untrusted reference before taking the lock: an access violation
		// inside the lock's scope would skip the guard's destructor under /EHsc.
		const auto formId = ref->formID;
		const auto now = Clock::now();
		const auto limit = GetActivationAttemptLimit(policy);
		bool held = false;
		std::uint32_t attempts = 0;
		std::uint32_t backoffLevel = 0;
		std::size_t removedCount = 0;
		{
			std::lock_guard<std::mutex> guard(activationAttemptLock);
			removedCount = CleanupStaleActivationAttemptsLocked(now);
			auto& entry = GetOrCreateActivationAttemptEntryLocked(key, formId);
			if (entry.attempts >= limit &&
				(now - entry.updatedAt) >= GetActivationHold(policy, entry.backoffLevel))
			{
				// The previous hold has already run out and the reference is still here to be
				// activated again, so lengthen the next hold. That is the whole test: for a
				// plain activator nothing is observable either way, so the escalation is
				// driven by the elapsed hold rather than by a verdict about the delivery.
				// Without it a reference that never hands anything over keeps costing one
				// activation per base hold forever, and with nearest-first ordering a few
				// staggered ones can starve the loot behind them indefinitely.
				entry.backoffLevel = entry.backoffLevel < kActivationBackoffLevelMax
					? entry.backoffLevel + 1
					: kActivationBackoffLevelMax;
			}
			if (entry.attempts < limit)
			{
				++entry.attempts;
			}
			entry.updatedAt = now;
			attempts = entry.attempts;
			backoffLevel = entry.backoffLevel;
			held = entry.attempts >= limit;
		}

		if (removedCount > 0)
		{
			REX::DEBUG(
				"source=native component=loot_state event=stale_activation_attempts_released count={}",
				removedCount);
		}
		REX::DEBUG(
			"source=native component=loot_state event=activation_attempt_recorded ref={:08X} attempts={} limit={} backoff_level={} hold_seconds={}",
			formId,
			attempts,
			limit,
			backoffLevel,
			GetActivationHold(policy, backoffLevel).count());
		return held;
	}

	void ClearActivationAttempts(TESObjectREFR* ref)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0)
		{
			return;
		}

		// Read the untrusted reference before taking the lock: an access violation
		// inside the lock's scope would skip the guard's destructor under /EHsc.
		const auto formId = ref->formID;
		const auto now = Clock::now();
		std::size_t removedCount = 0;
		{
			std::lock_guard<std::mutex> guard(activationAttemptLock);
			removedCount = CleanupStaleActivationAttemptsLocked(now);
			// An observed yield drops the entry outright, backoff level included. That is
			// the whole self-healing story: a reference that becomes productive again is
			// looted at full rate from the next pass on. The erase is conditional on the
			// stored identity, like every other operation on this map: handle keys are
			// recycled, and an entry a different reference earned is not this one's to
			// clear on its behalf.
			const auto it = activationAttempts.find(key);
			if (it != activationAttempts.end() && it->second.formID == formId)
			{
				activationAttempts.erase(it);
			}
		}

		if (removedCount > 0)
		{
			REX::DEBUG(
				"source=native component=loot_state event=stale_activation_attempts_released count={}",
				removedCount);
		}
	}

	bool IsPapyrusObjectHandleAvailable(TESObjectREFR* ref)
	{
		if (!ref)
		{
			return false;
		}

		auto* gameVM = GameVM::GetSingleton();
		auto vm = gameVM ? gameVM->GetVM().get() : nullptr;
		if (!vm)
		{
			return false;
		}

		const auto vmTypeID = BSScript::GetVMTypeID<TESObjectREFR>();
		auto& handles = vm->GetObjectHandlePolicy();
		auto handle = handles.GetHandleForObject(vmTypeID, ref);
		if (handle == handles.EmptyHandle() || !handles.IsHandleLoaded(handle))
		{
			return false;
		}

		return true;
	}

	// Caller must hold objectsLock. Returns the number of stale locks evicted (0 when the interval gate skips
	// this pass) so the caller can emit the diagnostic outside the lock.
	std::size_t CleanupStaleLockedObjectsLocked(const Clock::time_point& now)
	{
		if (lastLockedObjectCleanupAt.time_since_epoch().count() != 0 &&
			(now - lastLockedObjectCleanupAt) < kLockedObjectCleanupInterval)
		{
			return 0;
		}
		lastLockedObjectCleanupAt = now;

		std::size_t removedCount = 0;
		for (auto it = lockedObjects.begin();
			 it != lockedObjects.end() && removedCount < kLockedObjectCleanupMaxPerPass;)
		{
			const auto& entry = it->second;
			if ((now - entry.lockedAt) < kLockedObjectStaleTimeout)
			{
				++it;
				continue;
			}

			it = lockedObjects.erase(it);
			++removedCount;
		}
		return removedCount;
	}

	void CleanupStaleLockedObjects()
	{
		const auto now = Clock::now();
		std::size_t removedCount = 0;
		{
			std::lock_guard<std::mutex> guard(objectsLock);
			removedCount = CleanupStaleLockedObjectsLocked(now);
		}

		if (removedCount > 0)
		{
			REX::DEBUG("source=native component=loot_state event=stale_locks_released count={}", removedCount);
		}
	}

	bool TryLockObject(TESObjectREFR* obj)
	{
		if (!obj)
		{
			return false;
		}

		const auto now = Clock::now();
		const auto formId = obj->formID;
		std::size_t removedCount = 0;
		bool locked = false;
		{
			std::lock_guard<std::mutex> guard(objectsLock);
			removedCount = CleanupStaleLockedObjectsLocked(now);
			locked = lockedObjects.emplace(formId, LockedObjectEntry{ obj, now }).second;
		}

		if (removedCount > 0)
		{
			REX::DEBUG("source=native component=loot_state event=stale_locks_released count={}", removedCount);
		}
		return locked;
	}

	void UnlockObject(std::uint32_t formId)
	{
		std::lock_guard<std::mutex> guard(objectsLock);
		lockedObjects.erase(formId);
	}

	bool IsLockedObject(std::uint32_t formId)
	{
		std::lock_guard<std::mutex> guard(objectsLock);
		return lockedObjects.find(formId) != lockedObjects.end();
	}

	void ResetTransientState()
	{
		{
			std::lock_guard<std::mutex> guard(objectsLock);
			lockedObjects.clear();
			lastLockedObjectCleanupAt = {};
		}
		{
			std::lock_guard<std::mutex> guard(recentWorldLootLock);
			recentlyLootedWorldRefs.clear();
		}
		{
			std::lock_guard<std::mutex> guard(activationAttemptLock);
			activationAttempts.clear();
			lastActivationAttemptCleanupAt = {};
			activationAttemptCleanupBucket = 0;
		}
		ClearWorkshopRuntimeState("preload");
	}
}
