#include "papyrus_lootman_internal.h"

#include <cstdint>
#include <exception>
#include <vector>

#if defined(_MSC_VER)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <Windows.h>
#endif

#include "injection_data.h"

namespace papyrus_lootman
{
	using namespace RE;

	TBO_InstanceData* GetInstanceData(const ExtraDataList* extraDataList)
	{
		if (!extraDataList) return nullptr;

		auto instanceData = extraDataList->GetByType<ExtraInstanceData>();
		if (!instanceData) return nullptr;

		return instanceData->data.get();
	}

	TBO_InstanceData* GetInstanceData(const TESObjectREFR* ref)
	{
		if (!ref || !ref->extraList) return nullptr;

		return GetInstanceData(ref->extraList.get());
	}

	bool HasKeyword(const TESForm* form, BGSKeyword* kw, TBO_InstanceData* data)
	{
		if (!form || !kw) return false;

		auto keywordForm = form->As<BGSKeywordForm>();
		if (keywordForm && keywordForm->HasKeyword(kw, data))
		{
			return true;
		}

		auto keywordBase = form->As<IKeywordFormBase>();
		if (keywordBase && keywordBase->HasKeyword(kw, data))
		{
			return true;
		}

		return false;
	}

	bool HasKeyword(const TESForm* form, const std::vector<BGSKeyword*>& keywords, TBO_InstanceData* data)
	{
		if (!form) return false;
		for (const auto& kw : keywords)
		{
			if (!kw) continue;
			if (HasKeyword(form, kw, data))
			{
				return true;
			}
		}
		return false;
	}

	// These probes deliberately keep their __try frames free of objects that require C++ unwinding.
	// Callers can therefore retain an injection-data snapshot in an outer scope, catch the engine-memory
	// fault here, leave that owner scope normally, and only then propagate the fault to their existing
	// outer SEH boundary.
	bool TryReadFormIDSafe(
		const TESForm* form,
		TESFormID& outFormID,
		unsigned long& outExceptionCode)
	{
#if defined(_MSC_VER)
		__try
		{
			outFormID = form->formID;
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			outExceptionCode = GetExceptionCode();
			return false;
		}
#else
		outFormID = form->formID;
		return true;
#endif
	}

	bool TryHasKeywordSafe(
		const TESForm* form,
		BGSKeyword* keyword,
		TBO_InstanceData* data,
		bool& outMatched,
		unsigned long& outExceptionCode)
	{
#if defined(_MSC_VER)
		__try
		{
			outMatched = HasKeyword(form, keyword, data);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			outExceptionCode = GetExceptionCode();
			return false;
		}
#else
		outMatched = HasKeyword(form, keyword, data);
		return true;
#endif
	}

	bool TryHasKeywordSafe(
		const TESForm* form,
		const std::vector<BGSKeyword*>& keywords,
		TBO_InstanceData* data,
		bool& outMatched,
		unsigned long& outExceptionCode)
	{
#if defined(_MSC_VER)
		__try
		{
			outMatched = HasKeyword(form, keywords, data);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			outExceptionCode = GetExceptionCode();
			return false;
		}
#else
		outMatched = HasKeyword(form, keywords, data);
		return true;
#endif
	}

	bool TryHasReferenceKeywordSafe(
		const TESObjectREFR* ref,
		const std::vector<BGSKeyword*>& keywords,
		bool& outMatched,
		unsigned long& outExceptionCode)
	{
#if defined(_MSC_VER)
		__try
		{
			outMatched = HasKeyword(ref, keywords, GetInstanceData(ref));
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			outExceptionCode = GetExceptionCode();
			return false;
		}
#else
		outMatched = HasKeyword(ref, keywords, GetInstanceData(ref));
		return true;
#endif
	}

	[[noreturn]] void RaiseMatchProbeException(const unsigned long exceptionCode)
	{
#if defined(_MSC_VER)
		::RaiseException(exceptionCode, 0, 0, nullptr);
		// A VEH/debugger may request CONTINUE_EXECUTION even for this synthetic re-raise.
		// Do not return from a [[noreturn]] function if Windows honors that request.
		std::terminate();
#else
		(void)exceptionCode;
		std::terminate();
#endif
	}

	bool MatchesAny(const TESForm* form, const injection_data::Key& key)
	{
		if (!form) return false;

		TESFormID formID = 0;
		unsigned long exceptionCode = 0;
		if (!TryReadFormIDSafe(form, formID, exceptionCode))
		{
			RaiseMatchProbeException(exceptionCode);
		}

		{
			const auto formIDs = injection_data::GetFormIDSet(key);
			if (formIDs->find(formID) != formIDs->end())
			{
				return true;
			}
		}

		bool keywordProbeFailed = false;
		{
			// End this owner scope normally before re-raising a keyword probe fault below. Under /EHsc,
			// raising while `keywords` is alive would skip its shared_ptr destructor.
			const auto keywords = injection_data::GetKeywordListRef(key);
			if (keywords->empty())
			{
				return false;
			}

			for (auto* keyword : *keywords)
			{
				bool matched = false;
				if (!TryHasKeywordSafe(form, keyword, nullptr, matched, exceptionCode))
				{
					keywordProbeFailed = true;
					break;
				}
				if (matched)
				{
					return true;
				}
			}
		}

		if (keywordProbeFailed)
		{
			RaiseMatchProbeException(exceptionCode);
		}

		return false;
	}

	bool MatchesAnyCached(const TESForm* form, const injection_data::Key& key, MatchCache* cache)
	{
		if (!cache)
		{
			return MatchesAny(form, key);
		}
		if (!form)
		{
			return false;
		}

		TESFormID formID = 0;
		unsigned long exceptionCode = 0;
		if (!TryReadFormIDSafe(form, formID, exceptionCode))
		{
			RaiseMatchProbeException(exceptionCode);
		}

		const auto cacheKey =
			(static_cast<std::uint64_t>(static_cast<std::uint32_t>(key)) << 32) |
			static_cast<std::uint64_t>(formID);
		const auto it = cache->results.find(cacheKey);
		if (it != cache->results.end())
		{
			return it->second;
		}

		const bool matched = MatchesAny(form, key);
		cache->results.emplace(cacheKey, matched);
		return matched;
	}

	bool IsIncludedQuestItem(const TESForm* form, MatchCache* matchCache)
	{
		return MatchesAnyCached(form, injection_data::include_quest_item, matchCache);
	}
}
