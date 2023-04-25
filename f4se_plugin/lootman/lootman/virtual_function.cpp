#include "virtual_function.hpp"

#include "f4se/GameFormComponents.h"
#include "f4se/GameForms.h"
#include "f4se/GameObjects.h"
#include "f4se/GameReferences.h"

// ReSharper disable CppInconsistentNaming
namespace virtual_function
{
    typedef bool (*IKeywordFormBase_HasKeyword)(IKeywordFormBase* base, BGSKeyword* keyword, TBO_InstanceData* data);
    typedef bool (*TESForm_IsWater)(TESForm* form);
    typedef bool (*TESObjectREFR_IsDead)(TESObjectREFR* ref, bool notEssential);
    typedef bool (*Actor_PlayPickUpSound)(Actor* actor, TESBoundObject* obj, bool pickUp, bool use);
    typedef bool (*Actor_IsInFaction)(Actor* actor, TESFaction* faction);

    bool HasKeyword(IKeywordFormBase* base, BGSKeyword* keyword, TBO_InstanceData* data)
    {
        const auto function = GetVirtualFunction<IKeywordFormBase_HasKeyword>(base, 0x01);
        return function(base, keyword, data);
    }

    bool IsWater(TESForm* form)
    {
        const auto function = GetVirtualFunction<TESForm_IsWater>(form, 0x31);
        return function(form);
    }

    bool IsDead(TESObjectREFR* ref, bool notEssential)
    {
        const auto function = GetVirtualFunction<TESObjectREFR_IsDead>(ref, 0xC0);
        return function(ref, notEssential);
    }

    void PlayPickUpSound(Actor* actor, TESBoundObject* obj)
    {
        const auto function = GetVirtualFunction<Actor_PlayPickUpSound>(actor, 0xC6);
        function(actor, obj, true, false);
    }

    bool IsInFaction(Actor* actor, TESFaction* faction)
    {
        const auto function = GetVirtualFunction<Actor_IsInFaction>(actor, 0x10A);
        return function(actor, faction);
    }
}
