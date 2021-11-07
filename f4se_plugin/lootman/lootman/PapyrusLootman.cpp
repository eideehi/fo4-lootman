#include "PapyrusLootman.h"

#include <algorithm>
#include <map>
#include <utility>
#include <unordered_set>
#include <vector>

#include "f4se/PapyrusVM.h"
#include "f4se/PapyrusNativeFunctions.h"
#include "f4se/GameData.h"
#include "f4se/GameExtraData.h"
#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"

#include "FormIDCache.h"
#include "InjectionData.h"
#include "Utility.h"

#ifdef _DEBUG

#include <sstream>
#include <chrono>
#include <iomanip>

#include "Debug.h"

#endif

namespace PapyrusLootman
{
    DECLARE_STRUCT(MiscComponent, "MiscObject")

    // Get all mod data from extradata
    VMArray<BGSMod::Attachment::Mod*> _GetAllMod(ExtraDataList* extraDataList)
    {
        VMArray<BGSMod::Attachment::Mod*> result;

        if (!extraDataList)
        {
            return result;
        }

        BSExtraData* extraData = extraDataList->GetByType(kExtraData_ObjectInstance);
        if (!extraData)
        {
            return result;
        }

        BGSObjectInstanceExtra* objectModData = DYNAMIC_CAST(extraData, BSExtraData, BGSObjectInstanceExtra);
        if (!objectModData)
        {
            return result;
        }

        BGSObjectInstanceExtra::Data* data = objectModData->data;
        if (!data || !data->forms)
        {
            return result;
        }

        for (UInt32 i = 0; i < (data->blockSize / sizeof(BGSObjectInstanceExtra::Data::Form)); i++)
        {
            BGSMod::Attachment::Mod* objectMod = static_cast<BGSMod::Attachment::Mod*>(Runtime_DynamicCast(
                LookupFormByID(data->forms[i].formId), RTTI_TESForm, RTTI_BGSMod__Attachment__Mod));
            if (objectMod)
            {
                result.Push(&objectMod);
            }
        }

        return result;
    }

    // Verify that the Legendary is present in the Mod list
    bool _HasLegendaryMod(VMArray<BGSMod::Attachment::Mod*> mods)
    {
        for (UInt32 i = 0; i < mods.Length(); i++)
        {
            BGSMod::Attachment::Mod* objectMod = nullptr;
            mods.Get(&objectMod, i);

            // This will allow you to sift through the Legendary items (probably).
            if (objectMod->flags == (1 << 4 | 1 << 3 | 1 << 0))
            {
                return true;
            }
        }
        return false;
    }

    // Verify that the form is playable
    inline bool _IsPlayable(TESForm* form)
    {
        return form && (form->flags & 1 << 2) == 0;
    }

    // Verify that an object reference is a native object that cannot be manipulated by papyrus
    bool _IsNativeObject(TESObjectREFR* ref)
    {
        return (ref->formID >> 24) == 0xFF && (ref->baseForm->formID >> 24) == 0xFF && (ref->flags & 1 << 14) != 0;
    }

