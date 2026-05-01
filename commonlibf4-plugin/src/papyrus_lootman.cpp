#include "papyrus_lootman.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <excpt.h>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <REL/Relocation.h>

#include "constructible_object.h"
#include "form_cache.h"
#include "injection_data.h"
#include "properties.h"
#include "vendor_chest.h"

namespace papyrus_lootman
{
	using namespace form_cache;
	using namespace RE;

	// Safely pack a TESForm-derived pointer into a Papyrus Variable, bypassing
	// CommonLibF4's broken `PackVariable<object T>(Variable&, const volatile T*)`
	// which dispatches to `IVirtualMachine::CreateObject(BSFixedString&, BSTSmartPointer<Object>&)`
	// at a vtable slot that does not match the game binary (the interface
	// header declares the two CreateObject overloads in an order that fights
	// MSVC's reverse-declaration virtual overload placement) and CTDs the
	// game. We use `ObjectBindPolicy::bindInterface->CreateObjectWithProperties`
	// and drive `BindObject` ourselves, which is the equivalent of v1's
	// `BindID` / `PackHandle` helper in `PapyrusArgs.cpp`.
	//
	// `a_vmTypeID` is the type ID the VM should use to look up the Papyrus
	// script class. For `TESObjectREFR*` callers pass `kREFR`. For a generic
	// `TESForm*` we can't rely on the static `FORM_ID` (which is `kNONE`
	// because `TESForm::FORM_ID == ENUM_FORM_ID::kNONE`); v1 uses the runtime
	// `form->formType` instead so each inventory item gets its proper Papyrus
	// subclass (Weapon, Armor, Alchemy, ...).
	bool PackFormSafe(BSScript::Variable& a_var, TESForm* form, std::uint32_t vmTypeID)
	{
		if (!form)
		{
			a_var = nullptr;
			return true;
		}

		const auto success = [&]()
		{
			auto* game = GameVM::GetSingleton();
			auto vm = game ? game->GetVM() : nullptr;
			if (!vm)
			{
				return false;
			}

			BSTSmartPointer<BSScript::ObjectTypeInfo> typeInfo;
			if (!vm->GetScriptObjectType(vmTypeID, typeInfo) || !typeInfo)
			{
				return false;
			}

			auto& handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(
				vmTypeID, static_cast<const void*>(form));
			if (handle == handles.EmptyHandle())
			{
				return false;
			}

			BSTSmartPointer<BSScript::Object> object;
			if (!vm->FindBoundObject(handle, typeInfo->name.c_str(), false, object, false) || !object)
			{
				auto& binding = vm->GetObjectBindPolicy();
				auto* bindInterface = binding.bindInterface;
				if (!bindInterface ||
					!bindInterface->CreateObjectWithProperties(typeInfo->name, 0, object) ||
					!object)
				{
					return false;
				}

				binding.BindObject(object, handle);
			}

			if (!object)
			{
				return false;
			}

			a_var = std::move(object);
			return true;
		}();

		if (!success)
		{
			assert(false);
			REX::ERROR("failed to pack Form"sv);
			a_var = nullptr;
		}

		return success;
	}

	inline bool PackObjectReferenceSafe(BSScript::Variable& a_var, TESObjectREFR* ref)
	{
		return PackFormSafe(a_var, ref, static_cast<std::uint32_t>(TESObjectREFR::FORM_ID));
	}

	struct MiscComponent
	{
		static constexpr const char* name = "MiscObject#MiscComponent";

		MiscComponent()
		{
			if (_proxy)
			{
				return;
			}

			auto* game = GameVM::GetSingleton();
			auto vm = game ? game->GetVM() : nullptr;
			BSFixedString typeName(name);
			if (!vm || !vm->CreateStruct(typeName, _proxy) || !_proxy)
			{
				assert(false);
				REX::ERROR("failed to create structure of type \"{}\"", name);
			}
		}

		template <class T>
		std::optional<T> find(std::string_view a_name, bool a_quiet = false) const
		{
			if (_proxy && _proxy->type)
			{
				const auto& mappings = _proxy->type->varNameIndexMap;
				const auto it = mappings.find(a_name);
				if (it != mappings.end())
				{
					const auto& var = _proxy->variables[it->second];
					return BSScript::detail::UnpackVariable<T>(var);
				}
			}

			if (!a_quiet)
			{
				REX::ERROR("failed to find var \"{}\" on structure \"{}\"", a_name, name);
			}

			return std::nullopt;
		}

		template <class T>
		bool insert(std::string_view a_name, T&& a_val)
		{
			if (_proxy && _proxy->type)
			{
				auto& mappings = _proxy->type->varNameIndexMap;
				const auto it = mappings.find(a_name);
				if (it != mappings.end())
				{
					auto& var = _proxy->variables[it->second];
					BSScript::detail::PackVariable(var, std::forward<T>(a_val));
					return true;
				}
			}

			REX::ERROR("failed to pack var \"{}\" on structure \"{}\"", a_name, name);
			return false;
		}

	private:
		friend struct RE::BSScript::detail::wrapper_accessor;

		explicit MiscComponent(BSTSmartPointer<BSScript::Struct> a_proxy) noexcept :
			_proxy(std::move(a_proxy))
		{
			assert(_proxy != nullptr);
		}

		[[nodiscard]] BSTSmartPointer<BSScript::Struct> get_proxy() const&
		{
			return _proxy;
		}

		[[nodiscard]] BSTSmartPointer<BSScript::Struct> get_proxy() &&
		{
			return std::move(_proxy);
		}

		BSTSmartPointer<BSScript::Struct> _proxy;
	};
}

// CommonLibF4's generic `PackVariable<object T>(Variable&, const volatile T*)`
// for TESForm-derived pointer types calls `IVirtualMachine::CreateObject`,
// which in the current pinned submodule dispatches to the wrong vtable slot
// (MSVC places the two `CreateObject` overloads in the opposite order from the
// annotations in the interface header) and CTDs the game.
//
// We can't intercept `BSScript::PackVariable` directly because it's a
// concept-constrained overload set (no primary template to specialize), and
// because the array-overload's inner call is `detail::PackVariable(...)` which
// is a qualified call - phase-1 lookup in the template definition only sees
// declarations visible in `BSScriptUtil.h` at that point, so a non-template
// overload added later in our TU would be invisible.
//
// `BSScript::detail::PackVariable<T>` IS a primary function template. We
// specialize it for `TESObjectREFR*`. Function-template specializations are
// considered at instantiation time (phase 2) and therefore override the
// primary even though they are declared later in the TU. Specialization
// placement must occur before the first use, which in this file is the
// `BindNativeMethod(... &FindNearbyReferencesWithFormType ...)` call inside
// `Register()` near the bottom.
namespace RE::BSScript::detail
{
	template <>
	struct _is_structure_wrapper<papyrus_lootman::MiscComponent> :
		std::true_type
	{};

	// For `std::vector<TESObjectREFR*>` returns (FindNearbyReferencesWithFormType).
	template <>
	inline void PackVariable<TESObjectREFR*>(Variable& a_var, TESObjectREFR*&& a_val)
	{
		(void)papyrus_lootman::PackObjectReferenceSafe(a_var, a_val);
	}

	// For `std::vector<TESForm*>` returns (GetInventoryItemsWithItemType,
	// GetLootableItems, GetScrappableItems). v1's `PackHandle` used the
	// per-form runtime `formType` instead of a static type ID so each form
	// gets marshalled as its correct Papyrus subclass (Weapon, Armor, etc).
	// We mirror that behaviour.
	template <>
	inline void PackVariable<TESForm*>(Variable& a_var, TESForm*&& a_val)
	{
		const auto vmTypeID = a_val
			? static_cast<std::uint32_t>(a_val->GetFormType())
			: static_cast<std::uint32_t>(TESForm::FORM_ID);
		(void)papyrus_lootman::PackFormSafe(a_var, a_val, vmTypeID);
	}
}

