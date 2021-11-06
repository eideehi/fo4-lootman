#include "FormIDCache.h"

#include <unordered_set>

#include "f4se/GameRTTI.h"
#include "f4se/GameReferences.h"
#include "f4se/GameEvents.h"

namespace FormIDCache
{
    SimpleLock lock;
    std::unordered_set<UInt32> cells;

    class ObjectLoadedListener : public BSTEventSink<TESObjectLoadedEvent>
    {
    public:
        EventResult ReceiveEvent(TESObjectLoadedEvent* evn, void* dispatcher)
        {
            TESForm* form = LookupFormByID(evn->formId);
            if (!form)
            {
                return kEvent_Continue;
            }

            TESObjectREFR* ref = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
            if (ref)
            {
                UInt8 formType = ref->baseForm->formType;
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

    void RegisterEventListener()
    {
        GetEventDispatcher<TESObjectLoadedEvent>()->AddEventSink(&eventListener);
        _MESSAGE(">>   Registered a listener for TESObjectLoadedEvent for Form ID caching.");
    }

    void Clear()
    {
        SimpleLocker locker(&lock);
        cells.clear();
        _MESSAGE(">>   Clear the cached form ID.");
    }
}
