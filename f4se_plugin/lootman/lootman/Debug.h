#pragma once

#ifdef _DEBUG

#include <bitset>

#include "common/ITypes.h"

#include "f4se/GameForms.h"

inline const char* _bool2s(bool flag)
{
    return flag ? "true" : "false";
}

inline const char* _flags2s(UInt64 flags)
{
    return BSFixedString(std::bitset<64>(flags).to_string().c_str());
}

inline const char* _flags2s(UInt32 flags)
{
    return BSFixedString(std::bitset<32>(flags).to_string().c_str());
}

inline const char* _flags2s(UInt16 flags)
{
    return BSFixedString(std::bitset<16>(flags).to_string().c_str());
}

inline const char* _flags2s(SInt32 flags)
{
    return BSFixedString(std::bitset<32>(flags).to_string().c_str());
}

inline float _GetMagnitude(NiPoint3 pos)
{
    return std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
}

const char* _GetFormTypeIdentify(UInt8 formType);

const char* _GetRandomProcessID();

void _TraceExtraDataList(const char* processId, ExtraDataList* extraDataList, int indent, bool close = false);

void _TraceTESForm(const char* processId, TESForm* form, int indent, bool close = false);

void _TraceTESObjectCELL(const char* processId, TESObjectCELL* cell, int indent, bool close = false);

void _TraceTESObjectREFR(const char* processId, TESObjectREFR* ref, int indent, bool close = false);

void _TraceBGSInventoryItem(const char* processId, BGSInventoryItem* item, int indent, bool close = false);

void _TraceReferenceFlags(const char* processId, TESObjectREFR* ref, int indent, bool close = false);

#endif
