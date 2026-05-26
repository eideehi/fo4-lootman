#pragma once

namespace RE
{
	class TESObjectREFR;
}

namespace properties
{
	// Subset of `LTMN2:Properties` mirrored into native code for hot-path reads.
	// Keep this enum synchronized with `Update()` in properties.cpp.
	enum Key
	{
		is_installed,
		is_initialized,
		is_uninstalled,
		looting_range,
		max_items_processed_per_thread,
		max_lootable_objects_per_pass,
		max_containers_per_pass,
		max_actors_per_pass,
		max_activation_refs_per_pass,
		use_looting_time_budget,
		looting_time_budget_ms,
		enabled_looting_form_type_mask,
		not_looting_from_settlement,
		lootable_inventory_item_type,
		looting_legendary_only,
		always_looting_explosives,
		carry_weight,
		ignore_overweight,
		loot_is_deliver_to_player,
		looting_without_logs,
		lootable_alch_item_type,
		lootable_book_item_type,
		lootable_misc_item_type,
		lootable_weap_item_type,
	};

	enum Type
	{
		null,
		boolean,
		integer,
		decimal,
	};

	struct Value
	{
		Type type;

		union Data
		{
			bool b;
			int i;
			float f;

			Data() : b(false) {}
		} data;

		Value() : type(null) {}
	};

	Value Get(Key key);
	// Typed accessors return fallback values when the cached type does not match.
	bool GetBool(Key key, bool defaultValue = false);
	int GetInt(Key key, int defaultValue = 0);
	float GetFloat(Key key, float defaultValue = 0.0f);
	RE::TESObjectREFR* GetLootManWorkshopRef();

	// Resolves the `LTMN_Properties` quest once forms are available.
	void Initialize();
	// `key` matches the Papyrus property name that changed.
	// `nullptr` or empty refreshes the whole native cache after startup or load.
	void Update(const char* key);
}
