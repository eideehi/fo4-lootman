#include "message_queue.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <vector>

namespace message_queue
{
	namespace
	{
		using SteadyClock = std::chrono::steady_clock;

		constexpr auto kDisplayInterval = std::chrono::milliseconds(1500);

		enum class PendingMessageType
		{
			pickup,
			localizedText,
		};

		struct PendingMessage
		{
			PendingMessageType type = PendingMessageType::pickup;
			std::uint32_t formId = 0;
			std::string itemName;
			std::int32_t count = 0;
			std::string translationKey;
			std::string fallbackText;
			std::vector<TextReplacement> replacements;
		};

		std::deque<PendingMessage> queue;
		std::mutex queueMutex;
		SteadyClock::time_point lastPickupDisplayTime{};
		bool initialized = false;
		std::unordered_map<std::string, std::string> translations;

		void AppendUtf8(std::string& out, std::uint32_t codePoint)
		{
			if (codePoint <= 0x7F)
			{
				out.push_back(static_cast<char>(codePoint));
			}
			else if (codePoint <= 0x7FF)
			{
				out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
				out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
			}
			else if (codePoint <= 0xFFFF)
			{
				out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
				out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
			}
			else
			{
				out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
				out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
			}
		}

		std::string DecodeUtf16Le(const std::vector<std::uint8_t>& bytes)
		{
			std::string out;
			std::size_t offset = bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE ? 2 : 0;
			while (offset + 1 < bytes.size())
			{
				const auto unit = static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
				offset += 2;

				if (unit >= 0xD800 && unit <= 0xDBFF && offset + 1 < bytes.size())
				{
					const auto low = static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
					if (low >= 0xDC00 && low <= 0xDFFF)
					{
						offset += 2;
						const auto codePoint =
							0x10000u +
							((static_cast<std::uint32_t>(unit) - 0xD800u) << 10) +
							(static_cast<std::uint32_t>(low) - 0xDC00u);
						AppendUtf8(out, codePoint);
						continue;
					}
				}

				AppendUtf8(out, unit);
			}
			return out;
		}

		std::string ReplaceAll(std::string text, std::string_view token, std::string_view replacement)
		{
			if (token.empty())
			{
				return text;
			}

			std::size_t pos = 0;
			while ((pos = text.find(token, pos)) != std::string::npos)
			{
				text.replace(pos, token.length(), replacement);
				pos += replacement.length();
			}
			return text;
		}

		void LoadTranslationsFile(const std::filesystem::path& file)
		{
			std::ifstream in(file, std::ios::binary);
			if (!in.is_open())
			{
				return;
			}

			std::vector<std::uint8_t> bytes(
				(std::istreambuf_iterator<char>(in)),
				std::istreambuf_iterator<char>());
			const auto text = DecodeUtf16Le(bytes);

			std::size_t lineStart = 0;
			while (lineStart < text.size())
			{
				auto lineEnd = text.find('\n', lineStart);
				if (lineEnd == std::string::npos)
				{
					lineEnd = text.size();
				}

				auto line = text.substr(lineStart, lineEnd - lineStart);
				if (!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}

				const auto tab = line.find('\t');
				if (tab != std::string::npos && tab > 0)
				{
					translations[line.substr(0, tab)] = line.substr(tab + 1);
				}

				lineStart = lineEnd + 1;
			}
		}

		void LoadTranslations()
		{
			wchar_t modulePath[REX::W32::MAX_PATH]{};
			REX::W32::GetModuleFileNameW(nullptr, modulePath, REX::W32::MAX_PATH);
			const auto dir = std::filesystem::path(modulePath).parent_path() / "DATA" / "Interface" / "Translations";

			std::string language = "en";
			if (const auto* setting = RE::GetINISetting("sLanguage:General"); setting)
			{
				language = setting->GetString();
				std::transform(language.begin(), language.end(), language.begin(), [](unsigned char c) {
					return static_cast<char>(std::tolower(c));
				});
			}

			auto file = dir / ("LootMan_" + language + ".txt");
			if (!std::filesystem::exists(file))
			{
				file = dir / "LootMan_en.txt";
			}

			LoadTranslationsFile(file);
			REX::DEBUG(
				"source=native component=message_queue event=translations_loaded count={} path=\"{}\"",
				translations.size(),
				file.string());
		}

		std::int32_t SaturatingAdd(std::int32_t left, std::int32_t right)
		{
			const auto total = static_cast<std::int64_t>(left) + static_cast<std::int64_t>(right);
			return total > std::numeric_limits<std::int32_t>::max()
				? std::numeric_limits<std::int32_t>::max()
				: static_cast<std::int32_t>(total);
		}

		std::string FormatFallbackName(std::uint32_t formId)
		{
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", formId);
			return buffer;
		}

