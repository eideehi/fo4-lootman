#include "properties.h"
#include "runtime_probe.h"
#include "utility.h"

#include <atomic>
#include <shared_mutex>

namespace properties
{
	namespace
	{
		std::atomic<std::uint64_t> propertyProbeEpoch{ 0 };
		std::atomic<std::uint32_t> propertyProbeRecords{ 0 };
		std::atomic<std::uint32_t> activePropertyUpdates{ 0 };
		std::atomic<std::uint32_t> activePropertyCopies{ 0 };
		thread_local std::uint64_t currentPropertyUpdateEpoch = 0;
		thread_local bool currentPropertyProbeSession = false;
		constexpr std::uint32_t kPropertyProbeRecordLimit = 256;

		bool ReservePropertyProbeRecord(std::uint64_t& sequence)
		{
			if (!runtime_probe::TryReserve(propertyProbeRecords, kPropertyProbeRecordLimit))
			{
				return false;
			}
			sequence = runtime_probe::NextSequence();
			return true;
		}

		void TracePropertyProbe(
			const char* event,
			const char* property,
			std::uint64_t epoch,
			std::uint32_t activeUpdates,
			std::uint32_t activeCopies,
			const char* result)
		{
			std::uint64_t sequence = 0;
			if (!ReservePropertyProbeRecord(sequence))
			{
				return;
			}
			REX::TRACE(
				"source=native component=runtime_probe event={} probe_schema=1 ordering=reservation_only seq={} thread_id={} update_epoch={} active_updates_snapshot={} active_copies_snapshot={} property={} result={}",
				event,
				sequence,
				REX::W32::GetCurrentThreadId(),
				epoch,
				activeUpdates,
				activeCopies,
				property,
				result);
		}

		class PropertyUpdateProbeScope
		{
		public:
			explicit PropertyUpdateProbeScope(bool updateAll) :
				enabled(runtime_probe::IsEnabled()),
				previousEpoch(currentPropertyUpdateEpoch),
				previousSession(currentPropertyProbeSession)
			{
				if (!enabled)
				{
					return;
				}
				epoch = propertyProbeEpoch.fetch_add(1, std::memory_order_relaxed) + 1;
				currentPropertyUpdateEpoch = epoch;
				currentPropertyProbeSession = true;
				const auto active = activePropertyUpdates.fetch_add(1, std::memory_order_acq_rel) + 1;
				TracePropertyProbe("property_update", updateAll ? "all" : "named", epoch, active,
					activePropertyCopies.load(std::memory_order_acquire), "enter");
			}

			~PropertyUpdateProbeScope()
			{
				if (!enabled)
				{
					return;
				}
				const auto active = activePropertyUpdates.fetch_sub(1, std::memory_order_acq_rel) - 1;
				TracePropertyProbe("property_update", "none", epoch, active,
					activePropertyCopies.load(std::memory_order_acquire), "exit");
				currentPropertyUpdateEpoch = previousEpoch;
				currentPropertyProbeSession = previousSession;
			}

		private:
			bool enabled = false;
			std::uint64_t epoch = 0;
			std::uint64_t previousEpoch = 0;
			bool previousSession = false;
		};

		class PropertyCopyProbeScope
		{
		public:
			explicit PropertyCopyProbeScope(
				std::uint32_t& outActiveUpdatesAtEnter,
				std::uint32_t& outActiveCopiesAtEnter) :
				enabled(currentPropertyProbeSession)
			{
				if (enabled)
				{
					outActiveUpdatesAtEnter = activePropertyUpdates.load(std::memory_order_acquire);
					outActiveCopiesAtEnter = activePropertyCopies.fetch_add(1, std::memory_order_acq_rel) + 1;
				}
			}

			~PropertyCopyProbeScope() noexcept
			{
				if (!enabled)
				{
					return;
				}
				activePropertyCopies.fetch_sub(1, std::memory_order_acq_rel);
			}

		private:
			bool enabled = false;
		};
	}

