#include "papyrus_lootman_internal.h"

#include <cstring>
#include <excpt.h>
#include <vector>

#include "form_cache.h"
#include "injection_data.h"
#include "properties.h"
#include "vendor_chest.h"

namespace papyrus_lootman
{
	using namespace form_cache;
	using namespace RE;

	inline constexpr std::uint32_t kFormFlagDeleted = 1u << 5;
	inline constexpr std::uint32_t kFormFlagDisabled = 1u << 11;
	inline constexpr std::uint32_t kFormFlagDestroyed = 1u << 23;
	inline constexpr std::uint32_t kActivationBlocked = 1u << 0;
	inline constexpr std::uint32_t kActivationIgnored = 1u << 1;
	inline constexpr std::uint32_t kAmmoFusionCoreFormId = 0x00075FE4;

	struct ExtraFlagsCompat :
		public BSExtraData
	{
		static constexpr auto TYPE = EXTRA_DATA_TYPE::kFlags;
		std::uint32_t flags;
	};

	struct ExtraEncounterZoneCompat :
		public BSExtraData
	{
		static constexpr auto TYPE = EXTRA_DATA_TYPE::kEncounterZone;
		BGSEncounterZone* encounterZone;
	};

	bool IsSettlement(const BGSEncounterZone* zone)
	{
		if (!zone) return false;

		if (zone->data.location)
		{
			auto locKeywordForm = zone->data.location->As<BGSKeywordForm>();
			if (locKeywordForm)
			{
				if (locKeywordForm->HasKeyword(keyword::settlement) ||
				    locKeywordForm->HasKeyword(keyword::workshopSettlement))
				{
					return true;
				}
			}
		}

		if (zone->IsWorkshop())
		{
			return true;
		}

		return false;
	}

	bool IsFriendFaction(TESFaction* factionToCheck)
	{
		if (!factionToCheck || !faction::playerFaction) return false;

		constexpr std::uint32_t canBeOwnerFlag = 1 << 15;
		if ((factionToCheck->data.flags & canBeOwnerFlag) == 0) return false;

		auto& reactionList = factionToCheck->reactionList;
		for (auto it = reactionList.begin(); it != reactionList.end(); ++it)
		{
			auto reaction = *it;
			if (!reaction || !reaction->form) continue;
			if (reaction->form->formID == faction::playerFaction->formID)
			{
				return reaction->fightReaction >= FIGHT_REACTION::kFriend;
			}
		}

		return false;
	}

	bool IsOwnerEmptyOrFriend(TESForm* owner)
	{
		if (!owner) return true;

		auto factionOwner = owner->As<TESFaction>();
		if (factionOwner)
		{
			auto player = PlayerCharacter::GetSingleton();
			if (player && player->IsInFaction(factionOwner))
			{
				return true;
			}
			return IsFriendFaction(factionOwner);
		}

		auto npcOwner = owner->As<TESNPC>();
		if (npcOwner)
		{
			if (npcOwner->IsPlayer()) return true;

			for (const auto& factionRank : npcOwner->factions)
			{
				if (IsFriendFaction(factionRank.faction))
				{
					return true;
				}
			}
			return false;
		}

		return true;
	}

	bool CheckPrecondition(const TESObjectREFR* ref)
	{
		return (ref->formFlags & kFormFlagDeleted) == 0
		    && (ref->formFlags & kFormFlagDisabled) == 0
		    && (ref->formFlags & kFormFlagDestroyed) == 0;
	}

	bool IsFusionCoreBaseForm(const TESForm* form)
	{
		return form && form->GetFormType() == ENUM_FORM_ID::kAMMO &&
		       form->formID == kAmmoFusionCoreFormId;
	}

	struct ActivationExtraFlags
	{
		bool activateRef = false;
		bool openCloseActivateRef = false;
	};

