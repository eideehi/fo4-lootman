#include "constructible_object.h"

#include <shared_mutex>

namespace constructible_object
{
	// Read-mostly: rebuilt only by Initialize() on the main thread (kGameLoaded) but probed via
	// FromCreatedObjectId() on the per-loot/scrap hot path from VM worker threads. A shared_mutex plus
	// build-local-then-swap keeps concurrent readers from traversing a map that is mid-clear()/rehash
	// (data race / use-after-free), matching the sibling vendor_chest cache.
	std::shared_mutex cacheMutex;
	std::unordered_map<std::uint32_t, RE::BGSConstructibleObject*> cache;

	void CacheCObj(
		std::unordered_map<std::uint32_t, RE::BGSConstructibleObject*>& out,
		RE::TESForm* form,
		RE::BGSConstructibleObject* cobj,
		std::uint32_t depth = 0)
	{
		if (!form || !cobj)
		{
			return;
		}

		if (form->Is(RE::ENUM_FORM_ID::kFLST))
		{
			// Third-party data may contain self/mutually-referential or pathologically deep
			// FormList graphs. Unbounded recursion here would overflow the stack and CTD at load,
			// so cap the descent; real createdItem FormLists nest only a handful of levels.
			constexpr std::uint32_t kMaxFormListDepth = 64;
			if (depth >= kMaxFormListDepth)
			{
				REX::WARN(
					"source=native component=constructible_object event=form_list_depth_exceeded form={:08X} depth={}",
					form->formID,
					depth);
				return;
			}

			auto formList = form->As<RE::BGSListForm>();
			if (!formList) return;
			for (auto* item : formList->arrayOfForms)
			{
				CacheCObj(out, item, cobj, depth + 1);
			}
		}
		else if (form->Is(RE::ENUM_FORM_ID::kARMO) ||
		         form->Is(RE::ENUM_FORM_ID::kWEAP) ||
		         form->Is(RE::ENUM_FORM_ID::kOMOD))
		{
			out.emplace(form->formID, cobj);
		}
	}

	void Initialize()
	{
		REX::DEBUG("source=native component=constructible_object event=cache_started");

		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh)
		{
			REX::ERROR("source=native component=constructible_object event=initialize_failed reason=data_handler_unavailable");
			return;
		}

		// Build into a local map, then publish it under one exclusive lock so readers never observe a
		// half-rebuilt cache.
		std::unordered_map<std::uint32_t, RE::BGSConstructibleObject*> rebuilt;
		auto& allCObj = dh->GetFormArray<RE::BGSConstructibleObject>();
		rebuilt.reserve(allCObj.size());
		for (auto* cobj : allCObj)
		{
			if (!cobj || !cobj->createdItem || !cobj->requiredItems)
			{
				continue;
			}
			CacheCObj(rebuilt, cobj->createdItem, cobj);
		}

		const auto count = rebuilt.size();
		{
			std::unique_lock<std::shared_mutex> guard(cacheMutex);
			cache = std::move(rebuilt);
		}

		REX::DEBUG("source=native component=constructible_object event=cache_completed count={}", count);
	}

	RE::BGSConstructibleObject* FromCreatedObjectId(std::uint32_t formId)
	{
		std::shared_lock<std::shared_mutex> guard(cacheMutex);
		auto it = cache.find(formId);
		return it != cache.end() ? it->second : nullptr;
	}
}
