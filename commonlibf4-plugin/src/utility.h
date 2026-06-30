#pragma once

#include <string>

namespace utility
{
	RE::TESForm* LookupForm(const std::string& value);

	// Neutralize control characters (-> space) and double-quotes (-> single quote) in attacker-influenced
	// text before it is interpolated into a structured key="..." log, so a crafted value cannot inject
	// CR/LF record splits or forged key=value tokens.
	std::string SanitizeLogText(const std::string& value);
}
