#include "papyrus_lootman_internal.h"

#include <atomic>
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
	std::mutex noYieldActivationLock;

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
	// reference that keeps accepting activations without producing anything stays
	// lootable forever and burns a slot of every pass. Count those no-yield
	// activations per reference instead of marking the reference permanently: unlike
	// recentlyLootedWorldRefs this must expire, because mirelurk eggs and harvested
	// plants legitimately become lootable again within the same session.
	struct NoYieldActivationEntry
	{
		TESFormID formID = 0;
		std::uint32_t strikes = 0;
		// How many times the suppression has already re-armed after a retry that still
		// yielded nothing. It scales the cooldown, so a hopeless reference costs ever
		// fewer activations instead of one per cooldown forever.
		std::uint32_t backoffLevel = 0;
		// Set while a cooldown has handed back a retry whose result is still open, so
		// the record that closes that retry can tell a re-arm from a first arming.
		bool retryGranted = false;
		// Non-zero while an activation is waiting for cross-pass evidence. Holding the
		// id of the pass that activated is what stops that same pass from reading its
		// own mark back as proof of a missing yield.
		std::uint64_t awaitingEvidencePassId = 0;
		Clock::time_point updatedAt{};
	};

	std::unordered_map<std::uint32_t, LockedObjectEntry> lockedObjects;
	std::unordered_map<std::uint64_t, RecentlyLootedWorldRefEntry> recentlyLootedWorldRefs;
	std::unordered_map<std::uint64_t, NoYieldActivationEntry> noYieldActivationRefs;
	Clock::time_point lastLockedObjectCleanupAt{};
	Clock::time_point lastNoYieldActivationCleanupAt{};
	// Cleanup resume cursor: an unordered_map has no ordered traversal, so the pass
	// bound is expressed in buckets and picked up again where the previous pass left
	// off. Iterators cannot be stored across calls (an insert can rehash), a bucket
	// index can.
	std::size_t noYieldActivationCleanupBucket = 0;
	std::atomic<std::uint64_t> noYieldActivationPassCounter{ 0 };

	// Created refs are keyed by their 32-bit handle value, persistent refs by
	// their formID. Those two 32-bit ranges overlap, so tag handle-derived keys
	// in the high word to keep the two key spaces disjoint within one set.
	inline constexpr std::uint64_t kRecentLootHandleTag = std::uint64_t{ 1 } << 32;

	inline constexpr auto kLockedObjectStaleTimeout = std::chrono::minutes(5);
	inline constexpr auto kLockedObjectCleanupInterval = std::chrono::seconds(1);
	inline constexpr std::size_t kLockedObjectCleanupMaxPerPass = 32;

	// Three strikes before suppression: a single no-yield activation is routinely a
	// transient engine state (the produce is still being spawned, the destination
	// briefly refuses the add), so one miss must not stop retrying, while a reference
	// that misses three passes in a row is not going to yield on the fourth either.
	inline constexpr std::uint32_t kNoYieldActivationStrikeLimit = 3;
	// The cooldown is the self-healing part: a suppressed reference is retried once
	// per cooldown, so a permanently unlootable activator costs one activation per
	// interval instead of one per pass, while a plant that respawns mid-session is
	// harvested again on the first retry after it becomes harvestable.
	inline constexpr auto kNoYieldActivationCooldown = std::chrono::seconds(45);
	// A single fixed interval is not enough on its own. References are visited
	// nearest-first, so several staggered hopeless references each keep spending one
	// activation of the shared per-pass budget every interval, and farther loot behind
	// them can be starved indefinitely. Each re-arm therefore doubles the wait:
	// 45s, 90s, 180s, 360s, then the ceiling. The ceiling is what a reference that
	// becomes lootable again may have to wait through before it is retried, so it
	// trades responsiveness against the worst-case cost of a dead reference.
	inline constexpr auto kNoYieldActivationCooldownMax = std::chrono::minutes(10);
	inline constexpr std::uint32_t kNoYieldActivationBackoffLevelMax = 4;
	// Deliberately longer than kNoYieldActivationCooldownMax: an entry sitting out its
	// longest cooldown must not be culled as stale, because that would reset its
	// strikes and hand the reference back the full per-pass retry rate.
	inline constexpr auto kNoYieldActivationStaleTimeout = std::chrono::minutes(15);
	inline constexpr auto kNoYieldActivationCleanupInterval = std::chrono::seconds(1);
	// Work bound per cleanup pass: at most kNoYieldActivationCleanupMaxPerPassBuckets
	// buckets and kNoYieldActivationCleanupMaxPerPassExamined entries are *examined*
	// (not merely removed), and the bucket cursor resumes on the next pass, so the map
	// is covered over successive passes without the lock ever being held across a full
	// traversal.
	inline constexpr std::size_t kNoYieldActivationCleanupMaxPerPassExamined = 64;
	inline constexpr std::size_t kNoYieldActivationCleanupMaxPerPassBuckets = 64;
	// Absolute ceiling on tracked references. The stale timeout alone cannot bound the
	// map, because a player crossing a dense cell can insert faster than the timeout
	// retires: past this many entries the least recently updated one is evicted to make
	// room, which costs one pass over at most this many elements and only while full.
	inline constexpr std::size_t kNoYieldActivationMaxEntries = 512;

	std::chrono::seconds GetNoYieldActivationCooldown(std::uint32_t backoffLevel)
	{
		const auto level = backoffLevel < kNoYieldActivationBackoffLevelMax
			? backoffLevel
			: kNoYieldActivationBackoffLevelMax;
		const auto scaled = kNoYieldActivationCooldown * (std::int64_t{ 1 } << level);
		return scaled < kNoYieldActivationCooldownMax
			? scaled
			: std::chrono::duration_cast<std::chrono::seconds>(kNoYieldActivationCooldownMax);
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

	// Caller must hold noYieldActivationLock. An interval gate plus a bound on the
	// entries *examined* (not just the ones removed) keeps every call cheap: a map
	// full of young entries costs one bounded slice of work per pass instead of a
	// full traversal that removes nothing. The bucket cursor resumes where the
	// previous pass stopped, so the whole map is still covered over successive passes.
	std::size_t CleanupStaleNoYieldActivationsLocked(const Clock::time_point& now)
	{
		if (lastNoYieldActivationCleanupAt.time_since_epoch().count() != 0 &&
			(now - lastNoYieldActivationCleanupAt) < kNoYieldActivationCleanupInterval)
		{
			return 0;
		}
		lastNoYieldActivationCleanupAt = now;

		const auto bucketCount = noYieldActivationRefs.bucket_count();
		if (bucketCount == 0)
		{
			return 0;
		}
		if (noYieldActivationCleanupBucket >= bucketCount)
		{
			noYieldActivationCleanupBucket = 0;
		}

		// Collect first, erase after: erase() cannot take a bucket-local iterator, and
		// erasing during the walk would invalidate the one being held.
		std::vector<std::uint64_t> staleKeys;
		std::size_t examinedCount = 0;
		std::size_t visitedBuckets = 0;
		while (visitedBuckets < bucketCount &&
			   visitedBuckets < kNoYieldActivationCleanupMaxPerPassBuckets &&
			   examinedCount < kNoYieldActivationCleanupMaxPerPassExamined)
		{
			const auto bucket = noYieldActivationCleanupBucket;
			for (auto it = noYieldActivationRefs.begin(bucket); it != noYieldActivationRefs.end(bucket); ++it)
			{
				++examinedCount;
				if ((now - it->second.updatedAt) >= kNoYieldActivationStaleTimeout)
				{
					staleKeys.push_back(it->first);
				}
			}
			noYieldActivationCleanupBucket = (bucket + 1) % bucketCount;
			++visitedBuckets;
		}

		std::size_t removedCount = 0;
		for (const auto staleKey : staleKeys)
		{
			removedCount += noYieldActivationRefs.erase(staleKey);
		}
		return removedCount;
	}

	// Caller must hold noYieldActivationLock. The stale timeout retires entries by age,
	// which cannot bound the map on its own when a player crosses a dense cell faster
	// than the timeout retires. This is the hard ceiling: while the map is full, drop
	// the least recently updated entry so a new reference can still be tracked. The
	// scan is one pass over at most kNoYieldActivationMaxEntries elements and only runs
	// while the map is at that ceiling.
	void EvictOldestNoYieldActivationsLocked()
	{
		while (noYieldActivationRefs.size() >= kNoYieldActivationMaxEntries)
		{
			auto oldest = noYieldActivationRefs.begin();
			for (auto it = noYieldActivationRefs.begin(); it != noYieldActivationRefs.end(); ++it)
			{
				if (it->second.updatedAt < oldest->second.updatedAt)
				{
					oldest = it;
				}
			}
			noYieldActivationRefs.erase(oldest);
		}
	}

	// Caller must hold noYieldActivationLock. Returns the entry for key, resetting it
	// first when the key was recycled onto a different reference, and enforcing the
	// entry ceiling before a genuinely new entry is inserted.
	NoYieldActivationEntry& GetOrCreateNoYieldActivationEntryLocked(std::uint64_t key, TESFormID formId)
	{
		if (noYieldActivationRefs.find(key) == noYieldActivationRefs.end())
		{
			EvictOldestNoYieldActivationsLocked();
		}

		auto& entry = noYieldActivationRefs[key];
		if (entry.formID != formId)
		{
			entry = NoYieldActivationEntry{};
			entry.formID = formId;
		}
		return entry;
	}

	std::uint64_t BeginActivationYieldPass()
	{
		// Starts at 1, so a zeroed awaitingEvidencePassId can never match a real pass.
		return noYieldActivationPassCounter.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	bool IsSuppressedNoYieldActivationRef(const TESObjectREFR* ref)
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
		std::lock_guard<std::mutex> guard(noYieldActivationLock);
		auto it = noYieldActivationRefs.find(key);
		if (it == noYieldActivationRefs.end())
		{
			return false;
		}
		if (it->second.formID != formId)
		{
			// Handle keys are recycled, so a hit for a different formID is stale
			// identity, not strikes this reference earned.
			noYieldActivationRefs.erase(it);
			return false;
		}
		if (it->second.strikes < kNoYieldActivationStrikeLimit)
		{
			// Below the limit the reference keeps its normal per-pass retries. A single
			// miss is routinely transient, so suppressing before the limit would stop
			// retrying references that were about to yield.
			return false;
		}
		if ((now - it->second.updatedAt) >= GetNoYieldActivationCooldown(it->second.backoffLevel))
		{
			// Hand back exactly one retry per cooldown by dropping to one strike below
			// the limit: if that retry yields nothing the next recorded outcome
			// re-arms the suppression at the next backoff level, and if it yields the
			// entry is cleared.
			it->second.strikes = kNoYieldActivationStrikeLimit - 1;
			it->second.retryGranted = true;
			it->second.updatedAt = now;
			return false;
		}
		return true;
	}

	bool RecordActivationYieldOutcome(TESObjectREFR* ref, bool yielded)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0)
		{
			return false;
		}

		const auto formId = ref->formID;
		const auto now = Clock::now();
		bool suppressed = false;
		std::size_t removedCount = 0;
		{
			std::lock_guard<std::mutex> guard(noYieldActivationLock);
			removedCount = CleanupStaleNoYieldActivationsLocked(now);
			if (yielded)
			{
				// A yield drops the entry outright, backoff level included. That is the
				// whole self-healing story: a reference that becomes productive again is
				// looted at full rate from the next pass on.
				noYieldActivationRefs.erase(key);
			}
			else
			{
				auto& entry = GetOrCreateNoYieldActivationEntryLocked(key, formId);
				if (entry.strikes < kNoYieldActivationStrikeLimit)
				{
					++entry.strikes;
				}
				// The verdict is in, so nothing is awaiting cross-pass evidence anymore.
				entry.awaitingEvidencePassId = 0;
				entry.updatedAt = now;
				suppressed = entry.strikes >= kNoYieldActivationStrikeLimit;
				if (suppressed && entry.retryGranted)
				{
					// This closed a retry that a cooldown had handed back, and it still
					// yielded nothing, so lengthen the next cooldown. Without this every
					// hopeless reference keeps costing one activation per fixed cooldown
					// forever, which with nearest-first ordering starves farther loot.
					entry.backoffLevel = entry.backoffLevel < kNoYieldActivationBackoffLevelMax
						? entry.backoffLevel + 1
						: kNoYieldActivationBackoffLevelMax;
					entry.retryGranted = false;
				}
			}
		}

		if (removedCount > 0)
		{
			REX::DEBUG(
				"source=native component=loot_state event=stale_no_yield_activations_released count={}",
				removedCount);
		}
		return suppressed;
	}

	void MarkActivationAwaitingYieldEvidence(TESObjectREFR* ref, std::uint64_t passId)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0 || passId == 0)
		{
			return;
		}

		// Read the untrusted reference before taking the lock: an access violation
		// inside the lock's scope would skip the guard's destructor under /EHsc.
		const auto formId = ref->formID;
		const auto now = Clock::now();
		std::lock_guard<std::mutex> guard(noYieldActivationLock);
		auto& entry = GetOrCreateNoYieldActivationEntryLocked(key, formId);
		entry.awaitingEvidencePassId = passId;
		entry.updatedAt = now;
	}

	bool SettleActivationYieldEvidence(TESObjectREFR* ref, std::uint64_t passId)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0 || passId == 0)
		{
			return false;
		}

		const auto formId = ref->formID;
		bool settledAsNoYield = false;
		{
			std::lock_guard<std::mutex> guard(noYieldActivationLock);
			auto it = noYieldActivationRefs.find(key);
			if (it != noYieldActivationRefs.end() &&
				it->second.formID == formId &&
				it->second.awaitingEvidencePassId != 0 &&
				it->second.awaitingEvidencePassId != passId)
			{
				// The reference we activated on an earlier pass is being handed to us
				// again, so its handler never disabled or destroyed it and nothing it
				// delivered removed it from collection: it did not yield. Requiring a
				// *different* pass id is what keeps the pass that placed the mark from
				// reading it straight back as evidence.
				it->second.awaitingEvidencePassId = 0;
				settledAsNoYield = true;
			}
		}

		if (!settledAsNoYield)
		{
			return false;
		}
		return RecordActivationYieldOutcome(ref, false);
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
			std::lock_guard<std::mutex> guard(noYieldActivationLock);
			noYieldActivationRefs.clear();
			lastNoYieldActivationCleanupAt = {};
			noYieldActivationCleanupBucket = 0;
			noYieldActivationPassCounter.store(0, std::memory_order_relaxed);
		}
		ClearWorkshopRuntimeState("preload");
	}
}
