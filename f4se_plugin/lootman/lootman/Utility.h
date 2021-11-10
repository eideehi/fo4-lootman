#pragma once

#include "f4se/GameFormComponents.h"
#include "f4se/GameForms.h"

namespace Utility
{
    template <typename T>
    T GetVirtualFunction(void* baseClass, int index)
    {
        return reinterpret_cast<T>((*static_cast<uintptr_t***>(baseClass))[index]);
    }

    // Get and return the form that matches the identifier. The format of the identifier is 'ModName|HexFormID'
    // ModName = File name of the mod containing the extension. (Example: Lootman.esp
    // HexFormID = Six-digit hexadecimal string with the first two digits of Form ID removed. (Example: 01000F99 -> 000F99
    TESForm* LookupForm(std::string value);

    bool HasKeyword(TESForm* form, BGSKeyword* keyword);

    bool IsWater(TESForm* form);
}
