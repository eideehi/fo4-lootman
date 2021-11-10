#include "Utility.h"

#include "f4se/GameData.h"
#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"

namespace Utility
{
    typedef bool (*_IKeywordFormBase_HasKeyword)(IKeywordFormBase* keywordFormBase, BGSKeyword* keyword,
                                                 TBO_InstanceData* instanceData);
    typedef bool (*_TESForm_IsWater)(TESForm* form);

    TESForm* LookupForm(std::string value)
    {
        const std::string::size_type delimiter = value.find('|');
        if (delimiter != std::string::npos)
        {
            const std::string modName = value.substr(0, delimiter);
            const ModInfo* info = (*g_dataHandler)->LookupModByName(modName.c_str());
            if (!info)
            {
                return nullptr;
            }

            const std::string lowerFormId = value.substr(delimiter + 1);
            UInt32 formId = info->GetFormID(std::stoul(lowerFormId, nullptr, 16));
            TESForm* form = LookupFormByID(formId);
            if (form)
            {
                return form;
            }
        }
        return nullptr;
    }

    bool HasKeyword(TESForm* form, BGSKeyword* keyword)
    {
        IKeywordFormBase* keywordFormBase = DYNAMIC_CAST(form, TESForm, IKeywordFormBase);
        if (keywordFormBase)
        {
            const auto function = GetVirtualFunction<_IKeywordFormBase_HasKeyword>(keywordFormBase, 1);
            return function(keywordFormBase, keyword, nullptr);
        }
        return false;
    }

    bool IsWater(TESForm* form)
    {
        const auto function = GetVirtualFunction<_TESForm_IsWater>(form, 49);
        return function(form);
    }
}
