#include "papyrus_lootman_internal.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace papyrus_lootman
{
	using namespace RE;

	std::mutex objectsLock;
	std::mutex recentWorldLootLock;

	struct LockedObjectEntry
	{
		NiPointer<TESObjectREFR> ref;
		Clock::time_point lockedAt;
	};

	std::unordered_map<std::uint32_t, LockedObjectEntry> lockedObjects;
	std::unordered_set<std::uint64_t> recentlyLootedWorldRefs;
	Clock::time_point lastLockedObjectCleanupAt{};

	// Created refs are keyed by their 32-bit handle value, persistent refs by
	// their formID. Those two 32-bit ranges overlap, so tag handle-derived keys
	// in the high word to keep the two key spaces disjoint within one set.
	inline constexpr std::uint64_t kRecentLootHandleTag = std::uint64_t{ 1 } << 32;

	inline constexpr auto kLockedObjectStaleTimeout = std::chrono::minutes(5);
	inline constexpr auto kLockedObjectCleanupInterval = std::chrono::seconds(1);
	inline constexpr std::size_t kLockedObjectCleanupMaxPerPass = 32;

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

	bool IsRecentlyLootedWorldRef(std::uint64_t key)
	{
		std::lock_guard<std::mutex> guard(recentWorldLootLock);
		return recentlyLootedWorldRefs.find(key) != recentlyLootedWorldRefs.end();
	}

	bool TryMarkRecentlyLootedWorldRef(std::uint64_t key)
	{
		std::lock_guard<std::mutex> guard(recentWorldLootLock);
		return recentlyLootedWorldRefs.insert(key).second;
	}

	bool IsRecentlyLootedWorldRef(const TESObjectREFR* ref)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0)
		{
			return false;
		}
		return IsRecentlyLootedWorldRef(key);
	}

	bool TryMarkRecentlyLootedWorldRef(TESObjectREFR* ref)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0)
		{
			return false;
		}
		return TryMarkRecentlyLootedWorldRef(key);
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

	void CleanupStaleLockedObjects()
	{
		const auto now = Clock::now();
		std::size_t removedCount = 0;

		{
			std::lock_guard<std::mutex> guard(objectsLock);
			if (lastLockedObjectCleanupAt.time_since_epoch().count() != 0 &&
				(now - lastLockedObjectCleanupAt) < kLockedObjectCleanupInterval)
			{
				return;
			}
			lastLockedObjectCleanupAt = now;

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

		CleanupStaleLockedObjects();
		auto formId = obj->formID;
		std::lock_guard<std::mutex> guard(objectsLock);
		return lockedObjects.emplace(formId, LockedObjectEntry{ obj, Clock::now() }).second;
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

	void ReleaseObject(std::monostate, std::uint32_t objId)
	{
		UnlockObject(objId);
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
		ClearWorkshopRuntimeState("preload");
	}
}