namespace papyrus_lootman
{
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
		std::int32_t totalCount = 0;
	};

	enum InventoryInfoFlags : std::uint32_t
	{
		inventory_info_basic = 0,
		inventory_info_equipment = 1 << 0,
		inventory_info_quest = 1 << 1,
		inventory_info_full = inventory_info_equipment | inventory_info_quest,
	};

	struct MatchCache
	{
		std::unordered_map<std::uint64_t, bool> results;
	};

	struct ObjectEntry
	{
		NiPointer<TESObjectREFR> obj;
		float distanceSquared = 0.0f;
	};

	// ---- Constants ----

	inline constexpr std::uint32_t kFormFlagDeleted = 1u << 5;
	inline constexpr std::uint32_t kFormFlagDisabled = 1u << 11;
	inline constexpr std::uint32_t kFormFlagDestroyed = 1u << 23;
	inline constexpr std::uint32_t kLegendaryModFlag = 1u << 4;
	inline constexpr std::uint32_t kWeaponTargetKeywords = 18;
	inline constexpr std::uint32_t kArmorTargetKeywords = 11;
	inline constexpr std::uint32_t kActivationBlocked = 1u << 0;
	inline constexpr std::uint32_t kActivationIgnored = 1u << 1;
	inline constexpr std::uint32_t kAmmoFusionCoreFormId = 0x00075FE4;
	inline constexpr std::size_t kMaxItemsProcessedPerThreadLimit = 10000;
	inline constexpr std::uintptr_t kLoadChangeCellBeforeZoneResetCallRva = 0x4D23C4;
	inline constexpr std::uintptr_t kEncounterZoneResetElapsedFromDetachRva = 0x4D2E20;

	std::mutex lootCapacityLock;

	struct PropertiesSnapshot
	{
		bool notLootingFromSettlement = false;
		bool lootingLegendaryOnly = false;
		bool alwaysLootingExplosives = false;
		int lootableAlchItemType = 0;
		int lootableBookItemType = 0;
		int lootableMiscItemType = 0;
		int lootableWeapItemType = 0;
		int lootableInventoryItemType = 0;

		static PropertiesSnapshot Capture()
		{
			PropertiesSnapshot s;
			s.notLootingFromSettlement = properties::GetBool(properties::not_looting_from_settlement);
			s.lootingLegendaryOnly = properties::GetBool(properties::looting_legendary_only);
			s.alwaysLootingExplosives = properties::GetBool(properties::always_looting_explosives);
			s.lootableAlchItemType = properties::GetInt(properties::lootable_alch_item_type);
			s.lootableBookItemType = properties::GetInt(properties::lootable_book_item_type);
			s.lootableMiscItemType = properties::GetInt(properties::lootable_misc_item_type);
			s.lootableWeapItemType = properties::GetInt(properties::lootable_weap_item_type);
			s.lootableInventoryItemType = properties::GetInt(properties::lootable_inventory_item_type);
			return s;
		}
	};

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

	struct ExtraCellDetachTimeCompat :
		public BSExtraData
	{
		static constexpr auto TYPE = EXTRA_DATA_TYPE::kCellDetachTime;
		std::uint32_t detachTime;
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

	using Clock = std::chrono::steady_clock;

	std::optional<std::uint32_t> GetCellDetachTime(const TESObjectCELL* cell)
	{
		if (!cell)
		{
			return std::nullopt;
		}

		if (auto* extraList = cell->extraList.get())
		{
			if (auto* detachTime = extraList->GetByType<ExtraCellDetachTimeCompat>())
			{
				return detachTime->detachTime;
			}
		}

		return std::nullopt;
	}

	using CheckCellBeforeEncounterZoneResetFn = bool (*)(BGSEncounterZone*, TESObjectCELL*);
	using CheckResetElapsedFromDetachTimeFn =
		bool (*)(std::uint32_t, std::uint32_t, bool);

	CheckCellBeforeEncounterZoneResetFn originalCheckCellBeforeEncounterZoneReset = nullptr;
	CheckResetElapsedFromDetachTimeFn checkResetElapsedFromDetachTime = nullptr;
	std::mutex encounterZoneSuppressionLogLock;
	Clock::time_point lastEncounterZoneSuppressionLogAt{};

	inline constexpr auto kEncounterZoneSuppressionLogInterval = std::chrono::seconds(5);

	bool ShouldSuppressCellBeforeEncounterZoneReset(
		BGSEncounterZone* zone,
		TESObjectCELL* cell,
		bool originalResult)
	{
		if (!originalResult || !zone || !cell || !checkResetElapsedFromDetachTime)
		{
			return false;
		}

		const auto cellDetachTime = GetCellDetachTime(cell);
		if (!cellDetachTime)
		{
			return false;
		}

		const auto currentDay = zone->gameData.attachTime;
		if (currentDay == 0)
		{
			return false;
		}

		const bool locationCleared =
			zone->data.location ? zone->data.location->cleared : false;
		const bool cellDetachResetElapsed = checkResetElapsedFromDetachTime(
			currentDay,
			*cellDetachTime,
			locationCleared);

		return zone->gameData.detachTime == 0 &&
			zone->gameData.resetTime != 0 &&
			zone->gameData.resetTime == currentDay &&
			!cellDetachResetElapsed;
	}

	bool HookedCheckCellBeforeEncounterZoneReset(BGSEncounterZone* zone, TESObjectCELL* cell)
	{
		const bool result = originalCheckCellBeforeEncounterZoneReset(zone, cell);
		if (!ShouldSuppressCellBeforeEncounterZoneReset(zone, cell, result))
		{
			return result;
		}

		const auto now = Clock::now();
		{
			std::lock_guard<std::mutex> guard(encounterZoneSuppressionLogLock);
			if (lastEncounterZoneSuppressionLogAt.time_since_epoch().count() == 0 ||
				(now - lastEncounterZoneSuppressionLogAt) >= kEncounterZoneSuppressionLogInterval)
			{
				lastEncounterZoneSuppressionLogAt = now;
				const auto cellDetachTime = GetCellDetachTime(cell).value_or(0);
				REX::DEBUG(
					"Suppressed stale encounter-zone reset: zone={:08X}, cell={:08X}, zoneDetachTime={}, zoneAttachTime={}, zoneResetTime={}, cellDetachTime={}",
					zone ? zone->formID : 0,
					cell ? cell->formID : 0,
					zone ? zone->gameData.detachTime : 0,
					zone ? zone->gameData.attachTime : 0,
					zone ? zone->gameData.resetTime : 0,
					cellDetachTime);
			}
		}

		return false;
	}

	void InstallEncounterZoneResetSuppressionHooks()
	{
		static std::once_flag installOnce;
		std::call_once(installOnce, []()
		{
			REL::Relocation<std::uintptr_t> cellBeforeResetCallSite{
				REL::Offset(kLoadChangeCellBeforeZoneResetCallRva)
			};
			REL::Relocation<CheckResetElapsedFromDetachTimeFn> resetElapsedFromDetach{
				REL::Offset(kEncounterZoneResetElapsedFromDetachRva)
			};

			originalCheckCellBeforeEncounterZoneReset =
				reinterpret_cast<CheckCellBeforeEncounterZoneResetFn>(
					cellBeforeResetCallSite.write_call<5>(
						HookedCheckCellBeforeEncounterZoneReset));
			checkResetElapsedFromDetachTime = resetElapsedFromDetach.get();

			REX::INFO("Installed encounter-zone reset suppression hook");
		});
	}

	bool UsesWorldReferenceTransfer(ENUM_FORM_ID formType)
	{
		return formType == ENUM_FORM_ID::kALCH ||
		       formType == ENUM_FORM_ID::kAMMO ||
		       formType == ENUM_FORM_ID::kARMO ||
		       formType == ENUM_FORM_ID::kBOOK ||
		       formType == ENUM_FORM_ID::kINGR ||
		       formType == ENUM_FORM_ID::kKEYM ||
		       formType == ENUM_FORM_ID::kMISC ||
		       formType == ENUM_FORM_ID::kWEAP;
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
		if (!ref)
		{
			return false;
		}

		if (const auto* actor = ref->As<Actor>();
			actor && actor->boolFlags.all(Actor::BOOL_FLAGS::kEssential))
		{
			return true;
		}

		const auto* baseObj = ref->GetObjectReference();
		if (baseObj)
		{
			const auto* npc = baseObj->As<TESNPC>();
			if (npc && npc->IsEssential())
			{
				return true;
			}
		}

		return ref->extraList && GetQuestAliasFlags(ref->extraList.get()).isEssential;
	}

	bool IsAliveButDownActor(const TESObjectREFR* ref)
	{
		const auto* actor = ref ? ref->As<Actor>() : nullptr;
		if (!actor)
		{
			return false;
		}

		switch (static_cast<ACTOR_LIFE_STATE>(actor->lifeState))
		{
		case ACTOR_LIFE_STATE::kUnconscious:
		case ACTOR_LIFE_STATE::kRestrained:
		case ACTOR_LIFE_STATE::kEssentialDown:
		case ACTOR_LIFE_STATE::kBleedout:
			return true;
		default:
			return false;
		}
	}

	bool IsDeadForLooting(const TESObjectREFR* ref)
	{
		if (!ref)
		{
			return false;
		}

		if (IsAliveButDownActor(ref))
		{
			return false;
		}

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
		if (!GetMods(extraDataList, buffer))
		{
			return equipmentData;
		}

		for (const auto& mod : *buffer)
		{
			if (IsLegendaryMod(mod))
			{
				equipmentData.isLegendary = true;
			}

			// Read the property-mod block directly out of the BSTDataBuffer<2> that
			// BGSMod::Container inherits from, instead of going through
			// `BGSMod::Attachment::Mod::GetData(Data&)`. The REL::Relocation behind
			// that member function crashes in the currently targeted game build,
			// while `GetBuffer<T>(id)` is a pure in-memory template helper so it
			// stays safe even if the game function address has shifted. Block id 1
			// (`BLOCKIDS::kPMOD`) is the property-mod list.
			const auto propModSpan = mod->GetBuffer<const BGSMod::Property::Mod>(
				static_cast<std::uint8_t>(BGSMod::Property::BLOCKIDS::kPMOD));
			for (const auto& propMod : propModSpan)
			{
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

	using SehCall = void(*)(void*);

	int SehFilterRecoverable(unsigned long code)
	{
		constexpr unsigned long kAccessViolation = 0xC0000005UL;
		constexpr unsigned long kDatatypeMisalignment = 0x80000002UL;
		switch (code)
		{
		case kAccessViolation:
		case kDatatypeMisalignment:
			return 1;
		default:
			return 0;
		}
	}

	bool ExecuteSehCallSafe(SehCall call, void* context)
	{
#if defined(_MSC_VER)
		__try
		{
			call(context);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		call(context);
		return true;
#endif
	}

	bool StartsWithAscii(const char* value, const char* prefix)
	{
		if (!value || !prefix)
		{
			return false;
		}

		while (*prefix != '\0')
		{
			if (*value == '\0' || *value != *prefix)
			{
				return false;
			}
			++value;
			++prefix;
		}
		return true;
	}

	const char* GetFormEditorIDOrEmpty(const TESForm* form)
	{
		auto* mutableForm = const_cast<TESForm*>(form);
		auto* editorID = mutableForm ? mutableForm->GetFormEditorID() : nullptr;
		return editorID ? editorID : "";
	}

	bool IsSpecialContainerEditorID(const char* editorID)
	{
		return StartsWithAscii(editorID, "WorkshopResourceContainer") ||
		       StartsWithAscii(editorID, "Pipboy");
	}

	bool IsSpecialContainerReference(TESObjectREFR* ref, TESForm* baseForm = nullptr)
	{
		if (!ref)
		{
			return false;
		}

		auto* form = baseForm ? baseForm : ref->GetObjectReference();
		if (!form || form->GetFormType() != ENUM_FORM_ID::kCONT)
		{
			return false;
		}

		if (IsSpecialContainerEditorID(GetFormEditorIDOrEmpty(form)))
		{
			return true;
		}

		return IsSpecialContainerEditorID(GetFormEditorIDOrEmpty(ref));
	}

	struct WorldPickupDeleteCallContext
	{
		TESObjectREFR* ref = nullptr;
		bool wantsDelete = true;
	};

	void InvokeSetWantsDeleteCall(void* opaque)
	{
		auto* context = static_cast<WorldPickupDeleteCallContext*>(opaque);
		context->ref->SetWantsDelete(context->wantsDelete);
	}

	bool TrySetWantsDeleteSafe(TESObjectREFR* ref, bool wantsDelete = true)
	{
		if (!ref)
		{
			return false;
		}

		WorldPickupDeleteCallContext context{
			ref,
			wantsDelete
		};
		return ExecuteSehCallSafe(&InvokeSetWantsDeleteCall, &context);
	}

	struct WorldPickupDisableCallContext
	{
		TESObjectREFR* ref = nullptr;
	};

	void InvokeDisableCall(void* opaque)
	{
		auto* context = static_cast<WorldPickupDisableCallContext*>(opaque);
		context->ref->Disable();
	}

	bool TryDisableSafe(TESObjectREFR* ref)
	{
		if (!ref)
		{
			return false;
		}

		WorldPickupDisableCallContext context{ ref };
		return ExecuteSehCallSafe(&InvokeDisableCall, &context);
	}

	using ReferenceStateFlagGetter = bool (*)(TESObjectREFR*, std::uint32_t);
	using ReferenceStateFlagSetter = void (*)(TESObjectREFR*, std::uint32_t);

	using ObjectEmptyStateSetter = void (*)(TESForm*, bool);

	struct GetOpenStateCallContext
	{
		const TESObjectREFR* ref = nullptr;
		BGSOpenCloseForm::OPEN_STATE state = BGSOpenCloseForm::OPEN_STATE::kNone;
	};

	struct OpenCloseBoolQueryCallContext
	{
		const TESObjectREFR* ref = nullptr;
		bool result = false;
	};

	struct GetEquipmentDataCallContext
	{
		ExtraDataList* extraDataList;
		std::vector<BGSMod::Attachment::Mod*>* buffer;
		EquipmentData data;
	};

	void InvokeGetEquipmentDataCall(void* opaque)
	{
		auto* context = static_cast<GetEquipmentDataCallContext*>(opaque);
		context->data = GetEquipmentData(context->extraDataList, context->buffer);
	}

	bool TryGetEquipmentDataSafe(ExtraDataList* extraDataList,
		std::vector<BGSMod::Attachment::Mod*>* buffer,
		EquipmentData& outData)
	{
		GetEquipmentDataCallContext context{
			extraDataList,
			buffer,
			{}
		};
		if (!ExecuteSehCallSafe(&InvokeGetEquipmentDataCall, &context))
		{
			return false;
		}
		outData = context.data;
		return true;
	}

	// ---- Instance data ----

	TBO_InstanceData* GetInstanceData(const ExtraDataList* extraDataList)
	{
		if (!extraDataList) return nullptr;

		auto instanceData = extraDataList->GetByType<ExtraInstanceData>();
		if (!instanceData) return nullptr;

		return instanceData->data.get();
	}

	TBO_InstanceData* GetInstanceData(const TESObjectREFR* ref)
	{
		if (!ref || !ref->extraList) return nullptr;

		return GetInstanceData(ref->extraList.get());
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

	bool MatchesAnyCached(const TESForm* form, const injection_data::Key& key, MatchCache* cache)
	{
		if (!cache)
		{
			return MatchesAny(form, key);
		}
		if (!form)
		{
			return false;
		}

		const auto cacheKey =
			(static_cast<std::uint64_t>(static_cast<std::uint32_t>(key)) << 32) |
			static_cast<std::uint64_t>(form->formID);
		const auto it = cache->results.find(cacheKey);
		if (it != cache->results.end())
		{
			return it->second;
		}

		const bool matched = MatchesAny(form, key);
		cache->results.emplace(cacheKey, matched);
		return matched;
	}

	// ---- Item type classification ----

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

	bool IsDeferredActivationAmmoCandidate(TESObjectREFR* ref, TESForm* baseForm = nullptr)
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

		// Check extra flags (activation block)
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

	// ---- Form validation ----

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

	// ---- Inventory item helpers ----

	InventoryItemInfo GetInventoryItemInfo(const BGSInventoryItem& item,
		std::vector<BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags = inventory_info_full)
	{
		InventoryItemInfo result{};
		const bool includeEquipment = (infoFlags & inventory_info_equipment) != 0;
		const bool includeQuest = (infoFlags & inventory_info_quest) != 0;

		for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get())
		{
			result.totalCount += static_cast<std::int32_t>(stack->count);

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

			if (includeEquipment && !(result.featured && result.unscrappable && result.legendary))
			{
				auto data = GetEquipmentData(stack->extra.get(), &buffer);
				if (data.isFeaturedItem) result.featured = true;
				if (data.isUnscrappable) result.unscrappable = true;
				if (data.isLegendary) result.legendary = true;
			}

			if (includeQuest && !result.questItem && IsQuestItem(stack->extra.get()))
			{
				result.questItem = true;
			}
		}

		return result;
	}

	InventoryItemInfo GetInventoryStackInfo(const BGSInventoryItem::Stack& stack,
		std::vector<BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags = inventory_info_full)
	{
		InventoryItemInfo result{};
		const bool includeEquipment = (infoFlags & inventory_info_equipment) != 0;
		const bool includeQuest = (infoFlags & inventory_info_quest) != 0;

		result.totalCount = static_cast<std::int32_t>(stack.count);
		if (stack.flags.any(BGSInventoryItem::Stack::Flag::kTemporary))
		{
			result.dropped = true;
		}
		if (stack.flags.any(BGSInventoryItem::Stack::Flag::kSlotIndex1,
		                    BGSInventoryItem::Stack::Flag::kSlotIndex2,
		                    BGSInventoryItem::Stack::Flag::kSlotIndex3))
		{
			result.equipped = true;
		}
		if (includeEquipment)
		{
			auto data = GetEquipmentData(stack.extra.get(), &buffer);
			result.featured = data.isFeaturedItem;
			result.unscrappable = data.isUnscrappable;
			result.legendary = data.isLegendary;
		}
		if (includeQuest)
		{
			result.questItem = IsQuestItem(stack.extra.get());
		}

		return result;
	}

	InventoryItemInfo BuildFallbackStackInfo(const BGSInventoryItem::Stack& stack)
	{
		InventoryItemInfo result{};
		result.totalCount = static_cast<std::int32_t>(stack.count);
		if (stack.flags.any(BGSInventoryItem::Stack::Flag::kTemporary))
		{
			result.dropped = true;
		}
		if (stack.flags.any(BGSInventoryItem::Stack::Flag::kSlotIndex1,
		                    BGSInventoryItem::Stack::Flag::kSlotIndex2,
		                    BGSInventoryItem::Stack::Flag::kSlotIndex3))
		{
			result.equipped = true;
		}
		return result;
	}

	bool TryGetInventoryStackInfoSafe(const BGSInventoryItem::Stack& stack,
		std::vector<BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags,
		InventoryItemInfo& outInfo)
	{
#if defined(_MSC_VER)
		__try
		{
			outInfo = GetInventoryStackInfo(stack, buffer, infoFlags);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outInfo = GetInventoryStackInfo(stack, buffer, infoFlags);
		return true;
#endif
	}

	bool TryCreateInventoryListSafe(TESObjectREFR* ref, const TESContainer* container)
	{
		if (!ref || !container)
		{
			return false;
		}

#if defined(_MSC_VER)
		__try
		{
			ref->CreateInventoryList(container);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		ref->CreateInventoryList(container);
		return true;
#endif
	}

	bool EnsureContainerInventoryListForLootScan(TESObjectREFR* ref, TESForm* baseForm)
	{
		if (!ref || !baseForm || baseForm->GetFormType() != ENUM_FORM_ID::kCONT)
		{
			return false;
		}

		if (ref->inventoryList)
		{
			return true;
		}

		auto* containerForm = baseForm->As<TESObjectCONT>();
		if (!containerForm)
		{
			return false;
		}

		const auto* container = static_cast<const TESContainer*>(containerForm);
		if (!TryCreateInventoryListSafe(ref, container))
		{
			REX::WARN(
				"ContainerLoot scan: failed to create inventory list for ref={:08X}, base={:08X}",
				ref->formID,
				baseForm->formID);
			return false;
		}

		return ref->inventoryList != nullptr;
	}

	struct ReferenceItemCountCallContext
	{
		TESObjectREFR* ref = nullptr;
		TESBoundObject* object = nullptr;
		std::uint32_t count = 0;
		bool result = false;
	};

	void InvokeGetReferenceItemCountCall(void* opaque)
	{
		auto* context = static_cast<ReferenceItemCountCallContext*>(opaque);
		context->result = context->ref->GetItemCount(context->count, context->object, false);
	}

	bool TryGetReferenceItemCountSafe(
		TESObjectREFR* ref,
		TESBoundObject* object,
		std::int32_t& outCount)
	{
		if (!ref || !object)
		{
			return false;
		}

		ReferenceItemCountCallContext context{
			ref,
			object,
			0,
			false
		};
		if (!ExecuteSehCallSafe(&InvokeGetReferenceItemCountCall, &context) || !context.result)
		{
			return false;
		}

		outCount = static_cast<std::int32_t>(std::min<std::uint32_t>(
			context.count,
			static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())));
		return true;
	}

	struct ContainerWeightCallContext
	{
		TESObjectREFR* ref = nullptr;
		float weight = 0.0F;
	};

	void InvokeContainerWeightCall(void* opaque)
	{
		auto* context = static_cast<ContainerWeightCallContext*>(opaque);
		context->weight = context->ref->GetWeightInContainer();
	}

	bool TryGetContainerWeightSafe(TESObjectREFR* ref, float& outWeight)
	{
		if (!ref)
		{
			return false;
		}

		ContainerWeightCallContext context{ ref, 0.0F };
		if (!ExecuteSehCallSafe(&InvokeContainerWeightCall, &context) ||
			!std::isfinite(context.weight) ||
			context.weight < 0.0F)
		{
			return false;
		}

		outWeight = context.weight;
		return true;
	}

	struct FormWeightCallContext
	{
		TESBoundObject* object = nullptr;
		TBO_InstanceData* instanceData = nullptr;
		float weight = 0.0F;
	};

	void InvokeFormWeightCall(void* opaque)
	{
		auto* context = static_cast<FormWeightCallContext*>(opaque);
		context->weight = TESWeightForm::GetFormWeight(context->object, context->instanceData);
	}

	bool TryGetItemUnitWeightSafe(TESBoundObject* object, TBO_InstanceData* instanceData, float& outWeight)
	{
		if (!object)
		{
			return false;
		}

		FormWeightCallContext context{ object, instanceData, 0.0F };
		if (!ExecuteSehCallSafe(&InvokeFormWeightCall, &context) ||
			!std::isfinite(context.weight) ||
			context.weight < 0.0F)
		{
			return false;
		}

		outWeight = context.weight;
		return true;
	}

	float GetActorCarryWeight(TESObjectREFR* actorRef)
	{
		auto* actor = actorRef ? actorRef->As<Actor>() : nullptr;
		auto* actorValues = ActorValue::GetSingleton();
		if (!actor || !actorValues || !actorValues->carryWeight)
		{
			return 0.0F;
		}

		return actor->GetActorValue(*actorValues->carryWeight);
	}

	struct LootCapacityContext
	{
		bool enabled = false;
		bool valid = false;
		float projectedWeight = 0.0F;
		float limit = 0.0F;

		bool CanAccept(float unitWeight, std::int32_t count, float& outWeight) const
		{
			outWeight = 0.0F;
			if (!enabled)
			{
				return true;
			}
			if (!valid || count <= 0 || !std::isfinite(unitWeight) || unitWeight < 0.0F)
			{
				return false;
			}

			outWeight = unitWeight * static_cast<float>(count);
			if (!std::isfinite(outWeight) || outWeight < 0.0F)
			{
				return false;
			}

			return projectedWeight + outWeight <= limit;
		}

		void Accept(float weight)
		{
			if (enabled && valid)
			{
				projectedWeight += weight;
			}
		}
	};

	LootCapacityContext BuildLootCapacityContext(
		TESObjectREFR* player,
		TESObjectREFR* pendingContainer,
		TESObjectREFR* workshop)
	{
		LootCapacityContext context;
		if (properties::GetBool(properties::ignore_overweight, true))
		{
			return context;
		}

		context.enabled = true;
		const bool deliverToPlayer = properties::GetBool(properties::loot_is_deliver_to_player, false);
		TESObjectREFR* target = deliverToPlayer ? player : workshop;
		context.limit = deliverToPlayer
			? GetActorCarryWeight(player)
			: static_cast<float>(properties::GetInt(properties::carry_weight, 1000));

		float targetWeight = 0.0F;
		float pendingWeight = 0.0F;
		context.valid =
			target &&
			context.limit > 0.0F &&
			TryGetContainerWeightSafe(target, targetWeight) &&
			TryGetContainerWeightSafe(pendingContainer, pendingWeight);
		context.projectedWeight = targetWeight + pendingWeight;
		return context;
	}

	LootCapacityContext BuildDirectTransferCapacityContext(TESObjectREFR* dest)
	{
		LootCapacityContext context;
		if (properties::GetBool(properties::ignore_overweight, true))
		{
			return context;
		}

		const bool deliverToPlayer = properties::GetBool(properties::loot_is_deliver_to_player, false);
		if (deliverToPlayer != (dest && dest->IsPlayerRef()))
		{
			return context;
		}

		context.enabled = true;
		context.limit = deliverToPlayer
			? GetActorCarryWeight(dest)
			: static_cast<float>(properties::GetInt(properties::carry_weight, 1000));

		float targetWeight = 0.0F;
		context.valid =
			dest &&
			context.limit > 0.0F &&
			TryGetContainerWeightSafe(dest, targetWeight);
		context.projectedWeight = targetWeight;
		return context;
	}

	bool IsValidInventoryItem(const TESForm* form, const InventoryItemInfo& info, MatchCache* matchCache = nullptr)
	{
		if (info.dropped) return false;
		if (info.featured && !info.legendary &&
		    !MatchesAnyCached(form, injection_data::include_featured_item, matchCache)) return false;
		if (info.questItem && !MatchesAnyCached(form, injection_data::include_quest_item, matchCache)) return false;
		return true;
	}

	bool IsLootableInventoryItem(const TESForm* form, const InventoryItemInfo& info,
		const PropertiesSnapshot* props = nullptr)
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

	// ---- Lootable form / object validation ----

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

	// ---- Object lock / release ----

	std::mutex objectsLock;
	std::mutex recentWorldLootLock;

	struct LockedObjectEntry
	{
		NiPointer<TESObjectREFR> ref;
		Clock::time_point lockedAt;
	};

	std::unordered_map<std::uint32_t, LockedObjectEntry> lockedObjects;
	std::unordered_set<std::uint32_t> recentlyLootedWorldRefs;
	Clock::time_point lastLockedObjectCleanupAt{};

	inline constexpr auto kLockedObjectStaleTimeout = std::chrono::minutes(5);
	inline constexpr auto kLockedObjectCleanupInterval = std::chrono::seconds(1);
	inline constexpr std::size_t kLockedObjectCleanupMaxPerPass = 32;

	std::uint32_t GetRecentlyLootedWorldRefKey(const TESObjectREFR* ref)
	{
		if (!ref)
		{
			return 0;
		}

		if (!ref->IsCreated())
		{
			return ref->formID;
		}

		auto* mutableRef = const_cast<TESObjectREFR*>(ref);
		auto handle = mutableRef->GetHandle();
		auto handleKey = handle.get_handle();
		if (handleKey != 0)
		{
			return handleKey;
		}
		return ref->formID;
	}

	bool IsRecentlyLootedWorldRef(std::uint32_t formId)
	{
		std::lock_guard<std::mutex> guard(recentWorldLootLock);
		return recentlyLootedWorldRefs.find(formId) != recentlyLootedWorldRefs.end();
	}

	bool TryMarkRecentlyLootedWorldRef(std::uint32_t formId)
	{
		std::lock_guard<std::mutex> guard(recentWorldLootLock);
		return recentlyLootedWorldRefs.insert(formId).second;
	}

	bool IsRecentlyLootedWorldRef(const TESObjectREFR* ref)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0)
		{
			return false;
		}
		return IsRecentlyLootedWorldRef(key);
	}

	bool TryMarkRecentlyLootedWorldRef(TESObjectREFR* ref)
	{
		auto key = GetRecentlyLootedWorldRefKey(ref);
		if (key == 0)
		{
			return false;
		}
		return TryMarkRecentlyLootedWorldRef(key);
	}

	bool IsPapyrusObjectHandleAvailable(TESObjectREFR* ref)
	{
		if (!ref)
		{
			return false;
		}

		auto* gameVM = RE::GameVM::GetSingleton();
		auto vm = gameVM ? gameVM->GetVM().get() : nullptr;
		if (!vm)
		{
			return false;
		}

		const auto vmTypeID = RE::BSScript::GetVMTypeID<TESObjectREFR>();
		auto& handles = vm->GetObjectHandlePolicy();
		auto handle = handles.GetHandleForObject(vmTypeID, ref);
		if (handle == handles.EmptyHandle() || !handles.IsHandleLoaded(handle))
		{
			return false;
		}

		return true;
	}

	void CleanupStaleLockedObjects()
	{
		const auto now = Clock::now();
		std::size_t removedCount = 0;

		{
			std::lock_guard<std::mutex> guard(objectsLock);
			if (lastLockedObjectCleanupAt.time_since_epoch().count() != 0 &&
				(now - lastLockedObjectCleanupAt) < kLockedObjectCleanupInterval)
			{
				return;
			}
			lastLockedObjectCleanupAt = now;

			for (auto it = lockedObjects.begin();
				 it != lockedObjects.end() && removedCount < kLockedObjectCleanupMaxPerPass;)
			{
				const auto& entry = it->second;
				if ((now - entry.lockedAt) < kLockedObjectStaleTimeout)
				{
					++it;
					continue;
				}

				it = lockedObjects.erase(it);
				++removedCount;
			}
		}

		if (removedCount > 0)
		{
			REX::DEBUG("Released stale LootMan object locks: count={}", removedCount);
		}
	}

	bool TryLockObject(TESObjectREFR* obj)
	{
		if (!obj)
		{
			return false;
		}

		CleanupStaleLockedObjects();
		auto formId = obj->formID;
		std::lock_guard<std::mutex> guard(objectsLock);
		return lockedObjects.emplace(formId, LockedObjectEntry{ obj, Clock::now() }).second;
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

	template <typename FilterFn>
	void CollectFromCells(
		TESObjectREFR* actorRef,
		const NiPoint3& origin,
		float maxDistanceSq,
		bool notLootingFromSettlement,
		FilterFn&& filter,
		std::vector<ObjectEntry>& buffer)
	{
		auto collect = [&](TESObjectCELL* cell)
		{
			if (!cell) return;
			if (cell->cellState != TESObjectCELL::CELL_STATE::kAttached) return;
			if (!IsOwnerEmptyOrFriend(cell->GetOwner()))
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
				if (!obj)
				{
					continue;
				}
				auto* baseObj = obj->GetObjectReference();
				if (!baseObj)
				{
					continue;
				}
				if (!filter(baseObj->GetFormType()))
				{
					continue;
				}
				if (!CheckPrecondition(obj))
				{
					continue;
				}

				auto pos = obj->GetPosition();
				auto dx = origin.x - pos.x;
				auto dy = origin.y - pos.y;
				auto dz = origin.z - pos.z;
				auto distSq = dx * dx + dy * dy + dz * dz;
				if (distSq > 0.0f && distSq <= maxDistanceSq)
				{
					buffer.push_back({ objPtr, distSq });
				}
			}
		};

		auto parentCell = actorRef ? actorRef->GetParentCell() : nullptr;
		if (parentCell && parentCell->IsInterior())
		{
			collect(parentCell);
		}
		else
		{
			auto* tes = TES::GetSingleton();
			auto* gridCells = tes ? tes->gridCells : nullptr;
			bool collectedAnyCell = false;

			if (gridCells && gridCells->dimension > 0)
			{
				auto dim = gridCells->dimension;
				for (std::uint32_t y = 0; y < dim; ++y)
				{
					for (std::uint32_t x = 0; x < dim; ++x)
					{
						auto* gridCell = gridCells->Get(x, y);
						auto* loadedCell = gridCell ? gridCell->cell : nullptr;
						if (!loadedCell) continue;

						collect(loadedCell);
						collectedAnyCell = true;
					}
				}
			}

			if (!collectedAnyCell && parentCell)
			{
				collect(parentCell);
			}
		}
	}

	// ================================================================
	// Papyrus native functions (10 v2.2.0 functions)
	// ================================================================

	std::vector<TESObjectREFR*> FindNearbyReferencesWithFormTypeImpl(
		TESObjectREFR* ref, std::uint32_t formType, bool requirePapyrusObjectHandle)
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

		EnsureItemTypeCache();
		auto propsSnapshot = PropertiesSnapshot::Capture();
		auto origin = ref->GetPosition();
		auto lootingRange = std::clamp(properties::GetFloat(properties::looting_range), 0.0f, 200.0f);
		auto maxDistance = lootingRange * 100.0f;
		auto maxDistanceSq = maxDistance * maxDistance;
		auto notLootingFromSettlement = propsSnapshot.notLootingFromSettlement;
		auto maxItemsRaw = properties::GetInt(properties::max_items_processed_per_thread);
		auto maxItemsClamped = std::clamp(
			maxItemsRaw,
			1,
			static_cast<int>(kMaxItemsProcessedPerThreadLimit));
		auto maxItemsProcessedPerThread = static_cast<std::size_t>(maxItemsClamped);

		if (maxDistance < 1.0f)
		{
			return result;
		}

		auto matchType = static_cast<ENUM_FORM_ID>(formType);
		std::vector<ObjectEntry> buffer;
		buffer.reserve(1024);
		CollectFromCells(ref, origin, maxDistanceSq, notLootingFromSettlement,
			[matchType](ENUM_FORM_ID ft)
			{
				return IsFormTypeMatch(ft, matchType);
			},
			buffer);
		std::sort(buffer.begin(), buffer.end(),
			[](const ObjectEntry& lhs, const ObjectEntry& rhs)
			{
				return lhs.distanceSquared < rhs.distanceSquared;
			});

		std::vector<TESObjectREFR*> tmp;
		tmp.reserve(maxItemsProcessedPerThread);
		std::vector<BGSMod::Attachment::Mod*> equipmentBuffer;
		MatchCache matchCache;
		matchCache.results.reserve(buffer.size() * 2);
		for (const auto& entry : buffer)
		{
			auto* obj = entry.obj.get();
			if (!obj) continue;

			auto* baseObj = obj->GetObjectReference();
			if (!baseObj) continue;
			if (UsesWorldReferenceTransfer(matchType) && IsRecentlyLootedWorldRef(obj))
			{
				continue;
			}

			bool validForm = false;
			const bool gotValidForm = TryIsValidFormSafe(
				baseObj, &propsSnapshot, &matchCache, validForm);
			if (!gotValidForm)
			{
				continue;
			}
			if (!validForm)
			{
				continue;
			}

			bool lootableForm = false;
			const bool gotLootableForm = TryIsLootableFormSafe(
				baseObj, &propsSnapshot, &matchCache, lootableForm);
			if (!gotLootableForm)
			{
				continue;
			}
			if (!lootableForm)
			{
				continue;
			}

			bool validObject = false;
			const bool gotValidObject = TryIsValidObjectSafe(
				obj, &propsSnapshot, baseObj, &matchCache, validObject);
			if (!gotValidObject)
			{
				continue;
			}
			if (!validObject)
			{
				continue;
			}

			bool lootableObject = false;
			const bool gotLootableObject = TryIsLootableObjectSafe(
				obj,
				&propsSnapshot,
				baseObj,
				&equipmentBuffer,
				&matchCache,
				lootableObject);
			if (!gotLootableObject)
			{
				continue;
			}
			if (!lootableObject)
			{
				continue;
			}

			if (requirePapyrusObjectHandle && !IsPapyrusObjectHandleAvailable(obj))
			{
				continue;
			}

			if (TryLockObject(obj))
			{
				tmp.push_back(obj);
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

	// 1. FindNearbyReferencesWithFormType
	std::vector<TESObjectREFR*> FindNearbyReferencesWithFormType(
		std::monostate, TESObjectREFR* ref, std::uint32_t formType)
	{
		return FindNearbyReferencesWithFormTypeImpl(ref, formType, true);
	}

	std::vector<std::int32_t> FindNearbyReferenceIdsWithFormType(
		std::monostate, TESObjectREFR* ref, std::uint32_t formType)
	{
		auto refs = FindNearbyReferencesWithFormTypeImpl(ref, formType, false);
		std::vector<std::int32_t> ids;
		ids.reserve(refs.size());
		for (auto* obj : refs)
		{
			if (!obj)
			{
				continue;
			}

			ids.push_back(static_cast<std::int32_t>(obj->formID));
		}
		return ids;
	}

	// 2. GetEquipmentComponents
	// Returns an array of MiscComponent structs (Papyrus struct with "object" and "count" fields).

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
		EnsureItemTypeCache();

		bool isPlayer = inventoryOwner->IsPlayerRef();
		bool isDead = IsDeadForLooting(inventoryOwner);

		auto inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList) return result;

		MatchCache matchCache;
		matchCache.results.reserve(inventoryList->data.size());
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

			auto info = GetInventoryItemInfo(item, modBuffer, inventory_info_quest);
			if ((info.questItem && !MatchesAnyCached(form, injection_data::include_quest_item, &matchCache))
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
		EnsureItemTypeCache();

		auto inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList) return result;

		auto propsSnapshot = PropertiesSnapshot::Capture();
		MatchCache matchCache;
		matchCache.results.reserve(inventoryList->data.size() * 2);
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		ReadLockGuard guard(inventoryList->rwLock);
		result.reserve(inventoryList->data.size());
		modBuffer.reserve(8);
		for (auto& item : inventoryList->data)
		{
			auto form = item.object;
			if (!form) continue;

			if (!IsFormTypeMatchesItemType(form->GetFormType(), itemType)) continue;
			bool validForm = false;
			const bool gotValidForm = TryIsValidFormSafe(
				form,
				&propsSnapshot,
				&matchCache,
				validForm);
			if (!gotValidForm)
			{
				REX::WARN("GetLootableItems: skip {:08X}: valid-form-exception", form->formID);
				continue;
			}
			if (!validForm) continue;

			bool lootableForm = false;
			const bool gotLootableForm = TryIsLootableFormSafe(
				form,
				&propsSnapshot,
				&matchCache,
				lootableForm);
			if (!gotLootableForm)
			{
				REX::WARN("GetLootableItems: skip {:08X}: lootable-form-exception", form->formID);
				continue;
			}
			if (!lootableForm) continue;

			auto info = GetInventoryItemInfo(item, modBuffer, inventory_info_full);
			if (!IsValidInventoryItem(form, info, &matchCache) ||
			    !IsLootableInventoryItem(form, info, &propsSnapshot))
			{
				continue;
			}

			result.push_back(form);
		}

		return result;
	}

	struct InventoryTransferRequest
	{
		TESBoundObject* object = nullptr;
		std::uint32_t stackIndex = 0;
		std::int32_t count = 0;
		float unitWeight = 0.0F;
	};

	struct InventoryFormTransferRequest
	{
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
		float unitWeight = 0.0F;
	};

	void PlayPickUpSound(std::monostate, TESObjectREFR* player, TESObjectREFR* obj);
	void FinalizeWorldPickup(std::monostate, TESObjectREFR* ref);

	struct WorldReferenceAddCallContext
	{
		TESObjectREFR* dest = nullptr;
		TESBoundObject* object = nullptr;
		BSTSmartPointer<ExtraDataList> extra;
		TESObjectREFR* oldContainer = nullptr;
		std::int32_t count = 1;
	};

	void InvokeWorldReferenceAddCall(void* opaque)
	{
		auto* context = static_cast<WorldReferenceAddCallContext*>(opaque);
		context->dest->AddObjectToContainer(
			context->object,
			context->extra,
			context->count,
			context->oldContainer,
			ITEM_REMOVE_REASON::kStoreContainer);
	}

	bool TryAddWorldReferenceToContainerSafe(TESObjectREFR* dest, TESObjectREFR* ref)
	{
		if (!dest || !ref || dest == ref)
		{
			return false;
		}

		auto* object = ref->GetObjectReference();
		if (!object)
		{
			return false;
		}

		WorldReferenceAddCallContext context{
			dest,
			object,
			ref->extraList,
			ref,
			1
		};
		return ExecuteSehCallSafe(&InvokeWorldReferenceAddCall, &context);
	}

	struct ActivateRefCallContext
	{
		TESObjectREFR* ref = nullptr;
		TESObjectREFR* actionRef = nullptr;
		bool defaultProcessingOnly = false;
		bool result = false;
	};

	void InvokeActivateRefCall(void* opaque)
	{
		auto* context = static_cast<ActivateRefCallContext*>(opaque);
		context->result = context->ref->ActivateRef(
			context->actionRef,
			nullptr,
			1,
			context->defaultProcessingOnly,
			true,
			false);
	}

	bool TryActivateRefSafe(TESObjectREFR* ref, TESObjectREFR* actionRef, bool defaultProcessingOnly)
	{
		if (!ref || !actionRef)
		{
			return false;
		}

		ActivateRefCallContext context{
			ref,
			actionRef,
			defaultProcessingOnly,
			false
		};
		return ExecuteSehCallSafe(&InvokeActivateRefCall, &context) && context.result;
	}

	struct RemoveItemsCallContext
	{
		TESObjectREFR* ref = nullptr;
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
	};

	void InvokeRemoveItemsCall(void* opaque)
	{
		auto* context = static_cast<RemoveItemsCallContext*>(opaque);
		TESObjectREFR::RemoveItemData removeData(context->object, context->count);
		context->ref->RemoveItem(removeData);
	}

	bool TryRemoveItemsSafe(TESObjectREFR* ref, TESBoundObject* object, std::int32_t count)
	{
		if (!ref || !object || count <= 0)
		{
			return count == 0;
		}

		RemoveItemsCallContext context{
			ref,
			object,
			count
		};
		return ExecuteSehCallSafe(&InvokeRemoveItemsCall, &context);
	}

	struct UnlockReferenceCallContext
	{
		TESObjectREFR* ref = nullptr;
		REFR_LOCK* lock = nullptr;
	};

	void InvokeUnlockReferenceCall(void* opaque)
	{
		auto* context = static_cast<UnlockReferenceCallContext*>(opaque);
		context->lock->SetLocked(false);
		context->ref->AddLockChange();
	}

	bool TrySetReferenceUnlockedSafe(TESObjectREFR* ref, REFR_LOCK* lock)
	{
		if (!ref || !lock)
		{
			return false;
		}

		UnlockReferenceCallContext context{
			ref,
			lock
		};
		return ExecuteSehCallSafe(&InvokeUnlockReferenceCall, &context);
	}

	bool IsReferenceLockedForLooting(TESObjectREFR* ref)
	{
		auto* lock = ref ? ref->GetLock() : nullptr;
		if (!lock)
		{
			return false;
		}

		return lock->GetLockLevel(ref) != LOCK_LEVEL::kUnlocked;
	}

	bool HasPerk(Actor* actor, BGSPerk* perk)
	{
		return actor && perk && actor->GetPerkRank(perk) > 0;
	}

	std::int32_t GetRequiredBobbyPinCount(
		LOCK_LEVEL level,
		Actor* player,
		BGSPerk* locksmith01,
		BGSPerk* locksmith02,
		BGSPerk* locksmith03,
		BGSPerk* locksmith04)
	{
		std::int32_t consumeCount = -1;
		switch (level)
		{
		case LOCK_LEVEL::kVeryHard:
			if (HasPerk(player, locksmith03)) consumeCount = 4;
			break;
		case LOCK_LEVEL::kHard:
			if (HasPerk(player, locksmith02)) consumeCount = 3;
			break;
		case LOCK_LEVEL::kAverage:
			if (HasPerk(player, locksmith01)) consumeCount = 2;
			break;
		case LOCK_LEVEL::kEasy:
			consumeCount = 1;
			break;
		case LOCK_LEVEL::kUnlocked:
			consumeCount = 0;
			break;
		default:
			break;
		}

		if (consumeCount > 0 && HasPerk(player, locksmith04))
		{
			consumeCount = 0;
		}

		return consumeCount;
	}

	bool TryUnlockContainerWithPins(
		TESObjectREFR* ref,
		TESObjectREFR* bobbyPinContainerRef,
		Actor* player,
		TESForm* bobbyPinForm,
		BGSPerk* locksmith01,
		BGSPerk* locksmith02,
		BGSPerk* locksmith03,
		BGSPerk* locksmith04,
		bool unlockLockedContainer)
	{
		if (!ref || !unlockLockedContainer)
		{
			return false;
		}

		auto* lock = ref->GetLock();
		if (!lock || lock->IsBroken())
		{
			return false;
		}

		const auto lockLevel = lock->GetLockLevel(ref);
		const auto consumeCount = GetRequiredBobbyPinCount(
			lockLevel,
			player,
			locksmith01,
			locksmith02,
			locksmith03,
			locksmith04);
		if (consumeCount < 0)
		{
			return false;
		}

		auto* bobbyPin = bobbyPinForm ? bobbyPinForm->As<TESBoundObject>() : nullptr;
		std::int32_t bobbyPinCount = 0;
		const bool gotBobbyPinCount = consumeCount == 0 ||
			(bobbyPinContainerRef && bobbyPin &&
			 TryGetReferenceItemCountSafe(bobbyPinContainerRef, bobbyPin, bobbyPinCount));
		if (!gotBobbyPinCount || bobbyPinCount < consumeCount)
		{
			return false;
		}

		if (consumeCount > 0 && !TryRemoveItemsSafe(bobbyPinContainerRef, bobbyPin, consumeCount))
		{
			return false;
		}

		return TrySetReferenceUnlockedSafe(ref, lock);
	}

	bool TryUnlockContainerForLooting(
		TESObjectREFR* ref,
		TESObjectREFR* playerRef,
		TESObjectREFR* workshopRef,
		TESForm* bobbyPin,
		BGSPerk* locksmith01,
		BGSPerk* locksmith02,
		BGSPerk* locksmith03,
		BGSPerk* locksmith04,
		bool unlockLockedContainer)
	{
		auto* player = playerRef ? playerRef->As<Actor>() : nullptr;
		return TryUnlockContainerWithPins(
				ref,
				playerRef,
				player,
				bobbyPin,
				locksmith01,
				locksmith02,
				locksmith03,
				locksmith04,
				unlockLockedContainer) ||
			TryUnlockContainerWithPins(
				ref,
				workshopRef,
				player,
				bobbyPin,
				locksmith01,
				locksmith02,
				locksmith03,
				locksmith04,
				unlockLockedContainer);
	}

	bool TryLootWorldReference(
		TESObjectREFR* ref,
		TESObjectREFR* dest,
		TESObjectREFR* player,
		bool playPickupSound,
		LootCapacityContext* capacity = nullptr)
	{
		auto* object = ref ? ref->GetObjectReference() : nullptr;
		if (!ref || !dest || !object)
		{
			return false;
		}

		float acceptedWeight = 0.0F;
		float unitWeight = 0.0F;
		if (capacity && capacity->enabled)
		{
			if (!TryGetItemUnitWeightSafe(object, GetInstanceData(ref), unitWeight) ||
				!capacity->CanAccept(unitWeight, 1, acceptedWeight))
			{
				return false;
			}
		}

		std::int32_t beforeCount = 0;
		const bool gotBefore = TryGetReferenceItemCountSafe(dest, object, beforeCount);
		if (playPickupSound)
		{
			PlayPickUpSound(std::monostate{}, player, ref);
		}

		if (!TryAddWorldReferenceToContainerSafe(dest, ref))
		{
			REX::WARN(
				"LootNearbyReferences: failed to add world ref={:08X}, base={:08X} to dest={:08X}",
				ref->formID,
				object->formID,
				dest->formID);
			return false;
		}

		std::int32_t afterCount = 0;
		const bool gotAfter = TryGetReferenceItemCountSafe(dest, object, afterCount);
		if (gotBefore && gotAfter && afterCount > beforeCount)
		{
			FinalizeWorldPickup(std::monostate{}, ref);
			if (capacity)
			{
				capacity->Accept(acceptedWeight);
			}
			return true;
		}

		REX::WARN(
			"LootNearbyReferences: no observed world transfer for ref={:08X}, base={:08X}, before={}, after={}, gotBefore={}, gotAfter={}",
			ref->formID,
			object->formID,
			beforeCount,
			afterCount,
			gotBefore,
			gotAfter);
		return false;
	}

	bool TryLootActivationReference(
		TESObjectREFR* ref,
		TESObjectREFR* actionRef,
		TESObjectREFR* player,
		bool playPickupSound,
		LootCapacityContext* capacity = nullptr)
	{
		TESBoundObject* expectedItem = nullptr;
		float acceptedWeight = 0.0F;
		if (capacity && capacity->enabled)
		{
			auto* flora = ref && ref->GetObjectReference()
				? ref->GetObjectReference()->As<TESFlora>()
				: nullptr;
			expectedItem = flora ? flora->produceItem : nullptr;
			float unitWeight = 0.0F;
			if (!expectedItem ||
				!TryGetItemUnitWeightSafe(expectedItem, nullptr, unitWeight) ||
				!capacity->CanAccept(unitWeight, 1, acceptedWeight))
			{
				return false;
			}
		}

		std::int32_t beforeCount = 0;
		const bool gotBefore = expectedItem && TryGetReferenceItemCountSafe(actionRef, expectedItem, beforeCount);
		if (!TryActivateRefSafe(ref, actionRef, false))
		{
			return false;
		}

		if (playPickupSound)
		{
			PlayPickUpSound(std::monostate{}, player, ref);
		}
		if (capacity && expectedItem)
		{
			std::int32_t afterCount = 0;
			const bool gotAfter = TryGetReferenceItemCountSafe(actionRef, expectedItem, afterCount);
			if ((!gotBefore && !gotAfter) || (gotBefore && gotAfter && afterCount > beforeCount))
			{
				capacity->Accept(acceptedWeight);
			}
		}
		return true;
	}

	bool IsContainerAnimationCandidate(TESObjectREFR* ref)
	{
		return ref && ref->GetFullyLoaded3D() != nullptr;
	}

	bool IsExcludedByMiscSubtype(TESForm* form, std::int32_t subType, BGSKeyword* looseModKeyword)
	{
		if (!form || subType < 0 || form->GetFormType() != ENUM_FORM_ID::kMISC)
		{
			return false;
		}

		const bool isLooseModItem = looseModKeyword && HasKeyword(form, looseModKeyword);
		return (subType == 0 && isLooseModItem) || (subType == 1 && !isLooseModItem);
	}

	std::int32_t TransferInventoryItemsImpl(
		TESObjectREFR* src,
		TESObjectREFR* dest,
		std::uint32_t itemType,
		std::int32_t subType,
		BGSKeyword* looseModKeyword,
		LootCapacityContext* capacity = nullptr)
	{
		if (!src || !dest || src == dest || itemType > all_item)
		{
			return 0;
		}
		EnsureItemTypeCache();

		auto inventoryList = src->inventoryList;
		if (!inventoryList)
		{
			return 0;
		}

		const bool sourceIsPlayer = src->IsPlayerRef();
		const bool sourceIsDead = IsDeadForLooting(src);
		MatchCache matchCache;
		matchCache.results.reserve(inventoryList->data.size());
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		std::vector<InventoryFormTransferRequest> requests;
		requests.reserve(inventoryList->data.size());

		{
			ReadLockGuard guard(inventoryList->rwLock);
			for (auto& item : inventoryList->data)
			{
				auto* form = item.object;
				if (!form)
				{
					continue;
				}

				if (!IsPlayable(form) ||
					!IsFormTypeMatchesItemType(form->GetFormType(), itemType) ||
					IsExcludedByMiscSubtype(form, subType, looseModKeyword))
				{
					continue;
				}

				if (sourceIsPlayer)
				{
					if (form->formID == 0x0F || IsFavorite(form))
					{
						continue;
					}
				}

				auto info = GetInventoryItemInfo(item, modBuffer, inventory_info_quest);
				if ((info.questItem && !MatchesAnyCached(form, injection_data::include_quest_item, &matchCache)) ||
					info.dropped ||
					(info.equipped && !sourceIsDead) ||
					info.totalCount <= 0)
				{
					continue;
				}

				float unitWeight = 0.0F;
				if (capacity && capacity->enabled && !TryGetItemUnitWeightSafe(form, nullptr, unitWeight))
				{
					continue;
				}

				requests.push_back(InventoryFormTransferRequest{
					form,
					info.totalCount,
					unitWeight
				});
			}
		}

		std::int32_t movedItems = 0;
		for (const auto& request : requests)
		{
			if (!request.object || request.count <= 0)
			{
				continue;
			}

			float acceptedWeight = 0.0F;
			if (capacity && !capacity->CanAccept(request.unitWeight, request.count, acceptedWeight))
			{
				continue;
			}

			std::int32_t srcBefore = 0;
			std::int32_t destBefore = 0;
			const bool gotSrcBefore = TryGetReferenceItemCountSafe(src, request.object, srcBefore);
			const bool gotDestBefore = TryGetReferenceItemCountSafe(dest, request.object, destBefore);

			auto remaining = request.count;
			while (remaining > 0)
			{
				const auto chunk = std::min<std::int32_t>(remaining, 65535);
				TESObjectREFR::RemoveItemData removeData(request.object, chunk);
				removeData.reason = ITEM_REMOVE_REASON::kStoreContainer;
				removeData.a_otherContainer = dest;
				src->RemoveItem(removeData);
				remaining -= chunk;
			}

			std::int32_t srcAfter = 0;
			std::int32_t destAfter = 0;
			const bool gotSrcAfter = TryGetReferenceItemCountSafe(src, request.object, srcAfter);
			const bool gotDestAfter = TryGetReferenceItemCountSafe(dest, request.object, destAfter);
			const bool observedSourceReduction =
				gotSrcBefore && gotSrcAfter && srcAfter < srcBefore;
			const bool observedDestIncrease =
				gotDestBefore && gotDestAfter && destAfter > destBefore;
			const bool countUnavailable =
				!gotSrcBefore && !gotSrcAfter && !gotDestBefore && !gotDestAfter;

			if (observedSourceReduction || observedDestIncrease || countUnavailable)
			{
				++movedItems;
				if (capacity)
				{
					capacity->Accept(acceptedWeight);
				}
			}
			else
			{
				REX::WARN(
					"TransferInventoryItems: no observed transfer for item={:08X}, requestedCount={}, srcBefore={}, srcAfter={}, destBefore={}, destAfter={}, gotSrcBefore={}, gotSrcAfter={}, gotDestBefore={}, gotDestAfter={}",
					request.object->formID,
					request.count,
					srcBefore,
					srcAfter,
					destBefore,
					destAfter,
					gotSrcBefore,
					gotSrcAfter,
					gotDestBefore,
					gotDestAfter);
			}
		}

		return movedItems;
	}

	std::int32_t TransferLootableInventoryItemsImpl(
		TESObjectREFR* src,
		TESObjectREFR* dest,
		std::uint32_t itemType,
		LootCapacityContext* capacity = nullptr)
	{
		if (!src || !dest || src == dest || itemType > all_item)
		{
			return 0;
		}
		EnsureItemTypeCache();

		auto inventoryList = src->inventoryList;
		if (!inventoryList)
		{
			return 0;
		}
		auto propsSnapshot = PropertiesSnapshot::Capture();
		auto* sourceBase = src->GetObjectReference();
		const bool sourceIsNpc = sourceBase && sourceBase->GetFormType() == ENUM_FORM_ID::kNPC_;
		const bool sourceIsDead = IsDeadForLooting(src);
		if (sourceIsNpc && !sourceIsDead)
		{
			return 0;
		}

		MatchCache matchCache;
		matchCache.results.reserve(inventoryList->data.size() * 2);
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		modBuffer.reserve(8);
		std::vector<InventoryTransferRequest> requests;
		requests.reserve(inventoryList->data.size());

		{
			ReadLockGuard guard(inventoryList->rwLock);
			for (auto& item : inventoryList->data)
			{
				auto* form = item.object;
				if (!form)
				{
					continue;
				}

				if (!IsFormTypeMatchesItemType(form->GetFormType(), itemType))
				{
					continue;
				}
				const auto formType = form->GetFormType();

				bool validForm = false;
				const bool gotValidForm = TryIsValidFormSafe(
					form,
					&propsSnapshot,
					&matchCache,
					validForm);
				if (!gotValidForm || !validForm)
				{
					continue;
				}

				bool lootableForm = false;
				const bool gotLootableForm = TryIsLootableFormSafe(
					form,
					&propsSnapshot,
					&matchCache,
					lootableForm);
				if (!gotLootableForm || !lootableForm)
				{
					continue;
				}

				std::vector<InventoryTransferRequest> itemRequests;
				std::uint32_t stackIndex = 0;
				for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex)
				{
					InventoryItemInfo stackInfo{};
					bool gotStackInfo = TryGetInventoryStackInfoSafe(
						*stack,
						modBuffer,
						inventory_info_full,
						stackInfo);
					if (!gotStackInfo)
					{
						stackInfo = BuildFallbackStackInfo(*stack);
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
					if (!IsValidInventoryItem(form, stackInfo, &matchCache) ||
					    !IsLootableInventoryItem(form, stackInfo, &propsSnapshot))
					{
						continue;
					}

					float unitWeight = 0.0F;
					if (capacity && capacity->enabled &&
						!TryGetItemUnitWeightSafe(form, GetInstanceData(stack->extra.get()), unitWeight))
					{
						continue;
					}

					itemRequests.push_back(InventoryTransferRequest{
						form,
						stackIndex,
						resolvedCount,
						unitWeight
					});
				}

				for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
				{
					requests.push_back(*it);
				}
			}
		}

		std::int32_t movedStacks = 0;
		for (const auto& request : requests)
		{
			if (!request.object || request.count <= 0)
			{
				continue;
			}

			float acceptedWeight = 0.0F;
			if (capacity && !capacity->CanAccept(request.unitWeight, request.count, acceptedWeight))
			{
				continue;
			}

			std::int32_t srcBefore = 0;
			std::int32_t destBefore = 0;
			const bool gotSrcBefore = TryGetReferenceItemCountSafe(src, request.object, srcBefore);
			const bool gotDestBefore = TryGetReferenceItemCountSafe(dest, request.object, destBefore);

			TESObjectREFR::RemoveItemData removeData(request.object, request.count);
			removeData.reason = ITEM_REMOVE_REASON::kStoreContainer;
			removeData.a_otherContainer = dest;
			removeData.stackData.push_back(request.stackIndex);
			src->RemoveItem(removeData);

			std::int32_t srcAfter = 0;
			std::int32_t destAfter = 0;
			const bool gotSrcAfter = TryGetReferenceItemCountSafe(src, request.object, srcAfter);
			const bool gotDestAfter = TryGetReferenceItemCountSafe(dest, request.object, destAfter);
			const bool observedSourceReduction =
				gotSrcBefore && gotSrcAfter && srcAfter < srcBefore;
			const bool observedDestIncrease =
				gotDestBefore && gotDestAfter && destAfter > destBefore;
			const bool countUnavailable =
				!gotSrcBefore && !gotSrcAfter && !gotDestBefore && !gotDestAfter;

			if (observedSourceReduction || observedDestIncrease || countUnavailable)
			{
				++movedStacks;
				if (capacity)
				{
					capacity->Accept(acceptedWeight);
				}
			}
			else
			{
				REX::WARN(
					"TransferLootableInventoryItems: no observed transfer for item={:08X}, requestedCount={}, srcBefore={}, srcAfter={}, destBefore={}, destAfter={}, gotSrcBefore={}, gotSrcAfter={}, gotDestBefore={}, gotDestAfter={}",
					request.object->formID,
					request.count,
					srcBefore,
					srcAfter,
					destBefore,
					destAfter,
					gotSrcBefore,
					gotSrcAfter,
					gotDestBefore,
					gotDestAfter);
			}
		}

		return movedStacks;
	}

	// 6. TransferLootableInventoryItems
	std::int32_t TransferLootableInventoryItems(
		std::monostate, TESObjectREFR* src, TESObjectREFR* dest, std::uint32_t itemType)
	{
		return TransferLootableInventoryItemsImpl(src, dest, itemType);
	}

	std::int32_t TransferInventoryItems(
		std::monostate,
		TESObjectREFR* src,
		TESObjectREFR* dest,
		std::uint32_t itemType,
		std::int32_t subType,
		BGSKeyword* looseModKeyword,
		bool suppressPlayerMessages)
	{
		std::unique_lock<std::mutex> capacityGuard;
		if (!properties::GetBool(properties::ignore_overweight, true))
		{
			capacityGuard = std::unique_lock<std::mutex>(lootCapacityLock);
		}
		auto capacity = BuildDirectTransferCapacityContext(dest);
		if (dest && dest->IsPlayerRef() && suppressPlayerMessages)
		{
			PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
			return TransferInventoryItemsImpl(src, dest, itemType, subType, looseModKeyword, &capacity);
		}

		return TransferInventoryItemsImpl(src, dest, itemType, subType, looseModKeyword, &capacity);
	}

	std::int32_t LootNearbyReferences(
		std::monostate,
		TESObjectREFR* player,
		TESObjectREFR* dest,
		TESObjectREFR* activator,
		TESObjectREFR* workshop,
		std::uint32_t formType,
		std::uint32_t itemType,
		bool playPickupSound,
		bool playContainerAnimation,
		bool unlockLockedContainer,
		TESForm* bobbyPin,
		BGSPerk* locksmith01,
		BGSPerk* locksmith02,
		BGSPerk* locksmith03,
		BGSPerk* locksmith04)
	{
		if (!player || !dest)
		{
			return 0;
		}

		auto refs = FindNearbyReferencesWithFormTypeImpl(player, formType, false);
		if (refs.empty())
		{
			return 0;
		}

		// `FindNearbyReferencesWithFormTypeImpl` reverses the result for the
		// old Papyrus reverse loop. Native processing should preserve nearest
		// first ordering directly.
		std::reverse(refs.begin(), refs.end());

		const auto candidateCount = refs.size();
		const auto matchType = static_cast<ENUM_FORM_ID>(formType);
		std::unique_lock<std::mutex> capacityGuard;
		if (!properties::GetBool(properties::ignore_overweight, true))
		{
			capacityGuard = std::unique_lock<std::mutex>(lootCapacityLock);
		}
		auto capacity = BuildLootCapacityContext(player, dest, workshop);
		std::int32_t successfulObjects = 0;
		std::int32_t movedStacks = 0;

		for (auto* ref : refs)
		{
			const auto lockedFormId = ref ? ref->formID : 0;
			struct ReleaseGuard
			{
				std::uint32_t formId = 0;
				~ReleaseGuard()
				{
					if (formId != 0)
					{
						UnlockObject(formId);
					}
				}
			} releaseGuard{ lockedFormId };

			if (!ref)
			{
				continue;
			}

			auto* baseForm = ref->GetObjectReference();
			if (!baseForm)
			{
				continue;
			}

			const auto actualFormType = baseForm->GetFormType();
			if (actualFormType == ENUM_FORM_ID::kCONT)
			{
				if (IsReferenceLockedForLooting(ref) &&
					!TryUnlockContainerForLooting(
						ref,
						player,
						workshop,
						bobbyPin,
						locksmith01,
						locksmith02,
						locksmith03,
						locksmith04,
						unlockLockedContainer))
				{
					continue;
				}

				const auto moved = TransferLootableInventoryItemsImpl(
					ref,
					dest,
					itemType,
					&capacity);
				if (moved > 0)
				{
					++successfulObjects;
					movedStacks += moved;
					if (playContainerAnimation && activator && IsContainerAnimationCandidate(ref))
					{
						(void)TryActivateRefSafe(ref, activator, true);
					}
				}
				continue;
			}

			if (actualFormType == ENUM_FORM_ID::kNPC_)
			{
				const auto moved = TransferLootableInventoryItemsImpl(
					ref,
					dest,
					itemType,
					&capacity);
				if (moved > 0)
				{
					++successfulObjects;
					movedStacks += moved;
					if (playPickupSound)
					{
						PlayPickUpSound(std::monostate{}, player, ref);
					}
				}
				continue;
			}

			if (actualFormType == ENUM_FORM_ID::kACTI || actualFormType == ENUM_FORM_ID::kFLOR)
			{
				if (TryLootActivationReference(ref, dest, player, playPickupSound, &capacity))
				{
					++successfulObjects;
				}
				continue;
			}

			if (UsesWorldReferenceTransfer(matchType))
			{
				if (TryLootWorldReference(ref, dest, player, playPickupSound, &capacity))
				{
					++successfulObjects;
				}
			}
		}

		REX::DEBUG(
			"LootNearbyReferences: formType={}, candidates={}, successfulObjects={}, movedStacks={}",
			formType,
			candidateCount,
			successfulObjects,
			movedStacks);

		return static_cast<std::int32_t>(
			std::min<std::size_t>(
				candidateCount,
				static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));
	}

	// 7. GetScrappableItems
	std::vector<TESForm*> GetScrappableItems(
		std::monostate, TESObjectREFR* inventoryOwner, std::uint32_t itemType)
	{
		std::vector<TESForm*> result;

		if (!inventoryOwner || itemType > all_item) return result;
		EnsureItemTypeCache();

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

	// 13. FinalizeWorldPickup
	void FinalizeWorldPickup(std::monostate, TESObjectREFR* ref)
	{
		if (!ref)
		{
			return;
		}

		const auto markedRecent = TryMarkRecentlyLootedWorldRef(ref);
		const auto setWantsDeleteOk = TrySetWantsDeleteSafe(ref, true);
		const auto disableOk = TryDisableSafe(ref);
		if (!setWantsDeleteOk || !disableOk ||
			(!ref->IsDisabled() && !ref->IsDeleted()))
		{
			const auto* refName = ref->GetDisplayFullName();
			auto* baseForm = ref->GetObjectReference();
			std::string baseName;
			std::uint32_t baseFormId = 0;
			std::uint32_t baseFormType = 0;
			if (baseForm)
			{
				baseName = TESFullName::GetFullName(*baseForm);
				baseFormId = baseForm->formID;
				baseFormType = static_cast<std::uint32_t>(baseForm->GetFormType());
			}

			REX::WARN(
				"FinalizeWorldPickup incomplete: ref={:08X} \"{}\", base={:08X} \"{}\", baseFormType={}, markedRecent={}, setWantsDeleteOk={}, disableOk={}, disabled={}, deleted={}, created={}",
				ref->formID,
				refName ? refName : "",
				baseFormId,
				baseName,
				baseFormType,
				markedRecent,
				setWantsDeleteOk,
				disableOk,
				ref->IsDisabled(),
				ref->IsDeleted(),
				ref->IsCreated());
		}
	}

	// 15. OnUpdateLootManProperty
	void OnUpdateLootManProperty(std::monostate, BSFixedString propertyName)
	{
		properties::Update(propertyName.c_str());
	}

	// 16. ReleaseObject
	void ReleaseObject(std::monostate, std::uint32_t objId)
	{
		UnlockObject(objId);
	}

	// ================================================================
	// Registration and lifecycle
	// ================================================================

	void InstallInventoryRebuildHooks()
	{
		InstallEncounterZoneResetSuppressionHooks();
	}

	bool Register(RE::BSScript::IVirtualMachine* vm)
	{
		REX::DEBUG("[ Started binding papyrus functions for LootMan ]");

		vm->BindNativeMethod("LTMN2:LootMan"sv, "FindNearbyReferencesWithFormType"sv,
			&FindNearbyReferencesWithFormType, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "FindNearbyReferenceIdsWithFormType"sv,
			&FindNearbyReferenceIdsWithFormType, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetEquipmentComponents"sv,
			&GetEquipmentComponents, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetFormType"sv,
			&GetFormType, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetInventoryItemsWithItemType"sv,
			&GetInventoryItemsWithItemType, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetLootableItems"sv,
			&GetLootableItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "TransferLootableInventoryItems"sv,
			&TransferLootableInventoryItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "TransferInventoryItems"sv,
			&TransferInventoryItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "LootNearbyReferences"sv,
			&LootNearbyReferences, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetScrappableItems"sv,
			&GetScrappableItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "IsFormTypeEquals"sv,
			&IsFormTypeEquals, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "PlayPickUpSound"sv,
			&PlayPickUpSound, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "FinalizeWorldPickup"sv,
			&FinalizeWorldPickup, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "OnUpdateLootManProperty"sv,
			&OnUpdateLootManProperty, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "ReleaseObject"sv,
			&ReleaseObject, false, false);

		REX::DEBUG("  Papyrus functions binding is complete");
		return true;
	}

	void OnPreLoadGame()
	{
		{
			std::lock_guard<std::mutex> guard(objectsLock);
			lockedObjects.clear();
			lastLockedObjectCleanupAt = {};
		}
		{
			std::lock_guard<std::mutex> guard(recentWorldLootLock);
			recentlyLootedWorldRefs.clear();
		}
	}
}