    // Retrieves objects that exist within a certain range starting from a specified object, and returns the objects filtered by form type
    VMArray<TESObjectREFR*> _FindAllReferencesOfFormType(TESObjectREFR* ref, UInt32 range, UInt32 formType)
    {
        VMArray<TESObjectREFR*> result;
        if (!ref)
        {
            return result;
        }

        std::unordered_set<UInt32> knownIDs;
        typedef std::pair<TESObjectREFR*, float> foundObject;
        std::vector<foundObject> foundObjects;
        NiPoint3 origin = ref->pos;

        auto find = [&](TESObjectCELL* cell)
        {
            // Not explore cells that are not 3D loaded
            if (!cell || (cell->flags & 1 << 4) == 0)
            {
                return;
            }

            for (int i = 0; i < cell->objectList.count; i++)
            {
                TESObjectREFR* obj = cell->objectList.entries[i];
                if (!obj)
                {
                    continue;
                }

                // Skip items once they've been processed.
                if (!knownIDs.insert(obj->formID).second)
                {
                    continue;
                }

                // Ignore deleted or disabled objects.
                if ((obj->flags & (TESForm::kFlag_IsDeleted | TESForm::kFlag_IsDisabled)) != 0)
                {
                    continue;
                }

                // Ignore native objects that cannot be bound to papyrus
                if (_IsNativeObject(obj))
                {
#ifdef _DEBUG
                    const char* processId = _GetRandomProcessID();
                    _MESSAGE("| %s | ** Maybe a native object **", processId);
                    _TraceTESObjectREFR(processId, obj, 1, true);
#endif
                    continue;
                }

                TESForm* form = obj->baseForm;
                if (!_IsPlayable(form))
                {
                    continue;
                }

                UInt8 type = form->formType;
                if (form->formType != formType)
                {
                    if (formType != -1)
                    {
                        continue;
                    }

                    if (type != kFormType_ACTI && type != kFormType_ALCH && type != kFormType_AMMO &&
                        type != kFormType_ARMO && type != kFormType_BOOK && type != kFormType_CONT &&
                        type != kFormType_FLOR && type != kFormType_INGR && type != kFormType_KEYM &&
                        type != kFormType_MISC && type != kFormType_NPC_ && type != kFormType_WEAP)
                    {
                        continue;
                    }
                }

                NiPoint3 target = obj->pos;
                float x = origin.x - target.x;
                float y = origin.y - target.y;
                float z = origin.z - target.z;
                float distance = std::sqrtf((x * x) + (y * y) + (z * z));

                // Ignore objects with a distance of 0 because they are players
                if (distance > 0 && distance <= range)
                {
                    foundObjects.push_back(foundObject(obj, distance));
                }
            }
        };

        find(ref->parentCell);

        {
            SimpleLocker locker(&FormIDCache::lock);
            for (auto it = FormIDCache::cells.begin(); it != FormIDCache::cells.end(); ++it)
            {
                TESObjectCELL* cell = DYNAMIC_CAST(LookupFormByID(*it), TESForm, TESObjectCELL);
                // Not explore cells that are not 3D loaded
                if (cell && (cell->flags & 16) != 0)
                {
                    find(cell);
                }
            }
        }

        // The Papyrus loop scans in reverse order, so it sorts in descending order so that it is processed from the one closest to the player
        std::sort(foundObjects.begin(), foundObjects.end(), [](const foundObject x, const foundObject y)
        {
            return x.second > y.second;
        });

        for (auto& element : foundObjects)
        {
            result.Push(&element.first);
        }

        return result;
    }

    // Retrieves and returns the looting targets that exist within a certain range, starting from the specified object
    VMArray<TESObjectREFR*> FindAllLootingTarget(StaticFunctionTag*, TESObjectREFR* ref, UInt32 range, UInt32 formType)
    {
#ifdef _DEBUG
        const char* processId = _GetRandomProcessID();
        _MESSAGE("| %s | *** FindAllLootingTarget start ***", processId);
#endif
        VMArray<TESObjectREFR*> result;

        if (!ref)
        {
            return result;
        }

        ActorValueInfo* avif = DYNAMIC_CAST(Utility::LookupForm("Lootman.esp|00D5EF"), TESForm, ActorValueInfo);
        if (!avif)
        {
            _ERROR(">> [ERROR] ACTOR_VALUE GET ERROR!!");
            return result;
        }

        VMArray<TESObjectREFR*> tmp = _FindAllReferencesOfFormType(ref, range, formType);

#ifdef _DEBUG
        _MESSAGE("| %s |   ** Found objects **", processId);
#endif

        for (UInt32 i = 0; i < tmp.Length(); i++)
        {
            TESObjectREFR* obj = nullptr;
            tmp.Get(&obj, i);

            if (obj->actorValueOwner.GetValue(avif) <= 0)
            {
                result.Push(&obj);
#ifdef _DEBUG
                _TraceTESObjectREFR(processId, obj, 2);
                _MESSAGE("| %s |       Distance: [%f]", processId, _GetMagnitude(obj->pos));
                _TraceReferenceFlags(processId, obj, 3, true);
#endif
            }
        }

#ifdef _DEBUG
        _MESSAGE("| %s |     Total count: %d", processId, result.Length());
        _MESSAGE("| %s | *** FindAllLootingTarget end ***", processId);
#endif
        return result;
    }

