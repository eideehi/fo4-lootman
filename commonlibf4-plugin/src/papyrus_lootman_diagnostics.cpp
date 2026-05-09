#include "papyrus_lootman_internal.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "form_cache.h"
#include "properties.h"

namespace papyrus_lootman
{
	using namespace RE;
	using namespace form_cache;

	inline constexpr std::uint32_t kFormFlagDeleted = 1u << 5;
	inline constexpr std::uint32_t kFormFlagDisabled = 1u << 11;
	inline constexpr std::uint32_t kFormFlagDestroyed = 1u << 23;
	inline constexpr std::uint32_t kActivationBlocked = 1u << 0;
	inline constexpr std::uint32_t kActivationIgnored = 1u << 1;
	inline constexpr std::size_t kMaxDiagnosticKeywords = 16;

	struct DiagnosticExtraFlags :
		public BSExtraData
	{
		static constexpr auto TYPE = EXTRA_DATA_TYPE::kFlags;
		std::uint32_t flags;
	};

	struct DiagnosticObjectEntry
	{
		NiPointer<TESObjectREFR> ref;
		std::uint32_t refFormID = 0;
		std::uint32_t cellFormID = 0;
		NiPoint3 position{};
		float distanceSquared = 0.0F;
	};

	struct KeywordDiagnostics
	{
		std::string ids = "none";
		std::size_t count = 0;
		bool truncated = false;
	};

	struct DiagnosticRowSnapshot
	{
		TESForm* baseForm = nullptr;
		std::uint32_t refFormID = 0;
		std::uint32_t baseFormID = 0;
		std::uint32_t cellFormID = 0;
		std::uint32_t locationFormID = 0;
		std::uint32_t ownerFormID = 0;
		ENUM_FORM_ID formType = ENUM_FORM_ID::kNONE;
		NiPoint3 position{};
		bool preconditionOk = false;
		bool deleted = false;
		bool disabled = false;
		bool destroyed = false;
		bool activationBlocked = false;
		std::string baseSource = "None";
		std::string baseEditorID = "none";
		std::string baseName = "none";
		std::string refName = "none";
		KeywordDiagnostics keywords;
	};

	std::string FormatHex(const std::uint32_t value, const int width = 8)
	{
		char buffer[16]{};
		std::snprintf(buffer, sizeof(buffer), "%0*X", width, value);
		return buffer;
	}

	std::string SanitizeDiagnosticText(const std::string& value)
	{
		std::string result;
		result.reserve(value.size());
		bool lastWasSpace = false;
		for (const auto raw : value)
		{
			const auto ch = static_cast<unsigned char>(raw);
			if (std::iscntrl(ch) || std::isspace(ch))
			{
				if (!result.empty() && !lastWasSpace)
				{
					result.push_back(' ');
				}
				lastWasSpace = true;
				continue;
			}
			result.push_back(raw == '"' ? '\'' : raw);
			lastWasSpace = false;
		}
		while (!result.empty() && result.back() == ' ')
		{
			result.pop_back();
		}
		return result.empty() ? "none" : result;
	}

	std::string SanitizeDiagnosticText(const char* value)
	{
		return value ? SanitizeDiagnosticText(std::string(value)) : "none";
	}

	std::string GetSourceIdentifier(TESForm* form)
	{
		if (!form)
		{
			return "None";
		}

		auto* file = form->GetFile(0);
		if (!file || !file->IsActive())
		{
			return "None";
		}

		const auto filename = file->GetFilename();
		if (filename.empty())
		{
			return "None";
		}

		const auto localFormId = file->IsLight() ?
			(form->formID & 0x00000FFFu) :
			(form->formID & 0x00FFFFFFu);
		return std::string(filename) + "|" + FormatHex(localFormId, 6);
	}

