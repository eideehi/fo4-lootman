#pragma once

#include <cstdint>

namespace log_settings
{
	// Values match spdlog::level::level_enum: trace=0 through off=6.
	std::int32_t GetLogLevel();
	void SetLogLevel(std::int32_t logLevel);
	void Initialize();
}
