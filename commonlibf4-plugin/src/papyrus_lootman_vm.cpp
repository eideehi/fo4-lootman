#include "papyrus_lootman_internal.h"

#include <cassert>
#include <cstdint>
#include <utility>

using namespace std::literals;

namespace papyrus_lootman
{
	using namespace RE;

	// Safely pack a TESForm-derived pointer into a Papyrus Variable, bypassing
	// CommonLibF4's broken `PackVariable<object T>(Variable&, const volatile T*)`
	// which dispatches to `IVirtualMachine::CreateObject(BSFixedString&, BSTSmartPointer<Object>&)`
	// at a vtable slot that does not match the game binary.
	bool PackFormSafe(BSScript::Variable& a_var, TESForm* form, std::uint32_t vmTypeID)
	{
		if (!form)
		{
			a_var = nullptr;
			return true;
		}

		const auto success = [&]()
		{
			auto* game = GameVM::GetSingleton();
			auto vm = game ? game->GetVM() : nullptr;
			if (!vm)
			{
				return false;
			}

			BSTSmartPointer<BSScript::ObjectTypeInfo> typeInfo;
			if (!vm->GetScriptObjectType(vmTypeID, typeInfo) || !typeInfo)
			{
				return false;
			}

			auto& handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(
				vmTypeID, static_cast<const void*>(form));
			if (handle == handles.EmptyHandle())
			{
				return false;
			}

			BSTSmartPointer<BSScript::Object> object;
			if (!vm->FindBoundObject(handle, typeInfo->name.c_str(), false, object, false) || !object)
			{
				auto& binding = vm->GetObjectBindPolicy();
				auto* bindInterface = binding.bindInterface;
				if (!bindInterface ||
					!bindInterface->CreateObjectWithProperties(typeInfo->name, 0, object) ||
					!object)
				{
					return false;
				}

				binding.BindObject(object, handle);
			}

			if (!object)
			{
				return false;
			}

			a_var = std::move(object);
			return true;
		}();

		if (!success)
		{
			assert(false);
			REX::ERROR("failed to pack Form"sv);
			a_var = nullptr;
		}

		return success;
	}
}