	ActivationExtraFlags GetActivationExtraFlags(const TESObjectREFR* ref)
	{
		ActivationExtraFlags flags;
		auto* extraList = ref ? ref->extraList.get() : nullptr;
		if (!extraList)
		{
			return flags;
		}

		flags.activateRef = extraList->HasType(EXTRA_DATA_TYPE::kActivateRef);
		flags.openCloseActivateRef = extraList->HasType(EXTRA_DATA_TYPE::kOpenCloseActivateRef);
		return flags;
	}

	bool IsTriggeredAmmoActivationCandidate(TESObjectREFR* ref, TESForm* baseForm = nullptr)
	{
		if (!ref)
		{
			return false;
		}

		auto* form = baseForm ? baseForm : ref->GetObjectReference();
		if (!form || form->GetFormType() != ENUM_FORM_ID::kAMMO)
		{
			return false;
		}

		const auto flags = GetActivationExtraFlags(ref);
		return flags.activateRef || flags.openCloseActivateRef;
	}

	bool IsDeferredActivationAmmoCandidate(TESObjectREFR* ref, TESForm* baseForm)
	{
		if (!ref)
		{
			return false;
		}

		auto* form = baseForm ? baseForm : ref->GetObjectReference();
		if (!form || form->GetFormType() != ENUM_FORM_ID::kAMMO)
		{
			return false;
		}

		return IsTriggeredAmmoActivationCandidate(ref, form) || IsFusionCoreBaseForm(form);
	}

	bool IsValidObject(TESObjectREFR* ref, const PropertiesSnapshot* props = nullptr,
		TESForm* baseForm = nullptr, MatchCache* matchCache = nullptr)
	{
		if (!ref) return false;
		if (ref->IsPlayerRef()) return false;
		if (ref->IsWater()) return false;

		auto* placementHandle = Workshop::GetPlacementItem();
		if (placementHandle && *placementHandle && *placementHandle == ref->GetHandle())
		{
			return false;
		}

		const bool hasExcludeKeyword = HasKeyword(
			ref,
			injection_data::GetKeywordListRef(injection_data::exclude_keyword),
			GetInstanceData(ref));
		if (hasExcludeKeyword)
		{
			return false;
		}

		auto* form = baseForm ? baseForm : ref->GetObjectReference();
		if (!form)
		{
			return false;
		}

		if (!ref->extraList)
		{
			const auto name = ref->GetDisplayFullName();
			if (!name || strlen(name) == 0)
			{
				return false;
			}
			return true;
		}

		auto formType = form->GetFormType();
		const bool isDeferredActivationAmmoCandidate = IsDeferredActivationAmmoCandidate(ref, form);
		if (!IsFormTypeMatch(formType, ENUM_FORM_ID::kCONT) &&
		    !IsFormTypeMatch(formType, ENUM_FORM_ID::kNPC_))
		{
			if (IsQuestItem(ref->extraList.get()) &&
			    !isDeferredActivationAmmoCandidate &&
			    !MatchesAnyCached(form, injection_data::include_quest_item, matchCache))
			{
				return false;
			}
		}

		auto extraFlags = ref->extraList->GetByType<ExtraFlagsCompat>();
		if (extraFlags)
		{
			auto rawFlags = extraFlags->flags;
			if ((rawFlags & kActivationBlocked) || ((rawFlags & kActivationIgnored) != 0))
			{
				if (!MatchesAnyCached(form, injection_data::include_activation_block, matchCache))
				{
					return false;
				}
			}
		}

		auto refOwner = ref->GetOwner();
		if (!IsOwnerEmptyOrFriend(refOwner))
		{
			return false;
		}

		auto extraEZ = ref->extraList->GetByType<ExtraEncounterZoneCompat>();
		if (extraEZ)
		{
			auto ez = extraEZ->encounterZone;
			if (ez)
			{
				if (!IsOwnerEmptyOrFriend(ez->data.zoneOwner))
				{
					return false;
				}
				const bool notFromSettlement = props
					? props->notLootingFromSettlement
					: properties::GetBool(properties::not_looting_from_settlement);
				if (notFromSettlement && IsSettlement(ez))
				{
					return false;
				}
			}
		}

		const auto name = ref->GetDisplayFullName();
		if (!name || strlen(name) == 0)
		{
			return false;
		}

		return true;
	}