		std::string FormatPickupMessage(const PendingMessage& msg)
		{
			auto itemName = msg.itemName.empty() ? FormatFallbackName(msg.formId) : msg.itemName;
			const auto* translationKey = msg.count == 1
				? "$LTMN_NATIVE_PICKUP_ITEM_ADDED_SINGLE"
				: "$LTMN_NATIVE_PICKUP_ITEM_ADDED";
			auto it = translations.find(translationKey);
			auto text = it == translations.end()
				? (msg.count == 1 ? "{itemName} Added." : "{itemName} ({count}) Added.")
				: it->second;
			text = ReplaceAll(std::move(text), "{itemName}"sv, itemName);
			text = ReplaceAll(std::move(text), "{count}"sv, std::to_string(msg.count));
			return text;
		}

		std::string FormatLocalizedTextMessage(const PendingMessage& msg)
		{
			const auto it = translations.find(msg.translationKey);
			auto text = it == translations.end() ? msg.fallbackText : it->second;
			for (const auto& replacement : msg.replacements)
			{
				text = ReplaceAll(std::move(text), replacement.token, replacement.value);
			}
			return text;
		}

		std::string FormatMessage(const PendingMessage& msg)
		{
			switch (msg.type)
			{
			case PendingMessageType::pickup:
				return FormatPickupMessage(msg);
			case PendingMessageType::localizedText:
				return FormatLocalizedTextMessage(msg);
			default:
				return {};
			}
		}

		void ClearQueue()
		{
			std::lock_guard lock(queueMutex);
			queue.clear();
			lastPickupDisplayTime = {};
		}

		bool TakeNextLocalizedTextMessage(PendingMessage& out)
		{
			std::lock_guard lock(queueMutex);
			for (auto it = queue.begin(); it != queue.end(); ++it)
			{
				if (it->type == PendingMessageType::localizedText)
				{
					out = std::move(*it);
					queue.erase(it);
					return true;
				}
			}

			return false;
		}

		bool TakeNextPickupMessage(PendingMessage& out)
		{
			std::lock_guard lock(queueMutex);
			const auto now = SteadyClock::now();
			if (now - lastPickupDisplayTime < kDisplayInterval)
			{
				return false;
			}

			for (auto it = queue.begin(); it != queue.end(); ++it)
			{
				if (it->type == PendingMessageType::pickup)
				{
					out = std::move(*it);
					queue.erase(it);
					lastPickupDisplayTime = now;
					return true;
				}
			}

			return false;
		}

		bool TakeNextMessage(PendingMessage& out)
		{
			if (TakeNextLocalizedTextMessage(out))
			{
				return true;
			}

			return TakeNextPickupMessage(out);
		}

		void Tick()
		{
			PendingMessage msg;
			if (!TakeNextMessage(msg))
			{
				return;
			}

			const auto text = FormatMessage(msg);
			if (!text.empty())
			{
				RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, false, false);
			}
		}
	}

	void Initialize()
	{
		{
			std::lock_guard lock(queueMutex);
			if (initialized)
			{
				return;
			}
			initialized = true;
		}

		auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface)
		{
			std::lock_guard lock(queueMutex);
			initialized = false;
			REX::ERROR("source=native component=message_queue event=task_interface_missing outcome=failed");
			return;
		}

		ClearQueue();
		LoadTranslations();
		taskInterface->AddTaskPermanent([]() { Tick(); });
		REX::DEBUG("source=native component=message_queue event=initialized");
	}

	void Enqueue(std::uint32_t formId, std::string itemName, std::int32_t count)
	{
		if (count <= 0)
		{
			return;
		}

		std::lock_guard lock(queueMutex);
		for (auto it = queue.begin(); it != queue.end(); ++it)
		{
			if (it->type == PendingMessageType::pickup && it->formId == formId && it->itemName == itemName)
			{
				count = SaturatingAdd(it->count, count);
				queue.erase(it);
				break;
			}
		}

		PendingMessage msg;
		msg.type = PendingMessageType::pickup;
		msg.formId = formId;
		msg.itemName = std::move(itemName);
		msg.count = count;
		queue.push_back(std::move(msg));
	}

	void EnqueueLocalizedText(
		std::string translationKey,
		std::string fallbackText,
		std::vector<TextReplacement> replacements)
	{
		if (translationKey.empty() && fallbackText.empty())
		{
			return;
		}

		PendingMessage msg;
		msg.type = PendingMessageType::localizedText;
		msg.translationKey = std::move(translationKey);
		msg.fallbackText = std::move(fallbackText);
		msg.replacements = std::move(replacements);

		std::lock_guard lock(queueMutex);
		queue.push_back(std::move(msg));
	}
}
