#pragma once

class Actor;
class BGSKeyword;
class IKeywordFormBase;
class TBO_InstanceData;
class TESBoundObject;
class TESForm;
class TESObjectREFR;

namespace virtual_function
{
    template <typename T>
    T GetVirtualFunction(void* baseClass, int index)
    {
        return reinterpret_cast<T>((*static_cast<uintptr_t***>(baseClass))[index]);
    }

    bool HasKeyword(IKeywordFormBase* base, BGSKeyword* keyword, TBO_InstanceData* data = nullptr);

    bool IsWater(TESForm* form);

    bool IsDead(TESObjectREFR* ref, bool essential);

    void PlayPickUpSound(Actor* actor, TESBoundObject* obj);
}
