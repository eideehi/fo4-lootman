#pragma once

class TESForm;

namespace utility
{
    // Get and return the form that matches the identifier. The format of the identifier is 'ModName|HexFormId'
    // ModName = File name of the mod containing the extension. (Example: LootMan.esp
    // HexFormId = Six-digit hexadecimal string with the first two digits of Form Id removed. (Example: 01000F99 -> 000F99
    TESForm* LookupForm(const std::string& value);
}
