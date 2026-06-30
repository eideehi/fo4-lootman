#include "vendor_chest.h"

#include <shared_mutex>

namespace vendor_chest
{
	// Read-mostly: rebuilt only by Initialize() on the main thread (kGameLoaded) but probed per
	// container on the per-loot-item hot path from VM worker threads. A shared_mutex lets those
	// concurrent reads run in parallel instead of serializing on an exclusive lock.
	std::shared_mutex vendorChestsMutex;
	std::unordered_set<std::uint32_t> vendorChests;

	void Initialize()
	{
		REX::DEBUG("source=native component=vendor_chest event=cache_started");

		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh)
		{
			REX::ERROR("source=native component=vendor_chest event=initialize_failed reason=data_handler_unavailable");
			return;
		}

		// Build into a local set, then publish it under one exclusive lock so readers never observe a
		// half-rebuilt cache (and we do not re-acquire the lock per inserted entry).
		std::unordered_set<std::uint32_t> rebuilt;
		auto& allFactions = dh->GetFormArray<RE::TESFaction>();
		for (auto* faction : allFactions)
		{
			if (!faction)
			{
				continue;
			}

			constexpr std::uint32_t vendorFlag = 1 << 14;
			if ((faction->data.flags & vendorFlag) == 0)
			{
				continue;
			}

			if (faction->vendorData.merchantContainer)
			{
				auto baseObj = faction->vendorData.merchantContainer->GetObjectReference();
				if (baseObj)
				{
					rebuilt.emplace(baseObj->formID);
				}
			}
		}

		const auto count = rebuilt.size();
		{
			std::unique_lock<std::shared_mutex> guard(vendorChestsMutex);
			vendorChests = std::move(rebuilt);
		}

		REX::DEBUG("source=native component=vendor_chest event=cache_completed count={}", count);
	}

	bool IsVendorChest(const std::uint32_t formId)
	{
		std::shared_lock<std::shared_mutex> guard(vendorChestsMutex);
		return vendorChests.find(formId) != vendorChests.end();
	}
}
