#include "papyrus_lootman.h"

#include "constructible_object.h"
#include "form_cache.h"
#include "injection_data.h"
#include "properties.h"
#include "vendor_chest.h"

namespace papyrus_lootman
{
	using namespace form_cache;
	using namespace RE;

	// ---- Item type enums (matching v2.2.0 Papyrus bitmasks) ----

	enum Generic : std::uint32_t
	{
		alch = 1 << 0,
		ammo = 1 << 1,
		armo = 1 << 2,
		book = 1 << 3,
		ingr = 1 << 4,
		keym = 1 << 5,
		misc = 1 << 6,
		weap = 1 << 7,
		all_item = alch | ammo | armo | book | ingr | keym | misc | weap,
	};

	enum ALCH : std::uint32_t
	{
		alcohol = 1 << 0,
		chemistry = 1 << 1,
		food = 1 << 2,
		nuka_cola = 1 << 3,
		stimpak = 1 << 4,
		syringe_ammo = 1 << 5,
		water = 1 << 6,
		other_alchemy = 1 << 7,
	};

	enum BOOK : std::uint32_t
	{
		perkmagazine = 1 << 0,
		other_book = 1 << 1,
	};

	enum MISC : std::uint32_t
	{
		bobblehead = 1 << 0,
		other_miscellaneous = 1 << 1,
	};

	enum WEAP : std::uint32_t
	{
		grenade = 1 << 0,
		mine = 1 << 1,
		other_weapon = 1 << 2,
	};

	// ---- EquipmentData ----

	struct EquipmentData
	{
		bool isLegendary = false;
		bool isFeaturedItem = false;
		bool isUnscrappable = false;
	};

	// ---- QuestAliasInfo ----

	struct QuestAliasInfo
	{
		enum FLAG : std::uint16_t
		{
			enabled = 1 << 0,
			completed = 1 << 1,
			failed = 1 << 6,
			active = 1 << 11,
		};

		std::uint32_t questId;
		std::uint16_t flags;
		bool isEssential;
		bool isQuestItem;
	};

	// ---- InventoryItemInfo ----

	struct InventoryItemInfo
	{
		bool dropped = false;
		bool featured = false;
		bool unscrappable = false;
		bool equipped = false;
		bool legendary = false;
		bool questItem = false;
	};

	// ---- Constants ----

	inline constexpr std::uint32_t kFormFlagDeleted = 1u << 5;
	inline constexpr std::uint32_t kFormFlagDisabled = 1u << 11;
	inline constexpr std::uint32_t kFormFlagDestroyed = 1u << 23;
	inline constexpr std::uint32_t kLegendaryModFlag = 1u << 4;
	inline constexpr std::uint32_t kWeaponTargetKeywords = 31;
	inline constexpr std::uint32_t kArmorTargetKeywords = 3;
	inline constexpr std::uint32_t kActivationBlocked = 1u << 0;
	inline constexpr std::uint32_t kActivationIgnored = 1u << 1;

	// ---- Compat ExtraData structs ----

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

	struct ExtraOwnershipCompat :
		public BSExtraData
	{
		static constexpr auto TYPE = EXTRA_DATA_TYPE::kOwnership;
		TESForm* owner;
	};

	// ---- ReadLockGuard ----

	template <class T>
	struct ReadLockGuard
	{
		explicit ReadLockGuard(T& a_lock) :
			lock(a_lock)
		{
			lock.lock_read();
		}

		~ReadLockGuard()
		{
			lock.unlock_read();
		}

		T& lock;
	};

	// ---- Distance helpers ----

	inline float GetDistance(const NiPoint3& pos1, const NiPoint3& pos2)
	{
		return pos1.GetDistance(pos2);
	}

	// ---- Form type matching ----

	bool IsFormTypeMatch(ENUM_FORM_ID formType, ENUM_FORM_ID matchingType)
	{
		if (formType == matchingType) return true;
		if (matchingType == ENUM_FORM_ID::kTotal)
		{
			return formType == ENUM_FORM_ID::kACTI || formType == ENUM_FORM_ID::kALCH
			    || formType == ENUM_FORM_ID::kAMMO || formType == ENUM_FORM_ID::kARMO
			    || formType == ENUM_FORM_ID::kBOOK || formType == ENUM_FORM_ID::kCONT
			    || formType == ENUM_FORM_ID::kFLOR || formType == ENUM_FORM_ID::kINGR
			    || formType == ENUM_FORM_ID::kKEYM || formType == ENUM_FORM_ID::kMISC
			    || formType == ENUM_FORM_ID::kNPC_ || formType == ENUM_FORM_ID::kWEAP;
		}
		return false;
	}

	bool IsItemTypeMatch(std::uint32_t itemType, std::uint32_t matchingType)
	{
		return (itemType & matchingType) == matchingType;
	}