	// Resolved on the main thread in Initialize() (kGameLoaded) but read from VM worker threads via
	// GetPapyrusProperty(); kept atomic so a reload cannot tear the pointer out from under a reader.
	std::atomic<RE::TESForm*> propertiesQuest = nullptr;
	RE::TESObjectREFR* lootManWorkshopRef = nullptr;
	// Read-mostly (written only by Initialize()/Update() on MCM/load events) but read per-container from
	// concurrent VM worker threads; a shared_mutex lets those reads run in parallel instead of
	// serializing on an exclusive lock, matching the vendor_chest cache.
	std::shared_mutex lock;
	std::unordered_map<Key, Value> papyrusProperties;

	// Whether the last full update actually reached the Papyrus property object.
	// Read without `lock` because callers only need the outcome of the last full
	// update, not a value snapshot consistent with it.
	std::atomic<bool> valuesResolved{ false };

	// The key the resolution check reads. It has to be a property that is copied
	// straight out of the script object, so the witness reports the one read it
	// stands for: an aggregate such as `enabled_looting_form_type_mask` folds
	// twelve reads into a single value and would report those reads instead.
	constexpr Key kResolutionWitness = enable_lootman;

	bool GetPapyrusProperty(const char* propertyName, RE::BSScript::Variable& outValue)
	{
		const auto scriptName = "LTMN2:Properties"sv;
		auto* quest = propertiesQuest.load(std::memory_order_acquire);
		if (!quest || !propertyName)
		{
			return false;
		}

		auto* gameVM = RE::GameVM::GetSingleton();
		if (!gameVM)
		{
			return false;
		}

		auto vm = gameVM->GetVM().get();
		if (!vm)
		{
			return false;
		}

		// Resolve the bound script instance on the quest object, then read its property slot.
		auto& handles = vm->GetObjectHandlePolicy();
		auto handle = handles.GetHandleForObject(
			static_cast<std::uint32_t>(quest->GetFormType()),
			quest);

		RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> typeInfo;
		if (!vm->GetScriptObjectType(scriptName, typeInfo) || !typeInfo)
		{
			return false;
		}

		RE::BSTSmartPointer<RE::BSScript::Object> object;
		if (!vm->FindBoundObject(handle, typeInfo->name.c_str(), false, object, false) || !object)
		{
			return false;
		}

		auto propName = RE::BSFixedString(propertyName);
		auto prop = object->GetProperty(propName);
		if (!prop)
		{
			REX::WARN(
				"source=native component=properties event=property_lookup_failed script=\"{}\" property=\"{}\"",
				scriptName,
				propertyName);
			return false;
		}

		std::uint32_t activeUpdatesAtEnter = 0;
		std::uint32_t activeCopiesAtEnter = 0;
		{
			PropertyCopyProbeScope copyProbe(activeUpdatesAtEnter, activeCopiesAtEnter);
			outValue = *prop;
		}
		if (currentPropertyProbeSession)
		{
			TracePropertyProbe("property_copy", propertyName, currentPropertyUpdateEpoch,
				activeUpdatesAtEnter, activeCopiesAtEnter, "completed");
		}
		return true;
	}

	Value GetBoolProperty(const char* propertyName)
	{
		Value result;
		RE::BSScript::Variable value;
		if (GetPapyrusProperty(propertyName, value))
		{
			result.type = boolean;
			result.data.b = RE::BSScript::get<bool>(value);
		}
		return result;
	}

	Value GetIntProperty(const char* propertyName)
	{
		Value result;
		RE::BSScript::Variable value;
		if (GetPapyrusProperty(propertyName, value))
		{
			result.type = integer;
			result.data.i = RE::BSScript::get<std::int32_t>(value);
		}
		return result;
	}

	Value GetFloatProperty(const char* propertyName)
	{
		Value result;
		RE::BSScript::Variable value;
		if (GetPapyrusProperty(propertyName, value))
		{
			result.type = decimal;
			result.data.f = RE::BSScript::get<float>(value);
		}
		return result;
	}

