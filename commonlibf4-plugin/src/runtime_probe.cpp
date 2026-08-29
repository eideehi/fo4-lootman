#include "runtime_probe.h"

#include "log_settings.h"

namespace runtime_probe
{
	std::atomic<std::uint64_t> sequence{ 0 };

	bool IsEnabled()
	{
		return log_settings::IsRuntimeProbeEnabled() &&
			log_settings::ShouldLog(static_cast<std::int32_t>(spdlog::level::trace));
	}

	std::uint64_t NextSequence()
	{
		return sequence.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	bool TryReserve(std::atomic<std::uint32_t>& counter, const std::uint32_t limit)
	{
		auto current = counter.load(std::memory_order_relaxed);
		while (current < limit)
		{
			if (counter.compare_exchange_weak(
					current,
					current + 1,
					std::memory_order_relaxed,
					std::memory_order_relaxed))
			{
				return true;
			}
		}
		return false;
	}
}
