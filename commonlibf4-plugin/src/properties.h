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
		enable_looting_in_settlement,
		lootable_inventory_item_type,
		looting_legendary_only,
		always_looting_explosives,
		always_looting_clothing,
		carry_weight,
		enable_carry_weight_limit,
		loot_is_deliver_to_player,
		display_pickup_message,
		lootable_alch_item_type,
		lootable_book_item_type,
		lootable_misc_item_type,
		lootable_weap_item_type,
		enable_lootman,
		display_system_message,
		play_pickup_sound,
		play_container_animation,
		automatically_link_and_unlink_to_workshop,
		unlock_locked_container,
	};

	// Bit layout of `enabled_looting_form_type_mask`. Declared here because the
	// config holotape labels resolve each per-form-type toggle from the same mask.
	inline constexpr int kEnableFormTypeACTI = 1;
	inline constexpr int kEnableFormTypeALCH = 2;
	inline constexpr int kEnableFormTypeAMMO = 4;
	inline constexpr int kEnableFormTypeARMO = 8;
	inline constexpr int kEnableFormTypeBOOK = 16;
	inline constexpr int kEnableFormTypeCONT = 32;
	inline constexpr int kEnableFormTypeFLOR = 64;
	inline constexpr int kEnableFormTypeINGR = 128;
	inline constexpr int kEnableFormTypeKEYM = 256;
	inline constexpr int kEnableFormTypeMISC = 512;
	inline constexpr int kEnableFormTypeNPC_ = 1024;
	inline constexpr int kEnableFormTypeWEAP = 2048;

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
	// Whether the last full update actually read values out of the Papyrus
	// property object. False while the quest, the VM, its script type or its bound
	// object could not be reached: every cached value is then `null` and the typed
	// accessors below hand out their caller's default instead of a real setting.
	bool IsResolved();
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