	bool IsFormTypeMatchesItemType(ENUM_FORM_ID formType, std::uint32_t itemType)
	{
		return (formType == ENUM_FORM_ID::kALCH && IsItemTypeMatch(itemType, alch))
		    || (formType == ENUM_FORM_ID::kAMMO && IsItemTypeMatch(itemType, ammo))
		    || (formType == ENUM_FORM_ID::kARMO && IsItemTypeMatch(itemType, armo))
		    || (formType == ENUM_FORM_ID::kBOOK && IsItemTypeMatch(itemType, book))
		    || (formType == ENUM_FORM_ID::kINGR && IsItemTypeMatch(itemType, ingr))
		    || (formType == ENUM_FORM_ID::kKEYM && IsItemTypeMatch(itemType, keym))
		    || (formType == ENUM_FORM_ID::kMISC && IsItemTypeMatch(itemType, misc))
		    || (formType == ENUM_FORM_ID::kWEAP && IsItemTypeMatch(itemType, weap));
	}

	// ---- Mod helpers ----

	inline bool IsLegendaryMod(const BGSMod::Attachment::Mod* mod)
	{
		return mod && (mod->formFlags & kLegendaryModFlag) != 0;
	}

	bool IsPlayable(const TESForm* form)
	{
		if (!form) return false;
		return (form->formFlags & (1 << 2)) == 0;
	}

	bool IsFavorite(const TESForm* form)
	{
		if (!form) return false;
		auto favMgr = FavoritesManager::GetSingleton();
		if (!favMgr) return false;
		for (int i = 0; i < 12; ++i)
		{
			TESBoundObject* fav = favMgr->storedFavTypes[i];
			if (fav && fav->formID == form->formID)
			{
				return true;
			}
		}
		return false;
	}

	// ---- Quest alias helpers ----

	struct QuestAliasFlags
	{
		bool isEssential = false;
		bool isQuestItem = false;
	};

	QuestAliasFlags GetQuestAliasFlags(ExtraDataList* extraDataList)
	{
		QuestAliasFlags flags;
		if (!extraDataList) return flags;

		auto extraData = extraDataList->GetByType<ExtraAliasInstanceArray>();
		if (!extraData) return flags;

		ReadLockGuard guard(extraData->aliasArrayLock);
		for (auto& data : extraData->aliasArray)
		{
			if (!data.quest || data.quest->GetDelete() || !data.alias) continue;
			auto questFlags = data.quest->data.flags;

			if (!flags.isEssential)
			{
				if ((questFlags & QuestAliasInfo::enabled) != 0 &&
				    data.alias->flags.all(BGSBaseAlias::FLAGS::kEssential))
				{
					flags.isEssential = true;
				}
			}

			if (!flags.isQuestItem)
			{
				if (data.alias->IsQuestObject())
				{
					flags.isQuestItem = true;
				}
				else if ((questFlags & QuestAliasInfo::enabled) != 0 &&
				         (questFlags & (QuestAliasInfo::completed | QuestAliasInfo::failed)) == 0)
				{
					flags.isQuestItem = true;
				}
			}

			if (flags.isEssential && flags.isQuestItem) break;
		}

		return flags;
	}

	bool IsEssential(const TESObjectREFR* ref)
	{
		auto baseObj = ref->GetObjectReference();
		if (baseObj)
		{
			auto npc = baseObj->As<TESNPC>();
			if (npc && npc->IsEssential())
			{
				return true;
			}
		}

		return GetQuestAliasFlags(ref->extraList.get()).isEssential;
	}

	bool IsDeadForLooting(const TESObjectREFR* ref)
	{
		if (!ref) return false;
		// v2.2.0: IsDead(ref, !IsEssential(ref))
		return ref->IsDead(!IsEssential(ref));
	}

	// ---- Mod extraction ----

	bool GetMods(ExtraDataList* extraDataList, std::vector<BGSMod::Attachment::Mod*>* list)
	{
		if (!list) return false;
		list->clear();
		if (!extraDataList) return false;

		auto objectModData = extraDataList->GetByType<BGSObjectInstanceExtra>();
		if (!objectModData || !objectModData->values) return false;

		auto indexData = objectModData->GetIndexData();
		if (indexData.empty()) return false;

		bool found = false;
		for (auto& entry : indexData)
		{
			auto form = TESForm::GetFormByID(entry.objectID);
			auto objectMod = form ? form->As<BGSMod::Attachment::Mod>() : nullptr;
			if (objectMod)
			{
				list->push_back(objectMod);
				found = true;
			}
		}
		return found;
	}

