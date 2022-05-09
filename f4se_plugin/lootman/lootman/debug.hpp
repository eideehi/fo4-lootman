#pragma once

#include <bitset>

#include "f4se/GameForms.h"

inline const char* Bool2S(bool flag)
{
    return flag ? "true" : "false";
}

inline const char* Flag2S(UInt64 flags)
{
    return BSFixedString(std::bitset<64>(flags).to_string().c_str());
}

inline const char* Flag2S(UInt32 flags)
{
    return BSFixedString(std::bitset<32>(flags).to_string().c_str());
}

inline const char* Flag2S(UInt16 flags)
{
    return BSFixedString(std::bitset<16>(flags).to_string().c_str());
}

inline const char* Flag2S(SInt32 flags)
{
    return BSFixedString(std::bitset<32>(flags).to_string().c_str());
}

namespace debug
{
    const char* GetRandomProcessId();

    const char* GetFormTypeIdentifier(UInt8 formType);

    const char* GetItemTypeIdentifier(UInt32 itemType);

    const char* GetName(TESForm* form);

    const char* Form2S(TESForm* form);

    const char* Cell2S(TESObjectCELL* cell);

    const char* Ref2S(TESObjectREFR* ref);

    const char* InvItem2S(BGSInventoryItem* item);
}