    // Retrieves and returns objects that are past the period of time when they are no longer subject to looting
    VMArray<TESObjectREFR*> GetAllExpiredObject(StaticFunctionTag*, TESObjectREFR* ref, UInt32 range, float currentTime,
                                                float expiration)
    {
#ifdef _DEBUG
        const char* processId = _GetRandomProcessID();
        _MESSAGE("| %s | *** GetAllExpiredObject start ***", processId);
#endif
        VMArray<TESObjectREFR*> result;

        if (!ref)
        {
            return result;
        }

        ActorValueInfo* avif = DYNAMIC_CAST(Utility::LookupForm("Lootman.esp|00D5EF"), TESForm, ActorValueInfo);
        if (!avif)
        {
            _ERROR(">> [ERROR] ACTOR_VALUE GET ERROR!!");
            return result;
        }

        VMArray<TESObjectREFR*> objects = _FindAllReferencesOfFormType(ref, range, -1);
        for (UInt32 i = 0; i < objects.Length(); i++)
        {
            TESObjectREFR* obj = nullptr;
            objects.Get(&obj, i);

            float timestamp = obj->actorValueOwner.GetValue(avif);
            if (currentTime < timestamp || (timestamp > 0 && (currentTime - timestamp) >= expiration))
            {
                result.Push(&obj);
#ifdef _DEBUG
                _MESSAGE("| %s |   ** Expired object **", processId);
                _TraceTESObjectREFR(processId, obj, 2);
                _MESSAGE("| %s |       Distance: [%f]", processId, _GetMagnitude(obj->pos));
                _TraceReferenceFlags(processId, obj, 3, true);
#endif
            }
        }
#ifdef _DEBUG
        _MESSAGE("| %s | *** GetAllExpiredObject end ***", processId);
#endif
        return result;
    }

    // Return the result of scrapping an object. The object must be a weapon or armor
    VMArray<MiscComponent> GetEquipmentScrapComponents(StaticFunctionTag*, VMRefOrInventoryObj* ref)
    {
#ifdef _DEBUG
        const char* processId = _GetRandomProcessID();
        _MESSAGE("| %s | *** GetEquipmentScrapComponents start ***", processId);
#endif
        VMArray<MiscComponent> result;
        if (!ref)
        {
            return result;
        }

        TESForm* baseForm = nullptr;
        ExtraDataList* extraDataList = nullptr;
        ref->GetExtraData(&baseForm, &extraDataList);
        if (!baseForm || !extraDataList)
        {
            return result;
        }

        if (baseForm->formType != kFormType_ARMO && baseForm->formType != kFormType_WEAP)
        {
            return result;
        }

        std::map<BGSComponent*, UInt32> map;
        auto push = [&map](BGSConstructibleObject* cobj)
        {
            if (!cobj)
            {
                return;
            }

            for (UInt32 i = 0; i < cobj->components->count; i++)
            {
                BGSConstructibleObject::Component cobjComponent;
                cobj->components->GetNthItem(i, cobjComponent);

                TESObjectMISC* misc = DYNAMIC_CAST(cobjComponent.component, TESForm, TESObjectMISC);
                if (misc)
                {
                    for (UInt32 j = 0; j < cobjComponent.count; j++)
                    {
                        for (UInt32 k = 0; k < misc->components->count; k++)
                        {
                            TESObjectMISC::Component miscComponent;
                            misc->components->GetNthItem(k, miscComponent);

                            map[miscComponent.component] += static_cast<UInt32>(miscComponent.count);
                        }
                    }
                }
                else
                {
                    map[cobjComponent.component] += cobjComponent.count;
                }
            }
        };

        tArray<BGSConstructibleObject*> cobjList = (*g_dataHandler)->arrCOBJ;
        auto find = [&cobjList](const TESForm* form)
        {
            for (UInt32 i = 0; i < cobjList.count; i++)
            {
                BGSConstructibleObject* cobj = nullptr;
                cobjList.GetNthItem(i, cobj);

                if (!cobj || !cobj->createdObject || !cobj->components)
                {
                    continue;
                }

                if (form->formID == cobj->createdObject->formID)
                {
                    return cobj;
                }

                BGSListForm* formList = DYNAMIC_CAST(cobj->createdObject, TESForm, BGSListForm);
                if (formList)
                {
                    for (UInt32 j = 0; j < formList->forms.count; j++)
                    {
                        TESForm* item = nullptr;
                        formList->forms.GetNthItem(j, item);

                        if (item && form->formID == item->formID)
                        {
                            return cobj;
                        }
                    }
                }
            }
            return static_cast<BGSConstructibleObject*>(nullptr);
        };

        VMArray<BGSMod::Attachment::Mod*> mods = _GetAllMod(extraDataList);
        for (UInt32 i = 0; i < mods.Length(); i++)
        {
            BGSMod::Attachment::Mod* objectMod = nullptr;
            mods.Get(&objectMod, i);

            push(find(objectMod));
        }

        push(find(baseForm));

        for (const auto& it : map)
        {
            MiscComponent comp;
            comp.Set("object", it.first);
            comp.Set("count", it.second / 2);
            result.Push(&comp);
        }

#ifdef _DEBUG
        _MESSAGE("| %s | *** GetEquipmentScrapComponents end ***", processId);
#endif
        return result;
    }

