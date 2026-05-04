#include "papyrus_lootman_internal.h"

#include <cstdint>
#include <string>

namespace papyrus_lootman
{
	using namespace RE;

	struct WorldPickupDeleteCallContext
	{
		TESObjectREFR* ref = nullptr;
		bool wantsDelete = true;
	};

	void InvokeSetWantsDeleteCall(void* opaque)
	{
		auto* context = static_cast<WorldPickupDeleteCallContext*>(opaque);
		context->ref->SetWantsDelete(context->wantsDelete);
	}

	bool TrySetWantsDeleteSafe(TESObjectREFR* ref, bool wantsDelete = true)
	{
		if (!ref)
		{
			return false;
		}

		WorldPickupDeleteCallContext context{
			ref,
			wantsDelete
		};
		return ExecuteSehCallSafe(&InvokeSetWantsDeleteCall, &context);
	}

	struct WorldPickupDisableCallContext
	{
		TESObjectREFR* ref = nullptr;
	};

	void InvokeDisableCall(void* opaque)
	{
		auto* context = static_cast<WorldPickupDisableCallContext*>(opaque);
		context->ref->Disable();
	}

	bool TryDisableSafe(TESObjectREFR* ref)
	{
		if (!ref)
		{
			return false;
		}

		WorldPickupDisableCallContext context{ ref };
		return ExecuteSehCallSafe(&InvokeDisableCall, &context);
	}

	void PlayPickUpSound(std::monostate, TESObjectREFR* player, TESObjectREFR* obj)
	{
		auto actor = player ? player->As<Actor>() : nullptr;
		if (!actor) return;

		auto boundObject = obj ? obj->GetObjectReference() : nullptr;
		if (!boundObject) return;

		actor->PlayPickUpSound(boundObject, true, false);
	}

	void FinalizeWorldPickup(std::monostate, TESObjectREFR* ref)
	{
		if (!ref)
		{
			return;
		}

		const auto markedRecent = TryMarkRecentlyLootedWorldRef(ref);
		const auto setWantsDeleteOk = TrySetWantsDeleteSafe(ref, true);
		const auto disableOk = TryDisableSafe(ref);
		if (!setWantsDeleteOk || !disableOk ||
			(!ref->IsDisabled() && !ref->IsDeleted()))
		{
			const auto* refName = ref->GetDisplayFullName();
			auto* baseForm = ref->GetObjectReference();
			std::string baseName;
			std::uint32_t baseFormId = 0;
			std::uint32_t baseFormType = 0;
			if (baseForm)
			{
				baseName = TESFullName::GetFullName(*baseForm);
				baseFormId = baseForm->formID;
				baseFormType = static_cast<std::uint32_t>(baseForm->GetFormType());
			}

			REX::WARN(
				"FinalizeWorldPickup incomplete: ref={:08X} \"{}\", base={:08X} \"{}\", baseFormType={}, markedRecent={}, setWantsDeleteOk={}, disableOk={}, disabled={}, deleted={}, created={}",
				ref->formID,
				refName ? refName : "",
				baseFormId,
				baseName,
				baseFormType,
				markedRecent,
				setWantsDeleteOk,
				disableOk,
				ref->IsDisabled(),
				ref->IsDeleted(),
				ref->IsCreated());
		}
	}
}
