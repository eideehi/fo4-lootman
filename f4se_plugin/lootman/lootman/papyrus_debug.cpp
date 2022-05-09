#include "papyrus_debug.hpp"

#include <sstream>
#include <chrono>
#include <iomanip>

#include "f4se/PapyrusNativeFunctions.h"

#include "debug.hpp"

namespace papyrus_debug
{
    // Get the form type identifier.
    BSFixedString GetFormTypeIdentifier(StaticFunctionTag*, TESForm* form)
    {
        return debug::GetFormTypeIdentifier(form->formType);
    }

    // Convert the form id to a hexadecimal string.
    BSFixedString GetHexId(StaticFunctionTag*, TESForm* form)
    {
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(8) << std::uppercase << std::hex << form->formID;
        return ss.str().c_str();
    }

    // Get the item type identifier.
    BSFixedString GetItemTypeIdentifier(StaticFunctionTag*, UInt32 itemType)
    {
        return debug::GetItemTypeIdentifier(itemType);
    }

    // Get the current millisecond.
    BSFixedString GetMilliseconds(StaticFunctionTag*)
    {
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str().c_str();
    }

    // Get the simple name of the Form.
    BSFixedString GetName(StaticFunctionTag*, TESForm* form)
    {
        return debug::GetName(form);
    }

    // Generate and return a random process id.
    BSFixedString GetRandomProcessId(StaticFunctionTag*)
    {
        return debug::GetRandomProcessId();
    }
}

bool papyrus_debug::Register(VirtualMachine* vm)
{
    _MESSAGE("| INITIALIZE | [ Started binding papyrus functions for Debug ]");

    vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, BSFixedString, TESForm*>("GetFormTypeIdentifier", "LTMN2:Debug", GetFormTypeIdentifier, vm));
    vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, BSFixedString, TESForm*>("GetHexId", "LTMN2:Debug", GetHexId, vm));
    vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, BSFixedString, UInt32>("GetItemTypeIdentifier", "LTMN2:Debug", GetItemTypeIdentifier, vm));
    vm->RegisterFunction(new NativeFunction0<StaticFunctionTag, BSFixedString>("GetMilliseconds", "LTMN2:Debug", GetMilliseconds, vm));
    vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, BSFixedString, TESForm*>("GetName", "LTMN2:Debug", GetName, vm));
    vm->RegisterFunction(new NativeFunction0<StaticFunctionTag, BSFixedString>("GetRandomProcessId", "LTMN2:Debug", GetRandomProcessId, vm));

    vm->SetFunctionFlags("LTMN2:Debug", "GetFormTypeIdentifier", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:Debug", "GetHexId", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:Debug", "GetItemTypeIdentifier", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:Debug", "GetMilliseconds", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:Debug", "GetName", IFunction::kFunctionFlag_NoWait);
    vm->SetFunctionFlags("LTMN2:Debug", "GetRandomProcessId", IFunction::kFunctionFlag_NoWait);

    _MESSAGE("| INITIALIZE |   Papyrus functions binding is complete");
    return true;
}
