#include "FormIDCache.h"

#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"

#include "Utility.h"

namespace FormIDCache
{
    SimpleLock lock;
    std::unordered_set<UInt32> cells;
    UInt32 lastLootingTimestamp;
    UInt32 lootingMarker;

    class ObjectLoadedListener final : public BSTEventSink<TESObjectLoadedEvent>
    {
    public:
        EventResult ReceiveEvent(TESObjectLoadedEvent* evn, void* dispatcher) override
        {
            TESForm* form = LookupFormByID(evn->formId);
            if (!form)
            {
                return kEvent_Continue;
            }

            const auto ref = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
            if (ref)
            {
                const UInt8 formType = ref->baseForm->formType;
                if (formType == kFormType_ACTI || formType == kFormType_ALCH || formType == kFormType_AMMO ||
                    formType == kFormType_ARMO || formType == kFormType_BOOK || formType == kFormType_CONT ||
                    formType == kFormType_FLOR || formType == kFormType_INGR || formType == kFormType_KEYM ||
                    formType == kFormType_MISC || formType == kFormType_NPC_ || formType == kFormType_WEAP)
                {
                    TESObjectCELL* cell = ref->parentCell;
                    if (cell)
                    {
                        SimpleLocker locker(&lock);
                        if (evn->loaded)
                        {
                            cells.insert(cell->formID);
                        }
                    }
                }
            }

            return kEvent_Continue;
        }
    };

    ObjectLoadedListener eventListener;

    void Initialize()
    {
        _MESSAGE(">>   Start initialization the FormIDCache.");

        if (!GetEventDispatcher<TESObjectLoadedEvent>()->AddEventSink(&eventListener))
        {
            _ERROR(">>     [ERROR] Failed to register event listener for TESObjectLoadedEvent.");
            return;
        }

        TESForm* form = Utility::LookupForm("Lootman.esp|0098CF");
        if (!form)
        {
            _ERROR(">>     [ERROR] Failed to lookup form by identifier: 'Lootman.esp|0098CF'");
            return;
        }
        lootingMarker = form->formID;

        form = Utility::LookupForm("Lootman.esp|00D5EF");
        if (!form)
        {
            _ERROR(">>     [ERROR] Failed to lookup form by identifier: 'Lootman.esp|00D5EF'");
            return;
        }
        lastLootingTimestamp = form->formID;

        _MESSAGE(">>   Complete initialization the FormIDCache.");
    }

    void Clear()
    {
        SimpleLocker locker(&lock);
        cells.clear();
        _MESSAGE(">>   Clear the cached form ID.");
    }
}