	KeywordDiagnostics GetKeywordDiagnostics(TESForm* form)
	{
		KeywordDiagnostics result;
		auto* keywordForm = form ? form->As<BGSKeywordForm>() : nullptr;
		if (!keywordForm)
		{
			return result;
		}

		std::ostringstream ids;
		keywordForm->ForEachKeyword([&](BGSKeyword* keyword)
		{
			if (!keyword)
			{
				return BSContainer::ForEachResult::kContinue;
			}

			++result.count;
			if (result.count > kMaxDiagnosticKeywords)
			{
				result.truncated = true;
				return BSContainer::ForEachResult::kStop;
			}

			if (result.count > 1)
			{
				ids << ',';
			}
			ids << GetSourceIdentifier(keyword);
			return BSContainer::ForEachResult::kContinue;
		});

		const auto text = ids.str();
		if (!text.empty())
		{
			result.ids = text;
		}
		return result;
	}

	bool HasActivationBlockFlag(TESObjectREFR* ref)
	{
		auto* extraList = ref ? ref->extraList.get() : nullptr;
		auto* extraFlags = extraList ? extraList->GetByType<DiagnosticExtraFlags>() : nullptr;
		if (!extraFlags)
		{
			return false;
		}
		return (extraFlags->flags & kActivationBlocked) != 0 ||
		       (extraFlags->flags & kActivationIgnored) != 0;
	}

