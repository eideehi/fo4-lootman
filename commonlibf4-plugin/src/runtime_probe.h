#pragma once

#include <atomic>
#include <cstdint>

namespace runtime_probe
{
	bool IsEnabled();
	std::uint64_t NextSequence();
	bool TryReserve(std::atomic<std::uint32_t>& counter, std::uint32_t limit);
}
