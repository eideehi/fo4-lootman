#include "utility.h"

#include <exception>

namespace utility
{
	RE::TESForm* LookupForm(const std::string& value)
	{
		// Expected format: "PluginName.esp|00ABCDEF" (hex form id, load-order independent).
		const auto delimiter = value.find('|');
		if (delimiter == std::string::npos || delimiter == 0 || delimiter + 1 >= value.size()) return nullptr;

		const auto modName = value.substr(0, delimiter);
		const auto lowerFormId = value.substr(delimiter + 1);
		try
		{
			const auto rawFormID = static_cast<RE::TESFormID>(std::stoul(lowerFormId, nullptr, 16));
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm(rawFormID, modName) : nullptr;
		}
		catch (const std::exception&)
		{
			REX::WARN("source=native component=utility event=form_identifier_invalid value=\"{}\"", value);
			return nullptr;
		}
	}
}
