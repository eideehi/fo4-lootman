#include "vendor_chest.h"

namespace vendor_chest
{
	std::mutex vendorChestsMutex;
	std::unordered_set<std::uint32_t> vendorChests;

	void Initialize()
	{
		REX::DEBUG("source=native component=vendor_chest event=cache_started");

		auto& allFactions = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESFaction>();
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
					std::lock_guard<std::mutex> guard(vendorChestsMutex);
					vendorChests.emplace(baseObj->formID);
				}
			}
		}

		REX::DEBUG("source=native component=vendor_chest event=cache_completed count={}", vendorChests.size());
	}

	bool IsVendorChest(const std::uint32_t formId)
	{
		std::lock_guard<std::mutex> guard(vendorChestsMutex);
		return vendorChests.find(formId) != vendorChests.end();
	}
}