	bool IsAllowedUniqueItem(const TESForm* form, MatchCache* matchCache = nullptr)
	{
		if (!form || !form_list::IsUniqueItem(form->formID)) return true;
		return MatchesAnyCached(form, injection_data::include_unique_item, matchCache);
	}

	bool IsAllowedFeaturedItem(const TESForm* form, MatchCache* matchCache = nullptr)
	{
		if (!HasKeyword(form, keyword::featuredItem)) return true;
		return MatchesAnyCached(form, injection_data::include_featured_item, matchCache);
	}

	bool IsValidForm(TESForm* form, const PropertiesSnapshot* props = nullptr, MatchCache* matchCache = nullptr)
	{
		if (!form) return false;
		if (!IsPlayable(form)) return false;

		const auto& excludedForms = injection_data::GetFormIDSet(injection_data::exclude_form);
		if (excludedForms.find(form->formID) != excludedForms.end())
		{
			return false;
		}

		const auto& excludedKeywords = injection_data::GetKeywordListRef(injection_data::exclude_keyword);
		if (!excludedKeywords.empty() && HasKeyword(form, excludedKeywords))
		{
			return false;
		}

		if (!IsAllowedUniqueItem(form, matchCache) || !IsAllowedFeaturedItem(form, matchCache))
		{
			auto formType = form->GetFormType();
			if (formType == ENUM_FORM_ID::kBOOK)
			{
				const int bookType = props
					? props->lootableBookItemType
					: properties::GetInt(properties::lootable_book_item_type);
				if (MatchesAnyCached(form, injection_data::book_type_perk_magazine, matchCache)
				    && (bookType & perkmagazine) == 0)
				{
					return false;
				}
			}
			else if (formType == ENUM_FORM_ID::kMISC)
			{
				const int miscType = props
					? props->lootableMiscItemType
					: properties::GetInt(properties::lootable_misc_item_type);
				if (MatchesAnyCached(form, injection_data::misc_type_bobblehead, matchCache)
				    && (miscType & bobblehead) == 0)
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}

		return true;
	}

	bool TryIsValidFormSafe(TESForm* form, const PropertiesSnapshot* props,
		MatchCache* matchCache, bool& outResult)
	{
#if defined(_MSC_VER)
		__try
		{
			outResult = IsValidForm(form, props, matchCache);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outResult = IsValidForm(form, props, matchCache);
		return true;
#endif
	}

	bool IsValidInventoryItem(const TESForm* form, const InventoryItemInfo& info, MatchCache* matchCache)
	{
		if (info.dropped) return false;
		if (info.featured && !info.legendary &&
		    !MatchesAnyCached(form, injection_data::include_featured_item, matchCache)) return false;
		if (info.questItem && !MatchesAnyCached(form, injection_data::include_quest_item, matchCache)) return false;
		return true;
	}

	bool IsLootableInventoryItem(const TESForm* form, const InventoryItemInfo& info,
		const PropertiesSnapshot* props)
	{
		auto formType = form->GetFormType();
		if (formType == ENUM_FORM_ID::kWEAP || formType == ENUM_FORM_ID::kARMO)
		{
			const bool legendaryOnly = props
				? props->lootingLegendaryOnly
				: properties::GetBool(properties::looting_legendary_only);
			if (legendaryOnly && !info.legendary)
			{
				const bool alwaysExplosives = props
					? props->alwaysLootingExplosives
					: properties::GetBool(properties::always_looting_explosives);
				if (alwaysExplosives)
				{
					auto type = GetWEAPType(form);
					return type == WEAP::grenade || type == WEAP::mine;
				}
				return false;
			}
		}
		return true;
	}

	bool IsLootableForm(TESForm* form, const PropertiesSnapshot* props = nullptr,
		MatchCache* matchCache = nullptr)
	{
		auto formType = form->GetFormType();
		if (formType == ENUM_FORM_ID::kACTI)
		{
			if (!MatchesAnyCached(form, injection_data::include_activator, matchCache))
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kALCH)
		{
			const int alchType = props
				? props->lootableAlchItemType
				: properties::GetInt(properties::lootable_alch_item_type);
			if ((alchType & GetALCHType(form)) == 0)
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kBOOK)
		{
			const int bookType = props
				? props->lootableBookItemType
				: properties::GetInt(properties::lootable_book_item_type);
			if ((bookType & GetBOOKType(form)) == 0)
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kCONT)
		{
			if (HasKeyword(form, keyword::workshop))
			{
				return false;
			}
			if (vendor_chest::IsVendorChest(form->formID))
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kMISC)
		{
			const int miscType = props
				? props->lootableMiscItemType
				: properties::GetInt(properties::lootable_misc_item_type);
			if ((miscType & GetMISCType(form)) == 0)
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kWEAP)
		{
			const int weapType = props
				? props->lootableWeapItemType
				: properties::GetInt(properties::lootable_weap_item_type);
			if ((weapType & GetWEAPType(form)) == 0)
			{
				return false;
			}
		}

		return true;
	}

	bool TryIsLootableFormSafe(TESForm* form, const PropertiesSnapshot* props,
		MatchCache* matchCache, bool& outResult)
	{
#if defined(_MSC_VER)
		__try
		{
			outResult = IsLootableForm(form, props, matchCache);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outResult = IsLootableForm(form, props, matchCache);
		return true;
#endif
	}

	bool HasLootableItem(BGSInventoryList* inventoryList, const PropertiesSnapshot* props = nullptr,
		MatchCache* matchCache = nullptr, bool sourceIsDead = false)
	{
		if (!inventoryList) return false;

		PropertiesSnapshot localProps;
		if (!props)
		{
			localProps = PropertiesSnapshot::Capture();
			props = &localProps;
		}

		bool result = false;
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		const auto lootableInventoryItemType = props->lootableInventoryItemType;
		ReadLockGuard guard(inventoryList->rwLock);

		for (auto& item : inventoryList->data)
		{
			auto form = item.object;
			if (!form) continue;

			auto formType = form->GetFormType();
			if (!IsFormTypeMatchesItemType(formType, lootableInventoryItemType))
			{
				continue;
			}

			bool validForm = false;
			if (!TryIsValidFormSafe(form, props, matchCache, validForm) || !validForm)
			{
				continue;
			}

			bool lootableForm = false;
			if (!TryIsLootableFormSafe(form, props, matchCache, lootableForm) || !lootableForm)
			{
				continue;
			}

			for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get())
			{
				InventoryItemInfo stackInfo{};
				if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, inventory_info_full, stackInfo))
				{
					auto fallbackInfo = BuildFallbackStackInfo(*stack);
					auto resolvedFallbackCount = fallbackInfo.totalCount;
					if (resolvedFallbackCount <= 0 && fallbackInfo.equipped)
					{
						resolvedFallbackCount = 1;
					}
					if (resolvedFallbackCount <= 0 && sourceIsDead && formType == ENUM_FORM_ID::kWEAP)
					{
						resolvedFallbackCount = 1;
					}
					if (resolvedFallbackCount <= 0)
					{
						continue;
					}
					if (!IsValidInventoryItem(form, fallbackInfo, matchCache) ||
					    !IsLootableInventoryItem(form, fallbackInfo, props))
					{
						continue;
					}
					result = true;
					break;
				}

				auto resolvedCount = stackInfo.totalCount;
				if (resolvedCount <= 0 && stackInfo.equipped)
				{
					resolvedCount = 1;
				}
				if (resolvedCount <= 0 && sourceIsDead && formType == ENUM_FORM_ID::kWEAP)
				{
					resolvedCount = 1;
				}
				if (resolvedCount <= 0)
				{
					continue;
				}
				if (!IsValidInventoryItem(form, stackInfo, matchCache) ||
				    !IsLootableInventoryItem(form, stackInfo, props))
				{
					continue;
				}
				result = true;
				break;
			}
			if (result)
			{
				break;
			}
		}

		return result;
	}