	const char* GetInventoryStatusUnsafe(
		TESObjectREFR* ref,
		TESForm* baseForm,
		const PropertiesSnapshot& props,
		MatchCache& matchCache,
		std::uint32_t& outEntries)
	{
		outEntries = 0;
		if (!ref || !baseForm)
		{
			return "not_applicable";
		}

		const auto formType = baseForm->GetFormType();
		const auto inspectActor = formType == ENUM_FORM_ID::kNPC_;
		if (formType != ENUM_FORM_ID::kCONT && !inspectActor)
		{
			return "not_applicable";
		}

		if (formType == ENUM_FORM_ID::kCONT)
		{
			EnsureContainerInventoryListForLootScan(ref, baseForm);
		}
		else if (!IsDeadForLooting(ref))
		{
			return "actor_alive";
		}

		auto* inventoryList = ref->inventoryList;
		if (!inventoryList)
		{
			return "no_inventory_list";
		}

		{
			ReadLockGuard guard(inventoryList->rwLock);
			outEntries = static_cast<std::uint32_t>(
				std::min<std::size_t>(
					inventoryList->data.size(),
					static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
		}

		const auto hasLootableItems = HasLootableItem(inventoryList, &props, &matchCache, inspectActor);
		return hasLootableItems ? "has_lootable_items" : "no_lootable_items";
	}

	struct InventoryStatusProbeContext
	{
		TESObjectREFR* ref = nullptr;
		TESForm* baseForm = nullptr;
		const PropertiesSnapshot* props = nullptr;
		MatchCache* matchCache = nullptr;
		std::uint32_t entries = 0;
		const char* status = "not_applicable";
	};

	void CaptureInventoryStatusCall(void* opaque)
	{
		auto* context = static_cast<InventoryStatusProbeContext*>(opaque);
		context->entries = 0;
		context->status = GetInventoryStatusUnsafe(
			context->ref,
			context->baseForm,
			*context->props,
			*context->matchCache,
			context->entries);
	}

	bool TryGetInventoryStatusSafe(
		TESObjectREFR* ref,
		TESForm* baseForm,
		const PropertiesSnapshot& props,
		MatchCache& matchCache,
		std::string& outStatus,
		std::uint32_t& outEntries)
	{
		InventoryStatusProbeContext context{
			ref,
			baseForm,
			&props,
			&matchCache
		};
		if (!ExecuteSehCallSafe(&CaptureInventoryStatusCall, &context))
		{
			outStatus = "probe_failed";
			outEntries = 0;
			return false;
		}

		outStatus = context.status;
		outEntries = context.entries;
		return true;
	}

	std::string DetermineDiagnosticReason(
		TESObjectREFR* ref,
		TESForm* baseForm,
		const std::uint32_t enabledFormTypeMask,
		MatchCache& matchCache,
		const bool gotValidForm,
		const bool validForm,
		const bool gotLootableForm,
		const bool lootableForm,
		const bool gotValidObject,
		const bool validObject,
		const bool gotLootableObject,
		const bool lootableObject)
	{
		if (!ref)
		{
			return "missing_reference";
		}
		if (ref->IsPlayerRef())
		{
			return "player_ref";
		}
		if (!baseForm)
		{
			return "missing_base_form";
		}
		if (!CheckPrecondition(ref))
		{
			return "precondition_failed";
		}

		const auto formType = baseForm->GetFormType();
		const auto enabledBit = FormTypeToEnabledBit(formType);
		if (enabledBit == 0)
		{
			return "unsupported_form_type";
		}
		if ((enabledBit & enabledFormTypeMask) == 0)
		{
			return "disabled_by_mcm";
		}
		if (!IsPlayable(baseForm))
		{
			return "non_playable_form";
		}
		if (injection_data::GetFormIDSet(injection_data::exclude_form).contains(baseForm->formID) ||
			HasKeyword(baseForm, injection_data::GetKeywordListRef(injection_data::exclude_keyword), GetInstanceData(ref)))
		{
			return "excluded_by_injection_data";
		}
		if (formType == ENUM_FORM_ID::kACTI &&
			!MatchesAnyCached(baseForm, injection_data::include_activator, &matchCache))
		{
			return "requires_include_activator";
		}
		if (HasActivationBlockFlag(ref) &&
			!MatchesAnyCached(baseForm, injection_data::include_activation_block, &matchCache))
		{
			return "requires_include_activation_block";
		}
		if (ref->extraList && formType != ENUM_FORM_ID::kCONT && formType != ENUM_FORM_ID::kNPC_ &&
			!IsDeferredActivationAmmoCandidate(ref, baseForm) &&
			IsQuestItem(ref->extraList.get()) &&
			!MatchesAnyCached(baseForm, injection_data::include_quest_item, &matchCache))
		{
			return "requires_include_quest_item";
		}
		if (form_list::IsUniqueItem(baseForm->formID) &&
			!MatchesAnyCached(baseForm, injection_data::include_unique_item, &matchCache))
		{
			return "requires_include_unique_item";
		}
		if (HasKeyword(baseForm, keyword::featuredItem) &&
			!MatchesAnyCached(baseForm, injection_data::include_featured_item, &matchCache))
		{
			return "requires_include_featured_item";
		}
		if (!gotValidForm)
		{
			return "valid_form_probe_failed";
		}
		if (!validForm)
		{
			return "invalid_form";
		}
		if (!gotLootableForm)
		{
			return "lootable_form_probe_failed";
		}
		if (!lootableForm)
		{
			return "not_lootable_form";
		}
		if (!gotValidObject)
		{
			return "valid_object_probe_failed";
		}
		if (!validObject)
		{
			return "invalid_object";
		}
		if (!gotLootableObject)
		{
			return "lootable_object_probe_failed";
		}
		if (!lootableObject)
		{
			return "not_lootable_object";
		}
		return "looting_candidate";
	}

	struct DiagnosticReasonProbeContext
	{
		TESObjectREFR* ref = nullptr;
		TESForm* baseForm = nullptr;
		std::uint32_t enabledFormTypeMask = 0;
		MatchCache* matchCache = nullptr;
		bool gotValidForm = false;
		bool validForm = false;
		bool gotLootableForm = false;
		bool lootableForm = false;
		bool gotValidObject = false;
		bool validObject = false;
		bool gotLootableObject = false;
		bool lootableObject = false;
		std::string reason = "reason_probe_failed";
	};

	void CaptureDiagnosticReasonCall(void* opaque)
	{
		auto* context = static_cast<DiagnosticReasonProbeContext*>(opaque);
		context->reason = DetermineDiagnosticReason(
			context->ref,
			context->baseForm,
			context->enabledFormTypeMask,
			*context->matchCache,
			context->gotValidForm,
			context->validForm,
			context->gotLootableForm,
			context->lootableForm,
			context->gotValidObject,
			context->validObject,
			context->gotLootableObject,
			context->lootableObject);
	}

	bool TryDetermineDiagnosticReasonSafe(
		TESObjectREFR* ref,
		TESForm* baseForm,
		const std::uint32_t enabledFormTypeMask,
		MatchCache& matchCache,
		const bool gotValidForm,
		const bool validForm,
		const bool gotLootableForm,
		const bool lootableForm,
		const bool gotValidObject,
		const bool validObject,
		const bool gotLootableObject,
		const bool lootableObject,
		std::string& outReason)
	{
		DiagnosticReasonProbeContext context{
			ref,
			baseForm,
			enabledFormTypeMask,
			&matchCache,
			gotValidForm,
			validForm,
			gotLootableForm,
			lootableForm,
			gotValidObject,
			validObject,
			gotLootableObject,
			lootableObject
		};
		if (!ExecuteSehCallSafe(&CaptureDiagnosticReasonCall, &context))
		{
			outReason = "reason_probe_failed";
			return false;
		}

		outReason = std::move(context.reason);
		return true;
	}

	struct DiagnosticCollectionProbeContext
	{
		TESObjectREFR* ref = nullptr;
		TESObjectREFR* player = nullptr;
		TESObjectCELL* cell = nullptr;
		NiPoint3 origin{};
		float maxDistanceSquared = 0.0F;
		std::uint32_t refFormID = 0;
		std::uint32_t cellFormID = 0;
		NiPoint3 position{};
		float distanceSquared = 0.0F;
		bool include = false;
	};

	void CaptureDiagnosticCollectionEntryCall(void* opaque)
	{
		auto* context = static_cast<DiagnosticCollectionProbeContext*>(opaque);
		auto* ref = context->ref;
		if (!ref || ref == context->player || ref->IsPlayerRef())
		{
			return;
		}

		const auto position = ref->GetPosition();
		const auto dx = context->origin.x - position.x;
		const auto dy = context->origin.y - position.y;
		const auto dz = context->origin.z - position.z;
		const auto distanceSquared = dx * dx + dy * dy + dz * dz;
		if (distanceSquared <= 0.0F || distanceSquared > context->maxDistanceSquared)
		{
			return;
		}

		context->refFormID = ref->formID;
		context->cellFormID = context->cell ? context->cell->formID : 0;
		context->position = position;
		context->distanceSquared = distanceSquared;
		context->include = true;
	}

	bool TryCaptureDiagnosticCollectionEntrySafe(
		TESObjectREFR* ref,
		TESObjectREFR* player,
		TESObjectCELL* cell,
		const NiPoint3& origin,
		const float maxDistanceSquared,
		DiagnosticObjectEntry& outEntry)
	{
		DiagnosticCollectionProbeContext context{
			ref,
			player,
			cell,
			origin,
			maxDistanceSquared
		};
		if (!ExecuteSehCallSafe(&CaptureDiagnosticCollectionEntryCall, &context) || !context.include)
		{
			return false;
		}

		outEntry.refFormID = context.refFormID;
		outEntry.cellFormID = context.cellFormID;
		outEntry.position = context.position;
		outEntry.distanceSquared = context.distanceSquared;
		return true;
	}

	DiagnosticRowSnapshot BuildFallbackDiagnosticRowSnapshot(const DiagnosticObjectEntry& entry)
	{
		DiagnosticRowSnapshot snapshot;
		snapshot.refFormID = entry.refFormID;
		snapshot.cellFormID = entry.cellFormID;
		snapshot.position = entry.position;
		return snapshot;
	}

	struct DiagnosticRowProbeContext
	{
		TESObjectREFR* ref = nullptr;
		std::uint32_t fallbackCellFormID = 0;
		DiagnosticRowSnapshot snapshot;
	};

	void CaptureDiagnosticRowSnapshotCall(void* opaque)
	{
		auto* context = static_cast<DiagnosticRowProbeContext*>(opaque);
		auto* ref = context->ref;
		auto& snapshot = context->snapshot;
		if (!ref)
		{
			return;
		}

		snapshot.refFormID = ref->formID;
		snapshot.position = ref->GetPosition();
		snapshot.preconditionOk = CheckPrecondition(ref);
		snapshot.deleted = (ref->formFlags & kFormFlagDeleted) != 0;
		snapshot.disabled = (ref->formFlags & kFormFlagDisabled) != 0;
		snapshot.destroyed = (ref->formFlags & kFormFlagDestroyed) != 0;
		snapshot.activationBlocked = HasActivationBlockFlag(ref);
		snapshot.refName = SanitizeDiagnosticText(ref->GetDisplayFullName());

		auto* baseForm = ref->GetObjectReference();
		snapshot.baseForm = baseForm;
		if (baseForm)
		{
			snapshot.baseFormID = baseForm->formID;
			snapshot.formType = baseForm->GetFormType();
			snapshot.baseSource = GetSourceIdentifier(baseForm);
			snapshot.baseEditorID = SanitizeDiagnosticText(GetFormEditorIDOrEmpty(baseForm));
			snapshot.baseName = SanitizeDiagnosticText(GetFormName(baseForm));
			snapshot.keywords = GetKeywordDiagnostics(baseForm);
		}

		auto* cell = ref->GetParentCell();
		snapshot.cellFormID = cell ? cell->formID : context->fallbackCellFormID;
		auto* location = ref->GetCurrentLocation();
		if (!location && cell)
		{
			location = cell->GetLocation();
		}
		snapshot.locationFormID = location ? location->formID : 0;

		auto* owner = ref->GetOwner();
		snapshot.ownerFormID = owner ? owner->formID : 0;
	}

	bool TryCaptureDiagnosticRowSnapshotSafe(
		TESObjectREFR* ref,
		const DiagnosticObjectEntry& entry,
		DiagnosticRowSnapshot& outSnapshot)
	{
		DiagnosticRowProbeContext context{
			ref,
			entry.cellFormID,
			BuildFallbackDiagnosticRowSnapshot(entry)
		};
		if (!ExecuteSehCallSafe(&CaptureDiagnosticRowSnapshotCall, &context))
		{
			outSnapshot = BuildFallbackDiagnosticRowSnapshot(entry);
			return false;
		}

		outSnapshot = std::move(context.snapshot);
		return true;
	}

	void CollectDiagnosticReferences(
		TESObjectREFR* player,
		const NiPoint3& origin,
		const float maxDistanceSquared,
		std::vector<DiagnosticObjectEntry>& buffer)
	{
		auto collectCell = [&](TESObjectCELL* cell)
		{
			if (!cell || cell->cellState != TESObjectCELL::CELL_STATE::kAttached)
			{
				return;
			}

			BSAutoLock guard(cell->spinLock);
			for (auto& refPtr : cell->references)
			{
				auto* ref = refPtr.get();
				DiagnosticObjectEntry entry;
				if (TryCaptureDiagnosticCollectionEntrySafe(
					ref,
					player,
					cell,
					origin,
					maxDistanceSquared,
					entry))
				{
					entry.ref = refPtr;
					buffer.push_back(std::move(entry));
				}
			}
		};

		auto* parentCell = player ? player->GetParentCell() : nullptr;
		if (parentCell && parentCell->IsInterior())
		{
			collectCell(parentCell);
			return;
		}

		auto* tes = TES::GetSingleton();
		auto* gridCells = tes ? tes->gridCells : nullptr;
		bool collectedAnyCell = false;
		if (gridCells && gridCells->dimension > 0)
		{
			const auto dimension = gridCells->dimension;
			for (std::uint32_t y = 0; y < dimension; ++y)
			{
				for (std::uint32_t x = 0; x < dimension; ++x)
				{
					auto* gridCell = gridCells->Get(x, y);
					auto* loadedCell = gridCell ? gridCell->cell : nullptr;
					if (!loadedCell)
					{
						continue;
					}

					collectCell(loadedCell);
					collectedAnyCell = true;
				}
			}
		}

		if (!collectedAnyCell && parentCell)
		{
			collectCell(parentCell);
		}
	}

	std::string BuildCountsText(const std::map<std::string, std::uint32_t>& counts)
	{
		if (counts.empty())
		{
			return "none";
		}

		std::ostringstream out;
		bool first = true;
		for (const auto& [type, count] : counts)
		{
			if (!first)
			{
				out << ',';
			}
			first = false;
			out << type << '=' << count;
		}
		return out.str();
	}

	std::int32_t DumpNearbyObjectDiagnostics(std::monostate, TESObjectREFR* player, BSFixedString context)
	{
		const auto contextText = SanitizeDiagnosticText(context.c_str());
		if (!player)
		{
			REX::DEBUG(
				"source=native component=nearby_object_diagnostics event=scan_failed context=\"{}\" outcome=failed reason=missing_player",
				contextText);
			return 0;
		}

		EnsureItemTypeCache();
		const auto props = PropertiesSnapshot::Capture();
		const auto enabledFormTypeMask = static_cast<std::uint32_t>(
			properties::GetInt(properties::enabled_looting_form_type_mask, 0));
		const auto origin = player->GetPosition();
		const auto lootingRange = std::clamp(properties::GetFloat(properties::looting_range), 0.0F, 200.0F);
		const auto maxDistance = lootingRange * 100.0F;
		const auto maxDistanceSquared = maxDistance * maxDistance;
		const auto maxItemsRaw = properties::GetInt(properties::max_items_processed_per_thread);
		const auto maxItemsClamped = std::clamp(
			maxItemsRaw,
			1,
			static_cast<int>(kMaxItemsProcessedPerThreadLimit));
		const auto rowCap = static_cast<std::size_t>(maxItemsClamped);

		auto* playerCell = player->GetParentCell();
		auto* playerLocation = player->GetCurrentLocation();
		if (maxDistance < 1.0F)
		{
			REX::DEBUG(
				"source=native component=nearby_object_diagnostics event=scan_failed context=\"{}\" outcome=failed reason=range_too_small player={:08X} range_m={:.3f}",
				contextText,
				player->formID,
				lootingRange);
			return 0;
		}

		std::vector<DiagnosticObjectEntry> entries;
		entries.reserve(1024);
		CollectDiagnosticReferences(player, origin, maxDistanceSquared, entries);
		std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs)
		{
			return lhs.distanceSquared < rhs.distanceSquared;
		});

		const auto rowsToLog = std::min(rowCap, entries.size());
		const auto truncated = entries.size() > rowsToLog;
		REX::DEBUG(
			"source=native component=nearby_object_diagnostics event=scan_started context=\"{}\" player={:08X} player_cell={:08X} player_location={:08X} range_m={:.3f} max_distance_units={:.3f} row_cap={} candidates={} truncated={} enabled_mask={}",
			contextText,
			player->formID,
			playerCell ? playerCell->formID : 0,
			playerLocation ? playerLocation->formID : 0,
			lootingRange,
			maxDistance,
			rowCap,
			entries.size(),
			truncated,
			enabledFormTypeMask);

		MatchCache matchCache;
		matchCache.results.reserve(entries.size() * 2);
		std::vector<BGSMod::Attachment::Mod*> equipmentBuffer;
		std::map<std::string, std::uint32_t> formTypeCounts;

		for (std::size_t index = 0; index < rowsToLog; ++index)
		{
			const auto& entry = entries[index];
			auto* ref = entry.ref.get();
			DiagnosticRowSnapshot snapshot;
			const auto gotRowSnapshot = TryCaptureDiagnosticRowSnapshotSafe(ref, entry, snapshot);

			auto* baseForm = snapshot.baseForm;
			const auto formType = snapshot.formType;
			const auto formTypeName = GetFormTypeName(formType);
			++formTypeCounts[formTypeName];

			const auto enabledBit = FormTypeToEnabledBit(formType);
			const auto supportedFormType = enabledBit != 0;
			const auto enabledByMcm = supportedFormType && ((enabledBit & enabledFormTypeMask) != 0);

			bool gotValidForm = false;
			bool validForm = false;
			bool gotLootableForm = false;
			bool lootableForm = false;
			bool gotValidObject = false;
			bool validObject = false;
			bool gotLootableObject = false;
			bool lootableObject = false;
			if (baseForm)
			{
				gotValidForm = TryIsValidFormSafe(baseForm, &props, &matchCache, validForm);
				gotLootableForm = TryIsLootableFormSafe(baseForm, &props, &matchCache, lootableForm);
			}
			if (ref && baseForm)
			{
				gotValidObject = TryIsValidObjectSafe(ref, &props, baseForm, &matchCache, validObject);
				gotLootableObject = TryIsLootableObjectSafe(
					ref,
					&props,
					baseForm,
					&equipmentBuffer,
					&matchCache,
					lootableObject);
			}

			std::uint32_t inventoryEntries = 0;
			std::string inventoryStatus = "not_applicable";
			(void)TryGetInventoryStatusSafe(
				ref,
				baseForm,
				props,
				matchCache,
				inventoryStatus,
				inventoryEntries);
			std::string reason = "reason_probe_failed";
			const auto gotReason = TryDetermineDiagnosticReasonSafe(
				ref,
				baseForm,
				enabledFormTypeMask,
				matchCache,
				gotValidForm,
				validForm,
				gotLootableForm,
				lootableForm,
				gotValidObject,
				validObject,
				gotLootableObject,
				lootableObject,
				reason);
			const auto rowProbeFailed = !gotRowSnapshot || !gotReason;
			const auto distance = std::sqrt(entry.distanceSquared);

			REX::DEBUG(
				"source=native component=nearby_object_diagnostics event=reference context=\"{}\" index={} ref={:08X} base={:08X} base_source={} form_type={} supported_form_type={} enabled_by_mcm={} reason={} candidate={} row_probe_failed={} distance_units={:.3f} position_x={:.3f} position_y={:.3f} position_z={:.3f} cell={:08X} location={:08X} owner={:08X} precondition_ok={} deleted={} disabled={} destroyed={} got_valid_form={} valid_form={} got_lootable_form={} lootable_form={} got_valid_object={} valid_object={} got_lootable_object={} lootable_object={} inventory_status={} inventory_entries={} activation_blocked={} base_editor_id=\"{}\" base_name=\"{}\" ref_name=\"{}\" base_keywords=\"{}\" keyword_count={} keywords_truncated={}",
				contextText,
				index + 1,
				snapshot.refFormID,
				snapshot.baseFormID,
				snapshot.baseSource,
				formTypeName,
				supportedFormType,
				enabledByMcm,
				reason,
				!rowProbeFailed && reason == "looting_candidate",
				rowProbeFailed,
				distance,
				snapshot.position.x,
				snapshot.position.y,
				snapshot.position.z,
				snapshot.cellFormID,
				snapshot.locationFormID,
				snapshot.ownerFormID,
				snapshot.preconditionOk,
				snapshot.deleted,
				snapshot.disabled,
				snapshot.destroyed,
				gotValidForm,
				validForm,
				gotLootableForm,
				lootableForm,
				gotValidObject,
				validObject,
				gotLootableObject,
				lootableObject,
				inventoryStatus,
				inventoryEntries,
				snapshot.activationBlocked,
				snapshot.baseEditorID,
				snapshot.baseName,
				snapshot.refName,
				snapshot.keywords.ids,
				snapshot.keywords.count,
				snapshot.keywords.truncated);
		}

		REX::DEBUG(
			"source=native component=nearby_object_diagnostics event=summary context=\"{}\" player={:08X} candidates={} rows_logged={} truncated={} row_cap={} counts=\"{}\"",
			contextText,
			player->formID,
			entries.size(),
			rowsToLog,
			truncated,
			rowCap,
			BuildCountsText(formTypeCounts));

		return static_cast<std::int32_t>(
			std::min<std::size_t>(
				rowsToLog,
				static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));
	}
}
