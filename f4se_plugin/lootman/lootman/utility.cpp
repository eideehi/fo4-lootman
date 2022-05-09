#include "utility.hpp"

#include "f4se/GameData.h"

namespace utility
{
    TESForm* LookupForm(const std::string& value)
    {
        const std::string::size_type delimiter = value.find('|');
        if (delimiter != std::string::npos)
        {
            const std::string modName = value.substr(0, delimiter);
            const ModInfo* info = (*g_dataHandler)->LookupModByName(modName.c_str());
            if (!info || !info->IsActive())
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
}