    // Get and returns the form type of the form
    UInt32 GetFormType(StaticFunctionTag*, TESForm* form)
    {
        return !form ? -1 : form->formType;
    }

    // Get and return the injection data to be registered in the form list
    VMArray<TESForm*> GetInjectionDataForList(StaticFunctionTag*, BSFixedString identify)
    {
        VMArray<TESForm*> result;

        auto dataIt = InjectionData::formListData.FindMember(identify);
        if (dataIt == InjectionData::formListData.MemberEnd() || !dataIt->value.IsArray())
        {
            return result;
        }

        for (auto it = dataIt->value.Begin(); it != dataIt->value.End(); ++it)
        {
            if (!it->IsString())
            {
                continue;
            }

            std::string value = it->GetString();
            TESForm* form = Utility::LookupForm(value);
            if (!form)
            {
                _WARNING(">> [WARN] Form is not found: %s", value.c_str());
                continue;
            }

            result.Push(&form);
        }

        return result;
    }

    // Get and return only items of a specified form type from an inventory of object references
    VMArray<TESForm*> GetInventoryItemsOfFormTypes(StaticFunctionTag*, TESObjectREFR* ref, VMArray<UInt32> formTypes)
    {
#ifdef _DEBUG
        const char* processId = _GetRandomProcessID();
        _MESSAGE("| %s | *** GetInventoryItemsOfFormTypes start ***", processId);
#endif
        VMArray<TESForm*> result;

        if (!ref || formTypes.IsNone())
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   ** Illegal argument **", processId);
            _MESSAGE("| %s |     ref is null: %s", processId, _bool2s(!ref));
            _MESSAGE("| %s |     formTypes is empty: %s", processId, _bool2s(formTypes.IsNone()));
            _MESSAGE("| %s | *** GetInventoryItemsOfFormTypes end ***", processId);
#endif
            return result;
        }

#ifdef _DEBUG
        _MESSAGE("| %s |   Target: [Name=%s, ID=%08X]", processId, CALL_MEMBER_FN(ref, GetReferenceName)(),
                 ref->formID);
#endif

