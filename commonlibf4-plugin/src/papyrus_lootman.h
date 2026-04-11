#pragma once

namespace papyrus_lootman
{
	// Entry points used from plugin startup and save-load lifecycle hooks.
	// Registration wires the public Papyrus API, while OnPreLoadGame resets
	// transient runtime state that cannot survive a save reload.
	bool Register(RE::BSScript::IVirtualMachine* vm);
	void OnPreLoadGame();
}
