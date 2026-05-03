#include "papyrus_lootman.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <excpt.h>
#include <limits>
#include <memory>
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
#include "message_queue.h"
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
	inline constexpr std::uintptr_t kWorkshopCaravanKeywordGlobalRva = 0x30EC9B8;
	inline constexpr std::uintptr_t kCurrentWorkshopHandleGlobalRva = 0x30EC598;
	inline constexpr std::array<std::uintptr_t, 3> kPopulateLinkedWorkshopContainerCallSites{
		0x391F78,
		0xB28B76,
		0x10890F6,
	};
	inline constexpr std::array<std::uintptr_t, 4> kRebuildWorkshopSupplyCallSites{
		0xA653F6,
		0xA5F109,
		0xA6052C,
		0xAEFD89,
	};
	inline constexpr std::uintptr_t kComponentCountPapyrusCallSite = 0x59BC2A;
	inline constexpr std::uintptr_t kComponentCountWorkbenchUiCallSite = 0x117501B;
	inline constexpr std::array<std::uintptr_t, 5> kDirectComponentCountCallSites{
		0x3BC3ED,
		0x39F27F,
		0xB3308B,
		0xB37A38,
		0xB2D34E,
	};
	inline constexpr std::array<std::uintptr_t, 2> kWorkshopResourceStatusCallSites{
		0xB2F2C0,
		0xB2D266,
	};
	inline constexpr std::array<std::uintptr_t, 2> kWorkshopMenuSelectCallSites{
		0xB2C8AA,
		0xB2CB67,
	};
	inline constexpr std::array<std::uintptr_t, 5> kWorkshopMenuAvailabilityCallSites{
		0xB2C86E,
		0xB2C8D7,
		0xB2CB2E,
		0xB2CB94,
		0xB2EBE4,
	};
	inline constexpr std::array<std::uintptr_t, 4> kWorkshopCheckAndSetPlacementCallSites{
		0xB2B307,
		0xB2C8F2,
		0xB2CBAF,
		0xB2E88E,
	};
	inline constexpr std::array<std::uintptr_t, 2> kWorkshopStartPlacementCallSites{
		0xB2C9EA,
		0xB2CCA5,
	};
	inline constexpr std::array<std::uintptr_t, 2> kWorkshopBuildResourceCheckCallSites{
		0x392514,
		0x398E06,
	};
	inline constexpr std::array<std::uintptr_t, 2> kWorkshopConsumeComponentCallSites{
		0x398FF6,
		0x3B7D2A,
	};
	inline constexpr std::uintptr_t kWorkshopSelectedMenuNodeFunctionRva = 0x389A80;
	inline constexpr std::uintptr_t kWorkshopSelectedRowGlobalRva = 0x30EBE18;
	inline constexpr std::uint32_t kWorkshopResourceStatusMissingResources = 2;
	inline constexpr std::array<std::uintptr_t, 2> kRemoveComponentsCallSites{
		0x114EB19,
		0x114E543,
	};
	inline constexpr std::uintptr_t kWorkshopObjectCountPapyrusCallSite = 0x5DD484;
	inline constexpr std::uintptr_t kCurrentWorkshopObjectCountCallSite = 0x59D378;

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

	double ElapsedMilliseconds(const Clock::time_point& startedAt)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - startedAt).count();
	}

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

	using SehCall = void(*)(void*);
	bool ExecuteSehCallSafe(SehCall call, void* context);

	using SharedWorkshopContainers = BSScrapArray<NiPointer<TESObjectREFR>>;
	using PopulateLinkedWorkshopContainersFn =
		void (*)(SharedWorkshopContainers*, BGSLocation*, bool);
	using RebuildWorkshopSupplyFn = void (*)(void*);
	using ComponentCountHelperFn = bool (*)(void*, std::int32_t*, TESForm*, bool);
	using DirectComponentCountFn = std::int32_t (*)(void*, BGSComponent*, bool);
	using WorkshopResourceStatusFn = std::uint32_t (*)();
	using GetWorkshopMenuNodeFn = Workshop::WorkshopMenuNode* (*)(std::uint16_t, std::uint32_t*);
	using WorkshopMenuAvailabilityFn = bool (*)(std::uint32_t*, std::uint32_t, std::uint32_t);
	using WorkshopMenuSelectFn = bool (*)(bool, void*);
	using WorkshopCheckAndSetPlacementFn = void (*)(WorkshopMenu*);
	using WorkshopStartPlacementFn = void (*)(void*, bool, bool);
	using WorkshopBuildResourceCheckFn = bool (*)(BGSConstructibleObject*, TESObjectREFR*, void*, bool);
	using WorkshopConsumeComponentFn = void (*)(TESForm*, std::uint32_t);
	using RemoveComponentsFn = void (*)(
		TESObjectREFR*,
		TESForm*,
		std::uint32_t,
		bool,
		void*,
		bool,
		std::uint32_t,
		void*);
	using WorkshopObjectCountFn = bool (*)(void*, TESForm*, bool, float*);
	using CurrentWorkshopObjectCountFn = std::uint32_t (*)(TESForm*);

	TESObjectREFR* TryFindNearestValidWorkshop(TESObjectREFR* ref);

	PopulateLinkedWorkshopContainersFn originalPopulateLinkedWorkshopContainers = nullptr;
	RebuildWorkshopSupplyFn originalRebuildWorkshopSupply = nullptr;
	ComponentCountHelperFn originalComponentCountHelper = nullptr;
	DirectComponentCountFn originalDirectComponentCount = nullptr;
	WorkshopResourceStatusFn originalWorkshopResourceStatus = nullptr;
	WorkshopMenuAvailabilityFn originalWorkshopMenuAvailability = nullptr;
	WorkshopMenuSelectFn originalWorkshopMenuSelect = nullptr;
	WorkshopCheckAndSetPlacementFn originalWorkshopCheckAndSetPlacement = nullptr;
	WorkshopStartPlacementFn originalWorkshopStartPlacement = nullptr;
	WorkshopBuildResourceCheckFn originalWorkshopBuildResourceCheck = nullptr;
	WorkshopConsumeComponentFn originalWorkshopConsumeComponent = nullptr;
	RemoveComponentsFn originalRemoveComponents = nullptr;
	WorkshopObjectCountFn originalWorkshopObjectCount = nullptr;
	CurrentWorkshopObjectCountFn originalCurrentWorkshopObjectCount = nullptr;
	std::mutex rememberedWorkshopSupplyLinkLock;
	std::mutex sharedWorkshopContainerProbeLogLock;
	std::unordered_map<TESFormID, TESFormID> rememberedWorkshopSupplyLinks;
	std::unordered_set<TESFormID> loggedLootManSharedContainerLocations;
	std::unordered_set<std::uint64_t> loggedSharedWorkshopContainerProbeKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopMaterialProbeKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopMaterialAdjustmentKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopMaterialConsumptionKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopResourceStatusKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopMenuAvailabilityKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopCheckAndSetPlacementKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopPlacementTransitionKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopPlacementStateKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopBuildResourceCheckKeys;
	// Investigation-only traces. Keep false for release builds; set true only
	// while re-investigating indoor workshop supply behavior.
	inline constexpr bool kVerboseWorkshopMaterialDiagnostics = false;

	struct FormProbeSnapshot
	{
		std::uintptr_t pointer = 0;
		TESFormID formID = 0;
		std::uint32_t formType = 0;
		bool readable = false;
	};

	struct FormProbeSnapshotContext
	{
		TESForm* form = nullptr;
		FormProbeSnapshot snapshot;
	};

	void CaptureFormProbeSnapshotCall(void* opaque)
	{
		auto* context = static_cast<FormProbeSnapshotContext*>(opaque);
		auto* form = context->form;
		if (!form)
		{
			return;
		}

		context->snapshot.pointer = reinterpret_cast<std::uintptr_t>(form);
		context->snapshot.formID = form->GetFormID();
		context->snapshot.formType = static_cast<std::uint32_t>(form->GetFormType());
		context->snapshot.readable = true;
	}

	FormProbeSnapshot CaptureFormProbeSnapshot(TESForm* form)
	{
		FormProbeSnapshotContext context{ form };
		if (!ExecuteSehCallSafe(&CaptureFormProbeSnapshotCall, &context))
		{
			context.snapshot.pointer = reinterpret_cast<std::uintptr_t>(form);
			context.snapshot.readable = false;
		}
		return context.snapshot;
	}

	struct WorkshopSupplyOwnerProbeSnapshot
	{
		std::uintptr_t owner = 0;
		std::uintptr_t fieldE0 = 0;
		std::uintptr_t fieldE8 = 0;
		std::uintptr_t fieldF8 = 0;
		std::uintptr_t field2F8 = 0;
		bool readable = false;
	};

	struct WorkshopSupplyOwnerProbeContext
	{
		void* owner = nullptr;
		WorkshopSupplyOwnerProbeSnapshot snapshot;
	};

	void CaptureWorkshopSupplyOwnerProbeCall(void* opaque)
	{
		auto* context = static_cast<WorkshopSupplyOwnerProbeContext*>(opaque);
		auto base = reinterpret_cast<std::uintptr_t>(context->owner);
		if (base == 0)
		{
			return;
		}

		context->snapshot.owner = base;
		context->snapshot.fieldE0 = *reinterpret_cast<std::uintptr_t*>(base + 0xE0);
		context->snapshot.fieldE8 = *reinterpret_cast<std::uintptr_t*>(base + 0xE8);
		context->snapshot.fieldF8 = *reinterpret_cast<std::uintptr_t*>(base + 0xF8);
		context->snapshot.field2F8 = *reinterpret_cast<std::uintptr_t*>(base + 0x2F8);
		context->snapshot.readable = true;
	}

	WorkshopSupplyOwnerProbeSnapshot CaptureWorkshopSupplyOwnerProbe(void* owner)
	{
		WorkshopSupplyOwnerProbeContext context{ owner };
		if (!ExecuteSehCallSafe(&CaptureWorkshopSupplyOwnerProbeCall, &context))
		{
			context.snapshot.owner = reinterpret_cast<std::uintptr_t>(owner);
			context.snapshot.readable = false;
		}
		return context.snapshot;
	}

	bool ShouldLogWorkshopMaterialProbe(std::uint64_t key, std::size_t limit)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopMaterialProbeKeys.size() >= limit)
		{
			return false;
		}
		return loggedWorkshopMaterialProbeKeys.emplace(key).second;
	}

	std::uint64_t MakePointerProbeKey(std::uint32_t sourceId, std::uintptr_t pointer, std::uint32_t extra)
	{
		return (static_cast<std::uint64_t>(sourceId) << 56) ^
		       (static_cast<std::uint64_t>(extra) << 32) ^
		       (pointer >> 4);
	}

	bool ShouldLogWorkshopMaterialAdjustment(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		return loggedWorkshopMaterialAdjustmentKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopMaterialConsumption(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		return loggedWorkshopMaterialConsumptionKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopResourceStatus(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopResourceStatusKeys.size() >= 256)
		{
			return false;
		}
		return loggedWorkshopResourceStatusKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopMenuAvailability(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopMenuAvailabilityKeys.size() >= 256)
		{
			return false;
		}
		return loggedWorkshopMenuAvailabilityKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopCheckAndSetPlacement(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopCheckAndSetPlacementKeys.size() >= 128)
		{
			return false;
		}
		return loggedWorkshopCheckAndSetPlacementKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopPlacementTransition(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopPlacementTransitionKeys.size() >= 256)
		{
			return false;
		}
		return loggedWorkshopPlacementTransitionKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopPlacementState(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopPlacementStateKeys.size() >= 256)
		{
			return false;
		}
		return loggedWorkshopPlacementStateKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopBuildResourceCheck(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopBuildResourceCheckKeys.size() >= 256)
		{
			return false;
		}
		return loggedWorkshopBuildResourceCheckKeys.emplace(key).second;
	}

	std::uint32_t ReadOutCount(std::int32_t* outCount)
	{
		return outCount ? static_cast<std::uint32_t>(*outCount) : 0;
	}

	bool HasRememberedLootManWorkshopForLocation(TESFormID locationId)
	{
		if (locationId == 0)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock);
		return rememberedWorkshopSupplyLinks.find(locationId) != rememberedWorkshopSupplyLinks.end();
	}

	void FillFormProbeSnapshot(FormProbeSnapshot& snapshot, TESForm* form)
	{
		if (!form)
		{
			return;
		}

		snapshot.pointer = reinterpret_cast<std::uintptr_t>(form);
		snapshot.formID = form->GetFormID();
		snapshot.formType = static_cast<std::uint32_t>(form->GetFormType());
		snapshot.readable = true;
	}

	struct CurrentWorkshopProbeSnapshot
	{
		std::uint32_t handle = 0;
		FormProbeSnapshot workshop;
		FormProbeSnapshot location;
		bool locationRemembered = false;
	};

	struct CurrentWorkshopProbeContext
	{
		CurrentWorkshopProbeSnapshot snapshot;
	};

	void CaptureCurrentWorkshopProbeCall(void* opaque)
	{
		auto* context = static_cast<CurrentWorkshopProbeContext*>(opaque);
		static REL::Relocation<std::uint32_t*> currentWorkshopHandle{
			REL::Offset(kCurrentWorkshopHandleGlobalRva)
		};
		auto* handlePtr = currentWorkshopHandle.get();
		if (!handlePtr)
		{
			return;
		}

		context->snapshot.handle = *handlePtr;
		if (context->snapshot.handle == 0)
		{
			return;
		}

		ObjectRefHandle handle;
		static_assert(sizeof(handle) == sizeof(context->snapshot.handle));
		std::memcpy(&handle, &context->snapshot.handle, sizeof(context->snapshot.handle));

		auto workshop = handle.get();
		auto* workshopRef = workshop.get();
		FillFormProbeSnapshot(context->snapshot.workshop, workshopRef);

		auto* location = workshopRef ? workshopRef->GetCurrentLocation() : nullptr;
		FillFormProbeSnapshot(context->snapshot.location, location);
		context->snapshot.locationRemembered =
			HasRememberedLootManWorkshopForLocation(context->snapshot.location.formID);
	}

	CurrentWorkshopProbeSnapshot CaptureCurrentWorkshopProbe()
	{
		CurrentWorkshopProbeContext context;
		if (!ExecuteSehCallSafe(&CaptureCurrentWorkshopProbeCall, &context))
		{
			context.snapshot.workshop.readable = false;
			context.snapshot.location.readable = false;
		}
		return context.snapshot;
	}

	struct WorkshopMaterialContextProbeSnapshot
	{
		CurrentWorkshopProbeSnapshot currentWorkshop;
		FormProbeSnapshot nearestWorkshop;
		FormProbeSnapshot nearestLocation;
		bool nearestLocationRemembered = false;
	};

	WorkshopMaterialContextProbeSnapshot CaptureWorkshopMaterialContextProbe()
	{
		WorkshopMaterialContextProbeSnapshot context;
		context.currentWorkshop = CaptureCurrentWorkshopProbe();

		auto* player = PlayerCharacter::GetSingleton();
		auto* nearestWorkshop = TryFindNearestValidWorkshop(player);
		context.nearestWorkshop = CaptureFormProbeSnapshot(nearestWorkshop);

		auto* nearestLocation = nearestWorkshop ? nearestWorkshop->GetCurrentLocation() : nullptr;
		context.nearestLocation = CaptureFormProbeSnapshot(nearestLocation);
		context.nearestLocationRemembered =
			HasRememberedLootManWorkshopForLocation(context.nearestLocation.formID);

		return context;
	}

	struct PlacementItemProbeSnapshot
	{
		std::uintptr_t placementHandlePtr = 0;
		std::uint32_t placementHandle = 0;
		bool placementHandleResolved = false;
		FormProbeSnapshot placementRef;
		FormProbeSnapshot placementBase;
		std::uintptr_t placementDataPtr = 0;
		std::uint32_t dataPlacementHandle = 0;
		bool dataReadable = false;
		bool dataPlacementHandleResolved = false;
		bool dataIsSet = false;
		bool dataMustSnap = false;
		bool dataAnythingIsGround = false;
		std::uint32_t dataDropProxyCount = 0;
		std::uint32_t dataBodyCount = 0;
		FormProbeSnapshot dataPlacementRef;
		FormProbeSnapshot dataPlacementBase;
		bool sehFailed = false;
	};

	void FillPlacementHandleProbe(
		ObjectRefHandle handle,
		std::uint32_t& rawHandle,
		bool& resolved,
		FormProbeSnapshot& refSnapshot,
		FormProbeSnapshot& baseSnapshot)
	{
		rawHandle = handle.get_handle();
		if (!handle)
		{
			return;
		}

		auto ref = handle.get();
		auto* refPtr = ref.get();
		resolved = refPtr != nullptr;
		FillFormProbeSnapshot(refSnapshot, refPtr);
		auto* baseForm = refPtr ? refPtr->GetObjectReference() : nullptr;
		FillFormProbeSnapshot(baseSnapshot, baseForm);
	}

	struct PlacementItemProbeContext
	{
		PlacementItemProbeSnapshot snapshot;
	};

	void CapturePlacementItemProbeCall(void* opaque)
	{
		auto* context = static_cast<PlacementItemProbeContext*>(opaque);
		auto* placementHandle = Workshop::GetPlacementItem();
		context->snapshot.placementHandlePtr =
			reinterpret_cast<std::uintptr_t>(placementHandle);
		if (placementHandle)
		{
			FillPlacementHandleProbe(
				*placementHandle,
				context->snapshot.placementHandle,
				context->snapshot.placementHandleResolved,
				context->snapshot.placementRef,
				context->snapshot.placementBase);
		}

		auto* placementData = Workshop::GetCurrentPlacementItemData();
		context->snapshot.placementDataPtr =
			reinterpret_cast<std::uintptr_t>(placementData);
		if (!placementData)
		{
			return;
		}

		context->snapshot.dataReadable = true;
		context->snapshot.dataIsSet = placementData->isSet;
		context->snapshot.dataMustSnap = placementData->mustSnap;
		context->snapshot.dataAnythingIsGround = placementData->anythingIsGround;
		context->snapshot.dataDropProxyCount =
			static_cast<std::uint32_t>(placementData->dropProxy.size());
		context->snapshot.dataBodyCount =
			static_cast<std::uint32_t>(placementData->body.size());
		FillPlacementHandleProbe(
			placementData->placementItem,
			context->snapshot.dataPlacementHandle,
			context->snapshot.dataPlacementHandleResolved,
			context->snapshot.dataPlacementRef,
			context->snapshot.dataPlacementBase);
	}

	PlacementItemProbeSnapshot CapturePlacementItemProbe()
	{
		PlacementItemProbeContext context;
		if (!ExecuteSehCallSafe(&CapturePlacementItemProbeCall, &context))
		{
			context.snapshot.sehFailed = true;
		}
		return context.snapshot;
	}

	TESObjectREFR* ResolveCurrentWorkshopForOwner(TESObjectREFR* owner)
	{
		if (!owner)
		{
			return nullptr;
		}

		const auto ownerSnapshot = CaptureFormProbeSnapshot(owner);
		if (!ownerSnapshot.readable || ownerSnapshot.formID == 0)
		{
			return nullptr;
		}

		const auto currentWorkshop = CaptureCurrentWorkshopProbe();
		if (currentWorkshop.workshop.readable &&
			currentWorkshop.workshop.formID == ownerSnapshot.formID)
		{
			if (auto* workshop = TESForm::GetFormByID<TESObjectREFR>(currentWorkshop.workshop.formID))
			{
				return workshop;
			}
		}

		auto* player = PlayerCharacter::GetSingleton();
		auto* nearestWorkshop = TryFindNearestValidWorkshop(player);
		if (nearestWorkshop && nearestWorkshop->formID == ownerSnapshot.formID)
		{
			return nearestWorkshop;
		}

		return nullptr;
	}

	TESObjectREFR* ResolveActiveRememberedWorkshopForOwner(void* owner)
	{
		const auto ownerSnapshot = CaptureFormProbeSnapshot(reinterpret_cast<TESForm*>(owner));
		if (ownerSnapshot.readable &&
			ownerSnapshot.formType == static_cast<std::uint32_t>(ENUM_FORM_ID::kREFR))
		{
			if (auto* workshop = ResolveCurrentWorkshopForOwner(reinterpret_cast<TESObjectREFR*>(owner)))
			{
				return workshop;
			}
		}

		const auto currentWorkshop = CaptureCurrentWorkshopProbe();
		if (!currentWorkshop.workshop.readable ||
			currentWorkshop.workshop.formID == 0 ||
			!currentWorkshop.locationRemembered)
		{
			return nullptr;
		}

		return TESForm::GetFormByID<TESObjectREFR>(currentWorkshop.workshop.formID);
	}

	BGSKeyword* GetNativeWorkshopCaravanKeyword()
	{
		static REL::Relocation<BGSKeyword**> keyword{
			REL::Offset(kWorkshopCaravanKeywordGlobalRva)
		};
		auto* keywordPtr = keyword.get();
		return keywordPtr ? *keywordPtr : nullptr;
	}

	std::uint64_t MakeSharedWorkshopContainerProbeKey(BGSLocation* currentLocation, bool includePlayer)
	{
		const auto locationId = currentLocation ? currentLocation->formID : 0;
		return (static_cast<std::uint64_t>(locationId) << 1) |
		       static_cast<std::uint64_t>(includePlayer ? 1 : 0);
	}

	void LogSharedWorkshopContainerHookProbe(
		SharedWorkshopContainers* containers,
		BGSLocation* currentLocation,
		bool includePlayer)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return;
		}

		const auto key = MakeSharedWorkshopContainerProbeKey(currentLocation, includePlayer);
		{
			std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
			if (!loggedSharedWorkshopContainerProbeKeys.emplace(key).second)
			{
				return;
			}
		}

		const auto locationId = currentLocation ? currentLocation->formID : 0;
		auto* lootManWorkshop = properties::GetLootManWorkshopRef();
		auto* lootManLocation = lootManWorkshop ? lootManWorkshop->GetCurrentLocation() : nullptr;
		auto* workshopCaravanKeyword = GetNativeWorkshopCaravanKeyword();

		REX::INFO(
			"Native shared workshop container hook probe: currentLocation={:08X}, lootManLocation={:08X}, lootManWorkshop={:08X}, workshopCaravanKeyword={:08X}, includePlayer={}, containerCount={}, containers={:016X}",
			locationId,
			lootManLocation ? lootManLocation->formID : 0,
			lootManWorkshop ? lootManWorkshop->formID : 0,
			workshopCaravanKeyword ? workshopCaravanKeyword->formID : 0,
			includePlayer,
			containers ? containers->size() : 0,
			reinterpret_cast<std::uintptr_t>(containers));
	}

	TESObjectREFR* GetRememberedLootManWorkshopForLocation(BGSLocation* currentLocation)
	{
		if (!currentLocation)
		{
			return nullptr;
		}

		TESFormID lootManWorkshopId = 0;
		{
			std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock);
			const auto it = rememberedWorkshopSupplyLinks.find(currentLocation->formID);
			if (it == rememberedWorkshopSupplyLinks.end())
			{
				return nullptr;
			}
			lootManWorkshopId = it->second;
		}

		auto* lootManWorkshop = TESForm::GetFormByID<TESObjectREFR>(lootManWorkshopId);
		if (!lootManWorkshop)
		{
			lootManWorkshop = properties::GetLootManWorkshopRef();
		}

		return lootManWorkshop;
	}

	bool ContainsSharedContainer(SharedWorkshopContainers* containers, TESObjectREFR* ref)
	{
		if (!containers || !ref)
		{
			return true;
		}

		const auto count = containers->size();
		auto* data = containers->data();
		if (!data || count == 0)
		{
			return false;
		}

		for (std::uint32_t index = 0; index < count; ++index)
		{
			if (data[index].get() == ref)
			{
				return true;
			}
		}
		return false;
	}

	BGSLocation* ResolveRememberedWorkshopLocationFallback()
	{
		const auto currentWorkshop = CaptureCurrentWorkshopProbe();
		if (currentWorkshop.locationRemembered &&
			currentWorkshop.location.formID != 0)
		{
			if (auto* location = TESForm::GetFormByID<BGSLocation>(
				currentWorkshop.location.formID))
			{
				return location;
			}
		}

		auto* player = PlayerCharacter::GetSingleton();
		auto* nearestWorkshop = TryFindNearestValidWorkshop(player);
		auto* nearestLocation = nearestWorkshop ? nearestWorkshop->GetCurrentLocation() : nullptr;
		if (nearestLocation &&
			HasRememberedLootManWorkshopForLocation(nearestLocation->formID))
		{
			return nearestLocation;
		}

		return nullptr;
	}

	void LogLootManSharedContainerAdded(
		BGSLocation* requestedLocation,
		BGSLocation* effectiveLocation,
		bool inferredLocation,
		TESObjectREFR* lootManWorkshop,
		std::uint32_t containerCount)
	{
		const auto locationId = effectiveLocation ? effectiveLocation->formID : 0;
		{
			std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
			if (!loggedLootManSharedContainerLocations.emplace(locationId).second)
			{
				return;
			}
		}

		auto* lootManLocation = lootManWorkshop ? lootManWorkshop->GetCurrentLocation() : nullptr;
		REX::INFO(
			"Added LootMan workshop to native shared workshop containers: requestedLocation={:08X}, effectiveLocation={:08X}, inferredLocation={}, lootManLocation={:08X}, lootManWorkshop={:08X}, containerCount={}",
			requestedLocation ? requestedLocation->formID : 0,
			locationId,
			inferredLocation,
			lootManLocation ? lootManLocation->formID : 0,
			lootManWorkshop ? lootManWorkshop->formID : 0,
			containerCount);
	}

	void AddRememberedLootManWorkshopSharedContainer(
		SharedWorkshopContainers* containers,
		BGSLocation* currentLocation)
	{
		if (!containers)
		{
			return;
		}

		auto* effectiveLocation = currentLocation;
		bool inferredLocation = false;
		if (!effectiveLocation)
		{
			effectiveLocation = ResolveRememberedWorkshopLocationFallback();
			inferredLocation = effectiveLocation != nullptr;
		}

		if (!effectiveLocation)
		{
			return;
		}

		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(effectiveLocation);
		if (!lootManWorkshop || ContainsSharedContainer(containers, lootManWorkshop))
		{
			return;
		}

		containers->push_back(NiPointer<TESObjectREFR>(lootManWorkshop));
		LogLootManSharedContainerAdded(
			currentLocation,
			effectiveLocation,
			inferredLocation,
			lootManWorkshop,
			containers->size());
	}

	void HookedPopulateLinkedWorkshopContainers(
		SharedWorkshopContainers* containers,
		BGSLocation* currentLocation,
		bool includePlayer)
	{
		if (originalPopulateLinkedWorkshopContainers)
		{
			originalPopulateLinkedWorkshopContainers(containers, currentLocation, includePlayer);
		}
		LogSharedWorkshopContainerHookProbe(containers, currentLocation, includePlayer);
		AddRememberedLootManWorkshopSharedContainer(containers, currentLocation);
	}

	void LogRebuildWorkshopSupplyProbe(std::uint32_t sourceId, const char* sourceName, void* owner)
	{
		const auto ownerSnapshot = CaptureWorkshopSupplyOwnerProbe(owner);
		const auto ownerForm = CaptureFormProbeSnapshot(reinterpret_cast<TESForm*>(owner));
		const auto key = MakePointerProbeKey(sourceId, ownerSnapshot.owner, ownerForm.formID);
		if (!ShouldLogWorkshopMaterialProbe(key, 96))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=rebuild-supply, source={}, owner={:016X}, ownerReadable={}, ownerForm={:08X}, ownerType={}, fieldsReadable={}, fieldE0={:016X}, fieldE8={:016X}, fieldF8={:016X}, field2F8={:016X}",
			sourceName,
			ownerSnapshot.owner,
			ownerForm.readable,
			ownerForm.formID,
			ownerForm.formType,
			ownerSnapshot.readable,
			ownerSnapshot.fieldE0,
			ownerSnapshot.fieldE8,
			ownerSnapshot.fieldF8,
			ownerSnapshot.field2F8);
	}

	void HookedRebuildWorkshopSupply(
		void* owner,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		if (originalRebuildWorkshopSupply)
		{
			originalRebuildWorkshopSupply(owner);
		}
		LogRebuildWorkshopSupplyProbe(sourceId, sourceName, owner);
	}

	void HookedRebuildWorkshopSupplyA653F6(void* owner)
	{
		HookedRebuildWorkshopSupply(owner, 0xA1, "0x140A653F6");
	}

	void HookedRebuildWorkshopSupplyA5F109(void* owner)
	{
		HookedRebuildWorkshopSupply(owner, 0xA2, "0x140A5F109");
	}

	void HookedRebuildWorkshopSupplyA6052C(void* owner)
	{
		HookedRebuildWorkshopSupply(owner, 0xA3, "0x140A6052C");
	}

	void HookedRebuildWorkshopSupplyAEFD89(void* owner)
	{
		HookedRebuildWorkshopSupply(owner, 0xA4, "0x140AEFD89");
	}

	void LogComponentCountProbe(
		std::uint32_t sourceId,
		const char* sourceName,
		void* owner,
		std::int32_t* outCount,
		TESForm* form,
		bool includeLinked,
		bool result)
	{
		const auto ownerForm = CaptureFormProbeSnapshot(reinterpret_cast<TESForm*>(owner));
		const auto targetForm = CaptureFormProbeSnapshot(form);
		const auto context = sourceId == 0xB2 ?
			CaptureWorkshopMaterialContextProbe() :
			WorkshopMaterialContextProbeSnapshot{};
		const auto contextKey = targetForm.formID ^
			context.currentWorkshop.workshop.formID ^
			(context.nearestWorkshop.formID << 1);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(owner),
			contextKey);
		if (!ShouldLogWorkshopMaterialProbe(key, 192))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=component-count, source={}, owner={:016X}, ownerReadable={}, ownerForm={:08X}, ownerType={}, target={:016X}, targetReadable={}, targetForm={:08X}, targetType={}, includeLinked={}, result={}, outCount={}, currentWorkshopHandle={:08X}, currentWorkshopReadable={}, currentWorkshop={:08X}, currentWorkshopType={}, currentLocationReadable={}, currentLocation={:08X}, currentLocationRemembered={}, nearestWorkshopReadable={}, nearestWorkshop={:08X}, nearestWorkshopType={}, nearestLocationReadable={}, nearestLocation={:08X}, nearestLocationRemembered={}",
			sourceName,
			reinterpret_cast<std::uintptr_t>(owner),
			ownerForm.readable,
			ownerForm.formID,
			ownerForm.formType,
			reinterpret_cast<std::uintptr_t>(form),
			targetForm.readable,
			targetForm.formID,
			targetForm.formType,
			includeLinked,
			result,
			ReadOutCount(outCount),
			context.currentWorkshop.handle,
			context.currentWorkshop.workshop.readable,
			context.currentWorkshop.workshop.formID,
			context.currentWorkshop.workshop.formType,
			context.currentWorkshop.location.readable,
			context.currentWorkshop.location.formID,
			context.currentWorkshop.locationRemembered,
			context.nearestWorkshop.readable,
			context.nearestWorkshop.formID,
			context.nearestWorkshop.formType,
			context.nearestLocation.readable,
			context.nearestLocation.formID,
			context.nearestLocationRemembered);
	}

	struct WorkshopMaterialCountAdjustment
	{
		TESObjectREFR* currentWorkshop = nullptr;
		BGSLocation* currentLocation = nullptr;
		TESObjectREFR* lootManWorkshop = nullptr;
		std::int32_t baseCount = 0;
		std::int32_t extraCount = 0;
		std::int32_t totalCount = 0;
		bool extraResult = false;
		bool applied = false;
	};

	std::int32_t SaturatingAddNonNegative(std::int32_t baseCount, std::int32_t extraCount)
	{
		const auto safeExtra = std::max<std::int32_t>(extraCount, 0);
		const auto total = static_cast<std::int64_t>(baseCount) + safeExtra;
		return static_cast<std::int32_t>(std::min<std::int64_t>(
			total,
			static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
	}

	void LogWorkshopMaterialCountAdjustment(
		void* owner,
		TESForm* form,
		const WorkshopMaterialCountAdjustment& adjustment)
	{
		const auto targetForm = CaptureFormProbeSnapshot(form);
		const auto locationId = adjustment.currentLocation ? adjustment.currentLocation->formID : 0;
		const auto key = MakePointerProbeKey(
			0xD1,
			reinterpret_cast<std::uintptr_t>(owner),
			targetForm.formID ^ locationId);
		if (!ShouldLogWorkshopMaterialAdjustment(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material count adjusted: owner={:016X}, target={:08X}, targetType={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, baseCount={}, extraResult={}, extraCount={}, totalCount={}, applied={}",
			reinterpret_cast<std::uintptr_t>(owner),
			targetForm.formID,
			targetForm.formType,
			adjustment.currentWorkshop ? adjustment.currentWorkshop->formID : 0,
			locationId,
			adjustment.lootManWorkshop ? adjustment.lootManWorkshop->formID : 0,
			adjustment.baseCount,
			adjustment.extraResult,
			adjustment.extraCount,
			adjustment.totalCount,
			adjustment.applied);
	}

	bool IsReadableFormType(TESForm* form, ENUM_FORM_ID expectedType)
	{
		const auto snapshot = CaptureFormProbeSnapshot(form);
		return snapshot.readable &&
			snapshot.formType == static_cast<std::uint32_t>(expectedType);
	}

	WorkshopMaterialCountAdjustment ApplyRememberedWorkshopDirectComponentCount(
		void* owner,
		BGSComponent* component,
		std::int32_t baseCount)
	{
		WorkshopMaterialCountAdjustment adjustment;
		adjustment.baseCount = baseCount;
		adjustment.totalCount = baseCount;

		if (!originalDirectComponentCount ||
			!owner ||
			!component ||
			!IsReadableFormType(reinterpret_cast<TESForm*>(owner), ENUM_FORM_ID::kREFR) ||
			!IsReadableFormType(component, ENUM_FORM_ID::kCMPO))
		{
			return adjustment;
		}

		auto* currentWorkshop = ResolveActiveRememberedWorkshopForOwner(owner);
		if (!currentWorkshop)
		{
			return adjustment;
		}

		auto* currentLocation = currentWorkshop->GetCurrentLocation();
		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(currentLocation);
		if (!lootManWorkshop || lootManWorkshop == currentWorkshop)
		{
			return adjustment;
		}

		adjustment.currentWorkshop = currentWorkshop;
		adjustment.currentLocation = currentLocation;
		adjustment.lootManWorkshop = lootManWorkshop;
		adjustment.extraCount = originalDirectComponentCount(
			lootManWorkshop,
			component,
			false);
		adjustment.extraResult = true;
		if (adjustment.extraCount <= 0)
		{
			LogWorkshopMaterialCountAdjustment(owner, component, adjustment);
			return adjustment;
		}

		adjustment.totalCount = SaturatingAddNonNegative(adjustment.baseCount, adjustment.extraCount);
		adjustment.applied = true;
		LogWorkshopMaterialCountAdjustment(owner, component, adjustment);
		return adjustment;
	}

	WorkshopMaterialCountAdjustment ApplyRememberedWorkshopMaterialCount(
		std::uint32_t sourceId,
		void* owner,
		std::int32_t* outCount,
		TESForm* form,
		bool includeLinked,
		bool originalResult)
	{
		WorkshopMaterialCountAdjustment adjustment;
		adjustment.baseCount = outCount ? *outCount : 0;
		adjustment.totalCount = adjustment.baseCount;

		if (sourceId != 0xB2 ||
			!originalResult ||
			!originalComponentCountHelper ||
			!owner ||
			!outCount ||
			!form ||
			!IsReadableFormType(form, ENUM_FORM_ID::kCMPO) ||
			!IsReadableFormType(reinterpret_cast<TESForm*>(owner), ENUM_FORM_ID::kREFR) ||
			includeLinked)
		{
			return adjustment;
		}

		auto* currentWorkshop = ResolveActiveRememberedWorkshopForOwner(owner);
		if (!currentWorkshop)
		{
			return adjustment;
		}

		auto* currentLocation = currentWorkshop->GetCurrentLocation();
		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(currentLocation);
		if (!lootManWorkshop || lootManWorkshop == currentWorkshop)
		{
			return adjustment;
		}

		adjustment.currentWorkshop = currentWorkshop;
		adjustment.currentLocation = currentLocation;
		adjustment.lootManWorkshop = lootManWorkshop;
		adjustment.extraResult = originalComponentCountHelper(
			lootManWorkshop,
			&adjustment.extraCount,
			form,
			false);
		if (!adjustment.extraResult || adjustment.extraCount <= 0)
		{
			LogWorkshopMaterialCountAdjustment(owner, form, adjustment);
			return adjustment;
		}

		adjustment.totalCount = SaturatingAddNonNegative(adjustment.baseCount, adjustment.extraCount);
		*outCount = adjustment.totalCount;
		adjustment.applied = true;
		LogWorkshopMaterialCountAdjustment(owner, form, adjustment);
		return adjustment;
	}

	bool HookedComponentCountHelper(
		void* owner,
		std::int32_t* outCount,
		TESForm* form,
		bool includeLinked,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const bool result = originalComponentCountHelper ?
			originalComponentCountHelper(owner, outCount, form, includeLinked) :
			false;
		(void)ApplyRememberedWorkshopMaterialCount(
			sourceId,
			owner,
			outCount,
			form,
			includeLinked,
			result);
		LogComponentCountProbe(sourceId, sourceName, owner, outCount, form, includeLinked, result);
		return result;
	}

	bool HookedComponentCountPapyrus(void* owner, std::int32_t* outCount, TESForm* form, bool includeLinked)
	{
		return HookedComponentCountHelper(
			owner,
			outCount,
			form,
			includeLinked,
			0xB1,
			"0x14059BC2A:GetComponentCount");
	}

	bool HookedComponentCountWorkbenchUi(void* owner, std::int32_t* outCount, TESForm* form, bool includeLinked)
	{
		return HookedComponentCountHelper(
			owner,
			outCount,
			form,
			includeLinked,
			0xB2,
			"0x14117501B:WorkbenchUI");
	}

	void LogDirectComponentCountProbe(
		std::uint32_t sourceId,
		const char* sourceName,
		void* owner,
		BGSComponent* component,
		bool includeLinked,
		std::int32_t baseCount,
		const WorkshopMaterialCountAdjustment& adjustment)
	{
		const auto ownerForm = CaptureFormProbeSnapshot(reinterpret_cast<TESForm*>(owner));
		const auto componentForm = CaptureFormProbeSnapshot(component);
		const auto context = CaptureWorkshopMaterialContextProbe();
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(owner),
			componentForm.formID ^
				context.currentWorkshop.workshop.formID ^
				(context.nearestWorkshop.formID << 1));
		if (!ShouldLogWorkshopMaterialProbe(key, 320))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=direct-component-count, source={}, owner={:016X}, ownerReadable={}, ownerForm={:08X}, ownerType={}, component={:016X}, componentReadable={}, componentForm={:08X}, componentType={}, includeLinked={}, baseCount={}, totalCount={}, applied={}, currentWorkshopHandle={:08X}, currentWorkshopReadable={}, currentWorkshop={:08X}, currentWorkshopType={}, currentLocationReadable={}, currentLocation={:08X}, currentLocationRemembered={}, nearestWorkshopReadable={}, nearestWorkshop={:08X}, nearestWorkshopType={}, nearestLocationReadable={}, nearestLocation={:08X}, nearestLocationRemembered={}",
			sourceName,
			reinterpret_cast<std::uintptr_t>(owner),
			ownerForm.readable,
			ownerForm.formID,
			ownerForm.formType,
			reinterpret_cast<std::uintptr_t>(component),
			componentForm.readable,
			componentForm.formID,
			componentForm.formType,
			includeLinked,
			baseCount,
			adjustment.totalCount,
			adjustment.applied,
			context.currentWorkshop.handle,
			context.currentWorkshop.workshop.readable,
			context.currentWorkshop.workshop.formID,
			context.currentWorkshop.workshop.formType,
			context.currentWorkshop.location.readable,
			context.currentWorkshop.location.formID,
			context.currentWorkshop.locationRemembered,
			context.nearestWorkshop.readable,
			context.nearestWorkshop.formID,
			context.nearestWorkshop.formType,
			context.nearestLocation.readable,
			context.nearestLocation.formID,
			context.nearestLocationRemembered);
	}

	std::int32_t HookedDirectComponentCount(
		void* owner,
		BGSComponent* component,
		bool includeLinked,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto baseCount = originalDirectComponentCount ?
			originalDirectComponentCount(owner, component, includeLinked) :
			0;
		const auto adjustment = ApplyRememberedWorkshopDirectComponentCount(
			owner,
			component,
			baseCount);
		LogDirectComponentCountProbe(
			sourceId,
			sourceName,
			owner,
			component,
			includeLinked,
			baseCount,
			adjustment);
		return adjustment.applied ? adjustment.totalCount : baseCount;
	}

	std::int32_t HookedDirectComponentCount3BC3ED(void* owner, BGSComponent* component, bool includeLinked)
	{
		return HookedDirectComponentCount(owner, component, includeLinked, 0xE1, "0x1403BC3ED");
	}

	std::int32_t HookedDirectComponentCount39F27F(void* owner, BGSComponent* component, bool includeLinked)
	{
		return HookedDirectComponentCount(owner, component, includeLinked, 0xE2, "0x14039F27F");
	}

	std::int32_t HookedDirectComponentCountB3308B(void* owner, BGSComponent* component, bool includeLinked)
	{
		return HookedDirectComponentCount(owner, component, includeLinked, 0xE3, "0x140B3308B");
	}

	std::int32_t HookedDirectComponentCountB37A38(void* owner, BGSComponent* component, bool includeLinked)
	{
		return HookedDirectComponentCount(owner, component, includeLinked, 0xE4, "0x140B37A38");
	}

	std::int32_t HookedDirectComponentCountB2D34E(void* owner, BGSComponent* component, bool includeLinked)
	{
		return HookedDirectComponentCount(owner, component, includeLinked, 0xE5, "0x140B2D34E");
	}

	TESObjectREFR* ResolveActiveRememberedWorkshop()
	{
		const auto currentWorkshop = CaptureCurrentWorkshopProbe();
		if (currentWorkshop.workshop.readable &&
			currentWorkshop.workshop.formID != 0 &&
			currentWorkshop.locationRemembered)
		{
			if (auto* workshop = TESForm::GetFormByID<TESObjectREFR>(
				currentWorkshop.workshop.formID))
			{
				return workshop;
			}
		}

		auto* player = PlayerCharacter::GetSingleton();
		auto* nearestWorkshop = TryFindNearestValidWorkshop(player);
		auto* nearestLocation = nearestWorkshop ? nearestWorkshop->GetCurrentLocation() : nullptr;
		if (nearestLocation &&
			HasRememberedLootManWorkshopForLocation(nearestLocation->formID))
		{
			return nearestWorkshop;
		}

		return nullptr;
	}

	struct SelectedWorkshopRecipeProbeSnapshot
	{
		std::uint16_t selectedRow = 0;
		std::uint32_t menuResult = 0;
		std::uintptr_t menuNode = 0;
		BGSConstructibleObject* recipe = nullptr;
		FormProbeSnapshot recipeForm;
		bool readable = false;
		bool sehFailed = false;
	};

	struct SelectedWorkshopRecipeProbeContext
	{
		SelectedWorkshopRecipeProbeSnapshot snapshot;
	};

	void CaptureSelectedWorkshopRecipeProbeCall(void* opaque)
	{
		auto* context = static_cast<SelectedWorkshopRecipeProbeContext*>(opaque);
		static REL::Relocation<std::uint16_t*> selectedRowGlobal{
			REL::Offset(kWorkshopSelectedRowGlobalRva)
		};
		static REL::Relocation<GetWorkshopMenuNodeFn> getWorkshopMenuNode{
			REL::Offset(kWorkshopSelectedMenuNodeFunctionRva)
		};

		auto* selectedRow = selectedRowGlobal.get();
		if (!selectedRow || !getWorkshopMenuNode)
		{
			return;
		}

		context->snapshot.selectedRow = *selectedRow;
		std::uint32_t menuResult = 0;
		auto* menuNode = getWorkshopMenuNode(context->snapshot.selectedRow, &menuResult);
		context->snapshot.menuResult = menuResult;
		context->snapshot.menuNode = reinterpret_cast<std::uintptr_t>(menuNode);
		if (!menuNode)
		{
			return;
		}

		context->snapshot.readable = true;
		context->snapshot.recipe = menuNode->recipe;
		FillFormProbeSnapshot(context->snapshot.recipeForm, context->snapshot.recipe);
	}

	SelectedWorkshopRecipeProbeSnapshot CaptureSelectedWorkshopRecipeProbe()
	{
		SelectedWorkshopRecipeProbeContext context;
		if (!ExecuteSehCallSafe(&CaptureSelectedWorkshopRecipeProbeCall, &context))
		{
			context.snapshot.sehFailed = true;
		}
		return context.snapshot;
	}

	struct WorkshopMenuRecipeProbeContext
	{
		std::uint32_t row = 0;
		std::uint32_t menuResult = 0;
		SelectedWorkshopRecipeProbeSnapshot snapshot;
	};

	void CaptureWorkshopMenuRecipeProbeCall(void* opaque)
	{
		auto* context = static_cast<WorkshopMenuRecipeProbeContext*>(opaque);
		static REL::Relocation<GetWorkshopMenuNodeFn> getWorkshopMenuNode{
			REL::Offset(kWorkshopSelectedMenuNodeFunctionRva)
		};
		if (!getWorkshopMenuNode)
		{
			return;
		}

		context->snapshot.selectedRow = static_cast<std::uint16_t>(context->row);
		std::uint32_t menuResult = context->menuResult;
		auto* menuNode = getWorkshopMenuNode(
			context->snapshot.selectedRow,
			&menuResult);
		context->snapshot.menuResult = menuResult;
		context->snapshot.menuNode = reinterpret_cast<std::uintptr_t>(menuNode);
		if (!menuNode)
		{
			return;
		}

		context->snapshot.readable = true;
		context->snapshot.recipe = menuNode->recipe;
		FillFormProbeSnapshot(context->snapshot.recipeForm, context->snapshot.recipe);
	}

	SelectedWorkshopRecipeProbeSnapshot CaptureWorkshopMenuRecipeProbe(
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		WorkshopMenuRecipeProbeContext context;
		context.row = row;
		context.menuResult = menuResult;
		if (!ExecuteSehCallSafe(&CaptureWorkshopMenuRecipeProbeCall, &context))
		{
			context.snapshot.sehFailed = true;
		}
		return context.snapshot;
	}

	struct WorkshopRecipePointerProbeContext
	{
		BGSConstructibleObject* recipe = nullptr;
		SelectedWorkshopRecipeProbeSnapshot snapshot;
	};

	void CaptureWorkshopRecipePointerProbeCall(void* opaque)
	{
		auto* context = static_cast<WorkshopRecipePointerProbeContext*>(opaque);
		if (!context->recipe)
		{
			return;
		}

		context->snapshot.readable = true;
		context->snapshot.recipe = context->recipe;
		FillFormProbeSnapshot(context->snapshot.recipeForm, context->recipe);
	}

	SelectedWorkshopRecipeProbeSnapshot CaptureWorkshopRecipePointerProbe(
		BGSConstructibleObject* recipe)
	{
		WorkshopRecipePointerProbeContext context;
		context.recipe = recipe;
		if (!ExecuteSehCallSafe(&CaptureWorkshopRecipePointerProbeCall, &context))
		{
			context.snapshot.sehFailed = true;
		}
		return context.snapshot;
	}

	std::int32_t NonNegativeWorkshopComponentCount(
		TESObjectREFR* workshop,
		BGSComponent* component,
		bool includeLinked)
	{
		if (!originalDirectComponentCount || !workshop || !component)
		{
			return 0;
		}

		return std::max<std::int32_t>(
			originalDirectComponentCount(workshop, component, includeLinked),
			0);
	}

	struct WorkshopMaterialComponentRemoval
	{
		BGSComponent* component = nullptr;
		std::uint32_t requestedCount = 0;
		std::int32_t baseCount = 0;
		std::int32_t lootManCount = 0;
		std::uint32_t consumeFromLootMan = 0;
	};

	struct WorkshopResourceStatusEvaluation
	{
		TESObjectREFR* currentWorkshop = nullptr;
		BGSLocation* currentLocation = nullptr;
		TESObjectREFR* lootManWorkshop = nullptr;
		std::vector<WorkshopMaterialComponentRemoval> componentRemovals;
		std::uint32_t requiredItemCount = 0;
		std::uint32_t satisfiedItemCount = 0;
		std::uint32_t lootManBackedItemCount = 0;
		FormProbeSnapshot missingForm;
		std::uint32_t missingRequiredCount = 0;
		std::int32_t missingBaseCount = 0;
		std::int32_t missingLootManCount = 0;
		std::int32_t missingTotalCount = 0;
		FormProbeSnapshot unsupportedForm;
		bool evaluated = false;
		bool allSatisfied = false;
		bool applied = false;
		bool sehFailed = false;
	};

	struct PendingWorkshopBuildConsumptionContext
	{
		TESFormID currentWorkshopId = 0;
		TESFormID currentLocationId = 0;
		TESFormID lootManWorkshopId = 0;
		TESFormID recipeId = 0;
		Clock::time_point createdAt{};
		std::uint32_t remainingConsumeCalls = 0;
		bool active = false;
	};

	thread_local PendingWorkshopBuildConsumptionContext pendingWorkshopBuildConsumption;

	struct WorkshopResourceStatusEvaluationContext
	{
		SelectedWorkshopRecipeProbeSnapshot selectedRecipe;
		TESObjectREFR* ownerWorkshop = nullptr;
		bool requireOwnerWorkshop = false;
		WorkshopResourceStatusEvaluation evaluation;
	};

	void EvaluateWorkshopResourceStatusCall(void* opaque)
	{
		auto* context = static_cast<WorkshopResourceStatusEvaluationContext*>(opaque);
		auto& evaluation = context->evaluation;
		auto* recipe = context->selectedRecipe.recipe;
		if (!recipe ||
			!context->selectedRecipe.recipeForm.readable ||
			!originalDirectComponentCount)
		{
			return;
		}

		auto* currentWorkshop = context->ownerWorkshop ?
			ResolveActiveRememberedWorkshopForOwner(context->ownerWorkshop) :
			nullptr;
		if (!currentWorkshop && !context->requireOwnerWorkshop)
		{
			currentWorkshop = ResolveActiveRememberedWorkshop();
		}
		if (!currentWorkshop)
		{
			return;
		}

		if (context->requireOwnerWorkshop)
		{
			const auto ownerSnapshot = CaptureFormProbeSnapshot(context->ownerWorkshop);
			if (!ownerSnapshot.readable ||
				ownerSnapshot.formID == 0 ||
				currentWorkshop->formID != ownerSnapshot.formID)
			{
				return;
			}
		}

		auto* currentLocation = currentWorkshop->GetCurrentLocation();
		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(currentLocation);
		if (!lootManWorkshop || lootManWorkshop == currentWorkshop)
		{
			return;
		}

		evaluation.currentWorkshop = currentWorkshop;
		evaluation.currentLocation = currentLocation;
		evaluation.lootManWorkshop = lootManWorkshop;

		auto* requiredItems = recipe->requiredItems;
		if (!requiredItems)
		{
			evaluation.evaluated = true;
			evaluation.allSatisfied = true;
			return;
		}

		evaluation.requiredItemCount = requiredItems->size();
		evaluation.evaluated = true;
		if (requiredItems->empty())
		{
			evaluation.allSatisfied = true;
			return;
		}

		for (const auto& requiredItem : *requiredItems)
		{
			auto* requiredForm = requiredItem.first;
			const auto requiredCount = requiredItem.second.i;
			if (requiredCount == 0)
			{
				++evaluation.satisfiedItemCount;
				continue;
			}

			FormProbeSnapshot requiredFormSnapshot;
			FillFormProbeSnapshot(requiredFormSnapshot, requiredForm);
			if (!requiredFormSnapshot.readable ||
				requiredFormSnapshot.formType != static_cast<std::uint32_t>(ENUM_FORM_ID::kCMPO))
			{
				evaluation.unsupportedForm = requiredFormSnapshot;
				return;
			}

			auto* component = requiredForm->As<BGSComponent>();
			if (!component)
			{
				evaluation.unsupportedForm = requiredFormSnapshot;
				return;
			}

			const auto baseCount = NonNegativeWorkshopComponentCount(
				currentWorkshop,
				component,
				true);
			const auto lootManCount = NonNegativeWorkshopComponentCount(
				lootManWorkshop,
				component,
				false);
			const auto totalCount = SaturatingAddNonNegative(baseCount, lootManCount);
			const auto totalAvailable = static_cast<std::uint32_t>(totalCount);
			if (totalAvailable < requiredCount)
			{
				evaluation.missingForm = requiredFormSnapshot;
				evaluation.missingRequiredCount = requiredCount;
				evaluation.missingBaseCount = baseCount;
				evaluation.missingLootManCount = lootManCount;
				evaluation.missingTotalCount = totalCount;
				return;
			}

			const auto availableBase = static_cast<std::uint32_t>(baseCount);
			const auto availableLootMan = static_cast<std::uint32_t>(lootManCount);
			const auto deficit = requiredCount > availableBase ?
				requiredCount - availableBase :
				0u;
			const auto consumeFromLootMan = std::min(deficit, availableLootMan);

			++evaluation.satisfiedItemCount;
			if (consumeFromLootMan > 0)
			{
				++evaluation.lootManBackedItemCount;
				evaluation.componentRemovals.push_back(WorkshopMaterialComponentRemoval{
					component,
					static_cast<std::uint32_t>(requiredCount),
					baseCount,
					lootManCount,
					consumeFromLootMan
				});
			}
		}

		evaluation.allSatisfied = true;
	}

	WorkshopResourceStatusEvaluation EvaluateWorkshopResourceStatus(
		const SelectedWorkshopRecipeProbeSnapshot& selectedRecipe,
		TESObjectREFR* ownerWorkshop = nullptr,
		bool requireOwnerWorkshop = false)
	{
		WorkshopResourceStatusEvaluationContext context;
		context.selectedRecipe = selectedRecipe;
		context.ownerWorkshop = ownerWorkshop;
		context.requireOwnerWorkshop = requireOwnerWorkshop;
		if (!ExecuteSehCallSafe(&EvaluateWorkshopResourceStatusCall, &context))
		{
			context.evaluation.sehFailed = true;
		}
		return context.evaluation;
	}

	void UpdatePendingWorkshopBuildConsumption(
		std::uint32_t sourceId,
		const SelectedWorkshopRecipeProbeSnapshot& recipeProbe,
		bool originalResult,
		bool adjustedResult,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		if (sourceId != 0xB2)
		{
			return;
		}

		pendingWorkshopBuildConsumption = {};
		if (originalResult ||
			!adjustedResult ||
			!evaluation.applied ||
			evaluation.lootManBackedItemCount == 0 ||
			!evaluation.currentWorkshop ||
			!evaluation.currentLocation ||
			!evaluation.lootManWorkshop)
		{
			return;
		}

		pendingWorkshopBuildConsumption.currentWorkshopId = evaluation.currentWorkshop->formID;
		pendingWorkshopBuildConsumption.currentLocationId = evaluation.currentLocation->formID;
		pendingWorkshopBuildConsumption.lootManWorkshopId = evaluation.lootManWorkshop->formID;
		pendingWorkshopBuildConsumption.recipeId = recipeProbe.recipeForm.formID;
		pendingWorkshopBuildConsumption.createdAt = Clock::now();
		pendingWorkshopBuildConsumption.remainingConsumeCalls =
			std::max<std::uint32_t>(evaluation.requiredItemCount + 4, 1);
		pendingWorkshopBuildConsumption.active = true;
	}

	bool HasPendingWorkshopBuildConsumption(
		TESObjectREFR* currentWorkshop,
		BGSLocation* currentLocation,
		TESObjectREFR* lootManWorkshop)
	{
		if (!pendingWorkshopBuildConsumption.active ||
			!currentWorkshop ||
			!currentLocation ||
			!lootManWorkshop)
		{
			return false;
		}
		if (pendingWorkshopBuildConsumption.createdAt.time_since_epoch().count() != 0 &&
			Clock::now() - pendingWorkshopBuildConsumption.createdAt > std::chrono::seconds(5))
		{
			pendingWorkshopBuildConsumption = {};
			return false;
		}

		return pendingWorkshopBuildConsumption.currentWorkshopId == currentWorkshop->formID &&
		       pendingWorkshopBuildConsumption.currentLocationId == currentLocation->formID &&
		       pendingWorkshopBuildConsumption.lootManWorkshopId == lootManWorkshop->formID;
	}

	void NotePendingWorkshopBuildConsumptionCall(TESObjectREFR* currentWorkshop)
	{
		if (!pendingWorkshopBuildConsumption.active ||
			!currentWorkshop ||
			pendingWorkshopBuildConsumption.currentWorkshopId != currentWorkshop->formID)
		{
			return;
		}

		if (pendingWorkshopBuildConsumption.remainingConsumeCalls > 0)
		{
			--pendingWorkshopBuildConsumption.remainingConsumeCalls;
		}
		if (pendingWorkshopBuildConsumption.remainingConsumeCalls == 0)
		{
			pendingWorkshopBuildConsumption = {};
		}
	}

	void ConsumeWorkshopBuildDeficits(
		const char* sourceName,
		const SelectedWorkshopRecipeProbeSnapshot& recipeProbe,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		if (!evaluation.applied ||
			evaluation.componentRemovals.empty() ||
			!evaluation.lootManWorkshop)
		{
			return;
		}

		const bool canRemove = originalRemoveComponents != nullptr;
		std::uint32_t totalConsumeFromLootMan = 0;
		for (const auto& removal : evaluation.componentRemovals)
		{
			if (!removal.component || removal.consumeFromLootMan == 0)
			{
				continue;
			}

			totalConsumeFromLootMan = static_cast<std::uint32_t>(std::min<std::uint64_t>(
				static_cast<std::uint64_t>(totalConsumeFromLootMan) + removal.consumeFromLootMan,
				static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
			if (canRemove)
			{
				originalRemoveComponents(
					evaluation.lootManWorkshop,
					removal.component,
					removal.consumeFromLootMan,
					false,
					nullptr,
					false,
					0,
					nullptr);
			}
		}

		const auto* firstRemoval = evaluation.componentRemovals.empty() ?
			nullptr :
			&evaluation.componentRemovals.front();
		REX::INFO(
			"Native workshop material probe: kind=build-deficit-consume, source={}, recipe={:08X}, removalCount={}, totalConsumeFromLootMan={}, firstRemovalComponent={:08X}, firstRemovalRequested={}, firstRemovalBaseCount={}, firstRemovalLootManCount={}, firstRemovalConsumeFromLootMan={}, applied={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}",
			sourceName,
			recipeProbe.recipeForm.formID,
			evaluation.componentRemovals.size(),
			totalConsumeFromLootMan,
			firstRemoval && firstRemoval->component ? firstRemoval->component->formID : 0,
			firstRemoval ? firstRemoval->requestedCount : 0,
			firstRemoval ? firstRemoval->baseCount : 0,
			firstRemoval ? firstRemoval->lootManCount : 0,
			firstRemoval ? firstRemoval->consumeFromLootMan : 0,
			canRemove && totalConsumeFromLootMan > 0,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0);
	}

	void LogWorkshopResourceStatus(
		std::uint32_t sourceId,
		const char* sourceName,
		std::uint32_t originalStatus,
		std::uint32_t adjustedStatus,
		const SelectedWorkshopRecipeProbeSnapshot& selectedRecipe,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(selectedRecipe.recipe),
			selectedRecipe.recipeForm.formID ^
				(evaluation.currentLocation ? evaluation.currentLocation->formID : 0) ^
				evaluation.missingForm.formID ^
				evaluation.unsupportedForm.formID ^
				(originalStatus << 24) ^
				(adjustedStatus << 16));
		if (!ShouldLogWorkshopResourceStatus(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=resource-status, source={}, originalStatus={}, adjustedStatus={}, applied={}, selectedRow={}, menuResult={}, menuNode={:016X}, menuReadable={}, recipe={:016X}, recipeReadable={}, recipeForm={:08X}, recipeType={}, requiredItems={}, satisfiedItems={}, lootManBackedItems={}, evaluated={}, sehFailed={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, missingForm={:08X}, missingRequired={}, missingBaseCount={}, missingLootManCount={}, missingTotalCount={}, unsupportedForm={:08X}, unsupportedType={}",
			sourceName,
			originalStatus,
			adjustedStatus,
			evaluation.applied,
			selectedRecipe.selectedRow,
			selectedRecipe.menuResult,
			selectedRecipe.menuNode,
			selectedRecipe.readable,
			reinterpret_cast<std::uintptr_t>(selectedRecipe.recipe),
			selectedRecipe.recipeForm.readable,
			selectedRecipe.recipeForm.formID,
			selectedRecipe.recipeForm.formType,
			evaluation.requiredItemCount,
			evaluation.satisfiedItemCount,
			evaluation.lootManBackedItemCount,
			evaluation.evaluated,
			selectedRecipe.sehFailed || evaluation.sehFailed,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			evaluation.missingForm.formID,
			evaluation.missingRequiredCount,
			evaluation.missingBaseCount,
			evaluation.missingLootManCount,
			evaluation.missingTotalCount,
			evaluation.unsupportedForm.formID,
			evaluation.unsupportedForm.formType);
	}

	std::uint32_t HookedWorkshopResourceStatus(
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto originalStatus = originalWorkshopResourceStatus ?
			originalWorkshopResourceStatus() :
			0u;

		const auto selectedRecipe = CaptureSelectedWorkshopRecipeProbe();
		auto evaluation = EvaluateWorkshopResourceStatus(selectedRecipe);
		auto adjustedStatus = originalStatus;
		if (originalStatus == kWorkshopResourceStatusMissingResources &&
			evaluation.evaluated &&
			evaluation.allSatisfied &&
			evaluation.lootManBackedItemCount > 0)
		{
			adjustedStatus = 0;
			evaluation.applied = true;
		}

		LogWorkshopResourceStatus(
			sourceId,
			sourceName,
			originalStatus,
			adjustedStatus,
			selectedRecipe,
			evaluation);
		return adjustedStatus;
	}

	std::uint32_t HookedWorkshopResourceStatusB2F2C0()
	{
		return HookedWorkshopResourceStatus(0xF1, "0x140B2F2C0");
	}

	std::uint32_t HookedWorkshopResourceStatusB2D266()
	{
		return HookedWorkshopResourceStatus(0xF2, "0x140B2D266");
	}

	void LogWorkshopMenuAvailability(
		std::uint32_t sourceId,
		const char* sourceName,
		std::uint32_t row,
		std::uint32_t inputMenuResult,
		std::uint32_t beforeOut,
		std::uint32_t originalOut,
		std::uint32_t adjustedOut,
		bool result,
		bool adjusted,
		const SelectedWorkshopRecipeProbeSnapshot& selectedRecipe,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(selectedRecipe.recipe),
			selectedRecipe.recipeForm.formID ^
				(evaluation.currentLocation ? evaluation.currentLocation->formID : 0) ^
				(beforeOut << 24) ^
				(originalOut << 16) ^
				(adjustedOut << 8) ^
				(result ? 0x80u : 0u) ^
				(adjusted ? 0x40u : 0u));
		if (!ShouldLogWorkshopMenuAvailability(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=menu-availability, source={}, row={}, inputMenuResult={}, beforeOut={}, originalOut={}, adjustedOut={}, result={}, adjusted={}, selectedRow={}, menuResult={}, menuNode={:016X}, menuReadable={}, recipe={:016X}, recipeReadable={}, recipeForm={:08X}, recipeType={}, requiredItems={}, satisfiedItems={}, lootManBackedItems={}, evaluated={}, allSatisfied={}, sehFailed={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, missingForm={:08X}, missingRequired={}, missingBaseCount={}, missingLootManCount={}, missingTotalCount={}, unsupportedForm={:08X}, unsupportedType={}",
			sourceName,
			row,
			inputMenuResult,
			beforeOut,
			originalOut,
			adjustedOut,
			result,
			adjusted,
			selectedRecipe.selectedRow,
			selectedRecipe.menuResult,
			selectedRecipe.menuNode,
			selectedRecipe.readable,
			reinterpret_cast<std::uintptr_t>(selectedRecipe.recipe),
			selectedRecipe.recipeForm.readable,
			selectedRecipe.recipeForm.formID,
			selectedRecipe.recipeForm.formType,
			evaluation.requiredItemCount,
			evaluation.satisfiedItemCount,
			evaluation.lootManBackedItemCount,
			evaluation.evaluated,
			evaluation.allSatisfied,
			selectedRecipe.sehFailed || evaluation.sehFailed,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			evaluation.missingForm.formID,
			evaluation.missingRequiredCount,
			evaluation.missingBaseCount,
			evaluation.missingLootManCount,
			evaluation.missingTotalCount,
			evaluation.unsupportedForm.formID,
			evaluation.unsupportedForm.formType);
	}

	bool HookedWorkshopMenuAvailability(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto beforeOut = outValue ? *outValue : 0;
		auto selectedRecipe = CaptureWorkshopMenuRecipeProbe(row, menuResult);
		const bool result = originalWorkshopMenuAvailability ?
			originalWorkshopMenuAvailability(outValue, row, menuResult) :
			false;
		const auto originalOut = outValue ? *outValue : 0;
		auto evaluation = EvaluateWorkshopResourceStatus(selectedRecipe);
		auto adjustedOut = originalOut;
		bool adjusted = false;
		if (result &&
			outValue &&
			originalOut == 0 &&
			evaluation.evaluated &&
			evaluation.allSatisfied &&
			evaluation.lootManBackedItemCount > 0)
		{
			*outValue = 1;
			adjustedOut = *outValue;
			adjusted = true;
			evaluation.applied = true;
		}

		LogWorkshopMenuAvailability(
			sourceId,
			sourceName,
			row,
			menuResult,
			beforeOut,
			originalOut,
			adjustedOut,
			result,
			adjusted,
			selectedRecipe,
			evaluation);
		return result;
	}

	bool HookedWorkshopMenuAvailabilityB2C86E(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		return HookedWorkshopMenuAvailability(
			outValue,
			row,
			menuResult,
			0x91,
			"0x140B2C86E:WorkshopMenuAvailability");
	}

	bool HookedWorkshopMenuAvailabilityB2C8D7(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		return HookedWorkshopMenuAvailability(
			outValue,
			row,
			menuResult,
			0x92,
			"0x140B2C8D7:WorkshopMenuAvailability");
	}

	bool HookedWorkshopMenuAvailabilityB2CB2E(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		return HookedWorkshopMenuAvailability(
			outValue,
			row,
			menuResult,
			0x93,
			"0x140B2CB2E:WorkshopMenuAvailability");
	}

	bool HookedWorkshopMenuAvailabilityB2CB94(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		return HookedWorkshopMenuAvailability(
			outValue,
			row,
			menuResult,
			0x94,
			"0x140B2CB94:WorkshopMenuAvailability");
	}

	bool HookedWorkshopMenuAvailabilityB2EBE4(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		return HookedWorkshopMenuAvailability(
			outValue,
			row,
			menuResult,
			0x95,
			"0x140B2EBE4:WorkshopMenuAvailability");
	}

	void LogWorkshopCheckAndSetPlacement(
		std::uint32_t sourceId,
		const char* sourceName,
		const char* stage,
		WorkshopMenu* menu,
		const SelectedWorkshopRecipeProbeSnapshot& selectedRecipe,
		const WorkshopResourceStatusEvaluation& evaluation,
		const PlacementItemProbeSnapshot& placement)
	{
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(menu),
			selectedRecipe.recipeForm.formID ^
				(evaluation.currentLocation ? evaluation.currentLocation->formID : 0) ^
				placement.placementHandle ^
				(placement.dataPlacementHandle << 1) ^
				(stage[0] == 'a' ? 0x80000000u : 0u));
		if (!ShouldLogWorkshopCheckAndSetPlacement(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=check-placement, source={}, stage={}, menu={:016X}, selectedRow={}, menuResult={}, recipe={:08X}, recipeReadable={}, evaluated={}, allSatisfied={}, lootManBackedItems={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, placementHandle={:08X}, placementResolved={}, placementRef={:08X}, placementRefReadable={}, placementBase={:08X}, placementBaseType={}, dataHandle={:08X}, dataResolved={}, dataRef={:08X}, dataRefReadable={}, dataBase={:08X}, dataBaseType={}, dataIsSet={}, dataDropProxyCount={}, dataBodyCount={}, sehFailed={}",
			sourceName,
			stage,
			reinterpret_cast<std::uintptr_t>(menu),
			selectedRecipe.selectedRow,
			selectedRecipe.menuResult,
			selectedRecipe.recipeForm.formID,
			selectedRecipe.recipeForm.readable,
			evaluation.evaluated,
			evaluation.allSatisfied,
			evaluation.lootManBackedItemCount,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			placement.placementHandle,
			placement.placementHandleResolved,
			placement.placementRef.formID,
			placement.placementRef.readable,
			placement.placementBase.formID,
			placement.placementBase.formType,
			placement.dataPlacementHandle,
			placement.dataPlacementHandleResolved,
			placement.dataPlacementRef.formID,
			placement.dataPlacementRef.readable,
			placement.dataPlacementBase.formID,
			placement.dataPlacementBase.formType,
			placement.dataIsSet,
			placement.dataDropProxyCount,
			placement.dataBodyCount,
			selectedRecipe.sehFailed || evaluation.sehFailed || placement.sehFailed);
	}

	void HookedWorkshopCheckAndSetPlacement(
		WorkshopMenu* menu,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto beforeRecipe = CaptureSelectedWorkshopRecipeProbe();
		const auto beforeEvaluation = EvaluateWorkshopResourceStatus(beforeRecipe);
		const auto beforePlacement = CapturePlacementItemProbe();
		LogWorkshopCheckAndSetPlacement(
			sourceId,
			sourceName,
			"before",
			menu,
			beforeRecipe,
			beforeEvaluation,
			beforePlacement);

		if (originalWorkshopCheckAndSetPlacement)
		{
			originalWorkshopCheckAndSetPlacement(menu);
		}

		const auto afterRecipe = CaptureSelectedWorkshopRecipeProbe();
		const auto afterEvaluation = EvaluateWorkshopResourceStatus(afterRecipe);
		const auto afterPlacement = CapturePlacementItemProbe();
		LogWorkshopCheckAndSetPlacement(
			sourceId,
			sourceName,
			"after",
			menu,
			afterRecipe,
			afterEvaluation,
			afterPlacement);
	}

	void HookedWorkshopCheckAndSetPlacementB2B307(WorkshopMenu* menu)
	{
		HookedWorkshopCheckAndSetPlacement(
			menu,
			0xA5,
			"0x140B2B307:CheckAndSetItemForPlacement");
	}

	void HookedWorkshopCheckAndSetPlacementB2C8F2(WorkshopMenu* menu)
	{
		HookedWorkshopCheckAndSetPlacement(
			menu,
			0xA6,
			"0x140B2C8F2:CheckAndSetItemForPlacement");
	}

	void HookedWorkshopCheckAndSetPlacementB2CBAF(WorkshopMenu* menu)
	{
		HookedWorkshopCheckAndSetPlacement(
			menu,
			0xA7,
			"0x140B2CBAF:CheckAndSetItemForPlacement");
	}

	void HookedWorkshopCheckAndSetPlacementB2E88E(WorkshopMenu* menu)
	{
		HookedWorkshopCheckAndSetPlacement(
			menu,
			0xA8,
			"0x140B2E88E:CheckAndSetItemForPlacement");
	}

	void LogWorkshopPlacementTransition(
		std::uint32_t sourceId,
		const char* sourceName,
		const char* kind,
		bool forward,
		bool resultKnown,
		bool result,
		void* context,
		const SelectedWorkshopRecipeProbeSnapshot& before,
		const SelectedWorkshopRecipeProbeSnapshot& after,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		const auto contextKey =
			before.recipeForm.formID ^
			(after.recipeForm.formID << 1) ^
			(evaluation.currentLocation ? evaluation.currentLocation->formID : 0) ^
			(result ? 0x80000000u : 0u) ^
			(resultKnown ? 0x40000000u : 0u);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(context),
			contextKey);
		if (!ShouldLogWorkshopPlacementTransition(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind={}, source={}, forward={}, resultKnown={}, result={}, context={:016X}, beforeRow={}, beforeMenuResult={}, beforeNode={:016X}, beforeRecipe={:08X}, beforeRecipeReadable={}, afterRow={}, afterMenuResult={}, afterNode={:016X}, afterRecipe={:08X}, afterRecipeReadable={}, evaluated={}, allSatisfied={}, lootManBackedItems={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, missingForm={:08X}, missingRequired={}, missingBaseCount={}, missingLootManCount={}, missingTotalCount={}, unsupportedForm={:08X}, unsupportedType={}",
			kind,
			sourceName,
			forward,
			resultKnown,
			result,
			reinterpret_cast<std::uintptr_t>(context),
			before.selectedRow,
			before.menuResult,
			before.menuNode,
			before.recipeForm.formID,
			before.recipeForm.readable,
			after.selectedRow,
			after.menuResult,
			after.menuNode,
			after.recipeForm.formID,
			after.recipeForm.readable,
			evaluation.evaluated,
			evaluation.allSatisfied,
			evaluation.lootManBackedItemCount,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			evaluation.missingForm.formID,
			evaluation.missingRequiredCount,
			evaluation.missingBaseCount,
			evaluation.missingLootManCount,
			evaluation.missingTotalCount,
			evaluation.unsupportedForm.formID,
			evaluation.unsupportedForm.formType);
	}

	void LogWorkshopPlacementState(
		std::uint32_t sourceId,
		const char* sourceName,
		std::uint32_t stageId,
		const char* stage,
		bool allowPlacement,
		bool createPreview,
		void* context,
		const SelectedWorkshopRecipeProbeSnapshot& selectedRecipe,
		const WorkshopResourceStatusEvaluation& evaluation,
		const PlacementItemProbeSnapshot& placement)
	{
		const auto contextKey =
			selectedRecipe.recipeForm.formID ^
			(stageId << 24) ^
			placement.placementHandle ^
			(placement.dataPlacementHandle << 1) ^
			(placement.dataIsSet ? 0x40000000u : 0u);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(context),
			contextKey);
		if (!ShouldLogWorkshopPlacementState(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=placement-state, source={}, stage={}, allowPlacement={}, createPreview={}, context={:016X}, selectedRow={}, menuResult={}, recipe={:08X}, recipeReadable={}, evaluated={}, allSatisfied={}, lootManBackedItems={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, placementHandlePtr={:016X}, placementHandle={:08X}, placementResolved={}, placementRef={:08X}, placementRefReadable={}, placementBase={:08X}, placementBaseType={}, placementDataPtr={:016X}, dataReadable={}, dataHandle={:08X}, dataResolved={}, dataRef={:08X}, dataRefReadable={}, dataBase={:08X}, dataBaseType={}, dataIsSet={}, dataMustSnap={}, dataAnythingIsGround={}, dataDropProxyCount={}, dataBodyCount={}, sehFailed={}",
			sourceName,
			stage,
			allowPlacement,
			createPreview,
			reinterpret_cast<std::uintptr_t>(context),
			selectedRecipe.selectedRow,
			selectedRecipe.menuResult,
			selectedRecipe.recipeForm.formID,
			selectedRecipe.recipeForm.readable,
			evaluation.evaluated,
			evaluation.allSatisfied,
			evaluation.lootManBackedItemCount,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			placement.placementHandlePtr,
			placement.placementHandle,
			placement.placementHandleResolved,
			placement.placementRef.formID,
			placement.placementRef.readable,
			placement.placementBase.formID,
			placement.placementBase.formType,
			placement.placementDataPtr,
			placement.dataReadable,
			placement.dataPlacementHandle,
			placement.dataPlacementHandleResolved,
			placement.dataPlacementRef.formID,
			placement.dataPlacementRef.readable,
			placement.dataPlacementBase.formID,
			placement.dataPlacementBase.formType,
			placement.dataIsSet,
			placement.dataMustSnap,
			placement.dataAnythingIsGround,
			placement.dataDropProxyCount,
			placement.dataBodyCount,
			placement.sehFailed);
	}

	bool HookedWorkshopMenuSelect(
		bool forward,
		void* context,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto before = CaptureSelectedWorkshopRecipeProbe();
		const bool result = originalWorkshopMenuSelect ?
			originalWorkshopMenuSelect(forward, context) :
			false;
		const auto after = CaptureSelectedWorkshopRecipeProbe();
		const auto evaluation = EvaluateWorkshopResourceStatus(after);
		LogWorkshopPlacementTransition(
			sourceId,
			sourceName,
			"menu-select",
			forward,
			true,
			result,
			context,
			before,
			after,
			evaluation);
		return result;
	}

	bool HookedWorkshopMenuSelectB2C8AA(bool forward, void* context)
	{
		return HookedWorkshopMenuSelect(
			forward,
			context,
			0xA1,
			"0x140B2C8AA:SelectWorkshopMenuNode");
	}

	bool HookedWorkshopMenuSelectB2CB67(bool forward, void* context)
	{
		return HookedWorkshopMenuSelect(
			forward,
			context,
			0xA2,
			"0x140B2CB67:SelectWorkshopMenuNode");
	}

	void HookedWorkshopStartPlacement(
		void* menuContext,
		bool allowPlacement,
		bool createPreview,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto selectedRecipe = CaptureSelectedWorkshopRecipeProbe();
		const auto evaluation = EvaluateWorkshopResourceStatus(selectedRecipe);
		const auto placementBefore = CapturePlacementItemProbe();
		LogWorkshopPlacementState(
			sourceId,
			sourceName,
			1,
			"before",
			allowPlacement,
			createPreview,
			menuContext,
			selectedRecipe,
			evaluation,
			placementBefore);
		LogWorkshopPlacementTransition(
			sourceId,
			sourceName,
			"placement-start",
			allowPlacement,
			false,
			createPreview,
			menuContext,
			selectedRecipe,
			selectedRecipe,
			evaluation);

		if (originalWorkshopStartPlacement)
		{
			originalWorkshopStartPlacement(menuContext, allowPlacement, createPreview);
		}

		const auto afterRecipe = CaptureSelectedWorkshopRecipeProbe();
		const auto afterEvaluation = EvaluateWorkshopResourceStatus(afterRecipe);
		const auto placementAfter = CapturePlacementItemProbe();
		LogWorkshopPlacementState(
			sourceId,
			sourceName,
			2,
			"after",
			allowPlacement,
			createPreview,
			menuContext,
			afterRecipe,
			afterEvaluation,
			placementAfter);
	}

	void HookedWorkshopStartPlacementB2C9EA(
		void* menuContext,
		bool allowPlacement,
		bool createPreview)
	{
		HookedWorkshopStartPlacement(
			menuContext,
			allowPlacement,
			createPreview,
			0xA3,
			"0x140B2C9EA:StartWorkshopPlacement");
	}

	void HookedWorkshopStartPlacementB2CCA5(
		void* menuContext,
		bool allowPlacement,
		bool createPreview)
	{
		HookedWorkshopStartPlacement(
			menuContext,
			allowPlacement,
			createPreview,
			0xA4,
			"0x140B2CCA5:StartWorkshopPlacement");
	}

	void LogWorkshopBuildResourceCheck(
		std::uint32_t sourceId,
		const char* sourceName,
		BGSConstructibleObject* recipe,
		TESObjectREFR* owner,
		void* scratchList,
		bool scaleRequiredCount,
		bool originalResult,
		bool adjustedResult,
		const SelectedWorkshopRecipeProbeSnapshot& recipeProbe,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		const auto ownerForm = CaptureFormProbeSnapshot(owner);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(recipe),
			recipeProbe.recipeForm.formID ^
				ownerForm.formID ^
				(evaluation.currentLocation ? evaluation.currentLocation->formID : 0) ^
				(originalResult ? 0x40000000u : 0u) ^
				(adjustedResult ? 0x80000000u : 0u));
		if (!ShouldLogWorkshopBuildResourceCheck(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=build-resource-check, source={}, recipe={:016X}, recipeReadable={}, recipeForm={:08X}, recipeType={}, owner={:016X}, ownerReadable={}, ownerForm={:08X}, ownerType={}, scratchList={:016X}, scaleRequiredCount={}, originalResult={}, adjustedResult={}, applied={}, requiredItems={}, satisfiedItems={}, lootManBackedItems={}, evaluated={}, allSatisfied={}, sehFailed={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, missingForm={:08X}, missingRequired={}, missingBaseCount={}, missingLootManCount={}, missingTotalCount={}, unsupportedForm={:08X}, unsupportedType={}",
			sourceName,
			reinterpret_cast<std::uintptr_t>(recipe),
			recipeProbe.recipeForm.readable,
			recipeProbe.recipeForm.formID,
			recipeProbe.recipeForm.formType,
			reinterpret_cast<std::uintptr_t>(owner),
			ownerForm.readable,
			ownerForm.formID,
			ownerForm.formType,
			reinterpret_cast<std::uintptr_t>(scratchList),
			scaleRequiredCount,
			originalResult,
			adjustedResult,
			evaluation.applied,
			evaluation.requiredItemCount,
			evaluation.satisfiedItemCount,
			evaluation.lootManBackedItemCount,
			evaluation.evaluated,
			evaluation.allSatisfied,
			recipeProbe.sehFailed || evaluation.sehFailed,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			evaluation.missingForm.formID,
			evaluation.missingRequiredCount,
			evaluation.missingBaseCount,
			evaluation.missingLootManCount,
			evaluation.missingTotalCount,
			evaluation.unsupportedForm.formID,
			evaluation.unsupportedForm.formType);
	}

	bool HookedWorkshopBuildResourceCheck(
		BGSConstructibleObject* recipe,
		TESObjectREFR* owner,
		void* scratchList,
		bool scaleRequiredCount,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const bool originalResult = originalWorkshopBuildResourceCheck ?
			originalWorkshopBuildResourceCheck(recipe, owner, scratchList, scaleRequiredCount) :
			false;

		const auto recipeProbe = CaptureWorkshopRecipePointerProbe(recipe);
		auto evaluation = EvaluateWorkshopResourceStatus(recipeProbe, owner);
		auto adjustedResult = originalResult;
		if (!originalResult &&
			evaluation.evaluated &&
			evaluation.allSatisfied &&
			evaluation.lootManBackedItemCount > 0)
		{
			adjustedResult = true;
			evaluation.applied = true;
		}
		UpdatePendingWorkshopBuildConsumption(
			sourceId,
			recipeProbe,
			originalResult,
			adjustedResult,
			evaluation);
		if (sourceId == 0xB2 && evaluation.applied)
		{
			ConsumeWorkshopBuildDeficits(sourceName, recipeProbe, evaluation);
			pendingWorkshopBuildConsumption = {};
		}

		LogWorkshopBuildResourceCheck(
			sourceId,
			sourceName,
			recipe,
			owner,
			scratchList,
			scaleRequiredCount,
			originalResult,
			adjustedResult,
			recipeProbe,
			evaluation);
		return adjustedResult;
	}

	bool HookedWorkshopBuildResourceCheck392514(
		BGSConstructibleObject* recipe,
		TESObjectREFR* owner,
		void* scratchList,
		bool scaleRequiredCount)
	{
		return HookedWorkshopBuildResourceCheck(
			recipe,
			owner,
			scratchList,
			scaleRequiredCount,
			0xB1,
			"0x140392514:BuildResourceCheckForPlacement");
	}

	bool HookedWorkshopBuildResourceCheck398E06(
		BGSConstructibleObject* recipe,
		TESObjectREFR* owner,
		void* scratchList,
		bool scaleRequiredCount)
	{
		return HookedWorkshopBuildResourceCheck(
			recipe,
			owner,
			scratchList,
			scaleRequiredCount,
			0xB2,
			"0x140398E06:BuildResourceCheckForConfirm");
	}

	std::uint32_t SaturatingRequiredComponentCount(
		std::uint32_t itemCount,
		std::int32_t componentCount)
	{
		if (componentCount <= 0)
		{
			return 0;
		}

		const auto product =
			static_cast<std::uint64_t>(itemCount) *
			static_cast<std::uint64_t>(componentCount);
		return static_cast<std::uint32_t>(std::min<std::uint64_t>(
			product,
			static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
	}

	struct WorkshopMaterialConsumptionPlan
	{
		TESObjectREFR* currentWorkshop = nullptr;
		BGSLocation* currentLocation = nullptr;
		TESObjectREFR* lootManWorkshop = nullptr;
		std::vector<WorkshopMaterialComponentRemoval> componentRemovals;
		std::int32_t baseCount = 0;
		std::int32_t lootManCount = 0;
		std::uint32_t requestedCount = 0;
		std::uint32_t consumeFromLootMan = 0;
		bool targetIsDirectItem = false;
		bool applied = false;
	};

	WorkshopMaterialConsumptionPlan BuildRememberedWorkshopMaterialConsumptionPlan(
		TESObjectREFR* owner,
		TESForm* form,
		std::uint32_t requestedCount,
		bool includeLinked,
		bool allowDirectItem)
	{
		WorkshopMaterialConsumptionPlan plan;
		plan.requestedCount = requestedCount;

		if (!originalDirectComponentCount ||
			!owner ||
			!form ||
			requestedCount == 0 ||
			!IsReadableFormType(owner, ENUM_FORM_ID::kREFR))
		{
			return plan;
		}

		const auto targetForm = CaptureFormProbeSnapshot(form);
		if (!targetForm.readable)
		{
			return plan;
		}

		const bool targetIsComponent =
			targetForm.formType == static_cast<std::uint32_t>(ENUM_FORM_ID::kCMPO);
		const bool targetIsDirectItem =
			allowDirectItem &&
			targetForm.formType == static_cast<std::uint32_t>(ENUM_FORM_ID::kMISC);
		if (!targetIsComponent && !targetIsDirectItem)
		{
			return plan;
		}

		auto* currentWorkshop = ResolveActiveRememberedWorkshopForOwner(owner);
		if (!currentWorkshop)
		{
			return plan;
		}

		auto* currentLocation = currentWorkshop->GetCurrentLocation();
		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(currentLocation);
		if (!lootManWorkshop || lootManWorkshop == currentWorkshop)
		{
			return plan;
		}

		plan.currentWorkshop = currentWorkshop;
		plan.currentLocation = currentLocation;
		plan.lootManWorkshop = lootManWorkshop;
		plan.targetIsDirectItem = targetIsDirectItem;

		auto addComponentRemoval = [&](BGSComponent* component, std::uint32_t componentRequestedCount)
		{
			if (!component || componentRequestedCount == 0)
			{
				return;
			}

			const auto baseCount = NonNegativeWorkshopComponentCount(
				currentWorkshop,
				component,
				includeLinked);
			const auto lootManCount = NonNegativeWorkshopComponentCount(
				lootManWorkshop,
				component,
				false);
			const auto availableBase = static_cast<std::uint32_t>(baseCount);
			const auto availableLootMan = static_cast<std::uint32_t>(lootManCount);
			const auto deficit = componentRequestedCount > availableBase ?
				componentRequestedCount - availableBase :
				0u;
			const auto consumeFromLootMan = std::min(deficit, availableLootMan);

			plan.baseCount = SaturatingAddNonNegative(plan.baseCount, baseCount);
			plan.lootManCount = SaturatingAddNonNegative(plan.lootManCount, lootManCount);
			plan.consumeFromLootMan = static_cast<std::uint32_t>(std::min<std::uint64_t>(
				static_cast<std::uint64_t>(plan.consumeFromLootMan) + consumeFromLootMan,
				static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));

			if (consumeFromLootMan == 0)
			{
				return;
			}

			plan.componentRemovals.push_back(WorkshopMaterialComponentRemoval{
				component,
				componentRequestedCount,
				baseCount,
				lootManCount,
				consumeFromLootMan
			});
		};

		if (targetIsComponent)
		{
			addComponentRemoval(static_cast<BGSComponent*>(form), requestedCount);
		}
		else
		{
			auto* misc = form->As<TESObjectMISC>();
			if (misc && misc->componentData)
			{
				for (auto& [componentForm, componentValue] : *misc->componentData)
				{
					auto* component = componentForm ? componentForm->As<BGSComponent>() : nullptr;
					addComponentRemoval(
						component,
						SaturatingRequiredComponentCount(requestedCount, componentValue.i));
				}
			}
		}

		plan.applied = plan.consumeFromLootMan > 0 && !plan.componentRemovals.empty();
		return plan;
	}

	void LogWorkshopMaterialConsumption(
		std::uint32_t sourceId,
		const char* sourceName,
		TESObjectREFR* owner,
		TESForm* form,
		bool includeLinked,
		const WorkshopMaterialConsumptionPlan& plan)
	{
		const auto ownerForm = CaptureFormProbeSnapshot(owner);
		const auto targetForm = CaptureFormProbeSnapshot(form);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(owner),
			targetForm.formID ^ plan.requestedCount);
		if (!ShouldLogWorkshopMaterialConsumption(key))
		{
			return;
		}

		const auto* firstRemoval = plan.componentRemovals.empty() ?
			nullptr :
			&plan.componentRemovals.front();
		REX::INFO(
			"Native workshop material probe: kind=component-consume, source={}, owner={:016X}, ownerReadable={}, ownerForm={:08X}, ownerType={}, target={:016X}, targetReadable={}, targetForm={:08X}, targetType={}, targetIsDirectItem={}, includeLinked={}, requestedCount={}, baseCount={}, lootManCount={}, consumeFromLootMan={}, componentRemovalCount={}, firstRemovalComponent={:08X}, firstRemovalRequested={}, firstRemovalBaseCount={}, firstRemovalLootManCount={}, firstRemovalConsumeFromLootMan={}, applied={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}",
			sourceName,
			reinterpret_cast<std::uintptr_t>(owner),
			ownerForm.readable,
			ownerForm.formID,
			ownerForm.formType,
			reinterpret_cast<std::uintptr_t>(form),
			targetForm.readable,
			targetForm.formID,
			targetForm.formType,
			plan.targetIsDirectItem,
			includeLinked,
			plan.requestedCount,
			plan.baseCount,
			plan.lootManCount,
			plan.consumeFromLootMan,
			plan.componentRemovals.size(),
			firstRemoval && firstRemoval->component ? firstRemoval->component->formID : 0,
			firstRemoval ? firstRemoval->requestedCount : 0,
			firstRemoval ? firstRemoval->baseCount : 0,
			firstRemoval ? firstRemoval->lootManCount : 0,
			firstRemoval ? firstRemoval->consumeFromLootMan : 0,
			plan.applied,
			plan.currentWorkshop ? plan.currentWorkshop->formID : 0,
			plan.currentLocation ? plan.currentLocation->formID : 0,
			plan.lootManWorkshop ? plan.lootManWorkshop->formID : 0);
	}

	void HookedWorkshopConsumeComponent(
		TESForm* form,
		std::uint32_t count,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		auto* currentWorkshop = ResolveActiveRememberedWorkshop();
		auto* currentLocation = currentWorkshop ? currentWorkshop->GetCurrentLocation() : nullptr;
		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(currentLocation);
		const auto allowDirectItem = HasPendingWorkshopBuildConsumption(
			currentWorkshop,
			currentLocation,
			lootManWorkshop);
		const auto plan = BuildRememberedWorkshopMaterialConsumptionPlan(
			currentWorkshop,
			form,
			count,
			true,
			allowDirectItem);

		if (originalWorkshopConsumeComponent)
		{
			originalWorkshopConsumeComponent(form, count);
		}

		if (plan.consumeFromLootMan > 0 &&
			plan.lootManWorkshop &&
			originalRemoveComponents)
		{
			if (!plan.componentRemovals.empty())
			{
				for (const auto& removal : plan.componentRemovals)
				{
					if (!removal.component || removal.consumeFromLootMan == 0)
					{
						continue;
					}

					originalRemoveComponents(
						plan.lootManWorkshop,
						removal.component,
						removal.consumeFromLootMan,
						false,
						nullptr,
						false,
						0,
						nullptr);
				}
			}
			else
			{
				originalRemoveComponents(
					plan.lootManWorkshop,
					form,
					plan.consumeFromLootMan,
					false,
					nullptr,
					false,
					0,
					nullptr);
			}
		}

		LogWorkshopMaterialConsumption(
			sourceId,
			sourceName,
			currentWorkshop,
			form,
			true,
			plan);
		NotePendingWorkshopBuildConsumptionCall(currentWorkshop);
	}

	void HookedWorkshopConsumeComponent398FF6(TESForm* form, std::uint32_t count)
	{
		HookedWorkshopConsumeComponent(
			form,
			count,
			0xF3,
			"0x140398FF6:ConsumeWorkshopBuildComponent");
	}

	void HookedWorkshopConsumeComponent3B7D2A(TESForm* form, std::uint32_t count)
	{
		HookedWorkshopConsumeComponent(
			form,
			count,
			0xF4,
			"0x1403B7D2A:ConsumeWorkshopBuildComponent");
	}

	void HookedRemoveComponents(
		TESObjectREFR* owner,
		TESForm* form,
		std::uint32_t count,
		bool includeLinked,
		void* extraData,
		bool allowFallback,
		std::uint32_t uiMessageId,
		void* uiMessageContext,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto plan = BuildRememberedWorkshopMaterialConsumptionPlan(
			owner,
			form,
			count,
			includeLinked,
			false);

		if (originalRemoveComponents)
		{
			originalRemoveComponents(
				owner,
				form,
				count,
				includeLinked,
				extraData,
				allowFallback,
				uiMessageId,
				uiMessageContext);

			if (plan.consumeFromLootMan > 0 && plan.lootManWorkshop)
			{
				if (!plan.componentRemovals.empty())
				{
					for (const auto& removal : plan.componentRemovals)
					{
						if (!removal.component || removal.consumeFromLootMan == 0)
						{
							continue;
						}

						originalRemoveComponents(
							plan.lootManWorkshop,
							removal.component,
							removal.consumeFromLootMan,
							includeLinked,
							extraData,
							allowFallback,
							uiMessageId,
							uiMessageContext);
					}
				}
				else
				{
					originalRemoveComponents(
						plan.lootManWorkshop,
						form,
						plan.consumeFromLootMan,
						includeLinked,
						extraData,
						allowFallback,
						uiMessageId,
						uiMessageContext);
				}
			}
		}

		LogWorkshopMaterialConsumption(
			sourceId,
			sourceName,
			owner,
			form,
			includeLinked,
			plan);
	}

	void HookedRemoveComponents14114EB19(
		TESObjectREFR* owner,
		TESForm* form,
		std::uint32_t count,
		bool includeLinked,
		void* extraData,
		bool allowFallback,
		std::uint32_t uiMessageId,
		void* uiMessageContext)
	{
		HookedRemoveComponents(
			owner,
			form,
			count,
			includeLinked,
			extraData,
			allowFallback,
			uiMessageId,
			uiMessageContext,
			0xF1,
			"0x14114EB19:RemoveComponents");
	}

	void HookedRemoveComponents14114E543(
		TESObjectREFR* owner,
		TESForm* form,
		std::uint32_t count,
		bool includeLinked,
		void* extraData,
		bool allowFallback,
		std::uint32_t uiMessageId,
		void* uiMessageContext)
	{
		HookedRemoveComponents(
			owner,
			form,
			count,
			includeLinked,
			extraData,
			allowFallback,
			uiMessageId,
			uiMessageContext,
			0xF2,
			"0x14114E543:RemoveItemByComponent");
	}

	void LogWorkshopObjectCountProbe(
		void* scriptContext,
		TESForm* form,
		bool includeLinked,
		float* outValue,
		bool result)
	{
		const auto targetForm = CaptureFormProbeSnapshot(form);
		const auto key = MakePointerProbeKey(
			0xC1,
			reinterpret_cast<std::uintptr_t>(scriptContext),
			targetForm.formID);
		if (!ShouldLogWorkshopMaterialProbe(key, 224))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=workshop-object-count, source=0x1405DD484:GetWorkshopObjectCount, scriptContext={:016X}, target={:016X}, targetReadable={}, targetForm={:08X}, targetType={}, includeLinked={}, result={}, outValue={}",
			reinterpret_cast<std::uintptr_t>(scriptContext),
			reinterpret_cast<std::uintptr_t>(form),
			targetForm.readable,
			targetForm.formID,
			targetForm.formType,
			includeLinked,
			result,
			outValue ? *outValue : 0.0f);
	}

	bool HookedWorkshopObjectCount(void* scriptContext, TESForm* form, bool includeLinked, float* outValue)
	{
		const bool result = originalWorkshopObjectCount ?
			originalWorkshopObjectCount(scriptContext, form, includeLinked, outValue) :
			false;
		LogWorkshopObjectCountProbe(scriptContext, form, includeLinked, outValue, result);
		return result;
	}

	void LogCurrentWorkshopObjectCountProbe(TESForm* form, std::uint32_t count)
	{
		const auto targetForm = CaptureFormProbeSnapshot(form);
		const auto context = CaptureWorkshopMaterialContextProbe();
		const auto key = MakePointerProbeKey(
			0xC2,
			reinterpret_cast<std::uintptr_t>(form),
			targetForm.formID ^
				context.currentWorkshop.workshop.formID ^
				(context.nearestWorkshop.formID << 1));
		if (!ShouldLogWorkshopMaterialProbe(key, 256))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=current-workshop-object-count, source=0x14059D378:GetWorkshopObjectCountCurrent, target={:016X}, targetReadable={}, targetForm={:08X}, targetType={}, count={}, currentWorkshopHandle={:08X}, currentWorkshopReadable={}, currentWorkshop={:08X}, currentWorkshopType={}, currentLocationReadable={}, currentLocation={:08X}, currentLocationRemembered={}, nearestWorkshopReadable={}, nearestWorkshop={:08X}, nearestWorkshopType={}, nearestLocationReadable={}, nearestLocation={:08X}, nearestLocationRemembered={}",
			reinterpret_cast<std::uintptr_t>(form),
			targetForm.readable,
			targetForm.formID,
			targetForm.formType,
			count,
			context.currentWorkshop.handle,
			context.currentWorkshop.workshop.readable,
			context.currentWorkshop.workshop.formID,
			context.currentWorkshop.workshop.formType,
			context.currentWorkshop.location.readable,
			context.currentWorkshop.location.formID,
			context.currentWorkshop.locationRemembered,
			context.nearestWorkshop.readable,
			context.nearestWorkshop.formID,
			context.nearestWorkshop.formType,
			context.nearestLocation.readable,
			context.nearestLocation.formID,
			context.nearestLocationRemembered);
	}

	std::uint32_t HookedCurrentWorkshopObjectCount(TESForm* form)
	{
		const auto count = originalCurrentWorkshopObjectCount ?
			originalCurrentWorkshopObjectCount(form) :
			0;
		LogCurrentWorkshopObjectCountProbe(form, count);
		return count;
	}

	void RememberWorkshopSupplyLink(
		std::monostate,
		TESForm* targetLocationForm,
		TESObjectREFR* lootManWorkshop,
		BSFixedString prefix)
	{
		const auto prefixText = prefix.c_str();
		auto* targetLocation = targetLocationForm ? targetLocationForm->As<BGSLocation>() : nullptr;
		if (!targetLocation || !lootManWorkshop)
		{
			REX::WARN(
				"[Papyrus] {}      [ Failed to remember native workshop supply link ] targetLocation={:08X}, lootManWorkshop={:08X}",
				prefixText,
				targetLocationForm ? targetLocationForm->formID : 0,
				lootManWorkshop ? lootManWorkshop->formID : 0);
			return;
		}

		{
			std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock);
			rememberedWorkshopSupplyLinks[targetLocation->formID] = lootManWorkshop->formID;
		}

		auto* lootManLocation = lootManWorkshop->GetCurrentLocation();
		REX::INFO(
			"[Papyrus] {}      [ Remembered native workshop supply link ] targetLocation={:08X}, lootManLocation={:08X}, lootManWorkshop={:08X}",
			prefixText,
			targetLocation->formID,
			lootManLocation ? lootManLocation->formID : 0,
			lootManWorkshop->formID);
	}

	void ForgetWorkshopSupplyLink(std::monostate, TESForm* targetLocationForm, BSFixedString prefix)
	{
		const auto prefixText = prefix.c_str();
		auto* targetLocation = targetLocationForm ? targetLocationForm->As<BGSLocation>() : nullptr;
		if (!targetLocation)
		{
			REX::WARN(
				"[Papyrus] {}      [ Failed to forget native workshop supply link ] targetLocation={:08X}",
				prefixText,
				targetLocationForm ? targetLocationForm->formID : 0);
			return;
		}

		bool removed = false;
		{
			std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock);
			removed = rememberedWorkshopSupplyLinks.erase(targetLocation->formID) > 0;
		}

		REX::INFO(
			"[Papyrus] {}      [ Forgot native workshop supply link ] targetLocation={:08X}, removed={}",
			prefixText,
			targetLocation->formID,
			removed);
	}

	void LogWorkshopSupplyDiagnostics(
		std::monostate,
		TESObjectREFR* targetWorkshop,
		TESObjectREFR* lootManWorkshop,
		BSFixedString prefix)
	{
		const auto prefixText = prefix.c_str();
		auto* targetLocation = targetWorkshop ? targetWorkshop->GetCurrentLocation() : nullptr;
		auto* lootManLocation = lootManWorkshop ? lootManWorkshop->GetCurrentLocation() : nullptr;
		auto* workshopCaravanKeyword = GetNativeWorkshopCaravanKeyword();

		REX::INFO(
			"[Papyrus] {}      [ Native workshop supply diagnostics ] targetWorkshop={:08X}, targetLocation={:08X}, lootManWorkshop={:08X}, lootManLocation={:08X}, workshopCaravanKeyword={:08X}, nativeLinkedLocationScan=disabled-after-ctd",
			prefixText,
			targetWorkshop ? targetWorkshop->formID : 0,
			targetLocation ? targetLocation->formID : 0,
			lootManWorkshop ? lootManWorkshop->formID : 0,
			lootManLocation ? lootManLocation->formID : 0,
			workshopCaravanKeyword ? workshopCaravanKeyword->formID : 0);
	}

	void InstallWorkbenchSharedContainerHooks()
	{
		static std::once_flag installOnce;
		std::call_once(installOnce, []()
		{
			for (const auto callSiteRva : kPopulateLinkedWorkshopContainerCallSites)
			{
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<PopulateLinkedWorkshopContainersFn>(
					callSite.write_call<5>(HookedPopulateLinkedWorkshopContainers));
				if (!originalPopulateLinkedWorkshopContainers)
				{
					originalPopulateLinkedWorkshopContainers = original;
				}
				else if (originalPopulateLinkedWorkshopContainers != original)
				{
					REX::WARN(
						"Unexpected native shared workshop container target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalPopulateLinkedWorkshopContainers));
				}
			}

			REX::INFO("Installed native shared workshop container hooks");
		});
	}

	void InstallWorkshopMaterialProbeHooks()
	{
		static std::once_flag installOnce;
		std::call_once(installOnce, []()
		{
			const std::array<RebuildWorkshopSupplyFn, 4> rebuildHooks{
				&HookedRebuildWorkshopSupplyA653F6,
				&HookedRebuildWorkshopSupplyA5F109,
				&HookedRebuildWorkshopSupplyA6052C,
				&HookedRebuildWorkshopSupplyAEFD89,
			};

			for (std::size_t index = 0; index < kRebuildWorkshopSupplyCallSites.size(); ++index)
			{
				const auto callSiteRva = kRebuildWorkshopSupplyCallSites[index];
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<RebuildWorkshopSupplyFn>(
					callSite.write_call<5>(rebuildHooks[index]));
				if (!originalRebuildWorkshopSupply)
				{
					originalRebuildWorkshopSupply = original;
				}
				else if (originalRebuildWorkshopSupply != original)
				{
					REX::WARN(
						"Unexpected native workshop supply rebuild target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalRebuildWorkshopSupply));
				}
			}

			{
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(kComponentCountPapyrusCallSite) };
				originalComponentCountHelper = reinterpret_cast<ComponentCountHelperFn>(
					callSite.write_call<5>(&HookedComponentCountPapyrus));
			}
			{
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(kComponentCountWorkbenchUiCallSite) };
				const auto original = reinterpret_cast<ComponentCountHelperFn>(
					callSite.write_call<5>(&HookedComponentCountWorkbenchUi));
				if (!originalComponentCountHelper)
				{
					originalComponentCountHelper = original;
				}
				else if (originalComponentCountHelper != original)
				{
					REX::WARN(
						"Unexpected native component-count target: callSite={:X}, original={:X}, expected={:X}",
						kComponentCountWorkbenchUiCallSite,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalComponentCountHelper));
				}
			}

			const std::array<DirectComponentCountFn, 5> directComponentHooks{
				&HookedDirectComponentCount3BC3ED,
				&HookedDirectComponentCount39F27F,
				&HookedDirectComponentCountB3308B,
				&HookedDirectComponentCountB37A38,
				&HookedDirectComponentCountB2D34E,
			};

			for (std::size_t index = 0; index < kDirectComponentCountCallSites.size(); ++index)
			{
				const auto callSiteRva = kDirectComponentCountCallSites[index];
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<DirectComponentCountFn>(
					callSite.write_call<5>(directComponentHooks[index]));
				if (!originalDirectComponentCount)
				{
					originalDirectComponentCount = original;
				}
				else if (originalDirectComponentCount != original)
				{
					REX::WARN(
						"Unexpected native direct component-count target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalDirectComponentCount));
				}
			}

			const std::array<WorkshopResourceStatusFn, 2> resourceStatusHooks{
				&HookedWorkshopResourceStatusB2F2C0,
				&HookedWorkshopResourceStatusB2D266,
			};

			for (std::size_t index = 0; index < kWorkshopResourceStatusCallSites.size(); ++index)
			{
				const auto callSiteRva = kWorkshopResourceStatusCallSites[index];
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<WorkshopResourceStatusFn>(
					callSite.write_call<5>(resourceStatusHooks[index]));
				if (!originalWorkshopResourceStatus)
				{
					originalWorkshopResourceStatus = original;
				}
				else if (originalWorkshopResourceStatus != original)
				{
					REX::WARN(
						"Unexpected native workshop resource-status target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalWorkshopResourceStatus));
				}
			}

			const std::array<WorkshopMenuAvailabilityFn, 5> menuAvailabilityHooks{
				&HookedWorkshopMenuAvailabilityB2C86E,
				&HookedWorkshopMenuAvailabilityB2C8D7,
				&HookedWorkshopMenuAvailabilityB2CB2E,
				&HookedWorkshopMenuAvailabilityB2CB94,
				&HookedWorkshopMenuAvailabilityB2EBE4,
			};

			for (std::size_t index = 0; index < kWorkshopMenuAvailabilityCallSites.size(); ++index)
			{
				const auto callSiteRva = kWorkshopMenuAvailabilityCallSites[index];
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<WorkshopMenuAvailabilityFn>(
					callSite.write_call<5>(menuAvailabilityHooks[index]));
				if (!originalWorkshopMenuAvailability)
				{
					originalWorkshopMenuAvailability = original;
				}
				else if (originalWorkshopMenuAvailability != original)
				{
					REX::WARN(
						"Unexpected native workshop menu-availability target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalWorkshopMenuAvailability));
				}
			}

			const std::array<WorkshopCheckAndSetPlacementFn, 4> checkAndSetPlacementHooks{
				&HookedWorkshopCheckAndSetPlacementB2B307,
				&HookedWorkshopCheckAndSetPlacementB2C8F2,
				&HookedWorkshopCheckAndSetPlacementB2CBAF,
				&HookedWorkshopCheckAndSetPlacementB2E88E,
			};

			for (std::size_t index = 0; index < kWorkshopCheckAndSetPlacementCallSites.size(); ++index)
			{
				const auto callSiteRva = kWorkshopCheckAndSetPlacementCallSites[index];
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<WorkshopCheckAndSetPlacementFn>(
					callSite.write_call<5>(checkAndSetPlacementHooks[index]));
				if (!originalWorkshopCheckAndSetPlacement)
				{
					originalWorkshopCheckAndSetPlacement = original;
				}
				else if (originalWorkshopCheckAndSetPlacement != original)
				{
					REX::WARN(
						"Unexpected native workshop check-placement target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalWorkshopCheckAndSetPlacement));
				}
			}

			const std::array<WorkshopMenuSelectFn, 2> menuSelectHooks{
				&HookedWorkshopMenuSelectB2C8AA,
				&HookedWorkshopMenuSelectB2CB67,
			};

			for (std::size_t index = 0; index < kWorkshopMenuSelectCallSites.size(); ++index)
			{
				const auto callSiteRva = kWorkshopMenuSelectCallSites[index];
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<WorkshopMenuSelectFn>(
					callSite.write_call<5>(menuSelectHooks[index]));
				if (!originalWorkshopMenuSelect)
				{
					originalWorkshopMenuSelect = original;
				}
				else if (originalWorkshopMenuSelect != original)
				{
					REX::WARN(
						"Unexpected native workshop menu-select target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalWorkshopMenuSelect));
				}
			}

			const std::array<WorkshopStartPlacementFn, 2> startPlacementHooks{
				&HookedWorkshopStartPlacementB2C9EA,
				&HookedWorkshopStartPlacementB2CCA5,
			};

			for (std::size_t index = 0; index < kWorkshopStartPlacementCallSites.size(); ++index)
			{
				const auto callSiteRva = kWorkshopStartPlacementCallSites[index];
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<WorkshopStartPlacementFn>(
					callSite.write_call<5>(startPlacementHooks[index]));
				if (!originalWorkshopStartPlacement)
				{
					originalWorkshopStartPlacement = original;
				}
				else if (originalWorkshopStartPlacement != original)
				{
					REX::WARN(
						"Unexpected native workshop placement-start target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalWorkshopStartPlacement));
				}
			}

			const std::array<WorkshopBuildResourceCheckFn, 2> buildResourceCheckHooks{
				&HookedWorkshopBuildResourceCheck392514,
				&HookedWorkshopBuildResourceCheck398E06,
			};

			for (std::size_t index = 0; index < kWorkshopBuildResourceCheckCallSites.size(); ++index)
			{
				const auto callSiteRva = kWorkshopBuildResourceCheckCallSites[index];
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<WorkshopBuildResourceCheckFn>(
					callSite.write_call<5>(buildResourceCheckHooks[index]));
				if (!originalWorkshopBuildResourceCheck)
				{
					originalWorkshopBuildResourceCheck = original;
				}
				else if (originalWorkshopBuildResourceCheck != original)
				{
					REX::WARN(
						"Unexpected native workshop build-resource target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalWorkshopBuildResourceCheck));
				}
			}

			const std::array<RemoveComponentsFn, 2> removeComponentHooks{
				&HookedRemoveComponents14114EB19,
				&HookedRemoveComponents14114E543,
			};

			for (std::size_t index = 0; index < kRemoveComponentsCallSites.size(); ++index)
			{
				const auto callSiteRva = kRemoveComponentsCallSites[index];
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<RemoveComponentsFn>(
					callSite.write_call<5>(removeComponentHooks[index]));
				if (!originalRemoveComponents)
				{
					originalRemoveComponents = original;
				}
				else if (originalRemoveComponents != original)
				{
					REX::WARN(
						"Unexpected native component-consume target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalRemoveComponents));
				}
			}

			const std::array<WorkshopConsumeComponentFn, 2> consumeComponentHooks{
				&HookedWorkshopConsumeComponent398FF6,
				&HookedWorkshopConsumeComponent3B7D2A,
			};

			for (std::size_t index = 0; index < kWorkshopConsumeComponentCallSites.size(); ++index)
			{
				const auto callSiteRva = kWorkshopConsumeComponentCallSites[index];
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(callSiteRva) };
				const auto original = reinterpret_cast<WorkshopConsumeComponentFn>(
					callSite.write_call<5>(consumeComponentHooks[index]));
				if (!originalWorkshopConsumeComponent)
				{
					originalWorkshopConsumeComponent = original;
				}
				else if (originalWorkshopConsumeComponent != original)
				{
					REX::WARN(
						"Unexpected native workshop consume-component target: callSite={:X}, original={:X}, expected={:X}",
						callSiteRva,
						reinterpret_cast<std::uintptr_t>(original),
						reinterpret_cast<std::uintptr_t>(originalWorkshopConsumeComponent));
				}
			}

			{
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(kWorkshopObjectCountPapyrusCallSite) };
				originalWorkshopObjectCount = reinterpret_cast<WorkshopObjectCountFn>(
					callSite.write_call<5>(&HookedWorkshopObjectCount));
			}

			{
				REL::Relocation<std::uintptr_t> callSite{ REL::Offset(kCurrentWorkshopObjectCountCallSite) };
				originalCurrentWorkshopObjectCount = reinterpret_cast<CurrentWorkshopObjectCountFn>(
					callSite.write_call<5>(&HookedCurrentWorkshopObjectCount));
			}

			REX::INFO("Installed native workshop material probe hooks");
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

	int SehFilterRecoverable(unsigned long code);

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

	bool TryIsInventoryStackFavoriteSafe(const BGSInventoryItem::Stack& stack, bool& outFavorite)
	{
		outFavorite = false;
		auto* extra = stack.extra.get();
		if (!extra)
		{
			return true;
		}

#if defined(_MSC_VER)
		__try
		{
			outFavorite = extra->HasType(EXTRA_DATA_TYPE::kFavorite);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outFavorite = extra->HasType(EXTRA_DATA_TYPE::kFavorite);
		return true;
#endif
	}

	bool HasInventoryFavoriteStack(const BGSInventoryItem& item)
	{
		for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get())
		{
			bool stackFavorite = false;
			if (TryIsInventoryStackFavoriteSafe(*stack, stackFavorite) && stackFavorite)
			{
				return true;
			}
		}
		return false;
	}

	std::int32_t GetPlayerProtectedStackCount(
		const TESForm* form,
		const BGSInventoryItem::Stack& stack,
		const InventoryItemInfo& stackInfo,
		bool ownerIsPlayer,
		bool ownerIsDead,
		bool formIsFavorite,
		bool hasFavoriteStack,
		bool& retainedFormFavorite)
	{
		if (!ownerIsPlayer || !form || stackInfo.totalCount <= 0)
		{
			return 0;
		}

		std::int32_t protectedCount = 0;
		if (!ownerIsDead && stackInfo.equipped)
		{
			protectedCount = form->GetFormType() == ENUM_FORM_ID::kAMMO ? stackInfo.totalCount : 1;
		}

		bool stackFavorite = false;
		if (TryIsInventoryStackFavoriteSafe(stack, stackFavorite) && stackFavorite)
		{
			protectedCount = std::max(protectedCount, 1);
		}
		if (formIsFavorite && !hasFavoriteStack && !retainedFormFavorite)
		{
			protectedCount = std::max(protectedCount, 1);
			retainedFormFavorite = true;
		}

		return std::min(protectedCount, stackInfo.totalCount);
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

	std::string FormatFormId(TESForm* form)
	{
		if (!form)
		{
			return "None";
		}

		char buffer[9]{};
		std::snprintf(buffer, sizeof(buffer), "%08X", form->formID);
		return buffer;
	}

	std::string GetFormTypeName(ENUM_FORM_ID formType)
	{
		switch (formType)
		{
		case ENUM_FORM_ID::kNONE:
			return "NONE";
		case ENUM_FORM_ID::kACTI:
			return "ACTI";
		case ENUM_FORM_ID::kALCH:
			return "ALCH";
		case ENUM_FORM_ID::kAMMO:
			return "AMMO";
		case ENUM_FORM_ID::kARMO:
			return "ARMO";
		case ENUM_FORM_ID::kBOOK:
			return "BOOK";
		case ENUM_FORM_ID::kCELL:
			return "CELL";
		case ENUM_FORM_ID::kCMPO:
			return "CMPO";
		case ENUM_FORM_ID::kCONT:
			return "CONT";
		case ENUM_FORM_ID::kFACT:
			return "FACT";
		case ENUM_FORM_ID::kINGR:
			return "INGR";
		case ENUM_FORM_ID::kKEYM:
			return "KEYM";
		case ENUM_FORM_ID::kKYWD:
			return "KYWD";
		case ENUM_FORM_ID::kLCTN:
			return "LCTN";
		case ENUM_FORM_ID::kMISC:
			return "MISC";
		case ENUM_FORM_ID::kNPC_:
			return "NPC_";
		case ENUM_FORM_ID::kPERK:
			return "PERK";
		case ENUM_FORM_ID::kQUST:
			return "QUST";
		case ENUM_FORM_ID::kREFR:
			return "REFR";
		case ENUM_FORM_ID::kWEAP:
			return "WEAP";
		default:
			return std::to_string(static_cast<std::uint32_t>(formType));
		}
	}

	std::string GetFormName(TESForm* form)
	{
		if (!form)
		{
			return "";
		}

		if (auto* ref = form->As<TESObjectREFR>())
		{
			if (auto* displayName = ref->GetDisplayFullName(); displayName && displayName[0] != '\0')
			{
				return displayName;
			}

			if (auto* baseForm = ref->GetObjectReference())
			{
				const auto baseName = TESFullName::GetFullName(*baseForm);
				if (!baseName.empty())
				{
					return std::string(baseName);
				}
			}
		}

		const auto fullName = TESFullName::GetFullName(*form);
		if (!fullName.empty())
		{
			return std::string(fullName);
		}

		auto* editorID = GetFormEditorIDOrEmpty(form);
		return editorID ? editorID : "";
	}

	struct InventoryItemDisplayNameCallContext
	{
		const BGSInventoryItem* item = nullptr;
		std::uint32_t stackIndex = 0;
		std::string name;
	};

	void InvokeInventoryItemDisplayNameCall(void* opaque)
	{
		auto* context = static_cast<InventoryItemDisplayNameCallContext*>(opaque);
		auto* name = context->item->GetDisplayFullName(context->stackIndex);
		if (name && name[0] != '\0')
		{
			context->name = name;
		}
	}

	std::string GetInventoryItemDisplayNameSafe(
		const BGSInventoryItem& item,
		TESForm* fallbackForm,
		std::uint32_t stackIndex)
	{
		InventoryItemDisplayNameCallContext context{
			&item,
			stackIndex,
			{}
		};
		(void)ExecuteSehCallSafe(&InvokeInventoryItemDisplayNameCall, &context);
		if (!context.name.empty())
		{
			return context.name;
		}

		return GetFormName(fallbackForm);
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

	std::uint32_t GetNotifyCategoryBit(ENUM_FORM_ID formType)
	{
		switch (formType)
		{
		case ENUM_FORM_ID::kALCH:
			return injection_data::notify_alch;
		case ENUM_FORM_ID::kAMMO:
			return injection_data::notify_ammo;
		case ENUM_FORM_ID::kARMO:
			return injection_data::notify_armo;
		case ENUM_FORM_ID::kBOOK:
			return injection_data::notify_book;
		case ENUM_FORM_ID::kINGR:
			return injection_data::notify_ingr;
		case ENUM_FORM_ID::kKEYM:
			return injection_data::notify_keym;
		case ENUM_FORM_ID::kMISC:
			return injection_data::notify_misc;
		case ENUM_FORM_ID::kWEAP:
			return injection_data::notify_weap;
		default:
			return 0;
		}
	}

	bool IsEquipmentFormType(ENUM_FORM_ID formType)
	{
		return formType == ENUM_FORM_ID::kARMO || formType == ENUM_FORM_ID::kWEAP;
	}

	bool ShouldNotifyLootItem(const TESForm* form, const InventoryItemInfo& info, MatchCache* matchCache = nullptr)
	{
		if (!form || !injection_data::HasNotifyFilters())
		{
			return false;
		}

		if (MatchesAnyCached(form, injection_data::notify_item, matchCache))
		{
			return true;
		}

		const auto formType = form->GetFormType();
		const auto categoryMask = injection_data::GetNotifyCategoryMask();
		if ((categoryMask & GetNotifyCategoryBit(formType)) != 0)
		{
			return true;
		}

		return injection_data::GetNotifyLegendaryEquipment() &&
		       IsEquipmentFormType(formType) &&
		       info.legendary;
	}

	void QueueLootItemNotification(
		TESForm* form,
		const std::string& itemName,
		std::int32_t count,
		const InventoryItemInfo& info,
		MatchCache* matchCache = nullptr)
	{
		if (!ShouldNotifyLootItem(form, info, matchCache))
		{
			return;
		}

		message_queue::Enqueue(form ? form->formID : 0, itemName.empty() ? GetFormName(form) : itemName, count);
	}

	bool ShouldNotifyLootDestination(TESObjectREFR* dest)
	{
		if (!dest)
		{
			return false;
		}
		if (properties::GetBool(properties::looting_without_logs, true))
		{
			return false;
		}
		if (dest->IsPlayerRef())
		{
			return true;
		}

		return !properties::GetBool(properties::loot_is_deliver_to_player, false);
	}

	InventoryItemInfo BuildWorldReferenceNotificationInfo(
		TESObjectREFR* ref,
		TESBoundObject* object,
		std::int32_t count)
	{
		InventoryItemInfo info{};
		info.totalCount = count;
		if (!ref || !object || !IsEquipmentFormType(object->GetFormType()) || !ref->extraList)
		{
			return info;
		}

		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		EquipmentData equipmentData{};
		if (TryGetEquipmentDataSafe(ref->extraList.get(), &modBuffer, equipmentData))
		{
			info.legendary = equipmentData.isLegendary;
			info.featured = equipmentData.isFeaturedItem;
			info.unscrappable = equipmentData.isUnscrappable;
		}
		return info;
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

	bool TryGetInventoryItemInfoSafe(const BGSInventoryItem& item,
		std::vector<BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags,
		InventoryItemInfo& outInfo)
	{
#if defined(_MSC_VER)
		__try
		{
			outInfo = GetInventoryItemInfo(item, buffer, infoFlags);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		outInfo = GetInventoryItemInfo(item, buffer, infoFlags);
		return true;
#endif
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

	std::int32_t GetObservedMovedCount(
		std::int32_t beforeCount,
		std::int32_t afterCount,
		bool gotBefore,
		bool gotAfter,
		std::int32_t fallbackCount)
	{
		if (gotBefore && gotAfter && afterCount > beforeCount)
		{
			return afterCount - beforeCount;
		}
		return std::max<std::int32_t>(fallbackCount, 1);
	}

	std::int32_t GetObservedTransferCount(
		std::int32_t srcBefore,
		std::int32_t srcAfter,
		bool gotSrcBefore,
		bool gotSrcAfter,
		std::int32_t destBefore,
		std::int32_t destAfter,
		bool gotDestBefore,
		bool gotDestAfter,
		std::int32_t fallbackCount)
	{
		if (gotDestBefore && gotDestAfter && destAfter > destBefore)
		{
			return destAfter - destBefore;
		}
		if (gotSrcBefore && gotSrcAfter && srcAfter < srcBefore)
		{
			return srcBefore - srcAfter;
		}
		return std::max<std::int32_t>(fallbackCount, 1);
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

	struct FindNearestWorkshopCallContext
	{
		TESObjectREFR* ref = nullptr;
		TESObjectREFR* workshop = nullptr;
	};

	void InvokeFindNearestValidWorkshopCall(void* opaque)
	{
		auto* context = static_cast<FindNearestWorkshopCallContext*>(opaque);
		context->workshop = Workshop::FindNearestValidWorkshop(*context->ref);
	}

	TESObjectREFR* TryFindNearestValidWorkshop(TESObjectREFR* ref)
	{
		if (!ref)
		{
			return nullptr;
		}

		FindNearestWorkshopCallContext context{ ref };
		if (!ExecuteSehCallSafe(&InvokeFindNearestValidWorkshopCall, &context))
		{
			REX::WARN("FindNearestValidWorkshop failed for ref={:08X}", ref->formID);
			return nullptr;
		}

		auto* workshop = context.workshop;
		if (!workshop ||
			!workshop->extraList ||
			!workshop->extraList->HasType(EXTRA_DATA_TYPE::kWorkshop))
		{
			return nullptr;
		}

		return workshop;
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

	std::int32_t FindNearestValidWorkshopId(std::monostate, TESObjectREFR* ref)
	{
		auto* workshop = TryFindNearestValidWorkshop(ref);
		return workshop ? static_cast<std::int32_t>(workshop->formID) : 0;
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

	void Log(std::monostate, BSFixedString message)
	{
		REX::INFO("[Papyrus] {}", message.c_str());
	}

	std::string GetFormTypeIdentifier(std::monostate, TESForm* form)
	{
		if (!form)
		{
			return GetFormTypeName(ENUM_FORM_ID::kNONE);
		}

		return GetFormTypeName(form->GetFormType());
	}

	std::string GetHexID(std::monostate, TESForm* form)
	{
		return FormatFormId(form);
	}

	std::string GetName(std::monostate, TESForm* form)
	{
		return GetFormName(form);
	}

	std::uint32_t SaturatingInventoryCount(std::uint64_t count)
	{
		return static_cast<std::uint32_t>(std::min<std::uint64_t>(
			count,
			static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())));
	}

	void AddRawComponentCount(
		std::unordered_map<BGSComponent*, std::uint32_t>& data,
		BGSComponent* component,
		std::uint64_t count)
	{
		if (!component || count == 0)
		{
			return;
		}

		const auto current = data[component];
		data[component] = SaturatingInventoryCount(static_cast<std::uint64_t>(current) + count);
	}

	void MergeComponentData(
		std::unordered_map<BGSComponent*, std::uint32_t>& target,
		const std::unordered_map<BGSComponent*, std::uint32_t>& source,
		std::uint32_t multiplier = 1)
	{
		if (multiplier == 0)
		{
			return;
		}

		for (const auto& [component, count] : source)
		{
			AddRawComponentCount(
				target,
				component,
				static_cast<std::uint64_t>(count) * static_cast<std::uint64_t>(multiplier));
		}
	}

	void ExtractConstructibleComponents(
		const BGSConstructibleObject* object,
		std::unordered_map<BGSComponent*, std::uint32_t>& data)
	{
		if (!object || !object->requiredItems)
		{
			return;
		}

		for (auto& [requiredForm, requiredValue] : *object->requiredItems)
		{
			if (!requiredForm || requiredValue.i <= 0)
			{
				continue;
			}

			if (requiredForm->Is(ENUM_FORM_ID::kMISC))
			{
				auto* miscObject = requiredForm->As<TESObjectMISC>();
				if (!miscObject || !miscObject->componentData)
				{
					continue;
				}

				for (auto& [componentForm, componentValue] : *miscObject->componentData)
				{
					auto* component = componentForm ? componentForm->As<BGSComponent>() : nullptr;
					if (!component || componentValue.i <= 0)
					{
						continue;
					}

					auto* scalar = component->modScrapScalar;
					const auto scale = scalar ? scalar->value : 1.0F;
					if (!std::isfinite(scale) || scale <= 0.0F)
					{
						continue;
					}

					AddRawComponentCount(
						data,
						component,
						static_cast<std::uint64_t>(componentValue.i) * static_cast<std::uint64_t>(requiredValue.i));
				}
				continue;
			}

			auto* component = requiredForm->As<BGSComponent>();
			if (!component)
			{
				continue;
			}

			auto* scalar = component->modScrapScalar;
			const auto scale = scalar ? scalar->value : 1.0F;
			if (!std::isfinite(scale) || scale <= 0.0F)
			{
				continue;
			}

			AddRawComponentCount(data, component, static_cast<std::uint64_t>(requiredValue.i));
		}
	}

	struct ExtractModComponentsCallContext
	{
		const std::vector<BGSMod::Attachment::Mod*>* mods = nullptr;
		std::unordered_map<BGSComponent*, std::uint32_t>* outComponents = nullptr;
	};

	void InvokeExtractModComponentsCall(void* opaque)
	{
		auto* context = static_cast<ExtractModComponentsCallContext*>(opaque);
		if (!context || !context->mods || !context->outComponents)
		{
			return;
		}

		for (const auto& mod : *context->mods)
		{
			if (!mod)
			{
				continue;
			}

			ExtractConstructibleComponents(
				constructible_object::FromCreatedObjectId(mod->formID),
				*context->outComponents);
		}
	}

	bool TryExtractModComponentsSafe(
		const std::vector<BGSMod::Attachment::Mod*>& mods,
		std::unordered_map<BGSComponent*, std::uint32_t>& outComponents)
	{
		ExtractModComponentsCallContext context{
			&mods,
			&outComponents
		};
		return ExecuteSehCallSafe(&InvokeExtractModComponentsCall, &context);
	}

	std::int32_t GetRawInventoryItemCount(const BGSInventoryItem& item)
	{
		std::uint64_t count = 0;
		for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get())
		{
			count += stack->count;
		}

		return static_cast<std::int32_t>(std::min<std::uint64_t>(
			count,
			static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())));
	}

	void AddComponentTotal(
		std::vector<std::pair<TESObjectMISC*, std::int32_t>>& totals,
		TESObjectMISC* scrapItem,
		std::int32_t count)
	{
		if (!scrapItem || count == 0)
		{
			return;
		}

		for (auto& [item, total] : totals)
		{
			if (item == scrapItem)
			{
				total += count;
				return;
			}
		}

		totals.emplace_back(scrapItem, count);
	}

	void LogInventoryDiagnostics(std::monostate, TESObjectREFR* inventoryOwner, BSFixedString prefix)
	{
		const auto prefixText = prefix.c_str();
		if (!inventoryOwner)
		{
			REX::INFO("[Papyrus] {}        Raw inventory: missing owner", prefixText);
			return;
		}

		auto inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList)
		{
			REX::INFO(
				"[Papyrus] {}        Raw inventory: no inventory list for owner={:08X}",
				prefixText,
				inventoryOwner->formID);
			return;
		}

		std::vector<std::pair<TESObjectMISC*, std::int32_t>> componentTotals;
		std::int32_t totalItemCount = 0;
		std::uint32_t rawIndex = 1;

		ReadLockGuard guard(inventoryList->rwLock);
		REX::INFO("[Papyrus] {}        Raw inventory entries: {}", prefixText, inventoryList->data.size());

		for (auto& item : inventoryList->data)
		{
			auto* form = item.object;
			if (!form)
			{
				REX::INFO("[Papyrus] {}        [ Raw item {}: missing base form ]", prefixText, rawIndex++);
				continue;
			}

			const auto itemCount = GetRawInventoryItemCount(item);
			totalItemCount += itemCount;
			REX::INFO(
				"[Papyrus] {}        [ Raw item {} ] base={:08X} \"{}\", type={}, count={}",
				prefixText,
				rawIndex,
				form->formID,
				GetFormName(form),
				GetFormTypeName(form->GetFormType()),
				itemCount);

			auto* misc = form->As<TESObjectMISC>();
			if (!misc || !misc->componentData || misc->componentData->empty())
			{
				REX::INFO("[Papyrus] {}          Misc components: 0", prefixText);
				++rawIndex;
				continue;
			}

			REX::INFO("[Papyrus] {}          Misc components: {}", prefixText, misc->componentData->size());
			std::uint32_t componentIndex = 1;
			for (auto& [componentForm, componentValue] : *misc->componentData)
			{
				auto* component = componentForm ? componentForm->As<BGSComponent>() : nullptr;
				if (!component)
				{
					REX::INFO(
						"[Papyrus] {}          [ Component {}: invalid component form ]",
						prefixText,
						componentIndex++);
					continue;
				}

				const auto perItemCount = componentValue.i;
				const auto totalComponentCount = itemCount * perItemCount;
				auto* scrapItem = component->scrapItem;
				REX::INFO(
					"[Papyrus] {}          [ Component {} ] component={:08X} \"{}\", scrap={:08X} \"{}\", perItem={}, total={}",
					prefixText,
					componentIndex,
					component->formID,
					GetFormName(component),
					scrapItem ? scrapItem->formID : 0,
					GetFormName(scrapItem),
					perItemCount,
					totalComponentCount);
				AddComponentTotal(componentTotals, scrapItem, totalComponentCount);
				++componentIndex;
			}

			++rawIndex;
		}

		std::sort(componentTotals.begin(), componentTotals.end(), [](const auto& lhs, const auto& rhs)
		{
			const auto lhsId = lhs.first ? lhs.first->formID : 0;
			const auto rhsId = rhs.first ? rhs.first->formID : 0;
			return lhsId < rhsId;
		});

		REX::INFO("[Papyrus] {}        Raw total item count: {}", prefixText, totalItemCount);
		REX::INFO("[Papyrus] {}        Component totals: {}", prefixText, componentTotals.size());
		std::uint32_t componentTotalIndex = 1;
		for (const auto& [scrapItem, count] : componentTotals)
		{
			REX::INFO(
				"[Papyrus] {}        [ Component total {} ] scrap={:08X} \"{}\", count={}",
				prefixText,
				componentTotalIndex++,
				scrapItem ? scrapItem->formID : 0,
				GetFormName(scrapItem),
				count);
		}
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
		BSTSmartPointer<ExtraDataList> extra;
		bool preserveStackExtra = false;
		InventoryItemInfo info;
		std::string itemName;
	};

	struct InventoryFormTransferRequest
	{
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
		float unitWeight = 0.0F;
		std::optional<std::uint32_t> stackIndex;
		BSTSmartPointer<ExtraDataList> extra;
		bool preserveStackExtra = false;
		InventoryItemInfo info;
		std::string itemName;
	};

	struct ExtraCountData : BSExtraData
	{
		static constexpr auto TYPE{ EXTRA_DATA_TYPE::kCount };

		std::uint16_t count;  // 18
	};
	static_assert(sizeof(BSExtraData) == 0x18);

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

	std::int32_t GetWorldReferenceItemCount(TESObjectREFR* ref)
	{
		auto* extraList = ref ? ref->extraList.get() : nullptr;
		if (!extraList)
		{
			return ref ? 1 : 0;
		}

		auto* extraCount = static_cast<ExtraCountData*>(
			extraList->GetByType(EXTRA_DATA_TYPE::kCount));
		if (!extraCount || extraCount->count == 0)
		{
			return 1;
		}

		return static_cast<std::int32_t>(extraCount->count);
	}

	bool TryAddWorldReferenceToContainerSafe(TESObjectREFR* dest, TESObjectREFR* ref, std::int32_t count)
	{
		if (!dest || !ref || dest == ref || count <= 0)
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
			count
		};
		return ExecuteSehCallSafe(&InvokeWorldReferenceAddCall, &context);
	}

	struct AddInventoryItemCallContext
	{
		TESObjectREFR* dest = nullptr;
		TESBoundObject* object = nullptr;
		BSTSmartPointer<ExtraDataList> extra;
		std::uint32_t count = 0;
	};

	void InvokeAddInventoryItemCall(void* opaque)
	{
		auto* context = static_cast<AddInventoryItemCallContext*>(opaque);
		context->dest->AddInventoryItem(
			context->object,
			context->extra,
			context->count,
			nullptr,
			nullptr,
			nullptr);
	}

	bool TryAddInventoryItemSafe(
		TESObjectREFR* dest,
		TESBoundObject* object,
		std::uint32_t count,
		BSTSmartPointer<ExtraDataList> extra = {})
	{
		if (!dest || !object || count == 0)
		{
			return count == 0;
		}

		AddInventoryItemCallContext context{
			dest,
			object,
			std::move(extra),
			count
		};
		return ExecuteSehCallSafe(&InvokeAddInventoryItemCall, &context);
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

	struct MoveInventoryItemCallContext
	{
		TESObjectREFR* src = nullptr;
		TESObjectREFR* dest = nullptr;
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
		std::optional<std::uint32_t> stackIndex;
	};

	void InvokeMoveInventoryItemCall(void* opaque)
	{
		auto* context = static_cast<MoveInventoryItemCallContext*>(opaque);
		TESObjectREFR::RemoveItemData removeData(context->object, context->count);
		removeData.reason = ITEM_REMOVE_REASON::kStoreContainer;
		removeData.a_otherContainer = context->dest;
		if (context->stackIndex)
		{
			removeData.stackData.push_back(*context->stackIndex);
		}
		context->src->RemoveItem(removeData);
	}

	bool TryMoveInventoryItemSafe(
		TESObjectREFR* src,
		TESObjectREFR* dest,
		TESBoundObject* object,
		std::int32_t count,
		std::optional<std::uint32_t> stackIndex = std::nullopt)
	{
		if (!src || !dest || !object || count <= 0)
		{
			return count == 0;
		}

		MoveInventoryItemCallContext context{
			src,
			dest,
			object,
			count,
			stackIndex
		};
		return ExecuteSehCallSafe(&InvokeMoveInventoryItemCall, &context);
	}

	struct RemoveScrapSourceCallContext
	{
		TESObjectREFR* owner = nullptr;
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
		std::optional<std::uint32_t> stackIndex;
	};

	void InvokeRemoveScrapSourceCall(void* opaque)
	{
		auto* context = static_cast<RemoveScrapSourceCallContext*>(opaque);
		TESObjectREFR::RemoveItemData removeData(context->object, context->count);
		if (context->stackIndex)
		{
			removeData.stackData.push_back(*context->stackIndex);
		}
		context->owner->RemoveItem(removeData);
	}

	bool TryRemoveScrapSourceSafe(
		TESObjectREFR* owner,
		TESBoundObject* object,
		std::int32_t count,
		std::optional<std::uint32_t> stackIndex)
	{
		if (!owner || !object || count <= 0)
		{
			return false;
		}

		RemoveScrapSourceCallContext context{
			owner,
			object,
			count,
			stackIndex
		};
		return ExecuteSehCallSafe(&InvokeRemoveScrapSourceCall, &context);
	}

	struct TransferExtraPresenceCallContext
	{
		ExtraDataList* extra = nullptr;
		bool hasObjectInstance = false;
		bool hasInstanceData = false;
	};

	void InvokeTransferExtraPresenceCall(void* opaque)
	{
		auto* context = static_cast<TransferExtraPresenceCallContext*>(opaque);
		if (!context->extra)
		{
			return;
		}

		context->hasObjectInstance = context->extra->HasType(EXTRA_DATA_TYPE::kObjectInstance);
		context->hasInstanceData = context->extra->HasType(EXTRA_DATA_TYPE::kInstanceData);
	}

	bool TryHasTransferRelevantExtraSafe(ExtraDataList* extra, bool& outResult)
	{
		outResult = false;
		if (!extra)
		{
			return true;
		}

		TransferExtraPresenceCallContext context{ extra };
		if (!ExecuteSehCallSafe(&InvokeTransferExtraPresenceCall, &context))
		{
			return false;
		}

		outResult = context.hasObjectInstance || context.hasInstanceData;
		return true;
	}

	bool ShouldPreserveStackExtraForTransfer(
		TESBoundObject* object,
		const BGSInventoryItem::Stack& stack,
		std::int32_t movingCount,
		std::int32_t stackCount)
	{
		if (!object || movingCount <= 0 || movingCount != stackCount)
		{
			return false;
		}

		const auto formType = object->GetFormType();
		if (formType != ENUM_FORM_ID::kWEAP && formType != ENUM_FORM_ID::kARMO)
		{
			return false;
		}

		bool hasRelevantExtra = false;
		return TryHasTransferRelevantExtraSafe(stack.extra.get(), hasRelevantExtra) && hasRelevantExtra;
	}

	bool TryMoveInventoryItemPreservingStackExtraSafe(
		TESObjectREFR* src,
		TESObjectREFR* dest,
		TESBoundObject* object,
		std::int32_t count,
		std::optional<std::uint32_t> stackIndex,
		BSTSmartPointer<ExtraDataList> extra)
	{
		if (!src || !dest || !object || count <= 0 || !extra)
		{
			return false;
		}

		if (!TryAddInventoryItemSafe(dest, object, static_cast<std::uint32_t>(count), std::move(extra)))
		{
			return false;
		}

		if (!TryRemoveScrapSourceSafe(src, object, count, stackIndex))
		{
			REX::WARN(
				"Inventory transfer: source removal failed after instance-preserving add, src={:08X}, dest={:08X}, item={:08X}, count={}, stack={}",
				src->formID,
				dest->formID,
				object->formID,
				count,
				stackIndex ? static_cast<std::int32_t>(*stackIndex) : -1);
			return false;
		}

		return true;
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
		const bool notifyMovedItems = ShouldNotifyLootDestination(dest);
		const auto worldCount = GetWorldReferenceItemCount(ref);
		if (worldCount <= 0)
		{
			return false;
		}
		const auto itemName = notifyMovedItems ? GetFormName(ref) : std::string{};
		auto notificationInfo = notifyMovedItems
			? BuildWorldReferenceNotificationInfo(ref, object, worldCount)
			: InventoryItemInfo{};

		float acceptedWeight = 0.0F;
		float unitWeight = 0.0F;
		if (capacity && capacity->enabled)
		{
			if (!TryGetItemUnitWeightSafe(object, GetInstanceData(ref), unitWeight) ||
				!capacity->CanAccept(unitWeight, worldCount, acceptedWeight))
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

		const auto moved = [&]()
		{
			if (dest->IsPlayerRef())
			{
				PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
				return TryAddWorldReferenceToContainerSafe(dest, ref, worldCount);
			}
			return TryAddWorldReferenceToContainerSafe(dest, ref, worldCount);
		}();
		if (!moved)
		{
			REX::WARN(
				"LootNearbyReferences: failed to add world ref={:08X}, base={:08X}, count={} to dest={:08X}",
				ref->formID,
				object->formID,
				worldCount,
				dest->formID);
			return false;
		}

		std::int32_t afterCount = 0;
		const bool gotAfter = TryGetReferenceItemCountSafe(dest, object, afterCount);
		if (gotBefore && gotAfter && afterCount > beforeCount)
		{
			FinalizeWorldPickup(std::monostate{}, ref);
			const auto movedCount = GetObservedMovedCount(
				beforeCount,
				afterCount,
				gotBefore,
				gotAfter,
				worldCount);
			if (capacity)
			{
				capacity->Accept(acceptedWeight);
			}
			if (notifyMovedItems)
			{
				notificationInfo.totalCount = movedCount;
				QueueLootItemNotification(object, itemName, movedCount, notificationInfo);
			}
			return true;
		}

		REX::WARN(
			"LootNearbyReferences: no observed world transfer for ref={:08X}, base={:08X}, count={}, before={}, after={}, gotBefore={}, gotAfter={}",
			ref->formID,
			object->formID,
			worldCount,
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
		auto* baseObject = ref ? ref->GetObjectReference() : nullptr;
		auto* flora = baseObject ? baseObject->As<TESFlora>() : nullptr;
		expectedItem = flora ? flora->produceItem : nullptr;
		const bool notifyMovedItems = expectedItem && ShouldNotifyLootDestination(actionRef);
		InventoryItemInfo notificationInfo{};
		notificationInfo.totalCount = 1;
		const auto itemName = notifyMovedItems ? GetFormName(expectedItem) : std::string{};

		float acceptedWeight = 0.0F;
		if (capacity && capacity->enabled)
		{
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
		const auto activated = [&]()
		{
			if (actionRef && actionRef->IsPlayerRef())
			{
				PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
				return TryActivateRefSafe(ref, actionRef, false);
			}
			return TryActivateRefSafe(ref, actionRef, false);
		}();
		if (!activated)
		{
			return false;
		}

		if (playPickupSound)
		{
			PlayPickUpSound(std::monostate{}, player, ref);
		}
		std::int32_t afterCount = 0;
		const bool gotAfter =
			expectedItem &&
			(capacity || notifyMovedItems) &&
			TryGetReferenceItemCountSafe(actionRef, expectedItem, afterCount);
		if (capacity && expectedItem)
		{
			if ((!gotBefore && !gotAfter) || (gotBefore && gotAfter && afterCount > beforeCount))
			{
				capacity->Accept(acceptedWeight);
			}
		}
		if (notifyMovedItems)
		{
			const auto movedCount = GetObservedMovedCount(beforeCount, afterCount, gotBefore, gotAfter, 1);
			notificationInfo.totalCount = movedCount;
			QueueLootItemNotification(
				expectedItem,
				itemName,
				movedCount,
				notificationInfo);
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

		const auto* miscForm = form->As<TESObjectMISC>();
		const bool isLooseModItem = looseModKeyword ?
			HasKeyword(form, looseModKeyword) :
			(miscForm && miscForm->IsLooseMod());
		return (subType == 0 && isLooseModItem) || (subType == 1 && !isLooseModItem);
	}

	std::int32_t TransferInventoryItemsImpl(
		TESObjectREFR* src,
		TESObjectREFR* dest,
		std::uint32_t itemType,
		std::int32_t subType,
		BGSKeyword* looseModKeyword,
		LootCapacityContext* capacity = nullptr,
		bool notifyMovedItems = false)
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
		const auto requestInfoFlags = notifyMovedItems ? inventory_info_full : inventory_info_quest;

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
					if (form->formID == 0x0F)
					{
						continue;
					}

					const bool formIsFavorite = IsFavorite(form);
					const bool hasFavoriteStack = HasInventoryFavoriteStack(item);
					bool retainedFormFavorite = false;
					std::vector<InventoryFormTransferRequest> itemRequests;
					itemRequests.reserve(4);

					std::uint32_t stackIndex = 0;
					for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex)
					{
						InventoryItemInfo stackInfo{};
						if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, requestInfoFlags, stackInfo))
						{
							REX::WARN("TransferInventoryItems: skip stack for {:08X}: stack-info-exception", form->formID);
							continue;
						}

						if ((stackInfo.questItem && !MatchesAnyCached(form, injection_data::include_quest_item, &matchCache)) ||
							stackInfo.dropped ||
							stackInfo.totalCount <= 0)
						{
							continue;
						}

						const auto protectedCount = GetPlayerProtectedStackCount(
							form,
							*stack,
							stackInfo,
							sourceIsPlayer,
							sourceIsDead,
							formIsFavorite,
							hasFavoriteStack,
							retainedFormFavorite);
						const auto movableCount = stackInfo.totalCount - protectedCount;
						if (movableCount <= 0)
						{
							continue;
						}

						float unitWeight = 0.0F;
						if (capacity && capacity->enabled &&
							!TryGetItemUnitWeightSafe(form, GetInstanceData(stack->extra.get()), unitWeight))
						{
							continue;
						}

						itemRequests.push_back(InventoryFormTransferRequest{
							form,
							movableCount,
							unitWeight,
							stackIndex,
							stack->extra,
							ShouldPreserveStackExtraForTransfer(
								form,
								*stack,
								movableCount,
								stackInfo.totalCount),
							stackInfo,
							notifyMovedItems ? GetInventoryItemDisplayNameSafe(item, form, stackIndex) : std::string{}
						});
					}

					for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
					{
						requests.push_back(*it);
					}
					continue;
				}

				std::vector<InventoryFormTransferRequest> itemRequests;
				itemRequests.reserve(4);
				std::uint32_t stackIndex = 0;
				for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex)
				{
					InventoryItemInfo stackInfo{};
					if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, requestInfoFlags, stackInfo))
					{
						REX::WARN("TransferInventoryItems: skip stack for {:08X}: stack-info-exception", form->formID);
						continue;
					}
					if ((stackInfo.questItem && !MatchesAnyCached(form, injection_data::include_quest_item, &matchCache)) ||
						stackInfo.dropped ||
						(stackInfo.equipped && !sourceIsDead) ||
						stackInfo.totalCount <= 0)
					{
						continue;
					}

					float unitWeight = 0.0F;
					if (capacity && capacity->enabled &&
						!TryGetItemUnitWeightSafe(form, GetInstanceData(stack->extra.get()), unitWeight))
					{
						continue;
					}

					itemRequests.push_back(InventoryFormTransferRequest{
						form,
						stackInfo.totalCount,
						unitWeight,
						stackIndex,
						stack->extra,
						ShouldPreserveStackExtraForTransfer(
							form,
							*stack,
							stackInfo.totalCount,
							stackInfo.totalCount),
						stackInfo,
						notifyMovedItems ? GetInventoryItemDisplayNameSafe(item, form, stackIndex) : std::string{}
					});
				}

				for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
				{
					requests.push_back(*it);
				}
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
			bool transferFailed = false;
			if (request.preserveStackExtra)
			{
				if (!TryMoveInventoryItemPreservingStackExtraSafe(
						src,
						dest,
						request.object,
						request.count,
						request.stackIndex,
						request.extra))
				{
					REX::WARN(
						"TransferInventoryItems: instance-preserving transfer failed, src={:08X}, dest={:08X}, item={:08X}, count={}, stack={}",
						src->formID,
						dest->formID,
						request.object->formID,
						request.count,
						request.stackIndex ? static_cast<std::int32_t>(*request.stackIndex) : -1);
					transferFailed = true;
				}
				else
				{
					remaining = 0;
				}
			}
			while (remaining > 0 && !request.preserveStackExtra)
			{
				const auto chunk = std::min<std::int32_t>(remaining, 65535);
				if (!TryMoveInventoryItemSafe(
						src,
						dest,
						request.object,
						chunk,
						request.stackIndex))
				{
					REX::WARN(
						"TransferInventoryItems: transfer failed, src={:08X}, dest={:08X}, item={:08X}, remaining={}, stack={}",
						src->formID,
						dest->formID,
						request.object->formID,
						remaining,
						request.stackIndex ? static_cast<std::int32_t>(*request.stackIndex) : -1);
					break;
				}
				remaining -= chunk;
			}
			if (transferFailed)
			{
				continue;
			}
			const auto movedCount = request.count - remaining;

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

			if (movedCount > 0 && (observedSourceReduction || observedDestIncrease || countUnavailable))
			{
				++movedItems;
				const auto observedMovedCount = GetObservedTransferCount(
					srcBefore,
					srcAfter,
					gotSrcBefore,
					gotSrcAfter,
					destBefore,
					destAfter,
					gotDestBefore,
					gotDestAfter,
					movedCount);
				if (capacity)
				{
					capacity->Accept(acceptedWeight);
				}
				if (notifyMovedItems)
				{
					auto notificationInfo = request.info;
					notificationInfo.totalCount = observedMovedCount;
					QueueLootItemNotification(
						request.object,
						request.itemName,
						observedMovedCount,
						notificationInfo,
						&matchCache);
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
		const bool notifyMovedItems = ShouldNotifyLootDestination(dest);

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

					const auto preservationStackCount = stackInfo.totalCount > 0 ?
						stackInfo.totalCount :
						resolvedCount;
					itemRequests.push_back(InventoryTransferRequest{
						form,
						stackIndex,
						resolvedCount,
						unitWeight,
						stack->extra,
						ShouldPreserveStackExtraForTransfer(
							form,
							*stack,
							resolvedCount,
							preservationStackCount),
						stackInfo,
						notifyMovedItems ? GetInventoryItemDisplayNameSafe(item, form, stackIndex) : std::string{}
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

			auto remaining = request.count;
			bool transferFailed = false;
			auto moveRequest = [&]()
			{
				if (request.preserveStackExtra)
				{
					if (!TryMoveInventoryItemPreservingStackExtraSafe(
							src,
							dest,
							request.object,
							request.count,
							request.stackIndex,
							request.extra))
					{
						REX::WARN(
							"TransferLootableInventoryItems: instance-preserving transfer failed, src={:08X}, dest={:08X}, item={:08X}, count={}, stack={}",
							src->formID,
							dest->formID,
							request.object->formID,
							request.count,
							request.stackIndex);
						transferFailed = true;
					}
					else
					{
						remaining = 0;
					}
				}
				while (remaining > 0 && !request.preserveStackExtra)
				{
					const auto chunk = std::min<std::int32_t>(remaining, 65535);
					if (!TryMoveInventoryItemSafe(
							src,
							dest,
							request.object,
							chunk,
							request.stackIndex))
					{
						REX::WARN(
							"TransferLootableInventoryItems: transfer failed, src={:08X}, dest={:08X}, item={:08X}, remaining={}, stack={}",
							src->formID,
							dest->formID,
							request.object->formID,
							remaining,
							request.stackIndex);
						break;
					}
					remaining -= chunk;
				}
			};
			if (dest->IsPlayerRef())
			{
				PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
				moveRequest();
			}
			else
			{
				moveRequest();
			}
			if (transferFailed)
			{
				continue;
			}
			const auto movedCount = request.count - remaining;

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

			if (movedCount > 0 && (observedSourceReduction || observedDestIncrease || countUnavailable))
			{
				++movedStacks;
				const auto observedMovedCount = GetObservedTransferCount(
					srcBefore,
					srcAfter,
					gotSrcBefore,
					gotSrcAfter,
					destBefore,
					destAfter,
					gotDestBefore,
					gotDestAfter,
					movedCount);
				if (capacity)
				{
					capacity->Accept(acceptedWeight);
				}
				if (notifyMovedItems)
				{
					auto notificationInfo = request.info;
					notificationInfo.totalCount = observedMovedCount;
					QueueLootItemNotification(
						request.object,
						request.itemName,
						observedMovedCount,
						notificationInfo,
						&matchCache);
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
		const bool notifyMovedItems = dest && dest->IsPlayerRef() && !suppressPlayerMessages;
		if (dest && dest->IsPlayerRef())
		{
			PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
			return TransferInventoryItemsImpl(
				src,
				dest,
				itemType,
				subType,
				looseModKeyword,
				&capacity,
				notifyMovedItems);
		}

		return TransferInventoryItemsImpl(
			src,
			dest,
			itemType,
			subType,
			looseModKeyword,
			&capacity,
			notifyMovedItems);
	}

	bool IsLootingSafe(std::monostate)
	{
		auto* ui = UI::GetSingleton();
		if (ui && ui->menuMode > 0)
		{
			return false;
		}

		auto* vats = VATS::GetSingleton();
		if (vats && vats->mode == VATS::VATS_MODE_ENUM::kPlayback)
		{
			return false;
		}

		return true;
	}

	void MoveInventoryItem(
		std::monostate,
		TESObjectREFR* src,
		TESObjectREFR* dest,
		TESForm* item,
		std::int32_t count,
		bool silent)
	{
		if (!src || !dest || src == dest || !item)
		{
			return;
		}

		auto* object = item->As<TESBoundObject>();
		if (!object)
		{
			return;
		}

		std::int32_t resolvedCount = count;
		if (resolvedCount < 0 && !TryGetReferenceItemCountSafe(src, object, resolvedCount))
		{
			return;
		}
		if (resolvedCount <= 0)
		{
			return;
		}

		std::vector<InventoryFormTransferRequest> requests;
		if (src->IsPlayerRef())
		{
			auto inventoryList = src->inventoryList;
			if (!inventoryList)
			{
				return;
			}

			std::vector<BGSMod::Attachment::Mod*> modBuffer;
			std::int32_t remainingRequested = resolvedCount;
			{
				ReadLockGuard guard(inventoryList->rwLock);
				for (auto& inventoryItem : inventoryList->data)
				{
					if (!inventoryItem.object || inventoryItem.object->formID != object->formID)
					{
						continue;
					}

					const bool formIsFavorite = IsFavorite(object);
					const bool hasFavoriteStack = HasInventoryFavoriteStack(inventoryItem);
					bool retainedFormFavorite = false;
					std::vector<InventoryFormTransferRequest> itemRequests;
					itemRequests.reserve(4);

					std::uint32_t stackIndex = 0;
					for (auto stack = inventoryItem.stackData.get();
					     stack && remainingRequested > 0;
					     stack = stack->nextStack.get(), ++stackIndex)
					{
						InventoryItemInfo stackInfo{};
						if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, inventory_info_basic, stackInfo))
						{
							REX::WARN("MoveInventoryItem: skip stack for {:08X}: stack-info-exception", object->formID);
							continue;
						}
						if (stackInfo.totalCount <= 0)
						{
							continue;
						}

						const auto protectedCount = GetPlayerProtectedStackCount(
							object,
							*stack,
							stackInfo,
							true,
							false,
							formIsFavorite,
							hasFavoriteStack,
							retainedFormFavorite);
						const auto movableCount = stackInfo.totalCount - protectedCount;
						if (movableCount <= 0)
						{
							continue;
						}

						const auto requestCount = std::min(movableCount, remainingRequested);
						itemRequests.push_back(InventoryFormTransferRequest{
							object,
							requestCount,
							0.0F,
							stackIndex,
							stack->extra,
							ShouldPreserveStackExtraForTransfer(
								object,
								*stack,
								requestCount,
								stackInfo.totalCount)
						});
						remainingRequested -= requestCount;
					}

					for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
					{
						requests.push_back(*it);
					}
					break;
				}
			}
		}
		else
		{
			auto inventoryList = src->inventoryList;
			if (!inventoryList)
			{
				return;
			}

			std::vector<BGSMod::Attachment::Mod*> modBuffer;
			std::int32_t remainingRequested = resolvedCount;
			{
				ReadLockGuard guard(inventoryList->rwLock);
				for (auto& inventoryItem : inventoryList->data)
				{
					if (!inventoryItem.object || inventoryItem.object->formID != object->formID)
					{
						continue;
					}

					std::vector<InventoryFormTransferRequest> itemRequests;
					itemRequests.reserve(4);
					std::uint32_t stackIndex = 0;
					for (auto stack = inventoryItem.stackData.get();
					     stack && remainingRequested > 0;
					     stack = stack->nextStack.get(), ++stackIndex)
					{
						InventoryItemInfo stackInfo{};
						std::int32_t stackCount = 0;
						if (TryGetInventoryStackInfoSafe(*stack, modBuffer, inventory_info_basic, stackInfo))
						{
							stackCount = stackInfo.totalCount;
						}
						else
						{
							stackCount = static_cast<std::int32_t>(std::min<std::uint32_t>(
								stack->count,
								static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())));
						}
						if (stackCount <= 0)
						{
							continue;
						}

						const auto requestCount = std::min(stackCount, remainingRequested);
						itemRequests.push_back(InventoryFormTransferRequest{
							object,
							requestCount,
							0.0F,
							stackIndex,
							stack->extra,
							ShouldPreserveStackExtraForTransfer(
								object,
								*stack,
								requestCount,
								stackCount)
						});
						remainingRequested -= requestCount;
					}

					for (auto it = itemRequests.rbegin(); it != itemRequests.rend(); ++it)
					{
						requests.push_back(*it);
					}
					break;
				}
			}
		}

		if (requests.empty())
		{
			return;
		}

		auto move = [&]()
		{
			for (const auto& request : requests)
			{
				auto remaining = request.count;
				if (request.preserveStackExtra)
				{
					if (!TryMoveInventoryItemPreservingStackExtraSafe(
							src,
							dest,
							request.object,
							request.count,
							request.stackIndex,
							request.extra))
					{
						REX::WARN(
							"MoveInventoryItem: instance-preserving transfer failed, src={:08X}, dest={:08X}, item={:08X}, count={}, stack={}",
							src->formID,
							dest->formID,
							request.object->formID,
							request.count,
							request.stackIndex ? static_cast<std::int32_t>(*request.stackIndex) : -1);
						remaining = request.count;
					}
					else
					{
						remaining = 0;
					}
				}
				while (remaining > 0 && !request.preserveStackExtra)
				{
					const auto chunk = std::min<std::int32_t>(remaining, 65535);
					if (!TryMoveInventoryItemSafe(
							src,
							dest,
							request.object,
							chunk,
							request.stackIndex))
					{
						REX::WARN(
							"MoveInventoryItem: transfer failed, src={:08X}, dest={:08X}, item={:08X}, remaining={}, stack={}",
							src->formID,
							dest->formID,
							item->formID,
							remaining,
							request.stackIndex ? static_cast<std::int32_t>(*request.stackIndex) : -1);
						return;
					}
					remaining -= chunk;
				}
			}
		};

		if ((src->IsPlayerRef() || dest->IsPlayerRef()) && silent)
		{
			PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
			move();
			return;
		}

		move();
	}

	void MoveInventoryItems(
		std::monostate,
		TESObjectREFR* src,
		TESObjectREFR* dest,
		std::uint32_t itemType,
		std::int32_t subType,
		bool silent)
	{
		const auto startedAt = Clock::now();
		if (!src || !dest || src == dest || itemType > all_item)
		{
			return;
		}

		std::int32_t movedItems = 0;
		if ((src->IsPlayerRef() || dest->IsPlayerRef()) && silent)
		{
			PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
			movedItems = TransferInventoryItemsImpl(src, dest, itemType, subType, nullptr);
		}
		else
		{
			movedItems = TransferInventoryItemsImpl(src, dest, itemType, subType, nullptr);
		}

		if (movedItems > 0)
		{
			REX::DEBUG(
				"MoveInventoryItems: src={:08X}, dest={:08X}, itemType={}, subType={}, moved={}, elapsed_ms={:.3f}",
				src->formID,
				dest->formID,
				itemType,
				subType,
				movedItems,
				ElapsedMilliseconds(startedAt));
		}
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

	struct ScrapInventoryItem
	{
		TESForm* form = nullptr;
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
		std::optional<std::uint32_t> stackIndex;
		bool isMisc = false;
		std::unordered_map<BGSComponent*, std::uint32_t> componentData;
	};

	using ScrapComponentTotals = std::unordered_map<TESObjectMISC*, std::uint32_t>;

	bool IsScrappableInventoryFormType(ENUM_FORM_ID formType)
	{
		return formType == ENUM_FORM_ID::kARMO ||
		       formType == ENUM_FORM_ID::kMISC ||
		       formType == ENUM_FORM_ID::kWEAP;
	}

	void AddAwardedComponent(
		std::vector<std::pair<TESObjectMISC*, std::uint32_t>>& awards,
		TESObjectMISC* item,
		std::uint32_t count)
	{
		if (!item || count == 0)
		{
			return;
		}

		for (auto& [awardedItem, awardedCount] : awards)
		{
			if (awardedItem == item)
			{
				awardedCount = SaturatingInventoryCount(
					static_cast<std::uint64_t>(awardedCount) + static_cast<std::uint64_t>(count));
				return;
			}
		}

		awards.emplace_back(item, count);
	}

	std::uint32_t GetAdjustedScrapComponentCount(
		BGSComponent* component,
		std::uint32_t rawCount,
		bool isMisc,
		std::size_t componentCount)
	{
		if (!component || rawCount == 0)
		{
			return 0;
		}

		if (isMisc)
		{
			return rawCount;
		}

		const auto adjustedRawCount = rawCount / 2;
		if (adjustedRawCount == 0)
		{
			return 0;
		}

		auto* scalar = component->modScrapScalar;
		auto scale = scalar ? scalar->value : 1.0F;
		if (componentCount == 1 && adjustedRawCount == 1 && scale < 1.0F)
		{
			scale = 1.0F;
		}
		if (!std::isfinite(scale) || scale <= 0.0F)
		{
			return 0;
		}

		const auto scaledCount = static_cast<double>(adjustedRawCount) * static_cast<double>(scale);
		if (!std::isfinite(scaledCount) || scaledCount <= 0.0)
		{
			return 0;
		}

		if (scaledCount >= static_cast<double>(std::numeric_limits<std::int32_t>::max()))
		{
			return static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
		}

		return static_cast<std::uint32_t>(scaledCount);
	}

	std::vector<ScrapInventoryItem> CollectScrapInventoryItems(
		TESObjectREFR* inventoryOwner,
		std::uint32_t itemType)
	{
		std::vector<ScrapInventoryItem> scrappableItems;
		if (!inventoryOwner || itemType > all_item)
		{
			return scrappableItems;
		}
		EnsureItemTypeCache();

		auto* inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList)
		{
			return scrappableItems;
		}

		const bool ownerIsPlayer = inventoryOwner->IsPlayerRef();
		const bool ownerIsDead = IsDeadForLooting(inventoryOwner);
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		std::unordered_map<BGSComponent*, std::uint32_t> baseComponentData;
		std::unordered_map<BGSComponent*, std::uint32_t> modComponentData;
		std::unordered_map<BGSComponent*, std::uint32_t> componentData;
		modBuffer.reserve(8);
		baseComponentData.reserve(16);
		modComponentData.reserve(16);
		componentData.reserve(16);

		ReadLockGuard guard(inventoryList->rwLock);
		scrappableItems.reserve(inventoryList->data.size());

		for (auto& item : inventoryList->data)
		{
			auto* form = item.object;
			auto* object = form ? form->As<TESBoundObject>() : nullptr;
			if (!form || !object)
			{
				continue;
			}

			const auto formType = form->GetFormType();
			if (!IsScrappableInventoryFormType(formType) ||
			    !IsPlayable(form) ||
			    !IsFormTypeMatchesItemType(formType, itemType) ||
			    HasKeyword(form, keyword::unscrappableObject) ||
			    HasKeyword(form, keyword::featuredItem))
			{
				continue;
			}

			if (ownerIsPlayer && form->formID == 0x0F)
			{
				continue;
			}

			if (formType == ENUM_FORM_ID::kMISC)
			{
				auto* miscObject = form->As<TESObjectMISC>();
				if (!miscObject || !miscObject->componentData)
				{
					continue;
				}

				const bool formIsFavorite = ownerIsPlayer && IsFavorite(form);
				const bool hasFavoriteStack = ownerIsPlayer && HasInventoryFavoriteStack(item);
				bool retainedFormFavorite = false;
				std::vector<ScrapInventoryItem> itemEntries;
				itemEntries.reserve(4);

				std::uint32_t stackIndex = 0;
				for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex)
				{
					InventoryItemInfo stackInfo{};
					if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, inventory_info_full, stackInfo))
					{
						REX::WARN("ScrapInventoryItems: skip stack for {:08X}: stack-info-exception", form->formID);
						continue;
					}

					if (stackInfo.totalCount <= 0 ||
					    stackInfo.featured ||
					    stackInfo.unscrappable ||
					    stackInfo.questItem)
					{
						continue;
					}

					const auto protectedCount = GetPlayerProtectedStackCount(
						form,
						*stack,
						stackInfo,
						ownerIsPlayer,
						ownerIsDead,
						formIsFavorite,
						hasFavoriteStack,
						retainedFormFavorite);
					const auto scrapCount = stackInfo.totalCount - protectedCount;
					if (scrapCount <= 0)
					{
						continue;
					}

					componentData.clear();
					for (auto& [componentForm, componentValue] : *miscObject->componentData)
					{
						auto* component = componentForm ? componentForm->As<BGSComponent>() : nullptr;
						if (!component || componentValue.i <= 0)
						{
							continue;
						}

						AddRawComponentCount(
							componentData,
							component,
							static_cast<std::uint64_t>(componentValue.i) *
								static_cast<std::uint64_t>(scrapCount));
					}

					if (!componentData.empty())
					{
						ScrapInventoryItem entry{
							form,
							object,
							scrapCount,
							stackIndex,
							true,
							{}
						};
						entry.componentData.swap(componentData);
						itemEntries.push_back(std::move(entry));
					}
				}

				for (auto it = itemEntries.rbegin(); it != itemEntries.rend(); ++it)
				{
					scrappableItems.push_back(std::move(*it));
				}
				continue;
			}

			baseComponentData.clear();
			ExtractConstructibleComponents(
				constructible_object::FromCreatedObjectId(form->formID),
				baseComponentData);

			const bool formIsFavorite = ownerIsPlayer && IsFavorite(form);
			const bool hasFavoriteStack = ownerIsPlayer && HasInventoryFavoriteStack(item);
			bool retainedFormFavorite = false;
			std::vector<ScrapInventoryItem> itemEntries;
			itemEntries.reserve(4);
			std::uint32_t stackIndex = 0;
			for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex)
			{
				InventoryItemInfo stackInfo{};
				if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, inventory_info_full, stackInfo))
				{
					REX::WARN("ScrapInventoryItems: skip stack for {:08X}: stack-info-exception", form->formID);
					continue;
				}

				if (stackInfo.totalCount <= 0 ||
				    stackInfo.featured ||
				    stackInfo.unscrappable ||
				    stackInfo.questItem)
				{
					continue;
				}

				const auto protectedCount = GetPlayerProtectedStackCount(
					form,
					*stack,
					stackInfo,
					ownerIsPlayer,
					ownerIsDead,
					formIsFavorite,
					hasFavoriteStack,
					retainedFormFavorite);
				const auto scrapCount = stackInfo.totalCount - protectedCount;
				if (scrapCount <= 0)
				{
					continue;
				}

				componentData.clear();
				MergeComponentData(
					componentData,
					baseComponentData,
					static_cast<std::uint32_t>(scrapCount));

				modComponentData.clear();
				if (TryExtractModComponentsSafe(modBuffer, modComponentData))
				{
					MergeComponentData(
						componentData,
						modComponentData,
						static_cast<std::uint32_t>(scrapCount));
				}
				else
				{
					REX::WARN("ScrapInventoryItems: skip mod components for {:08X}: mod-extract-exception", form->formID);
				}

				if (componentData.empty())
				{
					continue;
				}

				ScrapInventoryItem entry{
					form,
					object,
					scrapCount,
					stackIndex,
					false,
					{}
				};
				entry.componentData.swap(componentData);
				itemEntries.push_back(std::move(entry));
			}

			for (auto it = itemEntries.rbegin(); it != itemEntries.rend(); ++it)
			{
				scrappableItems.push_back(std::move(*it));
			}
		}

		return scrappableItems;
	}

	bool RollBackAwardedComponents(
		TESObjectREFR* componentReceiver,
		const std::vector<std::pair<TESObjectMISC*, std::uint32_t>>& awards)
	{
		bool rollbackSucceeded = true;
		for (auto it = awards.rbegin(); it != awards.rend(); ++it)
		{
			auto* item = it->first;
			const auto count = it->second;
			if (!item || count == 0)
			{
				continue;
			}

			if (!TryRemoveItemsSafe(componentReceiver, item, static_cast<std::int32_t>(count)))
			{
				rollbackSucceeded = false;
			}
		}
		return rollbackSucceeded;
	}

	ScrapComponentTotals ApplyScrapInventoryItems(
		TESObjectREFR* inventoryOwner,
		TESObjectREFR* componentReceiver,
		const std::vector<ScrapInventoryItem>& scrappableItems)
	{
		ScrapComponentTotals componentTotals;
		componentTotals.reserve(16);
		if (!inventoryOwner || !componentReceiver)
		{
			return componentTotals;
		}

		for (const auto& entry : scrappableItems)
		{
			if (!entry.form || !entry.object || entry.count <= 0 || entry.componentData.empty())
			{
				continue;
			}

			std::vector<std::pair<TESObjectMISC*, std::uint32_t>> awards;
			awards.reserve(entry.componentData.size());
			for (const auto& [component, rawCount] : entry.componentData)
			{
				const auto adjustedCount = GetAdjustedScrapComponentCount(
					component,
					rawCount,
					entry.isMisc,
					entry.componentData.size());
				auto* scrapItem = component ? component->scrapItem : nullptr;
				AddAwardedComponent(awards, scrapItem, adjustedCount);
			}

			if (awards.empty())
			{
				continue;
			}

			std::vector<std::pair<TESObjectMISC*, std::uint32_t>> addedAwards;
			addedAwards.reserve(awards.size());
			bool addedAll = true;
			for (const auto& [scrapItem, count] : awards)
			{
				if (!TryAddInventoryItemSafe(componentReceiver, scrapItem, count))
				{
					REX::WARN(
						"ScrapInventoryItems: component-insert-exception, owner={:08X}, source={:08X}, component={:08X}, count={}",
						inventoryOwner->formID,
						entry.form->formID,
						scrapItem ? scrapItem->formID : 0,
						count);
					addedAll = false;
					break;
				}
				addedAwards.emplace_back(scrapItem, count);
			}

			if (!addedAll)
			{
				const auto rollbackSucceeded = RollBackAwardedComponents(componentReceiver, addedAwards);
				REX::WARN(
					"ScrapInventoryItems: source retained after component insert failure, owner={:08X}, source={:08X}, inserted_components={}, rollback_succeeded={}",
					inventoryOwner->formID,
					entry.form->formID,
					addedAwards.size(),
					rollbackSucceeded);
				continue;
			}

			std::int32_t beforeCount = 0;
			std::int32_t afterCount = 0;
			const bool gotBefore = TryGetReferenceItemCountSafe(inventoryOwner, entry.object, beforeCount);
			const bool removed = TryRemoveScrapSourceSafe(
				inventoryOwner,
				entry.object,
				entry.count,
				entry.stackIndex);
			const bool gotAfter = TryGetReferenceItemCountSafe(inventoryOwner, entry.object, afterCount);
			const bool observedRemoval = removed && (!gotBefore || !gotAfter || afterCount < beforeCount);

			if (!observedRemoval)
			{
				const auto rollbackSucceeded = RollBackAwardedComponents(componentReceiver, addedAwards);
				REX::WARN(
					"ScrapInventoryItems: source removal failed, owner={:08X}, source={:08X}, count={}, stack={}, before={}, after={}, gotBefore={}, gotAfter={}, rollback_succeeded={}",
					inventoryOwner->formID,
					entry.form->formID,
					entry.count,
					entry.stackIndex ? static_cast<std::int32_t>(*entry.stackIndex) : -1,
					beforeCount,
					afterCount,
					gotBefore,
					gotAfter,
					rollbackSucceeded);
				continue;
			}

			for (const auto& [scrapItem, count] : awards)
			{
				componentTotals[scrapItem] = SaturatingInventoryCount(
					static_cast<std::uint64_t>(componentTotals[scrapItem]) +
					static_cast<std::uint64_t>(count));
			}
		}

		return componentTotals;
	}

	std::vector<std::int32_t> FlattenScrapComponentTotals(const ScrapComponentTotals& componentTotals)
	{
		struct FlatComponentEntry
		{
			TESObjectMISC* form = nullptr;
			std::uint32_t count = 0;
			std::string name;
		};

		std::vector<FlatComponentEntry> entries;
		entries.reserve(componentTotals.size());
		for (const auto& [form, count] : componentTotals)
		{
			if (!form || count == 0)
			{
				continue;
			}
			entries.push_back({ form, count, GetFormName(form) });
		}

		std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs)
		{
			if (lhs.count != rhs.count)
			{
				return lhs.count > rhs.count;
			}
			if (lhs.name != rhs.name)
			{
				return lhs.name < rhs.name;
			}
			return lhs.form->formID < rhs.form->formID;
		});

		std::vector<std::int32_t> flattened;
		flattened.reserve(entries.size() * 2);
		for (const auto& entry : entries)
		{
			flattened.push_back(static_cast<std::int32_t>(entry.form->formID));
			flattened.push_back(static_cast<std::int32_t>(entry.count));
		}
		return flattened;
	}

	std::vector<std::int32_t> ScrapInventoryItemsWithResults(
		std::monostate,
		TESObjectREFR* inventoryOwner,
		TESObjectREFR* componentReceiver,
		std::uint32_t itemType)
	{
		const auto startedAt = Clock::now();
		if (!inventoryOwner || !componentReceiver || itemType > all_item)
		{
			return {};
		}

		auto scrappableItems = CollectScrapInventoryItems(inventoryOwner, itemType);
		ScrapComponentTotals componentTotals;
		if (inventoryOwner->IsPlayerRef() || componentReceiver->IsPlayerRef())
		{
			PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
			componentTotals = ApplyScrapInventoryItems(
				inventoryOwner,
				componentReceiver,
				scrappableItems);
		}
		else
		{
			componentTotals = ApplyScrapInventoryItems(
				inventoryOwner,
				componentReceiver,
				scrappableItems);
		}

		if (!scrappableItems.empty())
		{
			REX::DEBUG(
				"ScrapInventoryItems: owner={:08X}, receiver={:08X}, itemType={}, candidates={}, components={}, elapsed_ms={:.3f}",
				inventoryOwner->formID,
				componentReceiver->formID,
				itemType,
				scrappableItems.size(),
				componentTotals.size(),
				ElapsedMilliseconds(startedAt));
		}

		return FlattenScrapComponentTotals(componentTotals);
	}

	void ScrapInventoryItems(
		std::monostate,
		TESObjectREFR* inventoryOwner,
		TESObjectREFR* componentReceiver,
		std::uint32_t itemType)
	{
		(void)ScrapInventoryItemsWithResults(
			std::monostate{},
			inventoryOwner,
			componentReceiver,
			itemType);
	}

	// 7. GetScrappableItems
	std::vector<TESForm*> GetScrappableItems(
		std::monostate, TESObjectREFR* inventoryOwner, std::uint32_t itemType)
	{
		std::vector<TESForm*> result;

		if (!inventoryOwner || itemType > all_item)
		{
			return result;
		}

		auto scrappableItems = CollectScrapInventoryItems(inventoryOwner, itemType);
		result.reserve(scrappableItems.size());

		std::unordered_set<std::uint32_t> seenForms;
		seenForms.reserve(scrappableItems.size());
		for (const auto& item : scrappableItems)
		{
			if (!item.form || !seenForms.insert(item.form->formID).second)
			{
				continue;
			}
			result.push_back(item.form);
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
		InstallWorkbenchSharedContainerHooks();
		InstallWorkshopMaterialProbeHooks();
	}

	bool Register(RE::BSScript::IVirtualMachine* vm)
	{
		REX::DEBUG("[ Started binding papyrus functions for LootMan ]");

		vm->BindNativeMethod("LTMN2:LootMan"sv, "FindNearbyReferencesWithFormType"sv,
			&FindNearbyReferencesWithFormType, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "FindNearbyReferenceIdsWithFormType"sv,
			&FindNearbyReferenceIdsWithFormType, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "FindNearestValidWorkshopId"sv,
			&FindNearestValidWorkshopId, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetEquipmentComponents"sv,
			&GetEquipmentComponents, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetFormType"sv,
			&GetFormType, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "Log"sv,
			&Log, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetFormTypeIdentifier"sv,
			&GetFormTypeIdentifier, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetHexID"sv,
			&GetHexID, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetName"sv,
			&GetName, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "LogInventoryDiagnostics"sv,
			&LogInventoryDiagnostics, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "LogWorkshopSupplyDiagnostics"sv,
			&LogWorkshopSupplyDiagnostics, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "RememberWorkshopSupplyLink"sv,
			&RememberWorkshopSupplyLink, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "ForgetWorkshopSupplyLink"sv,
			&ForgetWorkshopSupplyLink, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetInventoryItemsWithItemType"sv,
			&GetInventoryItemsWithItemType, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetLootableItems"sv,
			&GetLootableItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "TransferLootableInventoryItems"sv,
			&TransferLootableInventoryItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "TransferInventoryItems"sv,
			&TransferInventoryItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "IsLootingSafe"sv,
			&IsLootingSafe, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "MoveInventoryItem"sv,
			&MoveInventoryItem, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "MoveInventoryItems"sv,
			&MoveInventoryItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "LootNearbyReferences"sv,
			&LootNearbyReferences, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetScrappableItems"sv,
			&GetScrappableItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "ScrapInventoryItems"sv,
			&ScrapInventoryItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "ScrapInventoryItemsWithResults"sv,
			&ScrapInventoryItemsWithResults, false, false);
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