	EquipmentData GetEquipmentData(ExtraDataList* extraDataList, std::vector<BGSMod::Attachment::Mod*>* buffer)
	{
		EquipmentData equipmentData;
		if (!GetMods(extraDataList, buffer)) return equipmentData;

		for (const auto& mod : *buffer)
		{
			if (IsLegendaryMod(mod))
			{
				equipmentData.isLegendary = true;
			}

			BGSMod::Attachment::Mod::Data containerData;
			mod->GetData(containerData);
			if (!containerData.propertyMods) continue;

			for (std::uint32_t i = 0; i < containerData.propertyModCount; ++i)
			{
				const auto& propMod = containerData.propertyMods[i];
				if (propMod.op != BGSMod::Property::OP::kAdd) continue;
				if (propMod.type != BGSMod::Property::TYPE::kForm) continue;

				if (propMod.target == kWeaponTargetKeywords || propMod.target == kArmorTargetKeywords)
				{
					auto form = propMod.data.form;
					if (!form) continue;

					if (keyword::featuredItem && form->formID == keyword::featuredItem->formID)
					{
						equipmentData.isFeaturedItem = true;
					}
					else if (keyword::unscrappableObject && form->formID == keyword::unscrappableObject->formID)
					{
						equipmentData.isUnscrappable = true;
					}
				}
			}
		}

		return equipmentData;
	}

	// ---- Instance data ----

	TBO_InstanceData* GetInstanceData(const TESObjectREFR* ref)
	{
		if (!ref || !ref->extraList) return nullptr;

		auto instanceData = ref->extraList->GetByType<ExtraInstanceData>();
		if (!instanceData) return nullptr;

		return instanceData->data.get();
	}

	// ---- Keyword helpers ----

	bool HasKeyword(const TESForm* form, BGSKeyword* kw, TBO_InstanceData* data = nullptr)
	{
		if (!form || !kw) return false;

		auto keywordForm = form->As<BGSKeywordForm>();
		if (keywordForm && keywordForm->HasKeyword(kw, data))
		{
			return true;
		}

		auto keywordBase = form->As<IKeywordFormBase>();
		if (keywordBase && keywordBase->HasKeyword(kw, data))
		{
			return true;
		}

		return false;
	}

	bool HasKeyword(const TESForm* form, const std::vector<BGSKeyword*>& keywords, TBO_InstanceData* data = nullptr)
	{
		if (!form) return false;
		for (const auto& kw : keywords)
		{
			if (!kw) continue;
			if (HasKeyword(form, kw, data))
			{
				return true;
			}
		}
		return false;
	}

	// ---- Injection data matching ----

	bool MatchesAny(const TESForm* form, const injection_data::Key& key)
	{
		if (!form) return false;

		const auto& formIDs = injection_data::GetFormIDSet(key);
		if (formIDs.find(form->formID) != formIDs.end())
		{
			return true;
		}

		const auto& keywords = injection_data::GetKeywordListRef(key);
		if (keywords.empty())
		{
			return false;
		}

		for (auto* keyword : keywords)
		{
			if (HasKeyword(form, keyword))
			{
				return true;
			}
		}

		return false;
	}

	// ---- Item type classification ----

	ALCH GetALCHType(const TESForm* form)
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

	BOOK GetBOOKType(const TESForm* form)
	{
		if (MatchesAny(form, injection_data::book_type_perk_magazine)) return perkmagazine;
		return other_book;
	}

	MISC GetMISCType(const TESForm* form)
	{
		if (MatchesAny(form, injection_data::misc_type_bobblehead)) return bobblehead;
		return other_miscellaneous;
	}

	WEAP GetWEAPType(const TESForm* form)
	{
		if (MatchesAny(form, injection_data::weap_type_grenade)) return grenade;
		if (MatchesAny(form, injection_data::weap_type_mine)) return mine;
		return other_weapon;
	}

	// ---- Cell / owner / settlement helpers ----

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

	bool IsQuestItem(ExtraDataList* extraDataList)
	{
		return GetQuestAliasFlags(extraDataList).isQuestItem;
	}

	// ---- Precondition / validation ----

	bool CheckPrecondition(const TESObjectREFR* ref)
	{
		return (ref->formFlags & kFormFlagDeleted) == 0
		    && (ref->formFlags & kFormFlagDisabled) == 0
		    && (ref->formFlags & kFormFlagDestroyed) == 0;
	}

