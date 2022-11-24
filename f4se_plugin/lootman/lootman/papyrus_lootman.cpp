#include "papyrus_lootman.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "f4se/PapyrusNativeFunctions.h"
#include "f4se/GameData.h"
#include "f4se/GameExtraData.h"
#include "f4se/GameMenus.h"
#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"

#include "fallout4.hpp"
#include "fallout4_extra.hpp"
#include "form_cache.hpp"
#include "injection_data.hpp"
#include "properties.hpp"
#include "vendor_chest.hpp"
#include "virtual_function.hpp"

#ifdef _DEBUG
#include "debug.hpp"
#endif

namespace papyrus_lootman
{
#ifdef _DEBUG
    SimpleLock logLock;
    inline void _MESSAGE(const char * fmt, ...)
    {
        {
            SimpleLocker locker(&logLock);
            va_list args;

            va_start(args, fmt);
            gLog.Log(IDebugLog::kLevel_Message, fmt, args);
            va_end(args);
        }
    }

    std::unordered_map<UInt32, std::vector<std::string>> knownTitle;
    void TraceOnce(TESObjectREFR* ref, const char* title)
    {
        if (knownTitle.find(ref->formID) == knownTitle.end())
        {
            std::vector<std::string> list;
            knownTitle.emplace(ref->formID, list);
        }

        const auto key = std::string(title);
        auto titles = knownTitle[ref->formID];
        if (std::find(titles.begin(), titles.end(), key) == titles.end())
        {
            titles.emplace_back(key);
            _MESSAGE("| ---------- | [ %s ]", title);
            _MESSAGE("| ---------- |   %s", debug::Ref2S(ref));
        }
    }

    void TraceOnce(TESForm* form, const char* title)
    {
        if (knownTitle.find(form->formID) == knownTitle.end())
        {
            std::vector<std::string> list;
            knownTitle.emplace(form->formID, list);
        }

        const auto key = std::string(title);
        auto titles = knownTitle[form->formID];
        if (std::find(titles.begin(), titles.end(), key) == titles.end())
        {
            titles.emplace_back(key);
            _MESSAGE("| ---------- | [ %s ]", title);
            _MESSAGE("| ---------- |   %s", debug::Form2S(form));
        }
    }
#endif

    // ReSharper disable once CppInconsistentNaming
    DECLARE_STRUCT(MiscComponent, "MiscObject")

    using namespace form_cache;

    enum Generic : UInt32
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

    enum ALCH : UInt32
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

    enum BOOK : UInt32
    {
        perkmagazine = 1 << 0,
        other_book = 1 << 1,
    };

    enum MISC : UInt32
    {
        bobblehead = 1 << 0,
        other_miscellaneous = 1 << 1,
    };

    enum WEAP : UInt32
    {
        grenade = 1 << 0,
        mine = 1 << 1,
        other_weapon = 1 << 2,
    };

    struct EquipmentData
    {
        bool isLegendary;
        bool isFeaturedItem;
        bool isUnscrappable;

        EquipmentData()
            : isLegendary(false),
              isFeaturedItem(false),
              isUnscrappable(false)
        {
        }
    };

    struct QuestAliasInfo
    {
        enum FLAG : UInt16
        {
            enabled = 1 << 0,
            completed = 1 << 1,
            failed = 1 << 6,
            active = 1 << 11,
        };

        UInt32 questId;
        UInt16 flags;
        bool isEssential;
        bool isQuestItem;
    };

    inline float GetMagnitude(const NiPoint3 pos)
    {
        return std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
    }

    inline float GetDistance(const NiPoint3 pos1, const NiPoint3 pos2)
    {
        return GetMagnitude(pos1 - pos2);
    }

    inline float GetDistance(const TESObjectREFR* ref1, const TESObjectREFR* ref2)
    {
        return GetDistance(ref1->pos, ref2->pos);
    }

    // Verify that the form type matches the specified form type. "kFormType_Max" can be used as a wildcard.
    inline bool IsFormTypeMatch(const UInt8 formType, const UInt32 matchingType)
    {
        return (formType == matchingType)
        || (matchingType == kFormType_Max
            && (   formType == kFormType_ACTI || formType == kFormType_ALCH || formType == kFormType_AMMO
                || formType == kFormType_ARMO || formType == kFormType_BOOK || formType == kFormType_CONT
                || formType == kFormType_FLOR || formType == kFormType_INGR || formType == kFormType_KEYM
                || formType == kFormType_MISC || formType == kFormType_NPC_ || formType == kFormType_WEAP));
    }

    // Verify that the item type matches the specified item type.
    inline bool IsItemTypeMatch(const UInt32 itemType, const UInt32 matchingType)
    {
        return (itemType & matchingType) == matchingType;
    }

    // Verify that the form type matches the item type.
    inline bool IsFormTypeMatchesItemType(const UInt8 formType, const UInt32 itemType)
    {
        return (formType == kFormType_ALCH && IsItemTypeMatch(itemType, alch))
        ||     (formType == kFormType_AMMO && IsItemTypeMatch(itemType, ammo))
        ||     (formType == kFormType_ARMO && IsItemTypeMatch(itemType, armo))
        ||     (formType == kFormType_BOOK && IsItemTypeMatch(itemType, book))
        ||     (formType == kFormType_INGR && IsItemTypeMatch(itemType, ingr))
        ||     (formType == kFormType_KEYM && IsItemTypeMatch(itemType, keym))
        ||     (formType == kFormType_MISC && IsItemTypeMatch(itemType, misc))
        ||     (formType == kFormType_WEAP && IsItemTypeMatch(itemType, weap));
    }

    // Verify that the mod is legendary.
    inline bool IsLegendaryMod(const BGSMod::Attachment::Mod* mod)
    {
        return (mod->flags >> 4) & 1;
    }

    // Verify that the form is playable
    inline bool IsPlayable(const TESForm* form)
    {
        return (form->flags & 1 << 2) == 0;
    }

