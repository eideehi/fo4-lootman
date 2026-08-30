#pragma once

#include <string>

namespace utility
{
	RE::TESForm* LookupForm(const std::string& value);

	// Neutralize control characters (-> space) and double-quotes (-> single quote) in attacker-influenced
	// text before it is interpolated into a structured key="..." log, so a crafted value cannot inject
	// CR/LF record splits or forged key=value tokens.
	std::string SanitizeLogText(const std::string& value);

	// Render a configuration float the way the mod shows numbers to the player:
	// the default decimal form with trailing zeros, and then a trailing decimal
	// point, stripped. Shared by the HUD confirmation line and the config
	// holotape item labels so both spell the same value identically.
	std::string FormatConfigFloat(float value);
}