        BGSInventoryList* inventoryList = ref->inventoryList;
        if (!inventoryList)
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   ** Inventory does not exist **", processId);
            _MESSAGE("| %s | *** GetInventoryItemsOfFormTypes end ***", processId);
#endif
            return result;
        }

        inventoryList->inventoryLock.LockForRead();

        for (int i = 0; i < inventoryList->items.count; i++)
        {
            BGSInventoryItem item;
            inventoryList->items.GetNthItem(i, item);

#ifdef _DEBUG
            _MESSAGE("| %s |   [Item %d]", processId, i);
            _TraceBGSInventoryItem(processId, &item, 2, true);
#endif

            TESForm* form = item.form;
            if (!_IsPlayable(form))
            {
#ifdef _DEBUG
                _MESSAGE("| %s |     ** Is not playable **", processId);
#endif
                continue;
            }

            UInt8 formType = form->formType;
            bool isFormTypeMatches = false;
            for (UInt32 j = 0; j < formTypes.Length(); j++)
            {
                UInt32 type;
                formTypes.Get(&type, j);

                if (type == -1)
                {
                    isFormTypeMatches =
                        formType == kFormType_ALCH || formType == kFormType_AMMO || formType == kFormType_ARMO ||
                        formType == kFormType_BOOK || formType == kFormType_INGR || formType == kFormType_KEYM ||
                        formType == kFormType_MISC || formType == kFormType_WEAP;
                    break;
                }

                if (formType == type)
                {
                    isFormTypeMatches = true;
                    break;
                }
            }

            if (!isFormTypeMatches)
            {
#ifdef _DEBUG
                _MESSAGE("| %s |     ** Mismatch form type **", processId);
#endif
                continue;
            }

            if (form->formType == kFormType_WEAP)
            {
                bool isDroppedWeapon = false;
                item.stack->Visit([&isDroppedWeapon](BGSInventoryItem::Stack* stack) mutable
                {
                    isDroppedWeapon = (stack->flags & 1 << 5) != 0;
                    return !isDroppedWeapon;
                });

                if (isDroppedWeapon)
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |     ** Is dropped weapon **", processId);
#endif
                    continue;
                }
            }

            result.Push(&form);
        }

        inventoryList->inventoryLock.Unlock();

#ifdef _DEBUG
        _MESSAGE("| %s | *** GetInventoryItemsOfFormTypes end ***", processId);
#endif
        return result;
    }

    // Verify the existence of the specified item's legendary in the object's inventory. Returns false if the item is not playable, or if it is neither a weapon nor armor
    bool HasLegendaryItem(StaticFunctionTag*, TESObjectREFR* ref, TESForm* form)
    {
#ifdef _DEBUG
        const char* processId = _GetRandomProcessID();
        _MESSAGE("| %s | *** HasLegendaryItem start ***", processId);
#endif
        if (!ref || !_IsPlayable(form) || (form->formType != kFormType_WEAP && form->formType != kFormType_ARMO))
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   ** Illegal argument **", processId);
            _MESSAGE("| %s |     ref is null: %s", processId, _bool2s(!ref));
            _MESSAGE("| %s |     form is not playable: %s", processId, _bool2s(!_IsPlayable(form)));
            _MESSAGE("| %s |     form is not equipment: %s", processId,
                     _bool2s(form->formType != kFormType_WEAP && form->formType != kFormType_ARMO));
            _MESSAGE("| %s | *** HasLegendaryItem end ***", processId);
#endif
            return false;
        }

#ifdef _DEBUG
        _MESSAGE("| %s |   ** Trace target **", processId);
        _TraceTESObjectREFR(processId, ref, 2, true);
#endif

        BGSInventoryList* inventoryList = ref->inventoryList;
        if (!inventoryList)
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   ** Has not inventory **", processId);
            _MESSAGE("| %s | *** HasLegendaryItem end ***", processId);
#endif
            return false;
        }

        bool hasLegendaryMod = false;
        {
            inventoryList->inventoryLock.LockForRead();

            for (int i = 0; i < inventoryList->items.count; i++)
            {
                BGSInventoryItem item;
                inventoryList->items.GetNthItem(i, item);

#ifdef _DEBUG
                _MESSAGE("| %s |   [Item %d]", processId, i);
                _TraceBGSInventoryItem(processId, &item, 2, true);
#endif

                TESForm* itemAsForm = item.form;
                if (!itemAsForm || itemAsForm->formID != form->formID)
                {
#ifdef _DEBUG
                    _MESSAGE("| %s |     Has not base form: %s", processId, _bool2s(!itemAsForm));
                    _MESSAGE("| %s |     Mismatch form type: %s", processId,
                             _bool2s(itemAsForm->formID != form->formID));
#endif
                    continue;
                }

                item.stack->Visit([&hasLegendaryMod](BGSInventoryItem::Stack* stack) mutable
                {
                    hasLegendaryMod = _HasLegendaryMod(_GetAllMod(stack->extraData));
                    return !hasLegendaryMod;
                });

#ifdef _DEBUG
                _MESSAGE("| %s |     ** Has legendary: %s", processId, _bool2s(hasLegendaryMod));
#endif

                if (hasLegendaryMod)
                {
                    break;
                }
            }

            inventoryList->inventoryLock.Unlock();
        }