	bool IsValidObject(TESObjectREFR* ref)
	{
		if (!ref) return false;
		if (ref->IsPlayerRef()) return false;
		if (ref->IsWater()) return false;

		const bool hasExcludeKeyword = HasKeyword(
			ref,
			injection_data::GetKeywordListRef(injection_data::exclude_keyword),
			GetInstanceData(ref));
		if (hasExcludeKeyword)
		{
			return false;
		}

		auto* baseForm = ref->GetObjectReference();
		if (!baseForm) return false;

		if (!ref->extraList)
		{
			const auto name = ref->GetDisplayFullName();
			if (!name || strlen(name) == 0)
			{
				return false;
			}
			return true;
		}

		auto formType = baseForm->GetFormType();
		if (!IsFormTypeMatch(formType, ENUM_FORM_ID::kCONT) &&
		    !IsFormTypeMatch(formType, ENUM_FORM_ID::kNPC_))
		{
			if (IsQuestItem(ref->extraList.get()) &&
			    !MatchesAny(baseForm, injection_data::include_quest_item))
			{
				return false;
			}
		}

		// Check extra flags (activation block)
		auto extraFlags = ref->extraList->GetByType<ExtraFlagsCompat>();
		if (extraFlags)
		{
			auto rawFlags = extraFlags->flags;
			if ((rawFlags & kActivationBlocked) || ((rawFlags & kActivationIgnored) != 0))
			{
				if (!MatchesAny(baseForm, injection_data::include_activation_block))
				{
					return false;
				}
			}
		}

		// v2.2.0 reads owner only from ref's extraDataList, not inherited from cell/zone
		TESForm* refOwner = nullptr;
		if (ref->extraList)
		{
			auto extraOwn = ref->extraList->GetByType<ExtraOwnershipCompat>();
			if (extraOwn) refOwner = extraOwn->owner;
		}
		if (!IsOwnerEmptyOrFriend(refOwner))
		{
			return false;
		}

		// Check encounter zone
		// Note: v2.2.0 passed the zone form itself (not its owner) to IsOwnerEmptyOrFriend.
		// Since BGSEncounterZone is not a faction/NPC, the cast always fails and returns true.
		// We replicate that by only checking the settlement flag here.
		auto extraEZ = ref->extraList->GetByType<ExtraEncounterZoneCompat>();
		if (extraEZ && extraEZ->encounterZone)
		{
			if (properties::GetBool(properties::not_looting_from_settlement) && IsSettlement(extraEZ->encounterZone))
			{
				return false;
			}
		}

		const auto name = ref->GetDisplayFullName();
		if (!name || strlen(name) == 0)
		{
			return false;
		}

		return true;
	}

	// ---- Form validation ----

	bool IsAllowedUniqueItem(const TESForm* form)
	{
		if (!form || !form_list::IsUniqueItem(form->formID)) return true;
		return MatchesAny(form, injection_data::include_unique_item);
	}

	bool IsAllowedFeaturedItem(const TESForm* form)
	{
		if (!HasKeyword(form, keyword::featuredItem)) return true;
		return MatchesAny(form, injection_data::include_featured_item);
	}

	bool IsValidForm(TESForm* form)
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

