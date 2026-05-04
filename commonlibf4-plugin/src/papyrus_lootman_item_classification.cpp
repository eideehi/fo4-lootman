#include "papyrus_lootman_internal.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "injection_data.h"

namespace papyrus_lootman
{
	using namespace RE;

	std::unordered_map<std::uint32_t, std::uint32_t> itemTypeCache;
	std::once_flag itemTypeCacheInitFlag;

	void BuildItemTypeCache()
	{
		auto* dataHandler = TESDataHandler::GetSingleton();
		if (!dataHandler)
		{
			return;
		}

		const auto& alchemyForms = dataHandler->GetFormArray<AlchemyItem>();
		const auto& bookForms = dataHandler->GetFormArray<TESObjectBOOK>();
		const auto& miscForms = dataHandler->GetFormArray<TESObjectMISC>();
		const auto& weaponForms = dataHandler->GetFormArray<TESObjectWEAP>();

		auto classify = [](const TESForm* form, ENUM_FORM_ID type) -> std::uint32_t
		{
			if (type == ENUM_FORM_ID::kALCH)
			{
				if (MatchesAny(form, injection_data::alch_type_alcohol)) return alcohol;
				if (MatchesAny(form, injection_data::alch_type_chemistry)) return chemistry;
				if (MatchesAny(form, injection_data::alch_type_food)) return food;
				if (MatchesAny(form, injection_data::alch_type_nuka_cola)) return nuka_cola;
				if (MatchesAny(form, injection_data::alch_type_stimpak)) return stimpak;
				if (MatchesAny(form, injection_data::alch_type_syringe_ammo)) return syringe_ammo;
				if (MatchesAny(form, injection_data::alch_type_water)) return water;
				return other_alchemy;
			}
			if (type == ENUM_FORM_ID::kBOOK)
			{
				if (MatchesAny(form, injection_data::book_type_perk_magazine)) return perkmagazine;
				return other_book;
			}
			if (type == ENUM_FORM_ID::kMISC)
			{
				if (MatchesAny(form, injection_data::misc_type_bobblehead)) return bobblehead;
				return other_miscellaneous;
			}
			if (type == ENUM_FORM_ID::kWEAP)
			{
				if (MatchesAny(form, injection_data::weap_type_grenade)) return grenade;
				if (MatchesAny(form, injection_data::weap_type_mine)) return mine;
				return other_weapon;
			}
			return 0;
		};

		itemTypeCache.clear();
		itemTypeCache.reserve(alchemyForms.size() + bookForms.size() + miscForms.size() + weaponForms.size());

		for (auto* form : alchemyForms)
		{
			if (form)
			{
				itemTypeCache.insert_or_assign(form->formID, classify(form, ENUM_FORM_ID::kALCH));
			}
		}
		for (auto* form : bookForms)
		{
			if (form)
			{
				itemTypeCache.insert_or_assign(form->formID, classify(form, ENUM_FORM_ID::kBOOK));
			}
		}
		for (auto* form : miscForms)
		{
			if (form)
			{
				itemTypeCache.insert_or_assign(form->formID, classify(form, ENUM_FORM_ID::kMISC));
			}
		}
		for (auto* form : weaponForms)
		{
			if (form)
			{
				itemTypeCache.insert_or_assign(form->formID, classify(form, ENUM_FORM_ID::kWEAP));
			}
		}
	}

	void EnsureItemTypeCache()
	{
		std::call_once(itemTypeCacheInitFlag, &BuildItemTypeCache);
	}

	ALCH GetALCHType(const TESForm* form)
	{
		if (form)
		{
			const auto it = itemTypeCache.find(form->formID);
			if (it != itemTypeCache.end())
			{
				return static_cast<ALCH>(it->second);
			}
		}

		if (MatchesAny(form, injection_data::alch_type_alcohol)) return alcohol;
		if (MatchesAny(form, injection_data::alch_type_chemistry)) return chemistry;
		if (MatchesAny(form, injection_data::alch_type_food)) return food;
		if (MatchesAny(form, injection_data::alch_type_nuka_cola)) return nuka_cola;
		if (MatchesAny(form, injection_data::alch_type_stimpak)) return stimpak;
		if (MatchesAny(form, injection_data::alch_type_syringe_ammo)) return syringe_ammo;
		if (MatchesAny(form, injection_data::alch_type_water)) return water;
		return other_alchemy;
	}

	BOOK GetBOOKType(const TESForm* form)
	{
		if (form)
		{
			const auto it = itemTypeCache.find(form->formID);
			if (it != itemTypeCache.end())
			{
				return static_cast<BOOK>(it->second);
			}
		}

		if (MatchesAny(form, injection_data::book_type_perk_magazine)) return perkmagazine;
		return other_book;
	}

	MISC GetMISCType(const TESForm* form)
	{
		if (form)
		{
			const auto it = itemTypeCache.find(form->formID);
			if (it != itemTypeCache.end())
			{
				return static_cast<MISC>(it->second);
			}
		}

		if (MatchesAny(form, injection_data::misc_type_bobblehead)) return bobblehead;
		return other_miscellaneous;
	}

	WEAP GetWEAPType(const TESForm* form)
	{
		if (form)
		{
			const auto it = itemTypeCache.find(form->formID);
			if (it != itemTypeCache.end())
			{
				return static_cast<WEAP>(it->second);
			}
		}

		if (MatchesAny(form, injection_data::weap_type_grenade)) return grenade;
		if (MatchesAny(form, injection_data::weap_type_mine)) return mine;
		return other_weapon;
	}
}