#ifdef _DEBUG
        _MESSAGE("| %s | *** HasLegendaryItem end ***", processId);
#endif
        return hasLegendaryMod;
    }

    // Verify the object is a Legendary item. Returns false if the object is not playable, or if it is neither a weapon nor armor
    bool IsLegendaryItem(StaticFunctionTag*, VMRefOrInventoryObj* ref)
    {
#ifdef _DEBUG
        const char* processId = _GetRandomProcessID();
        _MESSAGE("| %s | *** IsLegendaryItem start ***", processId);
#endif
        if (!ref)
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   ** Illegal argument **", processId);
            _MESSAGE("| %s |     ref is null: true", processId);
            _MESSAGE("| %s | *** IsLegendaryItem end ***", processId);
#endif
            return false;
        }

#ifdef _DEBUG
        _MESSAGE("| %s |   ** Trace object **", processId);
        _TraceTESObjectREFR(processId, ref->GetObjectReference(), 2, true);
#endif

        TESForm* baseForm = nullptr;
        ExtraDataList* extraDataList = nullptr;

        ref->GetExtraData(&baseForm, &extraDataList);
        if (!_IsPlayable(baseForm) || (baseForm->formType != kFormType_WEAP && baseForm->formType != kFormType_ARMO))
        {
#ifdef _DEBUG
            _MESSAGE("| %s |   Base form is not playable: %s", processId, _bool2s(!_IsPlayable(baseForm)));
            _MESSAGE("| %s |   Item is not equipment: %s", processId,
                     _bool2s(baseForm->formType != kFormType_WEAP && baseForm->formType != kFormType_ARMO));
            _MESSAGE("| %s | *** IsLegendaryItem end ***", processId);
#endif
            return false;
        }

#ifdef _DEBUG
        _MESSAGE("| %s |   Is legendary: %s", processId, _bool2s(_HasLegendaryMod(_GetAllMod(extraDataList))));
        _MESSAGE("| %s | *** IsLegendaryItem end ***", processId);
#endif
        return _HasLegendaryMod(_GetAllMod(extraDataList));
    }

    // Verify the object is linked to the workshop
    // Source code used for reference: PapyrusObjectReference#AttachWireLatent
    bool IsLinkedToWorkshop(StaticFunctionTag*, TESObjectREFR* ref)
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

        BSExtraData* extraDataWorkshop = workshopRef->extraDataList->GetByType(kExtraData_WorkshopExtraData);
        if (!extraDataWorkshop)
        {
            return false;
        }

        return true;
    }

    // Verify that the object reference is a valid
    bool IsValidRef(StaticFunctionTag*, TESObjectREFR* ref)
    {
        return !!ref;
    }
#ifdef _DEBUG
    // Get and return the identifier of the form type
    BSFixedString GetFormTypeIdentify(StaticFunctionTag*, TESForm* form)
    {
        return _GetFormTypeIdentify(form->formType);
    }

    // Converts and return the form ID to a hexadecimal string
    BSFixedString GetHexID(StaticFunctionTag*, TESForm* form)
    {
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(8) << std::uppercase << std::hex << form->formID;
        return ss.str().c_str();
    }

    // Get and return the form's identify
    BSFixedString GetIdentify(StaticFunctionTag*, TESForm* form)
    {
        TESFullName* fullName = DYNAMIC_CAST(form, TESForm, TESFullName);
        if (fullName && strlen(fullName->name))
        {
            return fullName->name;
        }
        return form->GetEditorID();
    }

    // Get and return the current millisecond
    BSFixedString GetMilliseconds(StaticFunctionTag*)
    {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str().c_str();
    }

    // Generate and return a random process ID
    BSFixedString GetRandomProcessID(StaticFunctionTag*)
    {
        return _GetRandomProcessID();
    }
