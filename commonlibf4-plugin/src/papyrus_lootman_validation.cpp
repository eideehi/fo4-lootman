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

		// Only walk the ref's extra data for instance data when there is actually an exclude-keyword
		// list to test against (empty is the common default), avoiding a wasted GetInstanceData walk
		// per ref on the per-frame nearby scan.
		bool excludedByKeyword = false;
		bool excludeKeywordProbeFailed = false;
		unsigned long excludeKeywordExceptionCode = 0;
		{
			const auto excludeKeywords = injection_data::GetKeywordListRef(injection_data::exclude_keyword);
			if (!excludeKeywords->empty() &&
				!TryHasReferenceKeywordSafe(
					ref,
					*excludeKeywords,
					excludedByKeyword,
					excludeKeywordExceptionCode))
			{
				excludeKeywordProbeFailed = true;
			}
		}
		if (excludeKeywordProbeFailed)
		{
			RaiseMatchProbeException(excludeKeywordExceptionCode);
		}
		if (excludedByKeyword)
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
		if (!IsFormTypeMatch(formType, ENUM_FORM_ID::kCONT) &&
		    !IsFormTypeMatch(formType, ENUM_FORM_ID::kNPC_))
		{
			// IsDeferredActivationAmmoCandidate walks the extra list; evaluate it lazily (short-circuit)
			// after IsQuestItem, since it is only consulted for the rare quest-item case.
			if (IsQuestItem(ref->extraList.get()) &&
			    !IsDeferredActivationAmmoCandidate(ref, form) &&
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
					: !properties::GetBool(properties::enable_looting_in_settlement, true);
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

		TESFormID formID = 0;
		unsigned long exceptionCode = 0;
		if (!TryReadFormIDSafe(form, formID, exceptionCode))
		{
			RaiseMatchProbeException(exceptionCode);
		}

		{
			const auto excludedForms = injection_data::GetFormIDSet(injection_data::exclude_form);
			if (excludedForms->find(formID) != excludedForms->end())
			{
				return false;
			}
		}

		bool excludedByKeyword = false;
		bool excludeKeywordProbeFailed = false;
		{
			const auto excludedKeywords = injection_data::GetKeywordListRef(injection_data::exclude_keyword);
			if (!excludedKeywords->empty() &&
				!TryHasKeywordSafe(
					form,
					*excludedKeywords,
					nullptr,
					excludedByKeyword,
					exceptionCode))
			{
				excludeKeywordProbeFailed = true;
			}
		}
		if (excludeKeywordProbeFailed)
		{
			RaiseMatchProbeException(exceptionCode);
		}
		if (excludedByKeyword)
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

	// Reads the base form's name in its own SEH frame. The enclosing TryIsLootableInventoryItemSafe guard
	// already covers this call, but it converts any fault into "not lootable", so a fault raised while
	// probing the name would silently drop a perfectly good item. Probing here instead lets the caller keep
	// the codebase's conservative bias for an inconclusive probe: a failed read reports nothing and the
	// caller treats the item as named.
	bool TryIsNamedFormSafe(const TESForm* form, bool& outResult)
	{
#if defined(_MSC_VER)
		__try
		{
			outResult = !TESFullName::GetFullName(*form).empty();
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outResult = !TESFullName::GetFullName(*form).empty();
		return true;
#endif
	}

	bool IsValidInventoryItem(const TESForm* form, const InventoryItemInfo& info, MatchCache* matchCache)
	{
		if (info.dropped) return false;
		if (info.featured && !info.legendary &&
		    !MatchesAnyCached(form, injection_data::include_featured_item, matchCache)) return false;
		if (info.questItem && !MatchesAnyCached(form, injection_data::include_quest_item, matchCache)) return false;
		// Same rule as the world path's final gate in IsValidObject, applied to inventory items: an object
		// with no name is hidden from the player's inventory UI, so moving it into the player's pack hands
		// over something that can never be seen, equipped or dropped again. The two are not semantically
		// identical. IsValidObject reads ref->GetDisplayFullName(), which resolves an instanced or renamed
		// name; this reads the base record's FULL. A form whose displayed name comes entirely from Instance
		// Naming Rules with an empty base FULL is therefore skipped here while the same reference is still
		// looted off the ground. No shipped Fallout 4 or DLC record has that shape - every playable nameless
		// ARMO/MISC/WEAP/AMMO/ALCH in the base game and the six DLCs is a creature skin, a turret skin or a
		// dummy - so the exposure is limited to third-party plugins that name equipment only through INNR.
		// Creature equipment (SkinFeralGhoul and the other skins) carries no
		// FULL field at all and leaves the non-playable flag clear, so the IsPlayable gate in IsValidForm does
		// not catch it and the same object was rejected on the ground but looted out of a corpse.
		//
		// This lives on the inventory-item gate rather than in the shared IsValidForm because the world scan
		// runs ACTI and FLOR base forms through IsValidForm too, and Fallout4.esm alone ships 685 playable
		// nameless activators; a shared check would disable activator looting outright. IsValidInventoryItem
		// is only reachable from TryIsLootableInventoryItemSafe, whose sole callers are the two inventory
		// scans (HasLootableItem here and the transfer loop in papyrus_lootman_inventory_transfer.cpp), so
		// the world path is untouched. Ordered last so the cheap flag tests short-circuit ahead of it.
		if (form)
		{
			bool named = true;
			if (TryIsNamedFormSafe(form, named) && !named)
			{
				return false;
			}
		}
		return true;
	}

	// When the AlwaysLootingClothing option is on, configured clothing-like ARMO is allowed through the
	// Legendary Only equipment gate. Applies to ARMO only; weapons keep the AlwaysLootingExplosives
	// exception. With the option off, Legendary Only rejects non-legendary ARMO as it did before this feature.
	bool IsLegendaryOnlyExceptionArmor(const TESForm* form, const PropertiesSnapshot* props, MatchCache* matchCache)
	{
		const bool alwaysClothing = props
			? props->alwaysLootingClothing
			: properties::GetBool(properties::always_looting_clothing);
		return alwaysClothing && form && form->GetFormType() == ENUM_FORM_ID::kARMO &&
		       MatchesAnyCached(form, injection_data::include_legendary_only_exception, matchCache);
	}

	bool IsLootableInventoryItem(const TESForm* form, const InventoryItemInfo& info,
		const PropertiesSnapshot* props, MatchCache* matchCache)
	{
		auto formType = form->GetFormType();
		if (formType == ENUM_FORM_ID::kWEAP || formType == ENUM_FORM_ID::kARMO)
		{
			const bool legendaryOnly = props
				? props->lootingLegendaryOnly
				: properties::GetBool(properties::looting_legendary_only);
			if (legendaryOnly && !info.legendary)
			{
				if (IsLegendaryOnlyExceptionArmor(form, props, matchCache))
				{
					return true;
				}
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

	// The helpers below wrap individual engine-memory reads in their own SEH frame so that an access
	// violation on a corrupt inventory entry is converted into a false return instead of unwinding. Each
	// keeps the __try frame free of objects requiring C++ unwinding (MSVC C2712), mirroring the existing
	// Try...Safe wrappers, so callers can hold a lock across the scan without risking a skipped destructor.
	bool TryGetInventoryItemCountSafe(BGSInventoryList* inventoryList, std::uint32_t& outCount)
	{
#if defined(_MSC_VER)
		__try
		{
			outCount = static_cast<std::uint32_t>(inventoryList->data.size());
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outCount = static_cast<std::uint32_t>(inventoryList->data.size());
		return true;
#endif
	}

	bool TryGetInventoryEntrySafe(BGSInventoryList* inventoryList, std::uint32_t index,
		TESForm*& outForm, BGSInventoryItem::Stack*& outFirstStack)
	{
#if defined(_MSC_VER)
		__try
		{
			auto& item = inventoryList->data[index];
			outForm = item.object;
			outFirstStack = item.stackData.get();
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		auto& item = inventoryList->data[index];
		outForm = item.object;
		outFirstStack = item.stackData.get();
		return true;
#endif
	}

	bool TryGetFormTypeSafe(const TESForm* form, ENUM_FORM_ID& outFormType)
	{
#if defined(_MSC_VER)
		__try
		{
			outFormType = form->GetFormType();
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outFormType = form->GetFormType();
		return true;
#endif
	}

	bool TryGetNextStackSafe(BGSInventoryItem::Stack* stack, BGSInventoryItem::Stack*& outNext)
	{
#if defined(_MSC_VER)
		__try
		{
			outNext = stack->nextStack.get();
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outNext = stack->nextStack.get();
		return true;
#endif
	}

	bool TryBuildFallbackStackInfoSafe(const BGSInventoryItem::Stack& stack, InventoryItemInfo& outInfo)
	{
#if defined(_MSC_VER)
		__try
		{
			outInfo = BuildFallbackStackInfo(stack);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outInfo = BuildFallbackStackInfo(stack);
		return true;
#endif
	}

	bool TryIsLootableInventoryItemSafe(const TESForm* form, const InventoryItemInfo& info,
		const PropertiesSnapshot* props, MatchCache* matchCache, bool& outResult)
	{
#if defined(_MSC_VER)
		__try
		{
			outResult = IsValidInventoryItem(form, info, matchCache) &&
			            IsLootableInventoryItem(form, info, props, matchCache);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outResult = IsValidInventoryItem(form, info, matchCache) &&
		            IsLootableInventoryItem(form, info, props, matchCache);
		return true;
#endif
	}

	bool TryGetStackExtraSafe(const BGSInventoryItem::Stack& stack, BSTSmartPointer<ExtraDataList>& outExtra)
	{
#if defined(_MSC_VER)
		__try
		{
			outExtra = stack.extra;
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outExtra = stack.extra;
		return true;
#endif
	}

	bool TryGetInstanceDataSafe(const ExtraDataList* extra, TBO_InstanceData*& outInstanceData)
	{
#if defined(_MSC_VER)
		__try
		{
			outInstanceData = GetInstanceData(extra);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outInstanceData = GetInstanceData(extra);
		return true;
#endif
	}

	bool HasLootableItem(BGSInventoryList* inventoryList, const PropertiesSnapshot* props = nullptr,
		MatchCache* matchCache = nullptr, bool sourceIsDead = false,
		std::vector<BGSMod::Attachment::Mod*>* modBuffer, LootPassBudget* passBudget)
	{
		if (!inventoryList) return false;

		PropertiesSnapshot localProps;
		if (!props)
		{
			localProps = PropertiesSnapshot::Capture();
			props = &localProps;
		}

		bool result = false;
		// Reuse the caller's scan buffer when provided (GetMods clears it before each fill), so a nearby
		// scan does not heap-allocate a fresh vector for every container/corpse it inspects.
		std::vector<BGSMod::Attachment::Mod*> localModBuffer;
		std::vector<BGSMod::Attachment::Mod*>& modBufferRef = modBuffer ? *modBuffer : localModBuffer;
		const auto lootableInventoryItemType = props->lootableInventoryItemType;
		// Hold the inventory read lock for the whole scan. Every engine-memory access below is routed through
		// an SEH-guarded Try...Safe helper that returns a value on an access violation instead of unwinding
		// past this frame, so the only exit while the lock is held is a normal C++ return and ~ReadLockGuard
		// always runs. Under /EHsc an SEH unwind skips C++ destructors, so an unguarded fault here would leak
		// the read lock and deadlock the next engine writer to this inventory.
		ReadLockGuard guard(inventoryList->rwLock);

		std::uint32_t itemCount = 0;
		if (!TryGetInventoryItemCountSafe(inventoryList, itemCount))
		{
			return result;
		}

		for (std::uint32_t index = 0; index < itemCount && !result; ++index)
		{
			if (passBudget && passBudget->ShouldStop())
			{
				break;
			}

			TESForm* form = nullptr;
			BGSInventoryItem::Stack* stack = nullptr;
			if (!TryGetInventoryEntrySafe(inventoryList, index, form, stack) || !form)
			{
				continue;
			}

			ENUM_FORM_ID formType{};
			if (!TryGetFormTypeSafe(form, formType))
			{
				continue;
			}
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

			while (stack)
			{
				if (passBudget && passBudget->ShouldStop())
				{
					break;
				}

				bool stackLootable = false;

				InventoryItemInfo stackInfo{};
				if (TryGetInventoryStackInfoSafe(*stack, modBufferRef, inventory_info_full, stackInfo))
				{
					auto resolvedCount = stackInfo.totalCount;
					if (resolvedCount <= 0 && stackInfo.equipped)
					{
						resolvedCount = 1;
					}
					if (resolvedCount <= 0 && sourceIsDead && formType == ENUM_FORM_ID::kWEAP)
					{
						resolvedCount = 1;
					}
					bool lootableStack = false;
					if (resolvedCount > 0 &&
					    TryIsLootableInventoryItemSafe(form, stackInfo, props, matchCache, lootableStack) &&
					    lootableStack)
					{
						stackLootable = true;
					}
				}
				else
				{
					// The stack-info read above already faulted on this stack; build the fallback through an
					// SEH guard too, because BuildFallbackStackInfo dereferences the same suspect stack.
					InventoryItemInfo fallbackInfo{};
					if (TryBuildFallbackStackInfoSafe(*stack, fallbackInfo))
					{
						auto resolvedFallbackCount = fallbackInfo.totalCount;
						if (resolvedFallbackCount <= 0 && fallbackInfo.equipped)
						{
							resolvedFallbackCount = 1;
						}
						if (resolvedFallbackCount <= 0 && sourceIsDead && formType == ENUM_FORM_ID::kWEAP)
						{
							resolvedFallbackCount = 1;
						}
						bool lootableFallbackStack = false;
						if (resolvedFallbackCount > 0 &&
						    TryIsLootableInventoryItemSafe(form, fallbackInfo, props, matchCache, lootableFallbackStack) &&
						    lootableFallbackStack)
						{
							stackLootable = true;
						}
					}
				}

				if (stackLootable)
				{
					result = true;
					break;
				}

				BGSInventoryItem::Stack* nextStack = nullptr;
				if (!TryGetNextStackSafe(stack, nextStack))
				{
					break;
				}
				stack = nextStack;
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
		MatchCache* matchCache = nullptr, LootPassBudget* passBudget = nullptr)
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
			const bool hasLootableItem = HasLootableItem(ref->inventoryList, props, matchCache, false, modBuffer, passBudget);
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
			if (!HasLootableItem(ref->inventoryList, props, matchCache, true, modBuffer, passBudget))
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kWEAP || formType == ENUM_FORM_ID::kARMO)
		{
			// The mod buffer must be caller-owned: this frame sits inside the
			// TryIsLootableObjectSafe SEH guard and the matching calls below can
			// re-raise a recoverable probe fault, whose unwind skips local
			// destructors under /EHsc. A frame-local fallback vector here would
			// leak its heap block on that path, so a missing buffer fails closed.
			if (!modBuffer)
			{
				return false;
			}
			EquipmentData data{};
			if (!TryGetEquipmentDataSafe(ref->extraList.get(), modBuffer, data))
			{
				const bool legendaryOnly = props
					? props->lootingLegendaryOnly
					: properties::GetBool(properties::looting_legendary_only);
				if (legendaryOnly)
				{
					if (IsLegendaryOnlyExceptionArmor(form, props, matchCache))
					{
						return true;
					}
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
				if (IsLegendaryOnlyExceptionArmor(form, props, matchCache))
				{
					return true;
				}
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
		MatchCache* matchCache, bool& outResult, LootPassBudget* passBudget)
	{
#if defined(_MSC_VER)
		__try
		{
			outResult = IsLootableObject(
				ref,
				props,
				baseForm,
				modBuffer,
				matchCache,
				passBudget);
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
			matchCache,
			passBudget);
		return true;
#endif
	}
}
