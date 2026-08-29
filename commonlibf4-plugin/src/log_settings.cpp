#include "log_settings.h"
#include "utility.h"

#include <array>
#include <atomic>
#include <cctype>
#include <optional>
#include <string_view>
#include <system_error>

namespace log_settings
{
	constexpr std::int32_t kDefaultLogLevel = static_cast<std::int32_t>(spdlog::level::info);
	constexpr std::int32_t kMinLogLevel = static_cast<std::int32_t>(spdlog::level::trace);
	constexpr std::int32_t kMaxLogLevel = static_cast<std::int32_t>(spdlog::level::off);

	constexpr std::array<std::string_view, 7> kLogLevelNames{
		"trace"sv,
		"debug"sv,
		"info"sv,
		"warn"sv,
		"error"sv,
		"critical"sv,
		"off"sv,
	};

	// Read on the per-event logging fast path (ShouldLog) from multiple VM worker threads. It is a lone
	// 32-bit value with no invariant tying it to other state, so an atomic lets the gate stay lock-free
	// instead of serializing every gate check on a global mutex.
	std::atomic<std::int32_t> currentLogLevel = kDefaultLogLevel;
	std::atomic<bool> runtimeProbeEnabled = false;

	std::int32_t NormalizeLogLevel(const std::int32_t logLevel)
	{
		return logLevel >= kMinLogLevel && logLevel <= kMaxLogLevel ? logLevel : kDefaultLogLevel;
	}

	std::string NormalizeLogLevelName(std::string value)
	{
		const auto first = value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos)
		{
			return {};
		}
		const auto last = value.find_last_not_of(" \t\r\n");
		value = value.substr(first, last - first + 1);
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	std::optional<std::int32_t> ParseLogLevelName(std::string value)
	{
		value = NormalizeLogLevelName(std::move(value));
		if (value == "warning")
		{
			value = "warn";
		}
		if (value == "err")
		{
			value = "error";
		}

		for (std::int32_t i = 0; i < static_cast<std::int32_t>(kLogLevelNames.size()); ++i)
		{
			if (value == kLogLevelNames[static_cast<std::size_t>(i)])
			{
				return i;
			}
		}
		return std::nullopt;
	}

	std::string_view GetLogLevelName(const std::int32_t logLevel)
	{
		const auto normalized = NormalizeLogLevel(logLevel);
		return kLogLevelNames[static_cast<std::size_t>(normalized)];
	}

	std::filesystem::path GetConfigPath()
	{
		wchar_t modulePath[REX::W32::MAX_PATH]{};
		REX::W32::GetModuleFileNameW(nullptr, modulePath, REX::W32::MAX_PATH);
		return std::filesystem::path(modulePath).parent_path() / "Data" / "F4SE" / "Plugins" / "LootMan" / "config.json";
	}

	void ApplyLogLevel(const std::int32_t logLevel)
	{
		const auto level = static_cast<spdlog::level::level_enum>(NormalizeLogLevel(logLevel));
		spdlog::set_level(level);
		if (auto logger = spdlog::default_logger())
		{
			logger->set_level(level);
			logger->flush_on(level);
		}
	}

	bool ReadLogLevel(const std::filesystem::path& path, std::int32_t& logLevel)
	{
		std::ifstream ifs(path);
		if (!ifs.is_open())
		{
			REX::WARN("source=native component=log_settings event=config_open_failed path=\"{}\"", path.string());
			return false;
		}

		nlohmann::json src;
		try
		{
			src = nlohmann::json::parse(ifs);
		}
		catch (const nlohmann::json::parse_error& e)
		{
			REX::WARN(
				"source=native component=log_settings event=config_parse_failed path=\"{}\" reason=\"{}\"",
				path.string(),
				utility::SanitizeLogText(e.what()));
			return false;
		}

		if (!src.is_object())
		{
			REX::WARN("source=native component=log_settings event=config_root_invalid path=\"{}\"", path.string());
			return false;
		}

		const auto logIt = src.find("log");
		if (logIt == src.end())
		{
			return false;
		}
		if (!logIt->is_object())
		{
			REX::WARN(
				"source=native component=log_settings event=config_entry_invalid path=\"{}\" entry=log expected=object",
				path.string());
			return false;
		}

		const auto levelIt = logIt->find("level");
		if (levelIt == logIt->end())
		{
			return false;
		}

		if (levelIt->is_string())
		{
			const auto parsed = ParseLogLevelName(levelIt->get<std::string>());
			if (parsed)
			{
				logLevel = *parsed;
				return true;
			}

			REX::WARN(
				"source=native component=log_settings event=config_level_unknown path=\"{}\" value=\"{}\"",
				path.string(),
				utility::SanitizeLogText(levelIt->get<std::string>()));
			return false;
		}

		if (levelIt->is_number_integer())
		{
			const auto value = levelIt->get<std::int32_t>();
			if (value >= kMinLogLevel && value <= kMaxLogLevel)
			{
				logLevel = value;
				return true;
			}

			REX::WARN(
				"source=native component=log_settings event=config_level_out_of_range path=\"{}\" value={}",
				path.string(),
				value);
			return false;
		}

		REX::WARN(
			"source=native component=log_settings event=config_entry_invalid path=\"{}\" entry=log.level expected=string_or_integer",
			path.string());
		return false;
	}