#endif
}

bool PapyrusLootman::RegisterFunctions(VirtualMachine* vm)
{
    _MESSAGE(">> Lootman papyrus functions register phase start.");

    vm->RegisterFunction(
        new NativeFunction3<StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectREFR*, UInt32, UInt32>(
            "FindAllLootingTarget", "Lootman", FindAllLootingTarget, vm));
    vm->RegisterFunction(
        new NativeFunction4<StaticFunctionTag, VMArray<TESObjectREFR*>, TESObjectREFR*, UInt32, float, float>(
            "GetAllExpiredObject", "Lootman", GetAllExpiredObject, vm));
    vm->RegisterFunction(
        new NativeFunction1<StaticFunctionTag, VMArray<MiscComponent>, VMRefOrInventoryObj*>(
            "GetEquipmentScrapComponents", "Lootman", GetEquipmentScrapComponents, vm));
    vm->RegisterFunction(
        new NativeFunction1<StaticFunctionTag, UInt32, TESForm*>("GetFormType", "Lootman", GetFormType, vm));
    vm->RegisterFunction(
        new NativeFunction1<StaticFunctionTag, VMArray<TESForm*>, BSFixedString>(
            "GetInjectionDataForList", "Lootman", GetInjectionDataForList, vm));
    vm->RegisterFunction(
        new NativeFunction2<StaticFunctionTag, VMArray<TESForm*>, TESObjectREFR*, VMArray<UInt32>>(
            "GetInventoryItemsOfFormTypes", "Lootman", GetInventoryItemsOfFormTypes, vm));
    vm->RegisterFunction(
        new NativeFunction2<StaticFunctionTag, bool, TESObjectREFR*, TESForm*>(
            "HasLegendaryItem", "Lootman", HasLegendaryItem, vm));
    vm->RegisterFunction(
        new NativeFunction1<StaticFunctionTag, bool, VMRefOrInventoryObj*>(
            "IsLegendaryItem", "Lootman", IsLegendaryItem, vm));
    vm->RegisterFunction(
        new NativeFunction1<StaticFunctionTag, bool, TESObjectREFR*>("IsLinkedToWorkshop", "Lootman",
                                                                     IsLinkedToWorkshop, vm));
    vm->RegisterFunction(
        new NativeFunction1<StaticFunctionTag, bool, TESObjectREFR*>("IsValidRef", "Lootman", IsValidRef, vm));

    vm->SetFunctionFlags("Lootman", "FindAllLootingTarget", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "GetAllExpiredObject", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "GetEquipmentScrapComponents", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "GetFormType", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "GetInjectionDataForList", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "GetInventoryItemsOfFormTypes", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "HasLegendaryItem", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "IsLegendaryItem", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "IsLinkedToWorkshop", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "IsValidRef", IFunction::kFunctionFlag_NoWait);

#ifdef _DEBUG
    vm->RegisterFunction(
        new NativeFunction1<StaticFunctionTag, BSFixedString, TESForm*>("GetFormTypeIdentify", "Lootman",
                                                                        GetFormTypeIdentify, vm));
    vm->RegisterFunction(
        new NativeFunction1<StaticFunctionTag, BSFixedString, TESForm*>("GetHexID", "Lootman", GetHexID, vm));
    vm->RegisterFunction(
        new NativeFunction1<StaticFunctionTag, BSFixedString, TESForm*>("GetIdentify", "Lootman", GetIdentify, vm));
    vm->RegisterFunction(
        new NativeFunction0<StaticFunctionTag, BSFixedString>("GetMilliseconds", "Lootman", GetMilliseconds, vm));
    vm->RegisterFunction(
        new NativeFunction0<StaticFunctionTag, BSFixedString>("GetRandomProcessID", "Lootman", GetRandomProcessID, vm));

    vm->SetFunctionFlags("Lootman", "GetFormTypeIdentify", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "GetHexID", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "GetIdentify", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "GetMilliseconds", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("Lootman", "GetRandomProcessID", IFunction::kFunctionFlag_NoWait);
#endif

    _MESSAGE(">> Lootman papyrus functions register phase end.");
    return true;
}