    inline bool IsFavorite(const TESForm* form)
    {
        for (int i = 0; i < FavoritesManager::kNumFavorites; ++i)
        {
            TESForm* favorite = (*g_favoritesManager)->favorites[i];
            if (favorite && favorite->formID == form->formID)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<QuestAliasInfo> GetAllQuestAliasInfo(ExtraDataList* extraDataList)
    {
        std::vector<QuestAliasInfo> result;

        if (!extraDataList)
        {
            return result;
        }

        const auto extraData = extraDataList->GetByType(kExtraData_AliasInstanceArray);
        if (!extraData)
        {
            return result;
        }

        const auto aliasArray = DYNAMIC_CAST(extraData, BSExtraData, ExtraAliasInstanceArray);
        if (aliasArray)
        {
            aliasArray->aliasesLock.LockForRead();

            for (UInt32 i = 0; i < aliasArray->aliases.count; ++i)
            {
                ExtraAliasInstanceArray::ALIAS_DATA data = {};
                if (!aliasArray->aliases.GetNthItem(i, data))
                {
                    continue;
                }

                QuestAliasInfo info = {};
                info.questId = data.quest->formID;
                info.flags = data.quest->flags;
                info.isEssential = (data.alias->flags >> 6) & 1;
                info.isQuestItem = (data.alias->flags >> 2) & 1;
                result.push_back(info);
            }

            aliasArray->aliasesLock.Unlock();
        }

        return result;
    }

    bool IsEssential(const TESObjectREFR* ref)
    {
        const auto npc = DYNAMIC_CAST(ref->baseForm, TESForm, TESNPC);
        if (npc && (npc->actorData.flags & TESActorBaseData::kFlagEssential) != 0)
        {
            return true;
        }

        const auto list = GetAllQuestAliasInfo(ref->extraDataList);
        const auto it = std::find_if(list.begin(), list.end(), [](const QuestAliasInfo& info)
        {
            //TODO: Is "enabled" ok?
            if ((info.flags & QuestAliasInfo::enabled) == 0) return false;
            return info.isEssential;
        });
        return it != list.end();
    }

    bool GetMods(ExtraDataList* extraDataList, std::vector<BGSMod::Attachment::Mod*>* list)
    {
        if (!extraDataList)
        {
            return false;
        }

        BSExtraData* extraData = extraDataList->GetByType(kExtraData_ObjectInstance);
        if (!extraData)
        {
            return false;
        }

        BGSObjectInstanceExtra* objectModData = DYNAMIC_CAST(extraData, BSExtraData, BGSObjectInstanceExtra);
        if (!objectModData)
        {
            return false;
        }

        BGSObjectInstanceExtra::Data* data = objectModData->data;
        if (!data || !data->forms)
        {
            return false;
        }

        bool found = false;

        list->clear();
        for (UInt32 i = 0; i < (data->blockSize / sizeof(BGSObjectInstanceExtra::Data::Form)); ++i)
        {
            auto objectMod = static_cast<BGSMod::Attachment::Mod*>(Runtime_DynamicCast(LookupFormByID(data->forms[i].formId), RTTI_TESForm, RTTI_BGSMod__Attachment__Mod));
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
        if (!GetMods(extraDataList, buffer))
        {
            const EquipmentData empty;
            return empty;
        }

        EquipmentData equipmentData;
        for (const auto& mod : *buffer)
        {
            if (IsLegendaryMod(mod))
            {
                equipmentData.isLegendary = true;
            }

            for (UInt32 i = 0; i < (mod->modContainer.dataSize / sizeof(BGSMod::Container::Data)); ++i)
            {
                const auto data = mod->modContainer.data[i];
                if (data.op != BGSMod::Container::Data::kOpFlag_Add_Form)
                {
                    continue;
                }
                if (data.target == BGSMod::Container::WeaponFormProperty::kWeaponTarget_pkKeywords
                    || data.target == BGSMod::Container::ArmorFormProperty::kArmorTarget_pkKeywords)
                {
                    const auto form = data.value.form;
                    if (!form)
                    {
                        continue;
                    }

                    if (form->formID == keyword::featuredItem->formID)
                    {
                        equipmentData.isFeaturedItem = true;
                    }
                    else if (form->formID == keyword::unscrappableObject->formID)
                    {
                        equipmentData.isUnscrappable = true;
                    }
                }
            }
        }

        return equipmentData;
    }

    TBO_InstanceData* GetInstanceData(const TESObjectREFR* ref)
    {
        if (!ref || !ref->extraDataList)
        {
            return nullptr;
        }

        const auto extraData = ref->extraDataList->GetByType(kExtraData_InstanceData);
        if (!extraData)
        {
            return nullptr;
        }

        const auto instanceData = DYNAMIC_CAST(extraData, BSExtraData, ExtraInstanceData);
        if (!instanceData)
        {
            return nullptr;
        }

        return instanceData->instanceData;
    }

    ExtraFlags* GetExtraFlags(ExtraDataList* extraDataList)
    {
        if (!extraDataList)
        {
            return nullptr;
        }

        const auto extraData = extraDataList->GetByType(kExtraData_Flags);
        if (!extraData)
        {
            return nullptr;
        }

        return DYNAMIC_CAST(extraData, BSExtraData, ExtraFlags);
    }

    TESForm* GetOwner(ExtraDataList* extraDataList)
    {
        if (!extraDataList)
        {
            return nullptr;
        }

        const auto extraOwnership = extraDataList->GetByType(kExtraData_Ownership);
        if (!extraOwnership)
        {
            return nullptr;
        }

        const auto ownership = DYNAMIC_CAST(extraOwnership, BSExtraData, ExtraOwnership);
        if (!ownership)
        {
            return nullptr;
        }

        return ownership->owner;
    }

    BGSEncounterZoneAlt* GetEncounterZone(ExtraDataList* extraDataList)
    {
        if (!extraDataList)
        {
            return nullptr;
        }

        const auto extraEncounterZone = extraDataList->GetByType(kExtraData_EncounterZone);
        if (!extraEncounterZone)
        {
            return nullptr;
        }

        const auto encounterZone = DYNAMIC_CAST(extraEncounterZone, BSExtraData, ExtraEncounterZone);
        if (!encounterZone)
        {
            return nullptr;
        }

        return encounterZone->encounterZone;
    }

    bool HasKeyword(const TESForm* form, BGSKeyword* keyword, TBO_InstanceData* data = nullptr)
    {
        const auto keywordForm = DYNAMIC_CAST(form, TESForm, BGSKeywordForm);
        if (keywordForm && virtual_function::HasKeyword(&keywordForm->keywordBase, keyword, data))
        {
            return true;
        }

        const auto keywordFormBase = DYNAMIC_CAST(form, TESForm, IKeywordFormBase);
        if (keywordFormBase)
        {
            return virtual_function::HasKeyword(keywordFormBase, keyword, data);
        }

        return false;
    }

    bool HasKeyword(const TESForm* form, const std::vector<BGSKeyword*>& keywords, TBO_InstanceData* data = nullptr)
    {
        const auto keywordForm = DYNAMIC_CAST(form, TESForm, BGSKeywordForm);
        if (keywordForm)
        {
            for (const auto& keyword : keywords)
            {
                if (virtual_function::HasKeyword(&keywordForm->keywordBase, keyword, data))
                {
                    return true;
                }
            }
        }

        const auto keywordFormBase = DYNAMIC_CAST(form, TESForm, IKeywordFormBase);
        if (keywordFormBase)
        {
            for (const auto& keyword : keywords)
            {
                if (virtual_function::HasKeyword(keywordFormBase, keyword, data))
                {
                    return true;
                }
            }
        }

        return false;
    }

    inline bool IsMarked(const TESObjectREFR* ref)
    {
        return HasKeyword(ref, keyword::lootingMarker, GetInstanceData(ref));
    }

    bool MatchesAny(const TESForm* form, const injection_data::Key& key)
    {
        bool match = false;
        for (auto value : GetList(key))
        {
            if (value.IsForm())
            {
                match = form->formID == value.data.form->formID;
            }
            else if (value.IsKeyword())
            {
                match = HasKeyword(form, value.data.keyword);
            }

            if (match)
            {
                return true;
            }
        }

        return false;
    }

    ALCH GetALCHType(const TESForm* form)
    {
        if (MatchesAny(form, injection_data::alch_type_alcohol))
        {
            return alcohol;
        }
        if (MatchesAny(form, injection_data::alch_type_chemistry))
        {
            return chemistry;
        }
        if (MatchesAny(form, injection_data::alch_type_food))
        {
            return food;
        }
        if (MatchesAny(form, injection_data::alch_type_nuka_cola))
        {
            return nuka_cola;
        }
        if (MatchesAny(form, injection_data::alch_type_stimpak))
        {
            return stimpak;
        }
        if (MatchesAny(form, injection_data::alch_type_syringe_ammo))
        {
            return syringe_ammo;
        }
        if (MatchesAny(form, injection_data::alch_type_water))
        {
            return water;
        }
        return other_alchemy;
    }

    BOOK GetBOOKType(const TESForm* form)
    {
        if (MatchesAny(form, injection_data::book_type_perk_magazine))
        {
            return perkmagazine;
        }
        return other_book;
    }

    MISC GetMISCType(const TESForm* form)
    {
        if (MatchesAny(form, injection_data::misc_type_bobblehead))
        {
            return bobblehead;
        }
        return other_miscellaneous;
    }

    WEAP GetWEAPType(const TESForm* form)
    {
        if (MatchesAny(form, injection_data::weap_type_grenade))
        {
            return grenade;
        }
        if (MatchesAny(form, injection_data::weap_type_mine))
        {
            return mine;
        }
        return other_weapon;
    }

    BGSEncounterZoneAlt* GetCellEncounterZone(const TESObjectCELLAlt* cell)
    {
        BGSEncounterZoneAlt* encounterZone;
        if (cell->loadedData)
        {
            encounterZone = cell->loadedData->encounterZone;
        }
        else
        {
            encounterZone = GetEncounterZone(cell->extraDataList);
            if (!encounterZone && (cell->cellFlags & 1) == 0)
            {
                encounterZone = cell->worldSpace ? cell->worldSpace->encounterZone : nullptr;
            }
        }
        return encounterZone;
    }

    TESForm* GetCellOwner(const TESObjectCELLAlt* cell)
    {
        const auto form = GetOwner(cell->extraDataList);
        if (form)
        {
            return form;
        }

        const BGSEncounterZoneAlt* encounterZone = GetCellEncounterZone(cell);
        return encounterZone ? encounterZone->data.zoneOwner : nullptr;
    }

    bool IsQuestItem(ExtraDataList* extraDataList)
    {
        const auto list = GetAllQuestAliasInfo(extraDataList);
        const auto it = std::find_if(list.begin(), list.end(), [](const QuestAliasInfo& info) {
            if (info.isQuestItem) return true;
            if ((info.flags & QuestAliasInfo::enabled) == 0) return false;
            return (info.flags & (QuestAliasInfo::completed | QuestAliasInfo::failed)) == 0;
        });
        return it != list.end();
    }

    SimpleLock objectsLock;
    std::unordered_set<UInt32> lockedObjects;

    inline bool TryLockObject(const UInt32 formId)
    {
        bool success = false;
        {
            SimpleLocker locker(&objectsLock);
            if (lockedObjects.find(formId) == lockedObjects.end())
            {
                lockedObjects.emplace(formId);
                success = true;
            }
        }
        return success;
    }

    inline void ReleaseObject(const UInt32 formId)
    {
        {
            SimpleLocker locker(&objectsLock);
            lockedObjects.erase(formId);
        }
    }

    inline bool IsLockedObject(const UInt32 formId)
    {
        bool result;
        {
            SimpleLocker locker(&objectsLock);
            result = lockedObjects.find(formId) != lockedObjects.end();
        }
        return result;
    }

    bool IsFriendFaction(TESFaction* faction)
    {
        if (faction->factionData.flags & TESFaction::FACTION_DATA::can_be_owner)
        {
            struct IsFriend
            {
                bool Accept(const TESReactionForm::GROUP_REACTION* reaction)
                {
                    if (!reaction || !reaction->form)
                    {
                        return false;
                    }
                    if (reaction->form->formID == faction::playerFaction->formID)
                    {
                        return reaction->fightReaction >= TESReactionForm::friend_;
                    }
                    return false;
                }
            };
            return faction->reactionForm.reactionList.CountIf(IsFriend()) > 0;
        }
        return false;
    }

    bool IsOwnerEmptyOrFriend(TESForm* owner)
    {
        if (!owner)
        {
            return true;
        }

        const auto factionOwner = DYNAMIC_CAST(owner, TESForm, TESFaction);
        if (factionOwner)
        {
            if (factionOwner->formID == faction::playerFaction->formID)
            {
                return true;
            }
            if (!IsFriendFaction(factionOwner))
            {
#ifdef _DEBUG
                _MESSAGE("| ---------- | faction owner is not friend");
#endif
                return false;
            }
            return true;
        }

        const auto actorOwner = DYNAMIC_CAST(owner, TESForm, Actor);
        if (actorOwner)
        {
#ifdef _DEBUG
            _MESSAGE("| ---------- | have actor owner");
#endif
            //TODO: Check to see if they ever pass through here.
        }

        const auto npcOwner = DYNAMIC_CAST(owner, TESForm, TESNPC);
        if (npcOwner)
        {
            if (npcOwner->formID == 0x07)
            {
                return true;
            }

            const auto actorData = reinterpret_cast<TESActorBaseDataAlt*>(&npcOwner->actorData);
            if (actorData)
            {
                bool hasFriendFaction = false;
                for (UInt32 i = 0; i < actorData->factions.count; ++i)
                {
                    TESActorBaseDataAlt::FACTION_DATA data = {};
                    actorData->factions.GetNthItem(i, data);
                    if (IsFriendFaction(data.faction))
                    {
                        hasFriendFaction = true;
                        break;
                    }
                }
                if (!hasFriendFaction)
                {
#ifdef _DEBUG
                    _MESSAGE("| ---------- | npc owner is not friend");
#endif
                    return false;
                }
            }
        }

        return true;
    }

    bool IsSettlement(const BGSEncounterZoneAlt* zone)
    {
        if (!zone)
        {
            return false;
        }

        if (zone->data.location)
        {
            const auto keywordBase = &zone->data.location->keywordForm.keywordBase;
            if (virtual_function::HasKeyword(keywordBase, keyword::settlement) ||
                virtual_function::HasKeyword(keywordBase, keyword::workshopSettlement))
            {
                return true;
            }
        }

        if (zone->data.flags & BGSEncounterZoneAlt::DATA::workshop_zone)
        {
#ifdef _DEBUG
            _MESSAGE("location is in workshop zone");
#endif
            return true;
        }

        return false;
    }

    inline bool CheckPrecondition(const TESObjectREFR* ref)
    {
        return (ref->flags & TESForm::kFlag_IsDeleted) == 0
        &&     (ref->flags & TESForm::kFlag_IsDisabled) == 0
        &&     (ref->flags & (1 << 23)) == 0; // 1<< 23 is destroyed
    }

    bool IsValidObject(TESObjectREFR* ref)
    {
        if (ref->formID == 0x14)
        {
            return false;
        }

        if (virtual_function::IsWater(ref))
        {
            return false;
        }

        const auto name = CALL_MEMBER_FN(ref, GetReferenceName)();
        if (!name || strlen(name) == 0)
        {
            return false;
        }

        if (HasKeyword(ref, GetAsKeywordList(injection_data::exclude_keyword), GetInstanceData(ref)))
        {
#ifdef _DEBUG
            TraceOnce(ref, "object has exclude keyword");
#endif
            return false;
        }

        if (!ref->extraDataList)
        {
            return true;
        }

        const auto form = ref->baseForm;
        if (!IsFormTypeMatch(form->formType, kFormType_CONT)
            && !IsFormTypeMatch(form->formType, kFormType_NPC_))
        {
            if (IsQuestItem(ref->extraDataList))
            {
#ifdef _DEBUG
                TraceOnce(ref, "object is quest item");
#endif
                return false;
            }
        }

        const auto extraFlags = GetExtraFlags(ref->extraDataList);
        if (extraFlags)
        {
            if ((extraFlags->flags & 1) || (extraFlags->flags >> 1) & 1)
            {
                if (!MatchesAny(form, injection_data::include_activation_block))
                {
#ifdef _DEBUG
                    TraceOnce(ref, "object activation blocked");
#endif
                    return false;
                }
            }
        }

        if (!IsOwnerEmptyOrFriend(GetOwner(ref->extraDataList)))
        {
#ifdef _DEBUG
            TraceOnce(ref, "object owner is not friend");
#endif
            return false;
        }

        const auto zone = GetEncounterZone(ref->extraDataList);
        if (zone)
        {
            if (!IsOwnerEmptyOrFriend(zone))
            {
#ifdef _DEBUG
                TraceOnce(ref, "zone owner is not friend");
#endif
                return false;
            }
            if (GetBool(properties::not_looting_from_settlement) && IsSettlement(zone))
            {
#ifdef _DEBUG
                TraceOnce(ref, "zone is settlement");
#endif
                return false;
            }
        }

        return true;
    }

    bool IsAllowedUniqueItem(const TESForm* form)
    {
        bool isUniqueItem = false;
        for (UInt32 i = 0; i < form_list::uniqueItems->forms.count; ++i)
        {
            TESForm* item = nullptr;
            if (!form_list::uniqueItems->forms.GetNthItem(i, item))
            {
                continue;
            }

            if (form->formID == item->formID)
            {
                isUniqueItem = true;
                break;
            }
        }

        if (!isUniqueItem)
        {
            return true;
        }

        return MatchesAny(form, injection_data::include_unique_item);
    }

    bool IsAllowedFeaturedItem(const TESForm* form)
    {
        if (!HasKeyword(form, keyword::featuredItem))
        {
            return true;
        }

        return MatchesAny(form, injection_data::include_featured_item);
    }

    bool IsValidForm(TESForm* form)
    {
        if (!IsPlayable(form))
        {
            return false;
        }

        for (const auto& exclude : GetAsFormList(injection_data::exclude_form))
        {
            if (form->formID == exclude->formID)
            {
#ifdef _DEBUG
                TraceOnce(form, "form is exclude form");
#endif
                return false;
            }
        }

        if (HasKeyword(form, GetAsKeywordList(injection_data::exclude_keyword)))
        {
#ifdef _DEBUG
            TraceOnce(form, "form has exclude keyword");
#endif
            return false;
        }

        if (!IsAllowedUniqueItem(form) || !IsAllowedFeaturedItem(form))
        {
            if (IsFormTypeMatch(form->formType, kFormType_BOOK))
            {
                if (MatchesAny(form, injection_data::book_type_perk_magazine)
                    && (GetInt(properties::lootable_book_item_type) & perkmagazine) == 0)
                {
                    return false;
                }
            }
            else if (IsFormTypeMatch(form->formType, kFormType_MISC))
            {
                if (MatchesAny(form, injection_data::misc_type_bobblehead)
                    && (GetInt(properties::lootable_misc_item_type) & bobblehead) == 0)
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

    struct InventoryItemInfo
    {
        bool dropped;
        bool featured;
        bool unscrappable;
        bool equipped;
        bool legendary;
        bool questItem;
    };

    InventoryItemInfo GetInventoryItemInfo(const BGSInventoryItem& item)
    {
        InventoryItemInfo result = {};

        std::vector<BGSMod::Attachment::Mod*> buffer;

        // ReSharper disable once CppMsExtBindingRValueToLvalueReference
        item.stack->Visit([=, &buffer, &result](const BGSInventoryItem::Stack* entry)
        {
            if ((entry->flags >> 5) & 1)
            {
                result.dropped = true;
            }

            if (entry->flags & BGSInventoryItem::Stack::kFlagEquipped)
            {
                result.equipped = true;
            }

            const auto data = GetEquipmentData(entry->extraData, &buffer);
            if (data.isFeaturedItem)
            {
                result.featured = true;
            }

            if (data.isUnscrappable)
            {
                result.unscrappable = true;
            }

            if (data.isLegendary)
            {
                result.legendary = true;
            }

            if (IsQuestItem(entry->extraData))
            {
                result.questItem = true;
            }

            return true;
        });

        return result;
    }

    bool IsValidInventoryItem(const TESForm* form, const InventoryItemInfo& info)
    {
        if (info.dropped)
        {
            return false;
        }

        if (info.featured && !info.legendary && !MatchesAny(form, injection_data::include_featured_item))
        {
            return false;
        }

        if (info.questItem)
        {
            return false;
        }

        return true;
    }

    bool IsLootableInventoryItem(const TESForm* form, const InventoryItemInfo& info)
    {
        if (IsFormTypeMatch(form->formType, kFormType_WEAP)
            || IsFormTypeMatch(form->formType, kFormType_ARMO))
        {
            if (GetBool(properties::looting_legendary_only) && !info.legendary)
            {
                return false;
            }
        }
        return true;
    }

    bool IsLootableForm(TESForm* form)
    {
        if (IsFormTypeMatch(form->formType, kFormType_ACTI))
        {
            if (!MatchesAny(form, injection_data::include_activator))
            {
                return false;
            }
        }
        else if (IsFormTypeMatch(form->formType, kFormType_ALCH))
        {
            if ((GetInt(properties::lootable_alch_item_type) & GetALCHType(form)) == 0)
            {
#ifdef _DEBUG
                TraceOnce(form, "item is not allowed alchemy");
#endif
                return false;
            }
        }
        else if (IsFormTypeMatch(form->formType, kFormType_BOOK))
        {
            if ((GetInt(properties::lootable_book_item_type) & GetBOOKType(form)) == 0)
            {
#ifdef _DEBUG
                TraceOnce(form, "item is not allowed book");
#endif
                return false;
            }
        }
        else if (IsFormTypeMatch(form->formType, kFormType_CONT))
        {
            if (HasKeyword(form, keyword::workshop))
            {
                return false;
            }
            if (vendor_chest::IsVendorChest(form->formID))
            {
#ifdef _DEBUG
                TraceOnce(form, "container is vendor chest");
#endif
                return false;
            }
        }
        else if (IsFormTypeMatch(form->formType, kFormType_MISC))
        {
            if ((GetInt(properties::lootable_misc_item_type) & GetMISCType(form)) == 0)
            {
#ifdef _DEBUG
                TraceOnce(form, "item is not allowed misc");
#endif
                return false;
            }
        }
        else if (IsFormTypeMatch(form->formType, kFormType_WEAP))
        {
            if ((GetInt(properties::lootable_weap_item_type) & GetWEAPType(form)) == 0)
            {
#ifdef _DEBUG
                TraceOnce(form, "item is not allowed weap");
#endif
                return false;
            }
        }

        return true;
    }

    bool HasLootableItem(BGSInventoryList* inventoryList)
    {
        bool result = false;

        if (inventoryList)
        {
            inventoryList->inventoryLock.LockForRead();

            for (UInt32 i = 0; i < inventoryList->items.count; ++i)
            {
                BGSInventoryItem item = {};
                if (!inventoryList->items.GetNthItem(i, item))
                {
                    continue;
                }

                TESForm* form = item.form;
                if (!form)
                {
                    continue;
                }

                if (!IsFormTypeMatchesItemType(form->formType, GetInt(properties::lootable_inventory_item_type)))
                {
                    continue;
                }

                if (!IsValidForm(form) || !IsLootableForm(form))
                {
                    continue;
                }

                const auto info = GetInventoryItemInfo(item);
                if (!IsValidInventoryItem(form, info) || !IsLootableInventoryItem(form, info))
                {
                    continue;
                }

                result = true;
                break;
            }

            inventoryList->inventoryLock.Unlock();
        }

        return result;
    }

    // Verify the object is linked to the workshop
    bool IsLinkedToWorkshop(TESObjectREFR* ref)
    {
        BGSKeyword* keyword = nullptr;
        BGSDefaultObject* workshopItemDefault = (*g_defaultObjectMap)->GetDefaultObject("WorkshopItem");
        if (workshopItemDefault)
        {
            keyword = DYNAMIC_CAST(workshopItemDefault->form, TESForm, BGSKeyword);
        }

        if (!keyword)
        {
            return false;
        }

        TESObjectREFR* workshopRef = GetLinkedRef_Native(ref, keyword);
        if (!workshopRef)
        {
            return false;
        }

        return workshopRef->extraDataList->HasType(kExtraData_WorkshopExtraData);
    }

    bool IsLootableObject(TESObjectREFR* ref)
    {
        const auto form = ref->baseForm;
        if (IsFormTypeMatch(form->formType, kFormType_CONT))
        {
            if (GetBool(properties::not_looting_from_settlement) && IsLinkedToWorkshop(ref))
            {
                return false;
            }
            if (!HasLootableItem(ref->inventoryList))
            {
                return false;
            }
        }
        else if (IsFormTypeMatch(form->formType, kFormType_FLOR))
        {
            // flora is harvested.
            if (ref->flags >> 13 & 1)
            {
                return false;
            }
        }
        else if (IsFormTypeMatch(form->formType, kFormType_NPC_))
        {
            if (!virtual_function::IsDead(ref, IsEssential(ref)))
            {
                return false;
            }
            if (!HasLootableItem(ref->inventoryList))
            {
                return false;
            }
        }
        else if (IsFormTypeMatch(form->formType, kFormType_WEAP)
            ||   IsFormTypeMatch(form->formType, kFormType_ARMO))
        {
            std::vector<BGSMod::Attachment::Mod*> buffer;
            const auto data = GetEquipmentData(ref->extraDataList, &buffer);
            if (GetBool(properties::looting_legendary_only) && !data.isLegendary)
            {
#ifdef _DEBUG
                TraceOnce(form, "looting legendary only enabled, but item is not legendary");
#endif
                return false;
            }
        }
        return true;
    }


    // Find nearby references of the specified form type.
    VMArray<TESObjectREFR*> FindNearbyReferencesWithFormType(StaticFunctionTag*, TESObjectREFR* ref, UInt32 formType)
    {
#ifdef _DEBUG
        const char* processId = debug::GetRandomProcessId();
        _MESSAGE("| %s | [ Start FindNearbyReferencesWithFormType ]", processId);
#endif
        VMArray<TESObjectREFR*> result;
        if ((*g_ui)->numPauseGame)
        {
            return result;
        }

        if (!ref)
        {
            return result;
        }

        const NiPoint3 origin = ref->pos;
        const float maxDistance = GetFloat(properties::Key::looting_range) * 100.0f;
        const bool notLootingFromSettlement = GetBool(properties::Key::not_looting_from_settlement);
        const std::size_t maxItemsProcessedPerThread = GetInt(properties::Key::max_items_processed_per_thread);

#ifdef _DEBUG
        _MESSAGE("| %s |   Origin: [ X: %f, Y: %f, Z: %f ]", processId, origin.x, origin.y, origin.z);  // NOLINT(clang-diagnostic-double-promotion)
        _MESSAGE("| %s |   Looting Range: %.1fm", processId, GetFloat(properties::Key::looting_range));  // NOLINT(clang-diagnostic-double-promotion)
        _MESSAGE("| %s |   Form Type: %s", processId, debug::GetFormTypeIdentifier(formType));  // NOLINT(clang-diagnostic-implicit-int-conversion)
#endif

        typedef std::pair<TESObjectREFR*, float> object;
        std::vector<object> buffer;
        buffer.reserve(1024);
        auto collect = [=, &buffer](TESObjectCELLAlt* cell)
        {
            if (!cell)
            {
                return;
            }

            if (cell->cellState != TESObjectCELLAlt::attached)
            {
#ifdef _DEBUG
                _MESSAGE("| %s |     [ Cell is not attached ]", processId);
#endif
                return;
            }

            if (!IsOwnerEmptyOrFriend(GetCellOwner(cell)))
            {
#ifdef _DEBUG
                _MESSAGE("| %s |     [ Cell owner is invalid ]", processId);
#endif
                return;
            }

            if (notLootingFromSettlement)
            {
                if (IsSettlement(GetCellEncounterZone(cell)))
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |     [ Cell is settlement ]", processId);
#endif
                    return;
                }
            }

            {
                SimpleLocker locker(&cell->lock);
                for (UInt32 i = 0; i < cell->objectList.count; ++i)
                {
                    TESObjectREFR* obj = cell->objectList.entries[i];
                    if (!obj || !obj->baseForm)
                    {
                        continue;
                    }

                    if (!IsFormTypeMatch(obj->baseForm->formType, formType))
                    {
                        continue;
                    }

                    if (!CheckPrecondition(obj))
                    {
                        continue;
                    }

                    const float distance = GetDistance(origin, obj->pos);
                    if (distance > 0 && distance <= maxDistance)
                    {
                        buffer.emplace_back(obj, distance);
                    }
                }
            }
        };

        TESWorldSpace* worldSpace = CALL_MEMBER_FN(ref, GetWorldspace)();
        if (!worldSpace)
        {
            if (ref->parentCell)
            {
                collect(reinterpret_cast<TESObjectCELLAlt*>(ref->parentCell));
            }
        }
        else
        {
            std::vector<UInt32> worldSpaceIds;
            while (worldSpace)
            {
                if (std::find(worldSpaceIds.begin(), worldSpaceIds.end(), worldSpace->formID) == worldSpaceIds.end())
                {
                    worldSpaceIds.emplace_back(worldSpace->formID);

                    // ReSharper disable once CppMsExtBindingRValueToLvalueReference
                    worldSpace->cells.ForEach([=](const TESWorldSpace::CELL_ENTRY* entry)
                    {
                        collect(entry->value);
                        return true;
                    });
                }

                worldSpace = worldSpace->parentWorld;
            }
        }

        std::sort(buffer.begin(), buffer.end(), [](const object& lhs, const object& rhs)
        {
            return lhs.second < rhs.second;
        });

#ifdef _DEBUG
        _MESSAGE("| %s |     Found objects: %d", processId, buffer.size());
#endif

        std::vector<TESObjectREFR*> tmp;
        tmp.reserve(maxItemsProcessedPerThread);
        for (const auto& value : buffer)
        {
            TESObjectREFR* obj = value.first;
            TESForm* form = obj->baseForm;
            const UInt32 formId = obj->formID;

            if (IsLockedObject(formId))
            {
                continue;
            }

            if (!IsValidForm(form) || !IsValidObject(obj))
            {
                continue;
            }

            if (!IsLootableForm(form) || !IsLootableObject(obj))
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

        // The Papyrus loop scans in reverse order, so it sorts in descending order so that it is processed from the one closest to the player.
        std::reverse(tmp.begin(), tmp.end());

        for (auto& obj : tmp)
        {
            result.Push(&obj);
        }

        return result;
    }

    // Get scrap components of the specified item type in the inventory.
    VMArray<MiscComponent> GetEquipmentComponents(StaticFunctionTag*, VMRefOrInventoryObj* inventoryItem)
    {
#ifdef _DEBUG
        const char* processId = debug::GetRandomProcessId();
        _MESSAGE("| %s | [ Start GetEquipmentComponents ]", processId);
#endif
        VMArray<MiscComponent> result;
        if (!inventoryItem)
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   [ Illegal argument error ]", processId);
            _MESSAGE("| %s |     inventoryItem is null: %s", processId, Bool2S(!inventoryItem));
#endif
            return result;
        }

        TESForm* baseForm = nullptr;
        ExtraDataList* extraDataList = nullptr;
        inventoryItem->GetExtraData(&baseForm, &extraDataList);
        if (!baseForm || !extraDataList)
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   [ VMRefOrInventoryObj is invalid ]", processId);
            _MESSAGE("| %s |     Base form is null: %s", processId, Bool2S(!baseForm));
            _MESSAGE("| %s |     Extra data is null: %s", processId, Bool2S(!extraDataList));
#endif
            return result;
        }

#ifdef _DEBUG
        _MESSAGE("| %s |   Item: %s", processId, debug::GetDisplayName(baseForm, extraDataList));
#endif

        if (baseForm->formType != kFormType_ARMO && baseForm->formType != kFormType_WEAP)
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   [ Item is invalid ]", processId);
            _MESSAGE("| %s |     Is not armor: %s", processId, Bool2S(baseForm->formType != kFormType_ARMO));
            _MESSAGE("| %s |     Is not weapon: %s", processId, Bool2S(baseForm->formType != kFormType_WEAP));
#endif
            return result;
        }

        std::unordered_map<BGSComponent*, UInt32> data;
        auto extractComponents = [=, &data](const BGSConstructibleObject* obj)
        {
            if (!obj)
            {
                return;
            }

            for (UInt32 i = 0; i < obj->components->count; ++i)
            {
                BGSConstructibleObject::Component objComponent = {};
                if (!obj->components->GetNthItem(i, objComponent) || objComponent.count == 0)
                {
                    continue;
                }

                const auto misc = DYNAMIC_CAST(objComponent.component, TESForm, TESObjectMISC);
                if (misc)
                {
                    for (UInt32 j = 0; j < misc->components->count; ++j)
                    {
                        TESObjectMISC::Component miscComponent = {};
                        if (misc->components->GetNthItem(j, miscComponent))
                        {
                            if (miscComponent.count == 0 || miscComponent.component->scrapScalar->value <= 0.0f)
                            {
                                continue;
                            }
#ifdef _DEBUG
                            _MESSAGE("| %s |       Found component: [ Id: %08X, Name: %s, Count: %d, Scale: %.1f ] x%d", processId, miscComponent.component->formID, debug::GetName(miscComponent.component), miscComponent.count, miscComponent.component->scrapScalar->value, objComponent.count);
#endif
                            data[miscComponent.component] += static_cast<UInt32>(miscComponent.count * objComponent.count);
                        }
                    }
                    continue;
                }

                if (objComponent.component->scrapScalar->value <= 0.0f)
                {
                    continue;
                }
#ifdef _DEBUG
                _MESSAGE("| %s |       Found component: [ Id: %08X, Name: %s, Count: %d, Scale: %.1f ]", processId, objComponent.component->formID, debug::GetName(objComponent.component), objComponent.count, objComponent.component->scrapScalar->value);
#endif
                data[objComponent.component] += objComponent.count;
            }
        };

        tArray<BGSConstructibleObject*> allObj = (*g_dataHandler)->arrCOBJ;
        auto findConstructibleObject = [=](TESForm* form)
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   [ Extract components ]", processId);
            _MESSAGE("| %s |     Scrap target: [ Id: %08X, Name: %s ]", processId, form->formID, debug::GetName(form));
#endif
            for (UInt32 i = 0; i < allObj.count; ++i)
            {
                BGSConstructibleObject* obj = nullptr;

                if (!allObj.GetNthItem(i, obj) || !obj->createdObject || !obj->components)
                {
                    continue;
                }

                if (form->formID == obj->createdObject->formID)
                {
                    return obj;
                }

                const auto formList = DYNAMIC_CAST(obj->createdObject, TESForm, BGSListForm);
                if (!formList)
                {
                    continue;
                }

                for (UInt32 j = 0; j < formList->forms.count; ++j)
                {
                    TESForm* item = nullptr;
                    if (formList->forms.GetNthItem(j, item) && form->formID == item->formID)
                    {
                        return obj;
                    }
                }
            }

            return static_cast<BGSConstructibleObject*>(nullptr);
        };

        std::vector<BGSMod::Attachment::Mod*> list;
        GetMods(extraDataList, &list);

        for (const auto& objectMod : list)
        {
            extractComponents(findConstructibleObject(objectMod));
        }

        extractComponents(findConstructibleObject(baseForm));

        for (const auto& pair : data)
        {
            MiscComponent component;
            component.Set("object", pair.first);
            component.Set("count", pair.second / 2);
            result.Push(&component);
        }

        return result;
    }

    // Get form's type.
    UInt32 GetFormType(StaticFunctionTag*, TESForm* form)
    {
        return !form ? kFormType_NONE : form->formType;
    }

    // Get items of the specified item type in the inventory.
    VMArray<TESForm*> GetInventoryItemsWithItemType(StaticFunctionTag*, TESObjectREFR* inventoryOwner, UInt32 itemType)
    {
#ifdef _DEBUG
        const char* processId = debug::GetRandomProcessId();
        _MESSAGE("| %s | [ Start GetInventoryItemsWithItemType ]", processId);
#endif
        VMArray<TESForm*> result;

        if (!inventoryOwner || (itemType < 0 || itemType > all_item))
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   [ Illegal argument error ]", processId);
            _MESSAGE("| %s |     inventoryOwner is null: %s", processId, Bool2S(!inventoryOwner));
            _MESSAGE("| %s |     itemType is out of range: %d", processId, itemType);
#endif
            return result;
        }

#ifdef _DEBUG
        _MESSAGE("| %s |   InventoryOwner is essential: %s", processId, Bool2S(IsEssential(inventoryOwner)));
#endif
        const bool isPlayer = (inventoryOwner->formID == (*g_player)->formID);
        const bool isDead = virtual_function::IsDead(inventoryOwner, IsEssential(inventoryOwner));

#ifdef _DEBUG
        _MESSAGE("| %s |   Inventory owner: [ Name: %s, Id: %08X ]", processId, CALL_MEMBER_FN(inventoryOwner, GetReferenceName)(), inventoryOwner->formID);
        _MESSAGE("| %s |   Is player: %s", processId, Bool2S(isPlayer));
        _MESSAGE("| %s |   Is dead: %s", processId, Bool2S(isDead));
        _MESSAGE("| %s |   Form type of item to look for: %s", processId, debug::GetItemTypeIdentifier(itemType));
#endif

        BGSInventoryList* inventoryList = inventoryOwner->inventoryList;
        if (!inventoryList)
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   [ Inventory does not exist ]", processId);
#endif
            return result;
        }

#ifdef _DEBUG
        _MESSAGE("| %s |   [ Start searching for items ]", processId);
#endif
        {
            inventoryList->inventoryLock.LockForRead();

            for (UInt32 i = 0; i < inventoryList->items.count; ++i)
            {
                BGSInventoryItem item = {};
                if (!inventoryList->items.GetNthItem(i, item))
                {
                    continue;
                }

                TESForm* form = item.form;
                if (!form)
                {
                    continue;
                }

#ifdef _DEBUG
                _MESSAGE("| %s |     [ Item %d ]", processId, i);
                _MESSAGE("| %s |       %s", processId, debug::InvItem2S(&item));
#endif

                if (!IsPlayable(form))
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Item is not playable ]", processId);
#endif
                    continue;
                }

                if (!IsFormTypeMatchesItemType(form->formType, itemType))
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Mismatch item type ]", processId);
#endif
                    continue;
                }

                if (isPlayer)
                {
                    if (form->formID == 0x0F)
                    {
#ifdef _DEBUG
                        _MESSAGE("| %s |       [ Item is caps ]", processId);
#endif
                        continue;
                    }

                    if (IsFavorite(item.form))
                    {
#ifdef _DEBUG
                        _MESSAGE("| %s |       [ Item is favorite ]", processId);
#endif
                        continue;
                    }
                }

                bool pickable = true;
                // ReSharper disable once CppMsExtBindingRValueToLvalueReference
                item.stack->Visit([=, &pickable](const BGSInventoryItem::Stack* stack)
                {
                    if ((stack->flags >> 5) & 1 || ((stack->flags & BGSInventoryItem::Stack::kFlagEquipped) != 0 && !isDead))
                    {
                        pickable = false;
                        return false;
                    }
                    return true;
                });

                if (!pickable)
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Item is equipped or dropped item ]", processId);
#endif
                    continue;
                }