		if (!IsAllowedUniqueItem(form) || !IsAllowedFeaturedItem(form))
		{
			auto formType = form->GetFormType();
			if (formType == ENUM_FORM_ID::kBOOK)
			{
				if (MatchesAny(form, injection_data::book_type_perk_magazine)
				    && (properties::GetInt(properties::lootable_book_item_type) & perkmagazine) == 0)
				{
					return false;
				}
			}
			else if (formType == ENUM_FORM_ID::kMISC)
			{
				if (MatchesAny(form, injection_data::misc_type_bobblehead)
				    && (properties::GetInt(properties::lootable_misc_item_type) & bobblehead) == 0)
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

	// ---- Inventory item helpers ----

	InventoryItemInfo GetInventoryItemInfo(const BGSInventoryItem& item,
		std::vector<BGSMod::Attachment::Mod*>& buffer)
	{
		InventoryItemInfo result{};

		for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get())
		{
			if (stack->flags.any(BGSInventoryItem::Stack::Flag::kTemporary))
			{
				result.dropped = true;
			}

			if (stack->flags.any(BGSInventoryItem::Stack::Flag::kSlotIndex1,
			                     BGSInventoryItem::Stack::Flag::kSlotIndex2,
			                     BGSInventoryItem::Stack::Flag::kSlotIndex3))
			{
				result.equipped = true;
			}

			if (!(result.featured && result.unscrappable && result.legendary))
			{
				auto data = GetEquipmentData(stack->extra.get(), &buffer);
				if (data.isFeaturedItem) result.featured = true;
				if (data.isUnscrappable) result.unscrappable = true;
				if (data.isLegendary) result.legendary = true;
			}

			if (!result.questItem && IsQuestItem(stack->extra.get()))
			{
				result.questItem = true;
			}
		}

		return result;
	}

	bool IsValidInventoryItem(const TESForm* form, const InventoryItemInfo& info)
	{
		if (info.dropped) return false;
		if (info.featured && !info.legendary &&
		    !MatchesAny(form, injection_data::include_featured_item)) return false;
		if (info.questItem && !MatchesAny(form, injection_data::include_quest_item)) return false;
		return true;
	}

	bool IsLootableInventoryItem(const TESForm* form, const InventoryItemInfo& info)
	{
		auto formType = form->GetFormType();
		if (formType == ENUM_FORM_ID::kWEAP || formType == ENUM_FORM_ID::kARMO)
		{
			if (properties::GetBool(properties::looting_legendary_only) && !info.legendary)
			{
				if (properties::GetBool(properties::always_looting_explosives))
				{
					auto type = GetWEAPType(form);
					return type == WEAP::grenade || type == WEAP::mine;
				}
				return false;
			}
		}
		return true;
	}

	// ---- Lootable form / object validation ----

	bool IsLootableForm(TESForm* form)
	{
		auto formType = form->GetFormType();
		if (formType == ENUM_FORM_ID::kACTI)
		{
			if (!MatchesAny(form, injection_data::include_activator))
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kALCH)
		{
			if ((properties::GetInt(properties::lootable_alch_item_type) & GetALCHType(form)) == 0)
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kBOOK)
		{
			if ((properties::GetInt(properties::lootable_book_item_type) & GetBOOKType(form)) == 0)
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
			if ((properties::GetInt(properties::lootable_misc_item_type) & GetMISCType(form)) == 0)
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kWEAP)
		{
			if ((properties::GetInt(properties::lootable_weap_item_type) & GetWEAPType(form)) == 0)
			{
				return false;
			}
		}

		return true;
	}

	bool HasLootableItem(BGSInventoryList* inventoryList)
	{
		if (!inventoryList) return false;

		bool result = false;
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		const auto lootableInventoryItemType = properties::GetInt(properties::lootable_inventory_item_type);
		ReadLockGuard guard(inventoryList->rwLock);

		for (auto& item : inventoryList->data)
		{
			auto form = item.object;
			if (!form) continue;

			if (!IsFormTypeMatchesItemType(form->GetFormType(), lootableInventoryItemType))
			{
				continue;
			}

			if (!IsValidForm(form) || !IsLootableForm(form))
			{
				continue;
			}

			auto info = GetInventoryItemInfo(item, modBuffer);
			if (!IsValidInventoryItem(form, info) || !IsLootableInventoryItem(form, info))
			{
				continue;
			}

			result = true;
			break;
		}

		return result;
	}

	bool IsLinkedToWorkshop(TESObjectREFR* ref)
	{
		if (!ref) return false;

		BGSKeyword* workshopKw = keyword::workshop;
		if (!workshopKw) return false;

		auto workshopRef = ref->GetLinkedRef(workshopKw);
		if (!workshopRef) return false;

		return workshopRef->extraList && workshopRef->extraList->HasType(EXTRA_DATA_TYPE::kWorkshop);
	}

	bool IsLootableObject(TESObjectREFR* ref)
	{
		if (!ref) return false;
		auto form = ref->GetObjectReference();
		if (!form) return false;
		auto formType = form->GetFormType();

		if (formType == ENUM_FORM_ID::kCONT)
		{
			if (IsLinkedToWorkshop(ref))
			{
				return false;
			}
			if (!HasLootableItem(ref->inventoryList))
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
			if (!HasLootableItem(ref->inventoryList))
			{
				return false;
			}
		}
		else if (formType == ENUM_FORM_ID::kWEAP || formType == ENUM_FORM_ID::kARMO)
		{
			std::vector<BGSMod::Attachment::Mod*> buffer;
			auto data = GetEquipmentData(ref->extraList.get(), &buffer);
			if (properties::GetBool(properties::looting_legendary_only) && !data.isLegendary)
			{
				if (properties::GetBool(properties::always_looting_explosives))
				{
					auto type = GetWEAPType(form);
					return type == WEAP::grenade || type == WEAP::mine;
				}
				return false;
			}
		}
		return true;
	}

	// ---- Object lock / release ----

	std::mutex objectsLock;
	std::unordered_set<std::uint32_t> lockedObjects;

	bool TryLockObject(std::uint32_t formId)
	{
		std::lock_guard<std::mutex> guard(objectsLock);
		return lockedObjects.insert(formId).second;
	}

	void UnlockObject(std::uint32_t formId)
	{
		std::lock_guard<std::mutex> guard(objectsLock);
		lockedObjects.erase(formId);
	}

	bool IsLockedObject(std::uint32_t formId)
	{
		std::lock_guard<std::mutex> guard(objectsLock);
		return lockedObjects.find(formId) != lockedObjects.end();
	}

	// ---- Cell collection ----

	void CollectFromCells(
		TESObjectREFR* actorRef,
		const NiPoint3& origin,
		float maxDistance,
		bool notLootingFromSettlement,
		std::uint32_t formType,
		std::vector<std::pair<TESObjectREFR*, float>>& buffer)
	{
		auto matchType = static_cast<ENUM_FORM_ID>(formType);

		auto collect = [&](TESObjectCELL* cell)
		{
			if (!cell) return;
			if (cell->cellState != TESObjectCELL::CELL_STATE::kAttached) return;

			// v2.2.0 GetCellOwner: check extraDataList first, fall back to encounter zone owner
			TESForm* cellOwner = nullptr;
			if (cell->extraList)
			{
				auto extraOwn = cell->extraList->GetByType<ExtraOwnershipCompat>();
				if (extraOwn) cellOwner = extraOwn->owner;
			}
			if (!cellOwner)
			{
				auto cellEZ = cell->GetEncounterZone();
				if (cellEZ) cellOwner = cellEZ->data.zoneOwner;
			}
			if (!IsOwnerEmptyOrFriend(cellOwner))
			{
				return;
			}

			if (notLootingFromSettlement)
			{
				auto cellEZ = cell->GetEncounterZone();
				if (IsSettlement(cellEZ))
				{
					return;
				}
			}

			BSAutoLock guard(cell->spinLock);
			for (auto& objPtr : cell->references)
			{
				auto obj = objPtr.get();
				if (!obj) continue;
				auto* baseObj = obj->GetObjectReference();
				if (!baseObj) continue;
				if (!IsFormTypeMatch(baseObj->GetFormType(), matchType)) continue;
				if (!CheckPrecondition(obj)) continue;

				auto pos = obj->GetPosition();
				float distance = GetDistance(origin, pos);
				if (distance > 0 && distance <= maxDistance)
				{
					buffer.emplace_back(obj, distance);
				}
			}
		};

		auto parentCell = actorRef->GetParentCell();
		if (parentCell && parentCell->IsInterior())
		{
			collect(parentCell);
		}
		else
		{
			// v2.2.0 walks worldSpace->cells ascending through parentWorld
			auto worldSpace = parentCell ? parentCell->worldSpace : nullptr;
			if (worldSpace)
			{
				std::vector<std::uint32_t> visitedIds;
				while (worldSpace)
				{
					if (std::find(visitedIds.begin(), visitedIds.end(), worldSpace->formID) == visitedIds.end())
					{
						visitedIds.push_back(worldSpace->formID);
						for (auto& [key, cell] : worldSpace->cellMap)
						{
							if (cell) collect(cell);
						}
					}
					worldSpace = worldSpace->parentWorld;
				}
			}
			else if (parentCell)
			{
				collect(parentCell);
			}
		}
	}

	// ================================================================
	// Papyrus native functions (10 v2.2.0 functions)
	// ================================================================

	// 1. FindNearbyReferencesWithFormType
	std::vector<TESObjectREFR*> FindNearbyReferencesWithFormType(
		std::monostate, TESObjectREFR* ref, std::uint32_t formType)
	{
		std::vector<TESObjectREFR*> result;

		auto ui = UI::GetSingleton();
		if (ui && ui->pauseMenuDisableCt.load_unchecked() > 0)
		{
			return result;
		}

		if (!ref)
		{
			return result;
		}

		auto origin = ref->GetPosition();
		auto maxDistance = properties::GetFloat(properties::looting_range) * 100.0f;
		auto notLootingFromSettlement = properties::GetBool(properties::not_looting_from_settlement);
		auto maxItemsProcessedPerThread = static_cast<std::size_t>(
			std::max(1, properties::GetInt(properties::max_items_processed_per_thread)));

		if (maxDistance < 1.0f)
		{
			return result;
		}

		std::vector<std::pair<TESObjectREFR*, float>> buffer;
		buffer.reserve(1024);

		CollectFromCells(ref, origin, maxDistance, notLootingFromSettlement, formType, buffer);

		std::sort(buffer.begin(), buffer.end(),
			[](const std::pair<TESObjectREFR*, float>& lhs, const std::pair<TESObjectREFR*, float>& rhs)
			{
				return lhs.second < rhs.second;
			});

		std::vector<TESObjectREFR*> tmp;
		tmp.reserve(maxItemsProcessedPerThread);
		for (const auto& entry : buffer)
		{
			auto* obj = entry.first;
			if (!obj) continue;

			auto formId = obj->formID;

			if (IsLockedObject(formId))
			{
				continue;
			}

			auto* baseObj = obj->GetObjectReference();
			if (!baseObj) continue;

			if (!IsValidForm(baseObj) || !IsValidObject(obj))
			{
				continue;
			}

			if (!IsLootableForm(baseObj) || !IsLootableObject(obj))
			{
				continue;
			}

			if (TryLockObject(formId))
			{
				tmp.emplace_back(obj);
				if (tmp.size() >= maxItemsProcessedPerThread)
				{
					break;
				}
			}
		}

		// Reverse for Papyrus loop ordering (closest items processed last = first in Papyrus reverse loop)
		std::reverse(tmp.begin(), tmp.end());
		result = std::move(tmp);
		return result;
	}

	// 2. GetEquipmentComponents
	// Returns an array of MiscComponent structs (Papyrus struct with "object" and "count" fields).
	// Note: structure_wrapper is required for BindNativeMethod type validation.
	// The debug assertion documented in AGENTS.md does not affect release builds.
	using MiscComponent = RE::BSScript::structure_wrapper<"MiscObject", "MiscComponent">;

	ExtraDataList* FindExtraDataForInventoryItem(
		TESObjectREFR* container, std::uint16_t uniqueID, TESForm*& outBaseForm)
	{
		outBaseForm = nullptr;
		if (!container || uniqueID == 0) return nullptr;

		auto inventoryList = container->inventoryList;
		if (!inventoryList) return nullptr;

		ReadLockGuard guard(inventoryList->rwLock);
		for (auto& item : inventoryList->data)
		{
			if (!item.object) continue;
			for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get())
			{
				if (!stack->extra) continue;
				auto extraUID = stack->extra->GetByType<ExtraUniqueID>();
				if (extraUID && extraUID->uniqueID == uniqueID)
				{
					outBaseForm = item.object;
					return stack->extra.get();
				}
			}
		}

		return nullptr;
	}

