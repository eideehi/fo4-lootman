#include "papyrus_lootman_internal.h"

#include <cstdint>
#include <string>

namespace papyrus_lootman
{
	using namespace RE;

	bool StartsWithAscii(const char* value, const char* prefix)
	{
		if (!value || !prefix)
		{
			return false;
		}

		while (*prefix != '\0')
		{
			if (*value == '\0' || *value != *prefix)
			{
				return false;
			}
			++value;
			++prefix;
		}
		return true;
	}

	struct InventoryItemDisplayNameCallContext
	{
		const BGSInventoryItem* item = nullptr;
		std::uint32_t stackIndex = 0;
		std::string name;
	};

	void InvokeInventoryItemDisplayNameCall(void* opaque)
	{
		auto* context = static_cast<InventoryItemDisplayNameCallContext*>(opaque);
		auto* name = context->item->GetDisplayFullName(context->stackIndex);
		if (name && name[0] != '\0')
		{
			context->name = name;
		}
	}

	std::string GetInventoryItemDisplayNameSafe(
		const BGSInventoryItem& item,
		TESForm* fallbackForm,
		std::uint32_t stackIndex)
	{
		InventoryItemDisplayNameCallContext context{
			&item,
			stackIndex,
			{}
		};
		(void)ExecuteSehCallSafe(&InvokeInventoryItemDisplayNameCall, &context);
		if (!context.name.empty())
		{
			return context.name;
		}

		return GetFormName(fallbackForm);
	}

	bool IsSpecialContainerEditorID(const char* editorID)
	{
		return StartsWithAscii(editorID, "WorkshopResourceContainer") ||
		       StartsWithAscii(editorID, "Pipboy");
	}

	bool IsSpecialContainerReference(TESObjectREFR* ref, TESForm* baseForm)
	{
		if (!ref)
		{
			return false;
		}

		auto* form = baseForm ? baseForm : ref->GetObjectReference();
		if (!form || form->GetFormType() != ENUM_FORM_ID::kCONT)
		{
			return false;
		}

		if (IsSpecialContainerEditorID(GetFormEditorIDOrEmpty(form)))
		{
			return true;
		}

		return IsSpecialContainerEditorID(GetFormEditorIDOrEmpty(ref));
	}
}
