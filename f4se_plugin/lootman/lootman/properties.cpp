#include "properties.hpp"

#include <unordered_map>

#include "f4se/GameForms.h"
#include "f4se/GameTypes.h"
#include "f4se/PapyrusUtilities.h"
#include "f4se/PapyrusValue.h"
#include "f4se/PapyrusVM.h"

#include "utility.hpp"

namespace properties
{
    TESForm* propertiesQuest;
    SimpleLock lock;
    std::unordered_map<Key, Value> papyrusProperties;

    bool GetPapyrusProperty(const char* propertyName, VMValue* value)
    {
        const auto scriptName = "LTMN2:Properties";
        auto name = BSFixedString(propertyName);
        const auto vm = (*g_gameVM)->m_virtualMachine;
        const auto handle = PapyrusVM::GetHandleFromObject(propertiesQuest, propertiesQuest->formType);
        VMIdentifier* identifier = nullptr;

        if (vm->GetObjectIdentifier(handle, scriptName, 0, &identifier, 0))
        {
            bool result = false;
            if (identifier)
            {
                VMPropertyInfo info;
                info.index = -1;

                GetVMPropertyInfo(identifier->m_typeInfo, &info, &name, true);
                if (info.index != -1)
                {
                    result = vm->GetPropertyValueByIndex(&identifier, info.index, value);
                }
            }

            if (identifier)
            {
                if (!identifier->DecrementLock())
                {
                    identifier->Destroy();
                }
            }

            if (!result)
            {
                _WARNING("| ---------- | << WARNING: Failed to get property %s.%s >>", scriptName, name.c_str());
            }

            return result;
        }

        return false;
    }

    Value GetBoolProperty(const char* propertyName)
    {
        Value result;
        VMValue value;
        if (GetPapyrusProperty(propertyName, &value))
        {
            result.type = boolean;
            result.data.b = value.data.b;
        }
        return result;
    }

    Value GetIntProperty(const char* propertyName)
    {
        Value result;
        VMValue value;
        if (GetPapyrusProperty(propertyName, &value))
        {
            result.type = integer;
            result.data.i = value.data.i;
        }
        return result;
    }

    Value GetFloatProperty(const char* propertyName)
    {
        Value result;
        VMValue value;
        if (GetPapyrusProperty(propertyName, &value))
        {
            result.type = decimal;
            result.data.f = value.data.f;
        }
        return result;
    }

    Value Get(const Key key)
    {
        SimpleLocker locker(&lock);
        return papyrusProperties.at(key);
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

    void Initialize()
    {
        propertiesQuest = utility::LookupForm("LootMan.esp|000F9A");
    }

    void Update(const char* key)
    {
        SimpleLocker locker(&lock);

        const auto updateProperty = std::string(key);
        const bool updateAll = updateProperty.empty();

        if (updateAll)
        {
            papyrusProperties[max_items_processed_per_thread] = GetIntProperty("MaxItemsProcessedPerThread");
        }

        auto propertyName = "LootingRange";
        if (updateAll || propertyName == updateProperty)
        {
            papyrusProperties[looting_range] = GetFloatProperty(propertyName);
        }

        propertyName = "NotLootingFromSettlement";
        if (updateAll || propertyName == updateProperty)
        {
            papyrusProperties[not_looting_from_settlement] = GetBoolProperty(propertyName);
        }

        propertyName = "LootableInventoryItemType";
        if (updateAll || updateProperty.rfind("EnableInventoryLootingOf", 0) == 0)
        {
            papyrusProperties[lootable_inventory_item_type] = GetIntProperty(propertyName);
        }

        propertyName = "LootingLegendaryOnly";
        if (updateAll || propertyName == updateProperty)
        {
            papyrusProperties[looting_legendary_only] = GetBoolProperty(propertyName);
        }

        propertyName = "LootableALCHItemType";
        if (updateAll || updateProperty.rfind("EnableALCHItem", 0) == 0)
        {
            papyrusProperties[lootable_alch_item_type] = GetIntProperty(propertyName);
        }

        propertyName = "LootableBOOKItemType";
        if (updateAll || updateProperty.rfind("EnableBOOKItem", 0) == 0)
        {
            papyrusProperties[lootable_book_item_type] = GetIntProperty(propertyName);
        }

        propertyName = "LootableMISCItemType";
        if (updateAll || updateProperty.rfind("EnableMISCItem", 0) == 0)
        {
            papyrusProperties[lootable_misc_item_type] = GetIntProperty(propertyName);
        }

        propertyName = "LootableWEAPItemType";
        if (updateAll || updateProperty.rfind("EnableWEAPItem", 0) == 0)
        {
            papyrusProperties[lootable_weap_item_type] = GetIntProperty(propertyName);
        }
    }
}