	std::vector<MiscComponent> GetEquipmentComponents(
		std::monostate, GameScript::RefrOrInventoryObj inventoryItem)
	{
		std::vector<MiscComponent> result;

		TESForm* baseForm = nullptr;
		ExtraDataList* extraDataList = nullptr;

		if (inventoryItem.Reference())
		{
			auto ref = inventoryItem.Reference();
			baseForm = ref->GetObjectReference();
			extraDataList = ref->extraList.get();
		}
		else if (inventoryItem.Container() && inventoryItem.UniqueID())
		{
			extraDataList = FindExtraDataForInventoryItem(
				inventoryItem.Container(), inventoryItem.UniqueID(), baseForm);
		}

		if (!baseForm || !extraDataList)
		{
			return std::move(result);
		}

		auto formType = baseForm->GetFormType();
		if (formType != ENUM_FORM_ID::kARMO && formType != ENUM_FORM_ID::kWEAP)
		{
			return std::move(result);
		}

		std::unordered_map<BGSComponent*, std::uint32_t> data;

		auto extractComponents = [&data](const BGSConstructibleObject* obj)
		{
			if (!obj || !obj->requiredItems) return;

			for (auto& [compForm, compVal] : *obj->requiredItems)
			{
				if (!compForm) continue;
				auto count = compVal.i;
				if (count == 0) continue;

				if (compForm->Is(ENUM_FORM_ID::kMISC))
				{
					auto miscObj = compForm->As<TESObjectMISC>();
					if (miscObj && miscObj->componentData)
					{
						for (auto& [miscCompForm, miscCompVal] : *miscObj->componentData)
						{
							auto miscComp = miscCompForm ? miscCompForm->As<BGSComponent>() : nullptr;
							if (!miscComp) continue;
							auto miscCount = miscCompVal.i;
							if (miscCount == 0) continue;
							auto scalar = miscComp->modScrapScalar;
							auto scale = !scalar ? 1.0f : scalar->value;
							if (scale <= 0.0f) continue;
							data[miscComp] += static_cast<std::uint32_t>(miscCount * count);
						}
					}
					continue;
				}

				auto comp = compForm->As<BGSComponent>();
				if (!comp) continue;
				auto scalar = comp->modScrapScalar;
				auto scale = !scalar ? 1.0f : scalar->value;
				if (scale <= 0.0f) continue;
				data[comp] += static_cast<std::uint32_t>(count);
			}
		};

		// Base item components
		extractComponents(constructible_object::FromCreatedObjectId(baseForm->formID));

		// Mod components
		std::vector<BGSMod::Attachment::Mod*> mods;
		GetMods(extraDataList, &mods);
		for (const auto& objectMod : mods)
		{
			extractComponents(constructible_object::FromCreatedObjectId(objectMod->formID));
		}

		for (const auto& [comp, count] : data)
		{
			MiscComponent component;
			component.insert("object"sv, comp);
			component.insert("count"sv, static_cast<std::int32_t>(count / 2));
			result.push_back(std::move(component));
		}

		return std::move(result);
	}