#ifdef _DEBUG
                _MESSAGE("| %s |     [ Add to result ]", processId);
#endif
                result.Push(&form);
            }

            inventoryList->inventoryLock.Unlock();
        }

        return result;
    }

    // Get lootable items of the specified item type in the inventory.
    VMArray<TESForm*> GetLootableItems(StaticFunctionTag*, TESObjectREFR* inventoryOwner, UInt32 itemType)
    {
#ifdef _DEBUG
        const char* processId = debug::GetRandomProcessId();
        _MESSAGE("| %s | [ Start GetLootableItems ]", processId);
#endif
        VMArray<TESForm*> result;

        if (!inventoryOwner || (itemType < 0 || itemType > all_item))
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   [ Illegal argument error ]", processId);
            _MESSAGE("| %s |     inventoryOwner is null: %s", processId, Bool2S(!inventoryOwner));
            _MESSAGE("| %s |     itemType is out of range: %d", processId, itemType);
#endif
            return result;
        }

        BGSInventoryList* inventoryList = inventoryOwner->inventoryList;
        if (!inventoryList)
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   [ Inventory does not exist ]", processId);
#endif
            return result;
        }

#ifdef _DEBUG
        _MESSAGE("| %s |   [ Start searching for items ]", processId);
#endif
        {
            inventoryList->inventoryLock.LockForRead();

            for (UInt32 i = 0; i < inventoryList->items.count; ++i)
            {
                BGSInventoryItem item = {};
                if (!inventoryList->items.GetNthItem(i, item))
                {
                    continue;
                }

                TESForm* form = item.form;
                if (!form)
                {
                    continue;
                }

#ifdef _DEBUG
                _MESSAGE("| %s |     [ Item %d ]", processId, i);
                _MESSAGE("| %s |       %s", processId, debug::InvItem2S(&item));
#endif

                if (!IsFormTypeMatchesItemType(form->formType, itemType))
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Mismatch item type ]", processId);
#endif
                    continue;
                }

                if (!IsValidForm(form) || !IsLootableForm(form))
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Item form is invalid ]", processId);
#endif
                    continue;
                }

                const auto info = GetInventoryItemInfo(item);
                if (!IsValidInventoryItem(form, info) || !IsLootableInventoryItem(form, info))
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Inventory item is invalid ]", processId);
#endif
                    continue;
                }

                result.Push(&form);
            }

            inventoryList->inventoryLock.Unlock();
        }

        return result;
    }

    VMArray<TESForm*> GetScrappableItems(StaticFunctionTag*, TESObjectREFR* inventoryOwner, UInt32 itemType)
    {
#ifdef _DEBUG
        const char* processId = debug::GetRandomProcessId();
        _MESSAGE("| %s | [ Start GetScrappableItems ]", processId);
#endif
        VMArray<TESForm*> result;

        if (!inventoryOwner || (itemType < 0 || itemType > all_item))
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   [ Illegal argument error ]", processId);
            _MESSAGE("| %s |     inventoryOwner is null: %s", processId, Bool2S(!inventoryOwner));
            _MESSAGE("| %s |     itemType is out of range: %d", processId, itemType);
#endif
            return result;
        }

        const bool isPlayer = (inventoryOwner->formID == (*g_player)->formID);
        const bool isDead = virtual_function::IsDead(inventoryOwner, IsEssential(inventoryOwner));

        BGSInventoryList* inventoryList = inventoryOwner->inventoryList;
        if (!inventoryList)
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   [ Inventory does not exist ]", processId);
#endif
            return result;
        }

