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

	struct WorldPickupMarkAsDeletedCallContext
	{
		TESObjectREFR* ref = nullptr;
	};

	void InvokeMarkAsDeletedCall(void* opaque)
	{
		auto* context = static_cast<WorldPickupMarkAsDeletedCallContext*>(opaque);
		context->ref->MarkAsDeleted();
	}

	bool TryMarkAsDeletedSafe(TESObjectREFR* ref)
	{
		if (!ref)
		{
			return false;
		}

		WorldPickupMarkAsDeletedCallContext context{ ref };
		return ExecuteSehCallSafe(&InvokeMarkAsDeletedCall, &context);
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
		const bool created = ref->IsCreated();
		bool setWantsDeleteOk = true;
		bool disableOk = true;
		bool markAsDeletedOk = true;
		if (created)
		{
			// Engine-created references are removed for good: Disable hides them
			// immediately and wants-delete lets the engine drop their save-side
			// change record entirely at cleanup, which is correct because nothing
			// in plugin data can resurrect a created reference.
			// Disable must run before the wants-delete marking: TESObjectREFR::
			// Disable skips its save-side registration once the wants-delete
			// in-game flag is set.
			disableOk = TryDisableSafe(ref);
			setWantsDeleteOk = TrySetWantsDeleteSafe(ref, true);
		}
		else
		{
			// Plugin-placed references get the vanilla "taken item" tombstone:
			// MarkAsDeleted stores the deleted flag as a change form that survives
			// save loads and is discarded by a legitimate cell reset, so the item
			// respawns exactly like a hand-picked vanilla item. Disable or
			// SetWantsDelete must not be used here: wants-delete erases the change
			// record (the item resurrects on the next save load), and Disable adds
			// a disabled-state signature vanilla taken items do not carry.
			markAsDeletedOk = TryMarkAsDeletedSafe(ref);
		}
		if (setWantsDeleteOk && disableOk && markAsDeletedOk &&
			(ref->IsDisabled() || ref->IsDeleted()))
		{
			// Record which reference in which cell was finalized so a later
			// save-load respawn of the same base item can be correlated with
			// encounter-zone reset_check logs for that cell.
			auto* baseForm = ref->GetObjectReference();
			auto* parentCell = ref->GetParentCell();
			REX::DEBUG(
				"source=native component=world_pickup event=finalized ref={:08X} base={:08X} cell={:08X} created={} disabled={} deleted={}",
				ref->formID,
				baseForm ? baseForm->formID : 0,
				parentCell ? parentCell->formID : 0,
				created,
				ref->IsDisabled(),
				ref->IsDeleted());
			return;
		}

		// Display names are arbitrary modded text; sanitize before logging so they cannot inject
		// CR/LF record splits or forged key=value fields into the structured log.
		const auto refName = SanitizeDiagnosticText(ref->GetDisplayFullName());
		auto* baseForm = ref->GetObjectReference();
		std::string baseName;
		std::uint32_t baseFormId = 0;
		std::uint32_t baseFormType = 0;
		if (baseForm)
		{
			baseName = SanitizeDiagnosticText(std::string(TESFullName::GetFullName(*baseForm)));
			baseFormId = baseForm->formID;
			baseFormType = static_cast<std::uint32_t>(baseForm->GetFormType());
		}

		REX::WARN(
			"source=native component=world_pickup event=finalize_incomplete ref={:08X} ref_name=\"{}\" base={:08X} base_name=\"{}\" base_form_type={} marked_recent={} set_wants_delete_ok={} disable_ok={} mark_as_deleted_ok={} disabled={} deleted={} created={}",
			ref->formID,
			refName,
			baseFormId,
			baseName,
			baseFormType,
			markedRecent,
			setWantsDeleteOk,
			disableOk,
			markAsDeletedOk,
			ref->IsDisabled(),
			ref->IsDeleted(),
			created);
	}
}