	// 3. GetFormType
	std::uint32_t GetFormType(std::monostate, TESForm* form)
	{
		return !form ? static_cast<std::uint32_t>(ENUM_FORM_ID::kNONE) : static_cast<std::uint32_t>(form->GetFormType());
	}

	// 4. GetInventoryItemsWithItemType
	std::vector<TESForm*> GetInventoryItemsWithItemType(
		std::monostate, TESObjectREFR* inventoryOwner, std::uint32_t itemType)
	{
		std::vector<TESForm*> result;

		if (!inventoryOwner || itemType > all_item) return result;

		bool isPlayer = inventoryOwner->IsPlayerRef();
		bool isDead = IsDeadForLooting(inventoryOwner);

		auto inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList) return result;

		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		ReadLockGuard guard(inventoryList->rwLock);
		result.reserve(inventoryList->data.size());

		for (auto& item : inventoryList->data)
		{
			auto form = item.object;
			if (!form) continue;

			if (!IsPlayable(form)) continue;
			if (!IsFormTypeMatchesItemType(form->GetFormType(), itemType)) continue;

			if (isPlayer)
			{
				if (form->formID == 0x0F) continue;
				if (IsFavorite(form)) continue;
			}

			auto info = GetInventoryItemInfo(item, modBuffer);
			if ((info.questItem && !MatchesAny(form, injection_data::include_quest_item))
			    || info.dropped || (info.equipped && !isDead))
			{
				continue;
			}

			result.push_back(form);
		}