	// Packs the twelve per-form-type looting toggles into one integer. The
	// aggregate is only worth as much as the reads behind it: a toggle that cannot
	// be read carries no value, and folding it in as `false` would publish a
	// resolved-looking mask whose bits are partly guesses. One failed read
	// therefore leaves the whole mask unresolved, so every reader sees that the
	// setting is unknown instead of reading a bit that was never actually read.
	Value BuildEnabledLootingFormTypeMask()
	{
		bool allResolved = true;
		int mask = 0;

		const auto applyToggle = [&](const char* propertyName, const int bit) {
			const auto toggle = GetBoolProperty(propertyName);
			if (toggle.type != boolean)
			{
				allResolved = false;
				return;
			}
			if (toggle.data.b)
			{
				mask |= bit;
			}
		};

		applyToggle("EnableObjectLootingOfACTI", kEnableFormTypeACTI);
		applyToggle("EnableObjectLootingOfALCH", kEnableFormTypeALCH);
		applyToggle("EnableObjectLootingOfAMMO", kEnableFormTypeAMMO);
		applyToggle("EnableObjectLootingOfARMO", kEnableFormTypeARMO);
		applyToggle("EnableObjectLootingOfBOOK", kEnableFormTypeBOOK);
		applyToggle("EnableObjectLootingOfCONT", kEnableFormTypeCONT);
		applyToggle("EnableObjectLootingOfFLOR", kEnableFormTypeFLOR);
		applyToggle("EnableObjectLootingOfINGR", kEnableFormTypeINGR);
		applyToggle("EnableObjectLootingOfKEYM", kEnableFormTypeKEYM);
		applyToggle("EnableObjectLootingOfMISC", kEnableFormTypeMISC);
		applyToggle("EnableObjectLootingOfNPC_", kEnableFormTypeNPC_);
		applyToggle("EnableObjectLootingOfWEAP", kEnableFormTypeWEAP);

		Value result;
		if (!allResolved)
		{
			// Left null on purpose. Readers that ask for the type see an unresolved
			// value; readers that ask for a number get their own default, which is the
			// same answer they already get before the first update publishes anything.
			return result;
		}

		result.type = integer;
		result.data.i = mask;
		return result;
	}

	RE::TESObjectREFR* GetObjectReferenceProperty(const char* propertyName)
	{
		RE::BSScript::Variable value;
		if (!GetPapyrusProperty(propertyName, value))
		{
			return nullptr;
		}

		return RE::BSScript::UnpackVariable<RE::TESObjectREFR>(value);
	}

	Value Get(const Key key)
	{
		std::shared_lock<std::shared_mutex> guard(lock);
		const auto it = papyrusProperties.find(key);
		return it != papyrusProperties.end() ? it->second : Value();
	}

	bool IsResolved()
	{
		return valuesResolved.load(std::memory_order_acquire);
	}

	bool GetBool(const Key key, const bool defaultValue)
	{
		const auto value = Get(key);
		return value.type == boolean ? value.data.b : defaultValue;
	}

	int GetInt(const Key key, const int defaultValue)
	{
		const auto value = Get(key);
		return value.type == integer ? value.data.i : defaultValue;
	}

	float GetFloat(const Key key, const float defaultValue)
	{
		const auto value = Get(key);
		return value.type == decimal ? value.data.f : defaultValue;
	}

	RE::TESObjectREFR* GetLootManWorkshopRef()
	{
		std::shared_lock<std::shared_mutex> guard(lock);
		return lootManWorkshopRef;
	}

	void Initialize()
	{
		// LTMN_Properties is a fixed quest record in LootMan.esp; native code reads its script properties directly.
		auto* quest = utility::LookupForm("LootMan.esp|000F9A");
		propertiesQuest.store(quest, std::memory_order_release);
		valuesResolved.store(false, std::memory_order_release);
		{
			std::unique_lock<std::shared_mutex> guard(lock);
			papyrusProperties.clear();
			lootManWorkshopRef = nullptr;
		}
		if (!quest)
		{
			REX::WARN("source=native component=properties event=properties_quest_resolution_failed form=LootMan.esp|000F9A");
		}
	}