#ifdef _DEBUG
        _MESSAGE("| %s |   [ Start searching for items ]", processId);
#endif
        {
            inventoryList->inventoryLock.LockForRead();

            for (UInt32 i = 0; i < inventoryList->items.count; ++i)
            {
                BGSInventoryItem item = {};
                if (!inventoryList->items.GetNthItem(i, item))
                {
                    continue;
                }

                TESForm* form = item.form;
                if (!form)
                {
                    continue;
                }

#ifdef _DEBUG
                _MESSAGE("| %s |     [ Item %d ]", processId, i);
                _MESSAGE("| %s |       %s", processId, debug::InvItem2S(&item));
#endif

                if (!IsPlayable(form))
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Item is not playable ]", processId);
#endif
                    continue;
                }

                if (!IsFormTypeMatchesItemType(form->formType, itemType))
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Mismatch item type ]", processId);
#endif
                    continue;
                }

                if (HasKeyword(form, keyword::featuredItem))
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Form is featured item ]", processId);
#endif
                    continue;
                }

                if (isPlayer)
                {
                    if (form->formID == 0x0F)
                    {
#ifdef _DEBUG
                        _MESSAGE("| %s |       [ Item is caps ]", processId);
#endif
                        continue;
                    }

                    if (IsFavorite(item.form))
                    {
#ifdef _DEBUG
                        _MESSAGE("| %s |       [ Item is favorite ]", processId);
#endif
                        continue;
                    }
                }

                const auto info = GetInventoryItemInfo(item);

                if (info.equipped && !isDead)
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Item is equipped ]", processId);
#endif
                    continue;
                }

                if (info.featured || info.unscrappable || info.questItem)
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |       [ Item is cannot be scrapped ]", processId);
#endif
                    continue;
                }

                result.Push(&form);
            }

            inventoryList->inventoryLock.Unlock();
        }

        return result;
    }

    // Verify that the item's form type matches the specified value.
    bool IsFormTypeEquals(StaticFunctionTag*, TESForm* form, UInt32 formType)
    {
        return !form ? false : form->formType == formType;
    }

    // Called when the value of LootMan's configuration is changed.
    void OnUpdateLootManProperty(StaticFunctionTag*, BSFixedString propertyName)
    {
        properties::Update(propertyName);
    }

    void PlayPickUpSound(StaticFunctionTag*, TESObjectREFR* player, TESObjectREFR* obj)
    {
        const auto actor = DYNAMIC_CAST(player, TESObjectREFR, Actor);
        if (!actor)
        {
            return;
        }

        const auto boundObject = DYNAMIC_CAST(obj->baseForm, TESForm, TESBoundObject);
        if (!boundObject)
        {
            return;
        }

        virtual_function::PlayPickUpSound(actor, boundObject);
    }

    // Requests the LootMan plugin to release object references got with FindNearbyReferencesWithFormType.
    void ReleaseObject(StaticFunctionTag*, UInt32 objId)
    {
        ReleaseObject(objId);
    }
}