	bool IsLinkedToWorkshop(TESObjectREFR* ref)
	{
		if (!ref) return false;

		// Stay on the cached workshop keyword path here; touching the default
		// object manager from this scan code is less stable than the keyword link.
		BGSKeyword* workshopKw = keyword::workshop;
		if (!workshopKw) return false;

		auto workshopRef = ref->GetLinkedRef(workshopKw);
		if (!workshopRef) return false;

		return workshopRef->extraList && workshopRef->extraList->HasType(EXTRA_DATA_TYPE::kWorkshop);
	}

	bool IsLootableObject(TESObjectREFR* ref, const PropertiesSnapshot* props = nullptr,
		TESForm* baseForm = nullptr, std::vector<BGSMod::Attachment::Mod*>* modBuffer = nullptr,
		MatchCache* matchCache = nullptr)
	{
		if (!ref) return false;
		auto form = baseForm ? baseForm : ref->GetObjectReference();
		if (!form) return false;
		auto formType = form->GetFormType();

		if (formType == ENUM_FORM_ID::kCONT)
		{
			if (IsSpecialContainerReference(ref, form))
			{
				return false;
			}
			if (IsLinkedToWorkshop(ref))
			{
				return false;
			}
			EnsureContainerInventoryListForLootScan(ref, form);
			const bool hasLootableItem = HasLootableItem(ref->inventoryList, props, matchCache);
			if (!hasLootableItem)
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kFLOR)
		{
			// Flora harvested flag: bit 13
			if ((ref->formFlags >> 13) & 1)
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kNPC_)
		{
			if (!IsDeadForLooting(ref))
			{
				return false;
			}
			if (!HasLootableItem(ref->inventoryList, props, matchCache, true))
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kWEAP || formType == ENUM_FORM_ID::kARMO)
		{
			std::vector<BGSMod::Attachment::Mod*> localBuffer;
			auto* equipmentBuffer = modBuffer ? modBuffer : &localBuffer;
			EquipmentData data{};
			if (!TryGetEquipmentDataSafe(ref->extraList.get(), equipmentBuffer, data))
			{
				const bool legendaryOnly = props
					? props->lootingLegendaryOnly
					: properties::GetBool(properties::looting_legendary_only);
				if (legendaryOnly)
				{
					const bool alwaysExplosives = props
						? props->alwaysLootingExplosives
						: properties::GetBool(properties::always_looting_explosives);
					if (alwaysExplosives)
					{
						auto type = GetWEAPType(form);
						return type == WEAP::grenade || type == WEAP::mine;
					}
					return false;
				}
				return true;
			}

			const bool legendaryOnly = props
				? props->lootingLegendaryOnly
				: properties::GetBool(properties::looting_legendary_only);
			if (legendaryOnly && !data.isLegendary)
			{
				const bool alwaysExplosives = props
					? props->alwaysLootingExplosives
					: properties::GetBool(properties::always_looting_explosives);
				if (alwaysExplosives)
				{
					auto type = GetWEAPType(form);
					return type == WEAP::grenade || type == WEAP::mine;
				}
				return false;
			}
		}
		return true;
	}

	bool TryIsValidObjectSafe(TESObjectREFR* ref, const PropertiesSnapshot* props,
		TESForm* baseForm, MatchCache* matchCache, bool& outResult)
	{
#if defined(_MSC_VER)
		__try
		{
			outResult = IsValidObject(ref, props, baseForm, matchCache);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outResult = IsValidObject(ref, props, baseForm, matchCache);
		return true;
#endif
	}

	bool TryIsLootableObjectSafe(TESObjectREFR* ref, const PropertiesSnapshot* props,
		TESForm* baseForm, std::vector<BGSMod::Attachment::Mod*>* modBuffer,
		MatchCache* matchCache, bool& outResult)
	{
#if defined(_MSC_VER)
		__try
		{
			outResult = IsLootableObject(
				ref,
				props,
				baseForm,
				modBuffer,
				matchCache);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outResult = IsLootableObject(
			ref,
			props,
			baseForm,
			modBuffer,
			matchCache);
		return true;
#endif
	}
}