	bool ReadRuntimeProbeEnabled(const std::filesystem::path& path, bool& enabled)
	{
		std::ifstream ifs(path);
		if (!ifs.is_open())
		{
			return false;
		}

		nlohmann::json src;
		try
		{
			src = nlohmann::json::parse(ifs);
		}
		catch (const nlohmann::json::parse_error&)
		{
			return false;
		}

		const auto diagnosticsIt = src.find("diagnostics");
		if (diagnosticsIt == src.end() || !diagnosticsIt->is_object())
		{
			return false;
		}
		const auto probeIt = diagnosticsIt->find("runtimeProbe");
		if (probeIt == diagnosticsIt->end() || !probeIt->is_boolean())
		{
			return false;
		}

		enabled = probeIt->get<bool>();
		return true;
	}

	bool SaveLogLevel(const std::filesystem::path& path, const std::int32_t logLevel)
	{
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec)
		{
			REX::WARN(
				"source=native component=log_settings event=config_directory_create_failed path=\"{}\" reason=\"{}\"",
				path.parent_path().string(),
				ec.message());
			return false;
		}

		nlohmann::json src = nlohmann::json::object();
		const auto exists = std::filesystem::exists(path, ec);
		if (ec)
		{
			REX::WARN(
				"source=native component=log_settings event=config_check_failed path=\"{}\" reason=\"{}\"",
				path.string(),
				ec.message());
			return false;
		}
		if (exists)
		{
			std::ifstream ifs(path);
			if (!ifs.is_open())
			{
				REX::WARN(
					"source=native component=log_settings event=config_write_skipped reason=open_failed path=\"{}\"",
					path.string());
				return false;
			}

			try
			{
				src = nlohmann::json::parse(ifs);
			}
			catch (const nlohmann::json::parse_error& e)
			{
				REX::WARN(
					"source=native component=log_settings event=config_write_skipped reason=parse_failed path=\"{}\" details=\"{}\"",
					path.string(),
					utility::SanitizeLogText(e.what()));
				return false;
			}
		}
		if (!src.is_object())
		{
			REX::WARN(
				"source=native component=log_settings event=config_write_skipped reason=root_invalid path=\"{}\"",
				path.string());
			return false;
		}

		if (!src.contains("log") || !src["log"].is_object())
		{
			src["log"] = nlohmann::json::object();
		}
		src["log"]["level"] = GetLogLevelName(logLevel);
		if (!src.contains("diagnostics") || !src["diagnostics"].is_object())
		{
			src["diagnostics"] = nlohmann::json::object();
		}
		if (!src["diagnostics"].contains("runtimeProbe") ||
			!src["diagnostics"]["runtimeProbe"].is_boolean())
		{
			src["diagnostics"]["runtimeProbe"] = false;
		}

		std::ofstream ofs(path, std::ios::trunc);
		if (!ofs.is_open())
		{
			REX::WARN("source=native component=log_settings event=config_write_failed path=\"{}\"", path.string());
			return false;
		}
		ofs << src.dump(4) << '\n';
		return true;
	}

	std::int32_t GetLogLevel()
	{
		return currentLogLevel.load(std::memory_order_relaxed);
	}

	bool ShouldLog(const std::int32_t logLevel)
	{
		const auto normalized = NormalizeLogLevel(logLevel);
		if (normalized == static_cast<std::int32_t>(spdlog::level::off))
		{
			return false;
		}

		const auto current = currentLogLevel.load(std::memory_order_relaxed);
		return current != static_cast<std::int32_t>(spdlog::level::off) &&
			normalized >= current;
	}

	bool IsRuntimeProbeEnabled()
	{
		return runtimeProbeEnabled.load(std::memory_order_relaxed);
	}

	void SetLogLevel(const std::int32_t logLevel)
	{
		const auto normalized = NormalizeLogLevel(logLevel);
		currentLogLevel.store(normalized, std::memory_order_relaxed);

		ApplyLogLevel(normalized);
		(void)SaveLogLevel(GetConfigPath(), normalized);
	}

	void Initialize()
	{
		const auto path = GetConfigPath();
		std::int32_t logLevel = kDefaultLogLevel;
		bool enableRuntimeProbe = false;

		std::error_code ec;
		const auto exists = std::filesystem::exists(path, ec);
		if (ec)
		{
			REX::WARN(
				"source=native component=log_settings event=config_check_failed path=\"{}\" reason=\"{}\"",
				path.string(),
				ec.message());
		}
		if (exists)
		{
			(void)ReadLogLevel(path, logLevel);
			(void)ReadRuntimeProbeEnabled(path, enableRuntimeProbe);
		}

		currentLogLevel.store(logLevel, std::memory_order_relaxed);
		runtimeProbeEnabled.store(enableRuntimeProbe, std::memory_order_relaxed);
		ApplyLogLevel(logLevel);

		if (!exists && !ec)
		{
			(void)SaveLogLevel(path, logLevel);
		}
	}
}
