#pragma once

#include <cstdint>

namespace terminal_labels
{
	// Config holotape label rendering. Every value-bearing item on the settings
	// pages shows its own current value in its label, composed from the item's
	// captured original text so a value suffix can never stack.
	//
	// Resolves the config pages, captures the shipped label text of every item the
	// module writes, and registers the menu sink that refreshes the labels when the
	// terminal menu opens. Called directly by the F4SE message dispatcher, so
	// nothing thrown inside the module escapes this entry point.
	void Initialize();
	// Invalidates everything that points at the outgoing session: queued refreshes,
	// the cached page pointers and the property-cache readiness flag. Captured
	// original labels survive on purpose, so a suffix can never stack on top of an
	// already written one. Called directly by the F4SE message dispatcher, so
	// nothing thrown inside the module escapes this entry point.
	void OnPreLoadGame();
	// Re-resolves the cached page pointers that OnPreLoadGame() cleared. Needed
	// because kGameLoaded fires once per process, not once per save load, so
	// nothing else re-resolves those pointers after the first load. Also retries
	// menu sink registration in case Initialize() ran before the UI singleton
	// existed; registration stays one-shot on success. Called directly by the F4SE
	// message dispatcher, so nothing thrown inside the module escapes this entry
	// point.
	void OnPostLoadGame();
	// The load epoch the module is currently on. Work that reads property values on
	// another thread captures this before the read and hands it back to the entry
	// points below, so values read for the outgoing session are recognizable as
	// such once a save load has moved the module on.
	std::uint32_t CurrentLoadEpoch();
	// Records that Papyrus has published a full set of property values. Label
	// composition additionally requires that the update actually resolved those
	// values; until both hold, a refresh writes nothing and the shipped text stays.
	// Does nothing when `epoch` is no longer current: the values it vouches for
	// were read before a save load and describe a session that is already gone.
	// Called directly by the Papyrus VM, so nothing thrown inside the module
	// escapes this entry point.
	void MarkPropertyCacheReady(std::uint32_t epoch);
	// Rewrites every label from its captured original. Serialized on the F4SE task
	// interface, and idempotent: the same settings always produce the same text.
	// Does nothing when `epoch` is no longer current, so the outgoing session's
	// values are never written onto the incoming session's pages. Called directly
	// by the Papyrus VM, so nothing thrown inside the module escapes this entry
	// point.
	void RefreshAll(std::uint32_t epoch);
}
