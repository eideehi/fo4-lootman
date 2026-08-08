#include "papyrus_lootman_internal.h"

#include <cstdint>
#include <excpt.h>

namespace papyrus_lootman
{
	using namespace RE;

	struct QuestAliasInfo
	{
		enum FLAG : std::uint16_t
		{
			enabled = 1 << 0,
			completed = 1 << 1,
			failed = 1 << 6,
			active = 1 << 11,
		};

		std::uint32_t questId;
		std::uint16_t flags;
		bool isEssential;
		bool isQuestItem;
	};

	struct QuestAliasFlags
	{
		bool isEssential = false;
		bool isQuestItem = false;
	};

	struct QuestAliasWalkCallContext
	{
		ExtraAliasInstanceArray* extraData = nullptr;
		QuestAliasFlags flags;
	};

	void WalkQuestAliasArrayCall(void* opaque)
	{
		auto* context = static_cast<QuestAliasWalkCallContext*>(opaque);
		auto& flags = context->flags;
		for (auto& data : context->extraData->aliasArray)
		{
			if (!data.quest || data.quest->GetDelete() || !data.alias) continue;
			auto questFlags = data.quest->data.flags;

			if (!flags.isEssential)
			{
				if ((questFlags & QuestAliasInfo::enabled) != 0 &&
				    data.alias->flags.all(BGSBaseAlias::FLAGS::kEssential))
				{
					flags.isEssential = true;
				}
			}

			if (!flags.isQuestItem)
			{
				if (data.alias->IsQuestObject())
				{
					flags.isQuestItem = true;
				}
				else if ((questFlags & QuestAliasInfo::enabled) != 0 &&
				         (questFlags & (QuestAliasInfo::completed | QuestAliasInfo::failed)) == 0)
				{
					flags.isQuestItem = true;
				}
			}

			if (flags.isEssential && flags.isQuestItem) break;
		}
	}

	bool TryWalkQuestAliasArraySafe(
		QuestAliasWalkCallContext& context)
	{
#if defined(_MSC_VER)
		__try
		{
			WalkQuestAliasArrayCall(&context);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		WalkQuestAliasArrayCall(&context);
		return true;
#endif
	}

	QuestAliasFlags GetQuestAliasFlags(ExtraDataList* extraDataList)
	{
		QuestAliasFlags flags;
		if (!extraDataList) return flags;

		auto extraData = extraDataList->GetByType<ExtraAliasInstanceArray>();
		if (!extraData) return flags;

		// Every caller reaches this walk through an SEH-guarded Try...Safe frame, and
		// under /EHsc an SEH unwind skips ~ReadLockGuard, permanently leaking the
		// engine alias-array read lock and deadlocking the next engine writer. Walk
		// the untrusted alias data inside its own SEH guard so a fault is caught
		// while this frame (and with it the lock scope) still exits normally.
		QuestAliasWalkCallContext context{ extraData };
		bool walkSucceeded = false;
		{
			ReadLockGuard guard(extraData->aliasArrayLock);
			walkSucceeded = TryWalkQuestAliasArraySafe(context);
		}
		if (!walkSucceeded)
		{
			// Some IsDeadForLooting callers do not have an outer SEH boundary. Keep a
			// fault distinct from a normal non-match without leaking an exception past
			// those callers: conservatively protect the suspect alias-backed object in
			// both classifications after the read lock has left scope normally.
			return QuestAliasFlags{ true, true };
		}
		return context.flags;
	}

	bool IsEssential(const TESObjectREFR* ref)
	{
		if (!ref)
		{
			return false;
		}

		if (const auto* actor = ref->As<Actor>();
			actor && actor->boolFlags.all(Actor::BOOL_FLAGS::kEssential))
		{
			return true;
		}

		const auto* baseObj = ref->GetObjectReference();
		if (baseObj)
		{
			const auto* npc = baseObj->As<TESNPC>();
			if (npc && npc->IsEssential())
			{
				return true;
			}
		}

		return ref->extraList && GetQuestAliasFlags(ref->extraList.get()).isEssential;
	}

	bool IsAliveButDownActor(const TESObjectREFR* ref)
	{
		const auto* actor = ref ? ref->As<Actor>() : nullptr;
		if (!actor)
		{
			return false;
		}

		switch (static_cast<ACTOR_LIFE_STATE>(actor->lifeState))
		{
		case ACTOR_LIFE_STATE::kUnconscious:
		case ACTOR_LIFE_STATE::kRestrained:
		case ACTOR_LIFE_STATE::kEssentialDown:
		case ACTOR_LIFE_STATE::kBleedout:
			return true;
		default:
			return false;
		}
	}

	bool IsDeadForLooting(const TESObjectREFR* ref)
	{
		if (!ref)
		{
			return false;
		}

		if (IsAliveButDownActor(ref))
		{
			return false;
		}

		return ref->IsDead(!IsEssential(ref));
	}

	bool IsQuestItem(ExtraDataList* extraDataList)
	{
		return GetQuestAliasFlags(extraDataList).isQuestItem;
	}
}