bool papyrus_lootman::Register(VirtualMachine* vm)
{
    _MESSAGE("| INITIALIZE | [ Started binding papyrus functions for LootMan ]");

    vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectREFR*, UInt32>("FindNearbyReferencesWithFormType", "LTMN2:LootMan", FindNearbyReferencesWithFormType, vm));
    vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<MiscComponent>, VMRefOrInventoryObj*>("GetEquipmentComponents", "LTMN2:LootMan", GetEquipmentComponents, vm));
    vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, UInt32, TESForm*>("GetFormType", "LTMN2:LootMan", GetFormType, vm));
    vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, VMArray<TESForm*>, TESObjectREFR*, UInt32>("GetInventoryItemsWithItemType", "LTMN2:LootMan", GetInventoryItemsWithItemType, vm));
    vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, VMArray<TESForm*>, TESObjectREFR*, UInt32>("GetLootableItems", "LTMN2:LootMan", GetLootableItems, vm));
    vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, VMArray<TESForm*>, TESObjectREFR*, UInt32>("GetScrappableItems", "LTMN2:LootMan", GetScrappableItems, vm));
    vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, bool, TESForm*, UInt32>("IsFormTypeEquals", "LTMN2:LootMan", IsFormTypeEquals, vm));
    vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, void, TESObjectREFR*, TESObjectREFR*>("PlayPickUpSound", "LTMN2:LootMan", PlayPickUpSound, vm));
    vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, BSFixedString>("OnUpdateLootManProperty", "LTMN2:LootMan", OnUpdateLootManProperty, vm));
    vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, UInt32>("ReleaseObject", "LTMN2:LootMan", ReleaseObject, vm));

    vm->SetFunctionFlags("LTMN2:LootMan", "FindNearbyReferencesWithFormType", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:LootMan", "GetEquipmentComponents", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:LootMan", "GetFormType", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:LootMan", "GetInventoryItemsWithItemType", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:LootMan", "GetLootableItems", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:LootMan", "GetScrappableItems", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:LootMan", "IsFormTypeEquals", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:LootMan", "PlayPickUpSound", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:LootMan", "OnUpdateLootManProperty", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:LootMan", "ReleaseObject", IFunction::kFunctionFlag_NoWait);

    _MESSAGE("| INITIALIZE |   Papyrus functions binding is complete");
    return true;
}

void papyrus_lootman::OnPreLoadGame()
{
    {
        SimpleLocker locker(&papyrus_lootman::objectsLock);
        papyrus_lootman::lockedObjects.clear();
    }
}
