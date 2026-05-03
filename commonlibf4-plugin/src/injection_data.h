#pragma once

namespace injection_data
{
	// Keys map 1:1 to JSON pointer paths defined in injection_data.cpp.
	enum Key
	{
		include_activation_block,
		include_activator,
		include_featured_item,
		include_quest_item,
		include_unique_item,
		exclude_form,
		exclude_keyword,
		notify_item,
		alch_type_alcohol,
		alch_type_chemistry,
		alch_type_food,
		alch_type_nuka_cola,
		alch_type_stimpak,
		alch_type_syringe_ammo,
		alch_type_water,
		book_type_perk_magazine,
		misc_type_bobblehead,
		weap_type_grenade,
		weap_type_mine,
	};

	enum Type : std::uint32_t
	{
		kForm = 1 << 0,
		kKeyword = 1 << 1,
		kLocationRefType = 1 << 2,
	};

	enum NotifyCategory : std::uint32_t
	{
		notify_alch = 1 << 0,
		notify_ammo = 1 << 1,
		notify_armo = 1 << 2,
		notify_book = 1 << 3,
		notify_ingr = 1 << 4,
		notify_keym = 1 << 5,
		notify_misc = 1 << 6,
		notify_weap = 1 << 7,
	};

	struct Value
	{
		// Type is treated as a small tag/bitmask describing the active union field.
		Type type;

		union Data
		{
			RE::TESForm* form;
			RE::BGSKeyword* keyword;
			RE::BGSLocationRefType* location_ref_type;

			Data() : form(nullptr) {}
		} data;

		Value() : type(kForm) {}

		bool operator==(const Value& other) const
		{
			if (type != other.type) return false;
			if (IsForm()) return data.form && other.data.form && data.form->formID == other.data.form->formID;
			if (IsKeyword()) return data.keyword && other.data.keyword && data.keyword->formID == other.data.keyword->formID;
			if (IsLocationRefType()) return data.location_ref_type && other.data.location_ref_type &&
			    data.location_ref_type->formID == other.data.location_ref_type->formID;
			return false;
		}

		bool IsForm() const { return (type & kForm) != 0 && (type & kKeyword) == 0 && (type & kLocationRefType) == 0; }
		bool IsKeyword() const { return (type & kKeyword) != 0; }
		bool IsLocationRefType() const { return (type & kLocationRefType) != 0; }
	};

	Value Get(Key key);
	RE::TESForm* GetAsForm(Key key);
	RE::BGSKeyword* GetAsKeyword(Key key);
	RE::BGSLocationRefType* GetAsLocationRefType(Key key);

	// Copy-returning helpers for call sites that do not need stable references.
	std::vector<Value> GetList(Key key);
	std::vector<RE::TESForm*> GetAsFormList(Key key);
	std::vector<RE::BGSKeyword*> GetAsKeywordList(Key key);
	std::vector<RE::BGSLocationRefType*> GetAsLocationRefTypeList(Key key);
	// Ref-returning helpers avoid repeated allocations in hot paths.
	const std::vector<RE::TESForm*>& GetAsFormListRef(Key key);
	const std::vector<RE::BGSKeyword*>& GetAsKeywordListRef(Key key);
	const std::vector<RE::BGSLocationRefType*>& GetAsLocationRefTypeListRef(Key key);
	const std::unordered_set<RE::TESFormID>& GetFormIDSet(Key key);
	const std::vector<RE::BGSKeyword*>& GetKeywordListRef(Key key);
	std::uint32_t GetNotifyCategoryMask();
	bool GetNotifyLegendaryEquipment();
	bool HasNotifyFilters();

	// Initialize reads and normalizes JSON values; LoadInjectionData resolves forms.
	bool Initialize();
	void LoadInjectionData();
}

template <>
struct std::hash<injection_data::Value>
{
	std::size_t operator()(const injection_data::Value& value) const
	{
		if (value.IsForm() && value.data.form) return value.data.form->formID;
		if (value.IsKeyword() && value.data.keyword) return value.data.keyword->formID;
		if (value.IsLocationRefType() && value.data.location_ref_type) return value.data.location_ref_type->formID;
		return 0;
	}
};
