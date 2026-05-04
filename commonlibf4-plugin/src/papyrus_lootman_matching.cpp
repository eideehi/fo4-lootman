#include "papyrus_lootman_internal.h"

#include <cstdint>
#include <vector>

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

	bool MatchesAny(const TESForm* form, const injection_data::Key& key)
	{
		if (!form) return false;

		const auto& formIDs = injection_data::GetFormIDSet(key);
		if (formIDs.find(form->formID) != formIDs.end())
		{
			return true;
		}

		const auto& keywords = injection_data::GetKeywordListRef(key);
		if (keywords.empty())
		{
			return false;
		}

		for (auto* keyword : keywords)
		{
			if (HasKeyword(form, keyword))
			{
				return true;
			}
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

		const auto cacheKey =
			(static_cast<std::uint64_t>(static_cast<std::uint32_t>(key)) << 32) |
			static_cast<std::uint64_t>(form->formID);
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