	void Update(const char* key)
	{
		const auto updateProperty = std::string(key ? key : "");
		const bool updateAll = updateProperty.empty();
		PropertyUpdateProbeScope probeScope(updateAll);
		// Collect first, then publish under lock so readers never observe partially refreshed settings.
		std::unordered_map<Key, Value> updates;
		RE::TESObjectREFR* updatedLootManWorkshopRef = nullptr;
		bool updateLootManWorkshopRef = false;

		auto propertyName = "IsInstalled";
		if (updateAll || propertyName == updateProperty)
		{
			updates[is_installed] = GetBoolProperty(propertyName);
		}

		propertyName = "IsInitialized";
		if (updateAll || propertyName == updateProperty)
		{
			updates[is_initialized] = GetBoolProperty(propertyName);
		}

		propertyName = "IsUninstalled";
		if (updateAll || propertyName == updateProperty)
		{
			updates[is_uninstalled] = GetBoolProperty(propertyName);
		}

		propertyName = "MaxItemsProcessedPerThread";
		if (updateAll || propertyName == updateProperty)
		{
			updates[max_items_processed_per_thread] = GetIntProperty(propertyName);
		}

		propertyName = "MaxLootableObjectsPerPass";
		if (updateAll || propertyName == updateProperty)
		{
			updates[max_lootable_objects_per_pass] = GetIntProperty(propertyName);
		}

		propertyName = "MaxContainersPerPass";
		if (updateAll || propertyName == updateProperty)
		{
			updates[max_containers_per_pass] = GetIntProperty(propertyName);
		}

		propertyName = "MaxActorsPerPass";
		if (updateAll || propertyName == updateProperty)
		{
			updates[max_actors_per_pass] = GetIntProperty(propertyName);
		}

		propertyName = "MaxActivationRefsPerPass";
		if (updateAll || propertyName == updateProperty)
		{
			updates[max_activation_refs_per_pass] = GetIntProperty(propertyName);
		}

		propertyName = "UseLootingTimeBudget";
		if (updateAll || propertyName == updateProperty)
		{
			updates[use_looting_time_budget] = GetBoolProperty(propertyName);
		}

		propertyName = "LootingTimeBudgetMs";
		if (updateAll || propertyName == updateProperty)
		{
			updates[looting_time_budget_ms] = GetFloatProperty(propertyName);
		}

		if (updateAll || updateProperty.rfind("EnableObjectLootingOf", 0) == 0)
		{
			updates[enabled_looting_form_type_mask] = BuildEnabledLootingFormTypeMask();
		}

		propertyName = "LootingRange";
		if (updateAll || propertyName == updateProperty)
		{
			updates[looting_range] = GetFloatProperty(propertyName);
		}

		propertyName = "NotLootingFromSettlement";
		if (updateAll || propertyName == updateProperty)
		{
			updates[not_looting_from_settlement] = GetBoolProperty(propertyName);
		}

		propertyName = "LootableInventoryItemType";
		if (updateAll || updateProperty.rfind("EnableInventoryLootingOf", 0) == 0)
		{
			// Papyrus stores UI-facing booleans, then recomputes this aggregate bitmask before notifying native code.
			updates[lootable_inventory_item_type] = GetIntProperty(propertyName);
		}

		propertyName = "LootingLegendaryOnly";
		if (updateAll || propertyName == updateProperty)
		{
			updates[looting_legendary_only] = GetBoolProperty(propertyName);
		}

		propertyName = "AlwaysLootingExplosives";
		if (updateAll || propertyName == updateProperty)
		{
			updates[always_looting_explosives] = GetBoolProperty(propertyName);
		}

		propertyName = "AlwaysLootingClothing";
		if (updateAll || propertyName == updateProperty)
		{
			updates[always_looting_clothing] = GetBoolProperty(propertyName);
		}

		propertyName = "CarryWeight";
		if (updateAll || propertyName == updateProperty)
		{
			updates[carry_weight] = GetIntProperty(propertyName);
		}

		propertyName = "IgnoreOverweight";
		if (updateAll || propertyName == updateProperty)
		{
			updates[ignore_overweight] = GetBoolProperty(propertyName);
		}

		propertyName = "LootIsDeliverToPlayer";
		if (updateAll || propertyName == updateProperty)
		{
			updates[loot_is_deliver_to_player] = GetBoolProperty(propertyName);
		}

		propertyName = "LootingWithoutLogs";
		if (updateAll || propertyName == updateProperty)
		{
			updates[looting_without_logs] = GetBoolProperty(propertyName);
		}

		propertyName = "LootableALCHItemType";
		if (updateAll || updateProperty.rfind("EnableALCHItem", 0) == 0)
		{
			updates[lootable_alch_item_type] = GetIntProperty(propertyName);
		}

		propertyName = "LootableBOOKItemType";
		if (updateAll || updateProperty.rfind("EnableBOOKItem", 0) == 0)
		{
			updates[lootable_book_item_type] = GetIntProperty(propertyName);
		}

		propertyName = "LootableMISCItemType";
		if (updateAll || updateProperty.rfind("EnableMISCItem", 0) == 0)
		{
			updates[lootable_misc_item_type] = GetIntProperty(propertyName);
		}

		propertyName = "LootableWEAPItemType";
		if (updateAll || updateProperty.rfind("EnableWEAPItem", 0) == 0)
		{
			updates[lootable_weap_item_type] = GetIntProperty(propertyName);
		}

		propertyName = "EnableLootMan";
		if (updateAll || propertyName == updateProperty)
		{
			updates[enable_lootman] = GetBoolProperty(propertyName);
		}

		propertyName = "DisplaySystemMessage";
		if (updateAll || propertyName == updateProperty)
		{
			updates[display_system_message] = GetBoolProperty(propertyName);
		}

		propertyName = "PlayPickupSound";
		if (updateAll || propertyName == updateProperty)
		{
			updates[play_pickup_sound] = GetBoolProperty(propertyName);
		}

		propertyName = "PlayContainerAnimation";
		if (updateAll || propertyName == updateProperty)
		{
			updates[play_container_animation] = GetBoolProperty(propertyName);
		}

		propertyName = "AutomaticallyLinkAndUnlinkToWorkshop";
		if (updateAll || propertyName == updateProperty)
		{
			updates[automatically_link_and_unlink_to_workshop] = GetBoolProperty(propertyName);
		}

		propertyName = "UnlockLockedContainer";
		if (updateAll || propertyName == updateProperty)
		{
			updates[unlock_locked_container] = GetBoolProperty(propertyName);
		}

		propertyName = "LootManWorkshopRef";
		if (updateAll || propertyName == updateProperty)
		{
			updateLootManWorkshopRef = true;
			updatedLootManWorkshopRef = GetObjectReferenceProperty(propertyName);
			if (!updatedLootManWorkshopRef)
			{
				REX::WARN(
					"source=native component=properties event=native_property_resolution_failed script=LTMN2:Properties property=\"{}\"",
					propertyName);
			}
		}

		if (updateAll)
		{
			// A full update that could not reach the property object leaves every value
			// null, and the typed accessors then return their caller's defaults. Record
			// which of the two happened so readers can tell a real setting from a
			// fallback.
			const auto witness = updates.find(kResolutionWitness);
			valuesResolved.store(
				witness != updates.end() && witness->second.type != null,
				std::memory_order_release);
		}

		if (updates.empty() && !updateLootManWorkshopRef)
		{
			// Unknown property names are ignored so Papyrus can call the hook for every MCM setting safely.
			return;
		}

		std::unique_lock<std::shared_mutex> guard(lock);
		for (auto& [propertyKey, value] : updates)
		{
			papyrusProperties[propertyKey] = value;
		}
		if (updateLootManWorkshopRef)
		{
			lootManWorkshopRef = updatedLootManWorkshopRef;
		}
	}
}