		return result;
	}

	// 5. GetLootableItems
	std::vector<TESForm*> GetLootableItems(
		std::monostate, TESObjectREFR* inventoryOwner, std::uint32_t itemType)
	{
		std::vector<TESForm*> result;

		if (!inventoryOwner || itemType > all_item) return result;

		auto inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList) return result;

		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		ReadLockGuard guard(inventoryList->rwLock);

		for (auto& item : inventoryList->data)
		{
			auto form = item.object;
			if (!form) continue;

			if (!IsFormTypeMatchesItemType(form->GetFormType(), itemType)) continue;
			if (!IsValidForm(form) || !IsLootableForm(form)) continue;

			auto info = GetInventoryItemInfo(item, modBuffer);
			if (!IsValidInventoryItem(form, info) || !IsLootableInventoryItem(form, info)) continue;

			result.push_back(form);
		}

		return result;
	}

	// 6. GetScrappableItems
	std::vector<TESForm*> GetScrappableItems(
		std::monostate, TESObjectREFR* inventoryOwner, std::uint32_t itemType)
	{
		std::vector<TESForm*> result;

		if (!inventoryOwner || itemType > all_item) return result;

		bool isPlayer = inventoryOwner->IsPlayerRef();
		bool isDead = IsDeadForLooting(inventoryOwner);

		auto inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList) return result;

		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		ReadLockGuard guard(inventoryList->rwLock);

		for (auto& item : inventoryList->data)
		{
			auto form = item.object;
			if (!form) continue;

			if (!IsPlayable(form)) continue;
			if (!IsFormTypeMatchesItemType(form->GetFormType(), itemType)) continue;

			if (HasKeyword(form, keyword::unscrappableObject)) continue;
			if (HasKeyword(form, keyword::featuredItem)) continue;

			if (isPlayer)
			{
				if (form->formID == 0x0F) continue;
				if (IsFavorite(form)) continue;
			}

			auto info = GetInventoryItemInfo(item, modBuffer);

			if (info.equipped && !isDead) continue;
			if (info.featured || info.unscrappable || info.questItem) continue;

			result.push_back(form);
		}

		return result;
	}

	// 7. IsFormTypeEquals
	bool IsFormTypeEquals(std::monostate, TESForm* form, std::uint32_t formType)
	{
		return form && static_cast<std::uint32_t>(form->GetFormType()) == formType;
	}

	// 8. PlayPickUpSound
	void PlayPickUpSound(std::monostate, TESObjectREFR* player, TESObjectREFR* obj)
	{
		auto actor = player ? player->As<Actor>() : nullptr;
		if (!actor) return;

		auto boundObject = obj ? obj->GetObjectReference() : nullptr;
		if (!boundObject) return;

		actor->PlayPickUpSound(boundObject, true, false);
	}

	// 9. OnUpdateLootManProperty
	void OnUpdateLootManProperty(std::monostate, BSFixedString propertyName)
	{
		properties::Update(propertyName.c_str());
	}

	// 10. ReleaseObject
	void ReleaseObject(std::monostate, std::uint32_t objId)
	{
		UnlockObject(objId);
	}

	// ================================================================
	// Registration and lifecycle
	// ================================================================

	bool Register(RE::BSScript::IVirtualMachine* vm)
	{
		REX::DEBUG("[ Started binding papyrus functions for LootMan ]");

		vm->BindNativeMethod("LTMN2:LootMan"sv, "FindNearbyReferencesWithFormType"sv,
			&FindNearbyReferencesWithFormType, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetEquipmentComponents"sv,
			&GetEquipmentComponents, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetFormType"sv,
			&GetFormType, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetInventoryItemsWithItemType"sv,
			&GetInventoryItemsWithItemType, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetLootableItems"sv,
			&GetLootableItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetScrappableItems"sv,
			&GetScrappableItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "IsFormTypeEquals"sv,
			&IsFormTypeEquals, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "PlayPickUpSound"sv,
			&PlayPickUpSound, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "OnUpdateLootManProperty"sv,
			&OnUpdateLootManProperty, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "ReleaseObject"sv,
			&ReleaseObject, false, false);

		REX::DEBUG("  Papyrus functions binding is complete");
		return true;
	}

	void OnPreLoadGame()
	{
		std::lock_guard<std::mutex> guard(objectsLock);
		lockedObjects.clear();
	}
}
