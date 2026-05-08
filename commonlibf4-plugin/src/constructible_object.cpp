#include "constructible_object.h"

namespace constructible_object
{
	std::unordered_map<std::uint32_t, RE::BGSConstructibleObject*> cache;

	void CacheCObj(RE::TESForm* form, RE::BGSConstructibleObject* cobj)
	{
		if (!form || !cobj)
		{
			return;
		}

		if (form->Is(RE::ENUM_FORM_ID::kFLST))
		{
			auto formList = form->As<RE::BGSListForm>();
			if (!formList) return;
			for (auto* item : formList->arrayOfForms)
			{
				CacheCObj(item, cobj);
			}
		}
		else if (form->Is(RE::ENUM_FORM_ID::kARMO) ||
		         form->Is(RE::ENUM_FORM_ID::kWEAP) ||
		         form->Is(RE::ENUM_FORM_ID::kOMOD))
		{
			cache.emplace(form->formID, cobj);
		}
	}

	void Initialize()
	{
		REX::DEBUG("source=native component=constructible_object event=cache_started");
		cache.clear();

		auto& allCObj = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSConstructibleObject>();
		cache.reserve(allCObj.size());
		for (auto* cobj : allCObj)
		{
			if (!cobj || !cobj->createdItem || !cobj->requiredItems)
			{
				continue;
			}
			CacheCObj(cobj->createdItem, cobj);
		}

		REX::DEBUG("source=native component=constructible_object event=cache_completed count={}", cache.size());
	}

	RE::BGSConstructibleObject* FromCreatedObjectId(std::uint32_t formId)
	{
		auto it = cache.find(formId);
		return it != cache.end() ? it->second : nullptr;
	}
}
