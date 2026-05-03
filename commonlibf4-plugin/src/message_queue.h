#pragma once

#include <cstdint>
#include <string>

namespace message_queue
{
	// Asynchronous HUD message queue used by native looting code.
	// Messages are throttled on a permanent F4SE task so loot workers can enqueue without blocking.
	void Initialize();
	void Enqueue(std::uint32_t formId, std::string itemName, std::int32_t count);
}
