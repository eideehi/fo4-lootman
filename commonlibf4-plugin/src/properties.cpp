#include "properties.h"
#include "utility.h"

namespace properties
{
	RE::TESForm* propertiesQuest = nullptr;
	RE::TESObjectREFR* lootManWorkshopRef = nullptr;
	std::mutex lock;
	std::unordered_map<Key, Value> papyrusProperties;

	bool GetPapyrusProperty(const char* propertyName, RE::BSScript::Variable& outValue)
	{
		const auto scriptName = "LTMN2:Properties"sv;
		if (!propertiesQuest || !propertyName)
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
			static_cast<std::uint32_t>(propertiesQuest->GetFormType()),
			propertiesQuest);

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
			REX::WARN("Failed to get property {}.{}", scriptName, propertyName);
			return false;
		}

		outValue = *prop;
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
		std::lock_guard<std::mutex> guard(lock);
		const auto it = papyrusProperties.find(key);
		return it != papyrusProperties.end() ? it->second : Value();
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
		std::lock_guard<std::mutex> guard(lock);
		return lootManWorkshopRef;
	}

	void Initialize()
	{
		// LTMN_Properties is a fixed quest record in LootMan.esp; native code reads its script properties directly.
		propertiesQuest = utility::LookupForm("LootMan.esp|000F9A");
		{
			std::lock_guard<std::mutex> guard(lock);
			lootManWorkshopRef = nullptr;
		}
		if (!propertiesQuest)
		{
			REX::WARN("Failed to resolve properties quest LootMan.esp|000F9A");
		}
	}

	void Update(const char* key)
	{
		const auto updateProperty = std::string(key ? key : "");
		const bool updateAll = updateProperty.empty();
		// Collect first, then publish under lock so readers never observe partially refreshed settings.
		std::unordered_map<Key, Value> updates;
		RE::TESObjectREFR* updatedLootManWorkshopRef = nullptr;
		bool updateLootManWorkshopRef = false;

		auto propertyName = "MaxItemsProcessedPerThread";
		if (updateAll || propertyName == updateProperty)
		{
			updates[max_items_processed_per_thread] = GetIntProperty(propertyName);
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

		propertyName = "LootManWorkshopRef";
		if (updateAll || propertyName == updateProperty)
		{
			updateLootManWorkshopRef = true;
			updatedLootManWorkshopRef = GetObjectReferenceProperty(propertyName);
			if (!updatedLootManWorkshopRef)
			{
				REX::WARN("Failed to resolve native property LTMN2:Properties.{}", propertyName);
			}
		}

		if (updates.empty() && !updateLootManWorkshopRef)
		{
			// Unknown property names are ignored so Papyrus can call the hook for every MCM setting safely.
			return;
		}

		std::lock_guard<std::mutex> guard(lock);
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
