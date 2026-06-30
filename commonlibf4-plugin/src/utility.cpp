#include "utility.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>

namespace utility
{
	std::string SanitizeLogText(const std::string& value)
	{
		std::string result;
		result.reserve(value.size());
		for (const char raw : value)
		{
			const auto ch = static_cast<unsigned char>(raw);
			result.push_back(std::iscntrl(ch) ? ' ' : (raw == '"' ? '\'' : raw));
		}
		return result;
	}

	RE::TESForm* LookupForm(const std::string& value)
	{
		// Expected format: "PluginName.esp|00ABCDEF" (hex form id, load-order independent).
		const auto delimiter = value.find('|');
		if (delimiter == std::string::npos || delimiter == 0 || delimiter + 1 >= value.size()) return nullptr;

		const auto modName = value.substr(0, delimiter);
		const auto lowerFormId = value.substr(delimiter + 1);
		// value originates from third-party JSON (injection_data) where nlohmann decodes \n / \" escapes
		// into real bytes; sanitize before logging it on any rejection path.
		const auto safeValue = SanitizeLogText(value);

		// std::stoul silently accepts a leading sign, leading whitespace, and a "0x" prefix and
		// counts them as consumed, so the trailing-garbage guard below cannot catch them
		// (e.g. "-10", "  10", "0x10" would all resolve to unintended forms). Require the id to
		// be pure hex digits before parsing.
		if (!std::all_of(lowerFormId.begin(), lowerFormId.end(),
				[](unsigned char c) { return std::isxdigit(c) != 0; }))
		{
			REX::WARN("source=native component=utility event=form_identifier_invalid value=\"{}\"", safeValue);
			return nullptr;
		}

		try
		{
			std::size_t consumed = 0;
			const auto rawFormID = static_cast<RE::TESFormID>(std::stoul(lowerFormId, &consumed, 16));
			if (consumed != lowerFormId.size())
			{
				// std::stoul stops at the first non-hex character without throwing,
				// so a typo'd id like "00ABCDEFzz" would silently resolve to a
				// different valid form. Reject any unconsumed trailing characters.
				REX::WARN("source=native component=utility event=form_identifier_invalid value=\"{}\"", safeValue);
				return nullptr;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm(rawFormID, modName) : nullptr;
		}
		catch (const std::exception&)
		{
			REX::WARN("source=native component=utility event=form_identifier_invalid value=\"{}\"", safeValue);
			return nullptr;
		}
	}
}
