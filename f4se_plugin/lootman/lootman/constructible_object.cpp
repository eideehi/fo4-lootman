#include <unordered_map>

#include "f4se/GameData.h"
#include "f4se/GameRTTI.h"

namespace constructible_object
{
    std::unordered_map<UInt32, BGSConstructibleObject*> cache;

    void _cacheCObj(TESForm* form, BGSConstructibleObject* cobj)
    {
        if (form->formType == kFormType_FLST)
        {
            const auto formList = DYNAMIC_CAST(form, TESForm, BGSListForm);
            if (!formList) {
                return;
            }
            for (UInt32 i = 0; i < formList->forms.count; ++i)
            {
                TESForm* item = nullptr;
                if (!formList->forms.GetNthItem(i, item))
                {
                    continue;
                }
                _cacheCObj(item, cobj);
            }
        }
        else if (form->formType == kFormType_ARMO || form->formType == kFormType_WEAP || form->formType == kFormType_OMOD)
        {
            cache.emplace(form->formID, cobj);
        }
    }

    void Initialize()
    {
        const auto prefix = "| INITIALIZE |";
        _MESSAGE("%s   [ Start a ConstructibleObject cache associated with the object ID ]", prefix);

        const auto allCObj = (*g_dataHandler)->arrCOBJ;
        for (UInt32 i = 0; i < allCObj.count; ++i)
        {
            BGSConstructibleObject* cobj = nullptr;
            if (!allCObj.GetNthItem(i, cobj) || !cobj->createdObject || !cobj->components)
            {
                continue;
            }
            _cacheCObj(cobj->createdObject, cobj);
        }

        _MESSAGE("%s     Cache size: %d", prefix, cache.size());
    }

    BGSConstructibleObject* FromCreatedObjectId(UInt32 formId)
    {
        return cache.find(formId) == cache.end() ? static_cast<BGSConstructibleObject*>(nullptr) : cache[formId];
    }
}
