#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace message_queue
{
	struct TextReplacement
	{
		std::string token;
		std::string value;
	};

	// Asynchronous HUD message queue used by native looting and system notification code.
	// Pickup messages are throttled on a permanent F4SE task so loot workers can enqueue without blocking.
	void Initialize();
	void Enqueue(std::uint32_t formId, std::string itemName, std::int32_t count);
	void EnqueueLocalizedText(
		std::string translationKey,
		std::string fallbackText,
		std::vector<TextReplacement> replacements = {});
}
