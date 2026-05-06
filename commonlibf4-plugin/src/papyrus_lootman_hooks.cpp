#include "papyrus_lootman_internal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>

#include <REL/Relocation.h>

#include "papyrus_lootman_hook_addresses.generated.h"
#include "properties.h"

namespace papyrus_lootman
{
	using namespace RE;

	template <class OriginalFn, class HookFn>
	bool InstallDirectCallHookSite(
		const NativeHookCallSite& site,
		HookFn hook,
		OriginalFn& original,
		const char* family);

	struct ExtraCellDetachTimeCompat :
		public BSExtraData
	{
		static constexpr auto TYPE = EXTRA_DATA_TYPE::kCellDetachTime;
		std::uint32_t detachTime;
	};

	std::optional<std::uint32_t> GetCellDetachTime(const TESObjectCELL* cell)
	{
		if (!cell)
		{
			return std::nullopt;
		}

		if (auto* extraList = cell->extraList.get())
		{
			if (auto* detachTime = extraList->GetByType<ExtraCellDetachTimeCompat>())
			{
				return detachTime->detachTime;
			}
		}

		return std::nullopt;
	}

	using CheckCellBeforeEncounterZoneResetFn = bool (*)(BGSEncounterZone*, TESObjectCELL*);
	using CheckResetElapsedFromDetachTimeFn =
		bool (*)(std::uint32_t, std::uint32_t, bool);

	CheckCellBeforeEncounterZoneResetFn originalCheckCellBeforeEncounterZoneReset = nullptr;
	CheckResetElapsedFromDetachTimeFn checkResetElapsedFromDetachTime = nullptr;
	std::mutex encounterZoneSuppressionLogLock;
	Clock::time_point lastEncounterZoneSuppressionLogAt{};

	inline constexpr auto kEncounterZoneSuppressionLogInterval = std::chrono::seconds(5);

	bool ShouldSuppressCellBeforeEncounterZoneReset(
		BGSEncounterZone* zone,
		TESObjectCELL* cell,
		bool originalResult)
	{
		if (!originalResult || !zone || !cell || !checkResetElapsedFromDetachTime)
		{
			return false;
		}

		const auto cellDetachTime = GetCellDetachTime(cell);
		if (!cellDetachTime)
		{
			return false;
		}

		const auto currentDay = zone->gameData.attachTime;
		if (currentDay == 0)
		{
			return false;
		}

		const bool locationCleared =
			zone->data.location ? zone->data.location->cleared : false;
		const bool cellDetachResetElapsed = checkResetElapsedFromDetachTime(
			currentDay,
			*cellDetachTime,
			locationCleared);

		return zone->gameData.detachTime == 0 &&
			zone->gameData.resetTime != 0 &&
			zone->gameData.resetTime == currentDay &&
			!cellDetachResetElapsed;
	}

	bool HookedCheckCellBeforeEncounterZoneReset(BGSEncounterZone* zone, TESObjectCELL* cell)
	{
		const bool result = originalCheckCellBeforeEncounterZoneReset(zone, cell);
		if (!ShouldSuppressCellBeforeEncounterZoneReset(zone, cell, result))
		{
			return result;
		}

		const auto now = Clock::now();
		{
			std::lock_guard<std::mutex> guard(encounterZoneSuppressionLogLock);
			if (lastEncounterZoneSuppressionLogAt.time_since_epoch().count() == 0 ||
				(now - lastEncounterZoneSuppressionLogAt) >= kEncounterZoneSuppressionLogInterval)
			{
				lastEncounterZoneSuppressionLogAt = now;
				const auto cellDetachTime = GetCellDetachTime(cell).value_or(0);
				REX::DEBUG(
					"Suppressed stale encounter-zone reset: zone={:08X}, cell={:08X}, zoneDetachTime={}, zoneAttachTime={}, zoneResetTime={}, cellDetachTime={}",
					zone ? zone->formID : 0,
					cell ? cell->formID : 0,
					zone ? zone->gameData.detachTime : 0,
					zone ? zone->gameData.attachTime : 0,
					zone ? zone->gameData.resetTime : 0,
					cellDetachTime);
			}
		}

		return false;
	}

	void InstallEncounterZoneResetSuppressionHooks()
	{
		static std::once_flag installOnce;
		std::call_once(installOnce, []()
		{
			REL::Relocation<CheckResetElapsedFromDetachTimeFn> resetElapsedFromDetach{
				kEncounterZoneResetElapsedFromDetachId
			};

			if (!InstallDirectCallHookSite(
					kLoadChangeCellBeforeZoneResetCallSite,
					HookedCheckCellBeforeEncounterZoneReset,
					originalCheckCellBeforeEncounterZoneReset,
					"encounter-zone.reset-suppression"))
			{
				return;
			}

			checkResetElapsedFromDetachTime = resetElapsedFromDetach.get();

			REX::INFO("Installed encounter-zone reset suppression hook");
		});
	}


	using SharedWorkshopContainers = BSScrapArray<NiPointer<TESObjectREFR>>;
	using PopulateLinkedWorkshopContainersFn =
		void (*)(SharedWorkshopContainers*, BGSLocation*, bool);
	using RebuildWorkshopSupplyFn = void (*)(void*);
	using ComponentCountHelperFn = bool (*)(void*, std::int32_t*, TESForm*, bool);
	using DirectComponentCountFn = std::int32_t (*)(void*, BGSComponent*, bool);
	using WorkshopResourceStatusFn = std::uint32_t (*)();
	using GetWorkshopMenuNodeFn = Workshop::WorkshopMenuNode* (*)(std::uint16_t, std::uint32_t*);
	using WorkshopMenuAvailabilityFn = bool (*)(std::uint32_t*, std::uint32_t, std::uint32_t);
	using WorkshopMenuSelectFn = bool (*)(bool, void*);
	using WorkshopCheckAndSetPlacementFn = void (*)(WorkshopMenu*);
	using WorkshopStartPlacementFn = void (*)(void*, bool, bool);
	using WorkshopBuildResourceCheckFn = bool (*)(BGSConstructibleObject*, TESObjectREFR*, void*, bool);
	using WorkshopConsumeComponentFn = void (*)(TESForm*, std::uint32_t);
	using RemoveComponentsFn = void (*)(
		TESObjectREFR*,
		TESForm*,
		std::uint32_t,
		bool,
		void*,
		bool,
		std::uint32_t,
		void*);
	using WorkshopObjectCountFn = bool (*)(void*, TESForm*, bool, float*);
	using CurrentWorkshopObjectCountFn = std::uint32_t (*)(TESForm*);


	PopulateLinkedWorkshopContainersFn originalPopulateLinkedWorkshopContainers = nullptr;
	RebuildWorkshopSupplyFn originalRebuildWorkshopSupply = nullptr;
	ComponentCountHelperFn originalComponentCountHelper = nullptr;
	DirectComponentCountFn originalDirectComponentCount = nullptr;
	WorkshopResourceStatusFn originalWorkshopResourceStatus = nullptr;
	WorkshopMenuAvailabilityFn originalWorkshopMenuAvailability = nullptr;
	WorkshopMenuSelectFn originalWorkshopMenuSelect = nullptr;
	WorkshopCheckAndSetPlacementFn originalWorkshopCheckAndSetPlacement = nullptr;
	WorkshopStartPlacementFn originalWorkshopStartPlacement = nullptr;
	WorkshopBuildResourceCheckFn originalWorkshopBuildResourceCheck = nullptr;
	WorkshopConsumeComponentFn originalWorkshopConsumeComponent = nullptr;
	RemoveComponentsFn originalRemoveComponents = nullptr;
	WorkshopObjectCountFn originalWorkshopObjectCount = nullptr;
	CurrentWorkshopObjectCountFn originalCurrentWorkshopObjectCount = nullptr;
	std::mutex rememberedWorkshopSupplyLinkLock;
	std::mutex sharedWorkshopContainerProbeLogLock;
	std::unordered_map<TESFormID, TESFormID> rememberedWorkshopSupplyLinks;
	std::unordered_set<TESFormID> loggedLootManSharedContainerLocations;
	std::unordered_set<std::uint64_t> loggedSharedWorkshopContainerProbeKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopMaterialProbeKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopMaterialAdjustmentKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopMaterialConsumptionKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopResourceStatusKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopMenuAvailabilityKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopCheckAndSetPlacementKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopPlacementTransitionKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopPlacementStateKeys;
	std::unordered_set<std::uint64_t> loggedWorkshopBuildResourceCheckKeys;
	// Investigation-only traces. Keep false for release builds; set true only
	// while re-investigating indoor workshop supply behavior.
	inline constexpr bool kVerboseWorkshopMaterialDiagnostics = false;
	inline constexpr std::uint32_t kWorkshopBuildResourceCheckConfirmSourceId =
		kWorkshopBuildResourceCheckCallSites[1].sourceId;
	inline constexpr std::uint32_t kWorkshopBuildResourceCheckConsumePrecheckSourceId =
		kWorkshopBuildResourceCheckCallSites[2].sourceId;

	struct DirectCallSiteDecode
	{
		const NativeHookCallSite* site = nullptr;
		std::uintptr_t address = 0;
		std::uintptr_t targetAddress = 0;
		std::uintptr_t targetRva = 0;
	};

	struct DirectCallInstructionReadContext
	{
		std::uintptr_t address = 0;
		std::array<std::uint8_t, 5> bytes{};
	};

	void ReadDirectCallInstructionBytes(void* opaque)
	{
		auto* context = static_cast<DirectCallInstructionReadContext*>(opaque);
		std::memcpy(
			context->bytes.data(),
			reinterpret_cast<const void*>(context->address),
			context->bytes.size());
	}

	std::optional<DirectCallSiteDecode> DecodeDirectCallSite(
		const NativeHookCallSite& site,
		const char* family)
	{
		REL::Relocation<std::uintptr_t> callSite{ REL::Offset(site.rva) };
		const auto address = callSite.address();
		DirectCallInstructionReadContext context{ address };
		if (!ExecuteSehCallSafe(&ReadDirectCallInstructionBytes, &context))
		{
			REX::ERROR(
				"Skipping native direct-call hook: family={}, site={}, rva={:X}, reason=instruction-read-failed",
				family,
				site.id,
				site.rva);
			return std::nullopt;
		}

		if (context.bytes[0] != 0xE8)
		{
			REX::ERROR(
				"Skipping native direct-call hook: family={}, site={}, rva={:X}, expectedOpcode=E8, actualOpcode={:02X}",
				family,
				site.id,
				site.rva,
				context.bytes[0]);
			return std::nullopt;
		}

		std::int32_t displacement = 0;
		std::memcpy(&displacement, context.bytes.data() + 1, sizeof(displacement));
		const auto targetAddress = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(address + 5) +
			static_cast<std::intptr_t>(displacement));
		const auto moduleBase = address - site.rva;
		return DirectCallSiteDecode{
			&site,
			address,
			targetAddress,
			targetAddress - moduleBase,
		};
	}

	bool ValidateDirectCallSiteFamily(
		std::span<const NativeHookCallSite> sites,
		const char* family,
		bool requireSharedOriginalTarget)
	{
		if (sites.empty())
		{
			REX::ERROR("Skipping native direct-call hook family: family={}, reason=no-sites", family);
			return false;
		}

		std::optional<std::uintptr_t> expectedTargetAddress;
		std::optional<std::uintptr_t> expectedTargetRva;
		for (const auto& site : sites)
		{
			const auto decoded = DecodeDirectCallSite(site, family);
			if (!decoded)
			{
				return false;
			}

			if (!requireSharedOriginalTarget)
			{
				continue;
			}

			if (!expectedTargetAddress)
			{
				expectedTargetAddress = decoded->targetAddress;
				expectedTargetRva = decoded->targetRva;
				continue;
			}

			if (*expectedTargetAddress != decoded->targetAddress)
			{
				REX::ERROR(
					"Skipping native direct-call hook family: family={}, site={}, rva={:X}, originalTargetRva={:X}, expectedOriginalTargetRva={:X}",
					family,
					site.id,
					site.rva,
					decoded->targetRva,
					expectedTargetRva.value_or(0));
				return false;
			}
		}

		return true;
	}

	template <class OriginalFn, class HookFn>
	OriginalFn WriteValidatedDirectCallHook(
		const NativeHookCallSite& site,
		HookFn hook,
		const char* family)
	{
		const auto decoded = DecodeDirectCallSite(site, family);
		if (!decoded)
		{
			return OriginalFn{};
		}

		REL::Relocation<std::uintptr_t> callSite{ REL::Offset(site.rva) };
		const auto original = reinterpret_cast<OriginalFn>(callSite.write_call<5>(hook));
		REX::INFO(
			"Installed native direct-call hook: family={}, site={}, rva={:X}, originalTargetRva={:X}",
			family,
			site.id,
			site.rva,
			decoded->targetRva);
		return original;
	}

	template <class OriginalFn, class HookFn, std::size_t N>
	bool InstallDirectCallHookFamily(
		const std::array<NativeHookCallSite, N>& sites,
		const std::array<HookFn, N>& hooks,
		OriginalFn& original,
		const char* family)
	{
		if (!ValidateDirectCallSiteFamily(
				std::span<const NativeHookCallSite>(sites.data(), sites.size()),
				family,
				true))
		{
			REX::ERROR("Skipped native direct-call hook family: family={}", family);
			return false;
		}

		for (std::size_t index = 0; index < sites.size(); ++index)
		{
			const auto patchedOriginal = WriteValidatedDirectCallHook<OriginalFn>(
				sites[index],
				hooks[index],
				family);
			if (!patchedOriginal)
			{
				REX::ERROR(
					"Skipped native direct-call hook family after validation changed: family={}, site={}, rva={:X}",
					family,
					sites[index].id,
					sites[index].rva);
				return false;
			}

			if (!original)
			{
				original = patchedOriginal;
			}
			else if (original != patchedOriginal)
			{
				REX::WARN(
					"Unexpected native direct-call original target after patch: family={}, site={}, rva={:X}, original={:X}, expected={:X}",
					family,
					sites[index].id,
					sites[index].rva,
					reinterpret_cast<std::uintptr_t>(patchedOriginal),
					reinterpret_cast<std::uintptr_t>(original));
			}
		}

		return true;
	}

	template <class OriginalFn, class HookFn>
	bool InstallDirectCallHookSite(
		const NativeHookCallSite& site,
		HookFn hook,
		OriginalFn& original,
		const char* family)
	{
		if (!ValidateDirectCallSiteFamily(
				std::span<const NativeHookCallSite>(&site, 1),
				family,
				false))
		{
			REX::ERROR("Skipped native direct-call hook: family={}, site={}", family, site.id);
			return false;
		}

		const auto patchedOriginal = WriteValidatedDirectCallHook<OriginalFn>(
			site,
			hook,
			family);
		if (!patchedOriginal)
		{
			REX::ERROR("Skipped native direct-call hook after validation changed: family={}, site={}", family, site.id);
			return false;
		}

		original = patchedOriginal;
		return true;
	}

	struct FormProbeSnapshot
	{
		std::uintptr_t pointer = 0;
		TESFormID formID = 0;
		std::uint32_t formType = 0;
		bool readable = false;
	};

	struct FormProbeSnapshotContext
	{
		TESForm* form = nullptr;
		FormProbeSnapshot snapshot;
	};

	void CaptureFormProbeSnapshotCall(void* opaque)
	{
		auto* context = static_cast<FormProbeSnapshotContext*>(opaque);
		auto* form = context->form;
		if (!form)
		{
			return;
		}

		context->snapshot.pointer = reinterpret_cast<std::uintptr_t>(form);
		context->snapshot.formID = form->GetFormID();
		context->snapshot.formType = static_cast<std::uint32_t>(form->GetFormType());
		context->snapshot.readable = true;
	}

	FormProbeSnapshot CaptureFormProbeSnapshot(TESForm* form)
	{
		FormProbeSnapshotContext context{ form };
		if (!ExecuteSehCallSafe(&CaptureFormProbeSnapshotCall, &context))
		{
			context.snapshot.pointer = reinterpret_cast<std::uintptr_t>(form);
			context.snapshot.readable = false;
		}
		return context.snapshot;
	}

	struct WorkshopSupplyOwnerProbeSnapshot
	{
		std::uintptr_t owner = 0;
		// Raw diagnostic reads from an opaque workshop supply owner layout.
		// They are not executable RVAs and do not drive behavior decisions.
		std::uintptr_t fieldE0 = 0;
		std::uintptr_t fieldE8 = 0;
		std::uintptr_t fieldF8 = 0;
		std::uintptr_t field2F8 = 0;
		bool readable = false;
	};

	struct WorkshopSupplyOwnerProbeContext
	{
		void* owner = nullptr;
		WorkshopSupplyOwnerProbeSnapshot snapshot;
	};

	void CaptureWorkshopSupplyOwnerProbeCall(void* opaque)
	{
		auto* context = static_cast<WorkshopSupplyOwnerProbeContext*>(opaque);
		auto base = reinterpret_cast<std::uintptr_t>(context->owner);
		if (base == 0)
		{
			return;
		}

		context->snapshot.owner = base;
		context->snapshot.fieldE0 = *reinterpret_cast<std::uintptr_t*>(base + kWorkshopSupplyOwnerFieldE0Offset);
		context->snapshot.fieldE8 = *reinterpret_cast<std::uintptr_t*>(base + kWorkshopSupplyOwnerFieldE8Offset);
		context->snapshot.fieldF8 = *reinterpret_cast<std::uintptr_t*>(base + kWorkshopSupplyOwnerFieldF8Offset);
		context->snapshot.field2F8 = *reinterpret_cast<std::uintptr_t*>(base + kWorkshopSupplyOwnerField2F8Offset);
		context->snapshot.readable = true;
	}

	WorkshopSupplyOwnerProbeSnapshot CaptureWorkshopSupplyOwnerProbe(void* owner)
	{
		WorkshopSupplyOwnerProbeContext context{ owner };
		if (!ExecuteSehCallSafe(&CaptureWorkshopSupplyOwnerProbeCall, &context))
		{
			context.snapshot.owner = reinterpret_cast<std::uintptr_t>(owner);
			context.snapshot.readable = false;
		}
		return context.snapshot;
	}

	bool ShouldLogWorkshopMaterialProbe(std::uint64_t key, std::size_t limit)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopMaterialProbeKeys.size() >= limit)
		{
			return false;
		}
		return loggedWorkshopMaterialProbeKeys.emplace(key).second;
	}

	std::uint64_t MakePointerProbeKey(std::uint32_t sourceId, std::uintptr_t pointer, std::uint32_t extra)
	{
		return (static_cast<std::uint64_t>(sourceId) << 56) ^
		       (static_cast<std::uint64_t>(extra) << 32) ^
		       (pointer >> 4);
	}

	bool ShouldLogWorkshopMaterialAdjustment(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		return loggedWorkshopMaterialAdjustmentKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopMaterialConsumption(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		return loggedWorkshopMaterialConsumptionKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopResourceStatus(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopResourceStatusKeys.size() >= 256)
		{
			return false;
		}
		return loggedWorkshopResourceStatusKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopMenuAvailability(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopMenuAvailabilityKeys.size() >= 256)
		{
			return false;
		}
		return loggedWorkshopMenuAvailabilityKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopCheckAndSetPlacement(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopCheckAndSetPlacementKeys.size() >= 128)
		{
			return false;
		}
		return loggedWorkshopCheckAndSetPlacementKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopPlacementTransition(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopPlacementTransitionKeys.size() >= 256)
		{
			return false;
		}
		return loggedWorkshopPlacementTransitionKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopPlacementState(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopPlacementStateKeys.size() >= 256)
		{
			return false;
		}
		return loggedWorkshopPlacementStateKeys.emplace(key).second;
	}

	bool ShouldLogWorkshopBuildResourceCheck(std::uint64_t key)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
		if (loggedWorkshopBuildResourceCheckKeys.size() >= 256)
		{
			return false;
		}
		return loggedWorkshopBuildResourceCheckKeys.emplace(key).second;
	}

	std::uint32_t ReadOutCount(std::int32_t* outCount)
	{
		return outCount ? static_cast<std::uint32_t>(*outCount) : 0;
	}

	bool HasRememberedLootManWorkshopForLocation(TESFormID locationId)
	{
		if (locationId == 0)
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock);
		return rememberedWorkshopSupplyLinks.find(locationId) != rememberedWorkshopSupplyLinks.end();
	}

	void FillFormProbeSnapshot(FormProbeSnapshot& snapshot, TESForm* form)
	{
		if (!form)
		{
			return;
		}

		snapshot.pointer = reinterpret_cast<std::uintptr_t>(form);
		snapshot.formID = form->GetFormID();
		snapshot.formType = static_cast<std::uint32_t>(form->GetFormType());
		snapshot.readable = true;
	}

	struct CurrentWorkshopProbeSnapshot
	{
		std::uint32_t handle = 0;
		FormProbeSnapshot workshop;
		FormProbeSnapshot location;
		bool locationRemembered = false;
	};

	struct CurrentWorkshopProbeContext
	{
		CurrentWorkshopProbeSnapshot snapshot;
	};

	void CaptureCurrentWorkshopProbeCall(void* opaque)
	{
		auto* context = static_cast<CurrentWorkshopProbeContext*>(opaque);
		static REL::Relocation<std::uint32_t*> currentWorkshopHandle{
			kCurrentWorkshopHandleGlobalId
		};
		auto* handlePtr = currentWorkshopHandle.get();
		if (!handlePtr)
		{
			return;
		}

		context->snapshot.handle = *handlePtr;
		if (context->snapshot.handle == 0)
		{
			return;
		}

		ObjectRefHandle handle;
		static_assert(sizeof(handle) == sizeof(context->snapshot.handle));
		std::memcpy(&handle, &context->snapshot.handle, sizeof(context->snapshot.handle));

		auto workshop = handle.get();
		auto* workshopRef = workshop.get();
		FillFormProbeSnapshot(context->snapshot.workshop, workshopRef);

		auto* location = workshopRef ? workshopRef->GetCurrentLocation() : nullptr;
		FillFormProbeSnapshot(context->snapshot.location, location);
		context->snapshot.locationRemembered =
			HasRememberedLootManWorkshopForLocation(context->snapshot.location.formID);
	}

	CurrentWorkshopProbeSnapshot CaptureCurrentWorkshopProbe()
	{
		CurrentWorkshopProbeContext context;
		if (!ExecuteSehCallSafe(&CaptureCurrentWorkshopProbeCall, &context))
		{
			context.snapshot.workshop.readable = false;
			context.snapshot.location.readable = false;
		}
		return context.snapshot;
	}

	struct WorkshopMaterialContextProbeSnapshot
	{
		CurrentWorkshopProbeSnapshot currentWorkshop;
		FormProbeSnapshot nearestWorkshop;
		FormProbeSnapshot nearestLocation;
		bool nearestLocationRemembered = false;
	};

	WorkshopMaterialContextProbeSnapshot CaptureWorkshopMaterialContextProbe()
	{
		WorkshopMaterialContextProbeSnapshot context;
		context.currentWorkshop = CaptureCurrentWorkshopProbe();

		auto* player = PlayerCharacter::GetSingleton();
		auto* nearestWorkshop = TryFindNearestValidWorkshop(player);
		context.nearestWorkshop = CaptureFormProbeSnapshot(nearestWorkshop);

		auto* nearestLocation = nearestWorkshop ? nearestWorkshop->GetCurrentLocation() : nullptr;
		context.nearestLocation = CaptureFormProbeSnapshot(nearestLocation);
		context.nearestLocationRemembered =
			HasRememberedLootManWorkshopForLocation(context.nearestLocation.formID);

		return context;
	}

	struct PlacementItemProbeSnapshot
	{
		std::uintptr_t placementHandlePtr = 0;
		std::uint32_t placementHandle = 0;
		bool placementHandleResolved = false;
		FormProbeSnapshot placementRef;
		FormProbeSnapshot placementBase;
		std::uintptr_t placementDataPtr = 0;
		std::uint32_t dataPlacementHandle = 0;
		bool dataReadable = false;
		bool dataPlacementHandleResolved = false;
		bool dataIsSet = false;
		bool dataMustSnap = false;
		bool dataAnythingIsGround = false;
		std::uint32_t dataDropProxyCount = 0;
		std::uint32_t dataBodyCount = 0;
		FormProbeSnapshot dataPlacementRef;
		FormProbeSnapshot dataPlacementBase;
		bool sehFailed = false;
	};

	void FillPlacementHandleProbe(
		ObjectRefHandle handle,
		std::uint32_t& rawHandle,
		bool& resolved,
		FormProbeSnapshot& refSnapshot,
		FormProbeSnapshot& baseSnapshot)
	{
		rawHandle = handle.get_handle();
		if (!handle)
		{
			return;
		}

		auto ref = handle.get();
		auto* refPtr = ref.get();
		resolved = refPtr != nullptr;
		FillFormProbeSnapshot(refSnapshot, refPtr);
		auto* baseForm = refPtr ? refPtr->GetObjectReference() : nullptr;
		FillFormProbeSnapshot(baseSnapshot, baseForm);
	}

	struct PlacementItemProbeContext
	{
		PlacementItemProbeSnapshot snapshot;
	};

	void CapturePlacementItemProbeCall(void* opaque)
	{
		auto* context = static_cast<PlacementItemProbeContext*>(opaque);
		auto* placementHandle = Workshop::GetPlacementItem();
		context->snapshot.placementHandlePtr =
			reinterpret_cast<std::uintptr_t>(placementHandle);
		if (placementHandle)
		{
			FillPlacementHandleProbe(
				*placementHandle,
				context->snapshot.placementHandle,
				context->snapshot.placementHandleResolved,
				context->snapshot.placementRef,
				context->snapshot.placementBase);
		}

		auto* placementData = Workshop::GetCurrentPlacementItemData();
		context->snapshot.placementDataPtr =
			reinterpret_cast<std::uintptr_t>(placementData);
		if (!placementData)
		{
			return;
		}

		context->snapshot.dataReadable = true;
		context->snapshot.dataIsSet = placementData->isSet;
		context->snapshot.dataMustSnap = placementData->mustSnap;
		context->snapshot.dataAnythingIsGround = placementData->anythingIsGround;
		context->snapshot.dataDropProxyCount =
			static_cast<std::uint32_t>(placementData->dropProxy.size());
		context->snapshot.dataBodyCount =
			static_cast<std::uint32_t>(placementData->body.size());
		FillPlacementHandleProbe(
			placementData->placementItem,
			context->snapshot.dataPlacementHandle,
			context->snapshot.dataPlacementHandleResolved,
			context->snapshot.dataPlacementRef,
			context->snapshot.dataPlacementBase);
	}

	PlacementItemProbeSnapshot CapturePlacementItemProbe()
	{
		PlacementItemProbeContext context;
		if (!ExecuteSehCallSafe(&CapturePlacementItemProbeCall, &context))
		{
			context.snapshot.sehFailed = true;
		}
		return context.snapshot;
	}

	TESObjectREFR* ResolveCurrentWorkshopForOwner(TESObjectREFR* owner)
	{
		if (!owner)
		{
			return nullptr;
		}

		const auto ownerSnapshot = CaptureFormProbeSnapshot(owner);
		if (!ownerSnapshot.readable || ownerSnapshot.formID == 0)
		{
			return nullptr;
		}

		const auto currentWorkshop = CaptureCurrentWorkshopProbe();
		if (currentWorkshop.workshop.readable &&
			currentWorkshop.workshop.formID == ownerSnapshot.formID)
		{
			if (auto* workshop = TESForm::GetFormByID<TESObjectREFR>(currentWorkshop.workshop.formID))
			{
				return workshop;
			}
		}

		auto* player = PlayerCharacter::GetSingleton();
		auto* nearestWorkshop = TryFindNearestValidWorkshop(player);
		if (nearestWorkshop && nearestWorkshop->formID == ownerSnapshot.formID)
		{
			return nearestWorkshop;
		}

		return nullptr;
	}

	TESObjectREFR* ResolveActiveRememberedWorkshopForOwner(void* owner)
	{
		const auto ownerSnapshot = CaptureFormProbeSnapshot(reinterpret_cast<TESForm*>(owner));
		if (ownerSnapshot.readable &&
			ownerSnapshot.formType == static_cast<std::uint32_t>(ENUM_FORM_ID::kREFR))
		{
			if (auto* workshop = ResolveCurrentWorkshopForOwner(reinterpret_cast<TESObjectREFR*>(owner)))
			{
				return workshop;
			}
		}

		const auto currentWorkshop = CaptureCurrentWorkshopProbe();
		if (!currentWorkshop.workshop.readable ||
			currentWorkshop.workshop.formID == 0 ||
			!currentWorkshop.locationRemembered)
		{
			return nullptr;
		}

		return TESForm::GetFormByID<TESObjectREFR>(currentWorkshop.workshop.formID);
	}

	BGSKeyword* GetNativeWorkshopCaravanKeyword()
	{
		static REL::Relocation<BGSKeyword**> keyword{
			kWorkshopCaravanKeywordGlobalId
		};
		auto* keywordPtr = keyword.get();
		return keywordPtr ? *keywordPtr : nullptr;
	}

	std::uint64_t MakeSharedWorkshopContainerProbeKey(BGSLocation* currentLocation, bool includePlayer)
	{
		const auto locationId = currentLocation ? currentLocation->formID : 0;
		return (static_cast<std::uint64_t>(locationId) << 1) |
		       static_cast<std::uint64_t>(includePlayer ? 1 : 0);
	}

	void LogSharedWorkshopContainerHookProbe(
		SharedWorkshopContainers* containers,
		BGSLocation* currentLocation,
		bool includePlayer)
	{
		if (!kVerboseWorkshopMaterialDiagnostics)
		{
			return;
		}

		const auto key = MakeSharedWorkshopContainerProbeKey(currentLocation, includePlayer);
		{
			std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
			if (!loggedSharedWorkshopContainerProbeKeys.emplace(key).second)
			{
				return;
			}
		}

		const auto locationId = currentLocation ? currentLocation->formID : 0;
		auto* lootManWorkshop = properties::GetLootManWorkshopRef();
		auto* lootManLocation = lootManWorkshop ? lootManWorkshop->GetCurrentLocation() : nullptr;
		auto* workshopCaravanKeyword = GetNativeWorkshopCaravanKeyword();

		REX::INFO(
			"Native shared workshop container hook probe: currentLocation={:08X}, lootManLocation={:08X}, lootManWorkshop={:08X}, workshopCaravanKeyword={:08X}, includePlayer={}, containerCount={}, containers={:016X}",
			locationId,
			lootManLocation ? lootManLocation->formID : 0,
			lootManWorkshop ? lootManWorkshop->formID : 0,
			workshopCaravanKeyword ? workshopCaravanKeyword->formID : 0,
			includePlayer,
			containers ? containers->size() : 0,
			reinterpret_cast<std::uintptr_t>(containers));
	}

	TESObjectREFR* GetRememberedLootManWorkshopForLocation(BGSLocation* currentLocation)
	{
		if (!currentLocation)
		{
			return nullptr;
		}

		TESFormID lootManWorkshopId = 0;
		{
			std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock);
			const auto it = rememberedWorkshopSupplyLinks.find(currentLocation->formID);
			if (it == rememberedWorkshopSupplyLinks.end())
			{
				return nullptr;
			}
			lootManWorkshopId = it->second;
		}

		auto* lootManWorkshop = TESForm::GetFormByID<TESObjectREFR>(lootManWorkshopId);
		if (!lootManWorkshop)
		{
			lootManWorkshop = properties::GetLootManWorkshopRef();
		}

		return lootManWorkshop;
	}

	bool ContainsSharedContainer(SharedWorkshopContainers* containers, TESObjectREFR* ref)
	{
		if (!containers || !ref)
		{
			return true;
		}

		const auto count = containers->size();
		auto* data = containers->data();
		if (!data || count == 0)
		{
			return false;
		}

		for (std::uint32_t index = 0; index < count; ++index)
		{
			if (data[index].get() == ref)
			{
				return true;
			}
		}
		return false;
	}

	BGSLocation* ResolveRememberedWorkshopLocationFallback()
	{
		const auto currentWorkshop = CaptureCurrentWorkshopProbe();
		if (currentWorkshop.locationRemembered &&
			currentWorkshop.location.formID != 0)
		{
			if (auto* location = TESForm::GetFormByID<BGSLocation>(
				currentWorkshop.location.formID))
			{
				return location;
			}
		}

		auto* player = PlayerCharacter::GetSingleton();
		auto* nearestWorkshop = TryFindNearestValidWorkshop(player);
		auto* nearestLocation = nearestWorkshop ? nearestWorkshop->GetCurrentLocation() : nullptr;
		if (nearestLocation &&
			HasRememberedLootManWorkshopForLocation(nearestLocation->formID))
		{
			return nearestLocation;
		}

		return nullptr;
	}

	void LogLootManSharedContainerAdded(
		BGSLocation* requestedLocation,
		BGSLocation* effectiveLocation,
		bool inferredLocation,
		TESObjectREFR* lootManWorkshop,
		std::uint32_t containerCount)
	{
		const auto locationId = effectiveLocation ? effectiveLocation->formID : 0;
		{
			std::lock_guard<std::mutex> guard(sharedWorkshopContainerProbeLogLock);
			if (!loggedLootManSharedContainerLocations.emplace(locationId).second)
			{
				return;
			}
		}

		auto* lootManLocation = lootManWorkshop ? lootManWorkshop->GetCurrentLocation() : nullptr;
		REX::INFO(
			"Added LootMan workshop to native shared workshop containers: requestedLocation={:08X}, effectiveLocation={:08X}, inferredLocation={}, lootManLocation={:08X}, lootManWorkshop={:08X}, containerCount={}",
			requestedLocation ? requestedLocation->formID : 0,
			locationId,
			inferredLocation,
			lootManLocation ? lootManLocation->formID : 0,
			lootManWorkshop ? lootManWorkshop->formID : 0,
			containerCount);
	}

	void AddRememberedLootManWorkshopSharedContainer(
		SharedWorkshopContainers* containers,
		BGSLocation* currentLocation)
	{
		if (!containers)
		{
			return;
		}

		auto* effectiveLocation = currentLocation;
		bool inferredLocation = false;
		if (!effectiveLocation)
		{
			effectiveLocation = ResolveRememberedWorkshopLocationFallback();
			inferredLocation = effectiveLocation != nullptr;
		}

		if (!effectiveLocation)
		{
			return;
		}

		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(effectiveLocation);
		if (!lootManWorkshop || ContainsSharedContainer(containers, lootManWorkshop))
		{
			return;
		}

		containers->push_back(NiPointer<TESObjectREFR>(lootManWorkshop));
		LogLootManSharedContainerAdded(
			currentLocation,
			effectiveLocation,
			inferredLocation,
			lootManWorkshop,
			containers->size());
	}

	void HookedPopulateLinkedWorkshopContainers(
		SharedWorkshopContainers* containers,
		BGSLocation* currentLocation,
		bool includePlayer)
	{
		if (originalPopulateLinkedWorkshopContainers)
		{
			originalPopulateLinkedWorkshopContainers(containers, currentLocation, includePlayer);
		}
		LogSharedWorkshopContainerHookProbe(containers, currentLocation, includePlayer);
		AddRememberedLootManWorkshopSharedContainer(containers, currentLocation);
	}

	void LogRebuildWorkshopSupplyProbe(std::uint32_t sourceId, const char* sourceName, void* owner)
	{
		const auto ownerSnapshot = CaptureWorkshopSupplyOwnerProbe(owner);
		const auto ownerForm = CaptureFormProbeSnapshot(reinterpret_cast<TESForm*>(owner));
		const auto key = MakePointerProbeKey(sourceId, ownerSnapshot.owner, ownerForm.formID);
		if (!ShouldLogWorkshopMaterialProbe(key, 96))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=rebuild-supply, source={}, owner={:016X}, ownerReadable={}, ownerForm={:08X}, ownerType={}, fieldsReadable={}, fieldE0={:016X}, fieldE8={:016X}, fieldF8={:016X}, field2F8={:016X}",
			sourceName,
			ownerSnapshot.owner,
			ownerForm.readable,
			ownerForm.formID,
			ownerForm.formType,
			ownerSnapshot.readable,
			ownerSnapshot.fieldE0,
			ownerSnapshot.fieldE8,
			ownerSnapshot.fieldF8,
			ownerSnapshot.field2F8);
	}

	void HookedRebuildWorkshopSupply(
		void* owner,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		if (originalRebuildWorkshopSupply)
		{
			originalRebuildWorkshopSupply(owner);
		}
		LogRebuildWorkshopSupplyProbe(sourceId, sourceName, owner);
	}

	void HookedRebuildWorkshopSupplySourceA1(void* owner)
	{
		const auto& site = kRebuildWorkshopSupplyCallSites[0];
		HookedRebuildWorkshopSupply(owner, site.sourceId, site.label);
	}

	void HookedRebuildWorkshopSupplySourceA2(void* owner)
	{
		const auto& site = kRebuildWorkshopSupplyCallSites[1];
		HookedRebuildWorkshopSupply(owner, site.sourceId, site.label);
	}

	void HookedRebuildWorkshopSupplySourceA3(void* owner)
	{
		const auto& site = kRebuildWorkshopSupplyCallSites[2];
		HookedRebuildWorkshopSupply(owner, site.sourceId, site.label);
	}

	void HookedRebuildWorkshopSupplySourceA4(void* owner)
	{
		const auto& site = kRebuildWorkshopSupplyCallSites[3];
		HookedRebuildWorkshopSupply(owner, site.sourceId, site.label);
	}

	void LogComponentCountProbe(
		std::uint32_t sourceId,
		const char* sourceName,
		void* owner,
		std::int32_t* outCount,
		TESForm* form,
		bool includeLinked,
		bool result)
	{
		const auto ownerForm = CaptureFormProbeSnapshot(reinterpret_cast<TESForm*>(owner));
		const auto targetForm = CaptureFormProbeSnapshot(form);
		const auto context = sourceId == 0xB2 ?
			CaptureWorkshopMaterialContextProbe() :
			WorkshopMaterialContextProbeSnapshot{};
		const auto contextKey = targetForm.formID ^
			context.currentWorkshop.workshop.formID ^
			(context.nearestWorkshop.formID << 1);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(owner),
			contextKey);
		if (!ShouldLogWorkshopMaterialProbe(key, 192))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=component-count, source={}, owner={:016X}, ownerReadable={}, ownerForm={:08X}, ownerType={}, target={:016X}, targetReadable={}, targetForm={:08X}, targetType={}, includeLinked={}, result={}, outCount={}, currentWorkshopHandle={:08X}, currentWorkshopReadable={}, currentWorkshop={:08X}, currentWorkshopType={}, currentLocationReadable={}, currentLocation={:08X}, currentLocationRemembered={}, nearestWorkshopReadable={}, nearestWorkshop={:08X}, nearestWorkshopType={}, nearestLocationReadable={}, nearestLocation={:08X}, nearestLocationRemembered={}",
			sourceName,
			reinterpret_cast<std::uintptr_t>(owner),
			ownerForm.readable,
			ownerForm.formID,
			ownerForm.formType,
			reinterpret_cast<std::uintptr_t>(form),
			targetForm.readable,
			targetForm.formID,
			targetForm.formType,
			includeLinked,
			result,
			ReadOutCount(outCount),
			context.currentWorkshop.handle,
			context.currentWorkshop.workshop.readable,
			context.currentWorkshop.workshop.formID,
			context.currentWorkshop.workshop.formType,
			context.currentWorkshop.location.readable,
			context.currentWorkshop.location.formID,
			context.currentWorkshop.locationRemembered,
			context.nearestWorkshop.readable,
			context.nearestWorkshop.formID,
			context.nearestWorkshop.formType,
			context.nearestLocation.readable,
			context.nearestLocation.formID,
			context.nearestLocationRemembered);
	}

	struct WorkshopMaterialCountAdjustment
	{
		TESObjectREFR* currentWorkshop = nullptr;
		BGSLocation* currentLocation = nullptr;
		TESObjectREFR* lootManWorkshop = nullptr;
		std::int32_t baseCount = 0;
		std::int32_t extraCount = 0;
		std::int32_t totalCount = 0;
		bool extraResult = false;
		bool applied = false;
	};

	std::int32_t SaturatingAddNonNegative(std::int32_t baseCount, std::int32_t extraCount)
	{
		const auto safeExtra = std::max<std::int32_t>(extraCount, 0);
		const auto total = static_cast<std::int64_t>(baseCount) + safeExtra;
		return static_cast<std::int32_t>(std::min<std::int64_t>(
			total,
			static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
	}

	void LogWorkshopMaterialCountAdjustment(
		void* owner,
		TESForm* form,
		const WorkshopMaterialCountAdjustment& adjustment)
	{
		const auto targetForm = CaptureFormProbeSnapshot(form);
		const auto locationId = adjustment.currentLocation ? adjustment.currentLocation->formID : 0;
		const auto key = MakePointerProbeKey(
			0xD1,
			reinterpret_cast<std::uintptr_t>(owner),
			targetForm.formID ^ locationId);
		if (!ShouldLogWorkshopMaterialAdjustment(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material count adjusted: owner={:016X}, target={:08X}, targetType={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, baseCount={}, extraResult={}, extraCount={}, totalCount={}, applied={}",
			reinterpret_cast<std::uintptr_t>(owner),
			targetForm.formID,
			targetForm.formType,
			adjustment.currentWorkshop ? adjustment.currentWorkshop->formID : 0,
			locationId,
			adjustment.lootManWorkshop ? adjustment.lootManWorkshop->formID : 0,
			adjustment.baseCount,
			adjustment.extraResult,
			adjustment.extraCount,
			adjustment.totalCount,
			adjustment.applied);
	}

	bool IsReadableFormType(TESForm* form, ENUM_FORM_ID expectedType)
	{
		const auto snapshot = CaptureFormProbeSnapshot(form);
		return snapshot.readable &&
			snapshot.formType == static_cast<std::uint32_t>(expectedType);
	}

	WorkshopMaterialCountAdjustment ApplyRememberedWorkshopDirectComponentCount(
		void* owner,
		BGSComponent* component,
		std::int32_t baseCount)
	{
		WorkshopMaterialCountAdjustment adjustment;
		adjustment.baseCount = baseCount;
		adjustment.totalCount = baseCount;

		if (!originalDirectComponentCount ||
			!owner ||
			!component ||
			!IsReadableFormType(reinterpret_cast<TESForm*>(owner), ENUM_FORM_ID::kREFR) ||
			!IsReadableFormType(component, ENUM_FORM_ID::kCMPO))
		{
			return adjustment;
		}

		auto* currentWorkshop = ResolveActiveRememberedWorkshopForOwner(owner);
		if (!currentWorkshop)
		{
			return adjustment;
		}

		auto* currentLocation = currentWorkshop->GetCurrentLocation();
		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(currentLocation);
		if (!lootManWorkshop || lootManWorkshop == currentWorkshop)
		{
			return adjustment;
		}

		adjustment.currentWorkshop = currentWorkshop;
		adjustment.currentLocation = currentLocation;
		adjustment.lootManWorkshop = lootManWorkshop;
		adjustment.extraCount = originalDirectComponentCount(
			lootManWorkshop,
			component,
			false);
		adjustment.extraResult = true;
		if (adjustment.extraCount <= 0)
		{
			LogWorkshopMaterialCountAdjustment(owner, component, adjustment);
			return adjustment;
		}

		adjustment.totalCount = SaturatingAddNonNegative(adjustment.baseCount, adjustment.extraCount);
		adjustment.applied = true;
		LogWorkshopMaterialCountAdjustment(owner, component, adjustment);
		return adjustment;
	}

	WorkshopMaterialCountAdjustment ApplyRememberedWorkshopMaterialCount(
		std::uint32_t sourceId,
		void* owner,
		std::int32_t* outCount,
		TESForm* form,
		bool includeLinked,
		bool originalResult)
	{
		WorkshopMaterialCountAdjustment adjustment;
		adjustment.baseCount = outCount ? *outCount : 0;
		adjustment.totalCount = adjustment.baseCount;

		if (sourceId != 0xB2 ||
			!originalResult ||
			!originalComponentCountHelper ||
			!owner ||
			!outCount ||
			!form ||
			!IsReadableFormType(form, ENUM_FORM_ID::kCMPO) ||
			!IsReadableFormType(reinterpret_cast<TESForm*>(owner), ENUM_FORM_ID::kREFR) ||
			includeLinked)
		{
			return adjustment;
		}

		auto* currentWorkshop = ResolveActiveRememberedWorkshopForOwner(owner);
		if (!currentWorkshop)
		{
			return adjustment;
		}

		auto* currentLocation = currentWorkshop->GetCurrentLocation();
		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(currentLocation);
		if (!lootManWorkshop || lootManWorkshop == currentWorkshop)
		{
			return adjustment;
		}

		adjustment.currentWorkshop = currentWorkshop;
		adjustment.currentLocation = currentLocation;
		adjustment.lootManWorkshop = lootManWorkshop;
		adjustment.extraResult = originalComponentCountHelper(
			lootManWorkshop,
			&adjustment.extraCount,
			form,
			false);
		if (!adjustment.extraResult || adjustment.extraCount <= 0)
		{
			LogWorkshopMaterialCountAdjustment(owner, form, adjustment);
			return adjustment;
		}

		adjustment.totalCount = SaturatingAddNonNegative(adjustment.baseCount, adjustment.extraCount);
		*outCount = adjustment.totalCount;
		adjustment.applied = true;
		LogWorkshopMaterialCountAdjustment(owner, form, adjustment);
		return adjustment;
	}

	bool HookedComponentCountHelper(
		void* owner,
		std::int32_t* outCount,
		TESForm* form,
		bool includeLinked,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const bool result = originalComponentCountHelper ?
			originalComponentCountHelper(owner, outCount, form, includeLinked) :
			false;
		(void)ApplyRememberedWorkshopMaterialCount(
			sourceId,
			owner,
			outCount,
			form,
			includeLinked,
			result);
		LogComponentCountProbe(sourceId, sourceName, owner, outCount, form, includeLinked, result);
		return result;
	}

	bool HookedComponentCountPapyrus(void* owner, std::int32_t* outCount, TESForm* form, bool includeLinked)
	{
		const auto& site = kComponentCountHelperCallSites[0];
		return HookedComponentCountHelper(
			owner,
			outCount,
			form,
			includeLinked,
			site.sourceId,
			site.label);
	}

	bool HookedComponentCountWorkbenchUi(void* owner, std::int32_t* outCount, TESForm* form, bool includeLinked)
	{
		const auto& site = kComponentCountHelperCallSites[1];
		return HookedComponentCountHelper(
			owner,
			outCount,
			form,
			includeLinked,
			site.sourceId,
			site.label);
	}

	void LogDirectComponentCountProbe(
		std::uint32_t sourceId,
		const char* sourceName,
		void* owner,
		BGSComponent* component,
		bool includeLinked,
		std::int32_t baseCount,
		const WorkshopMaterialCountAdjustment& adjustment)
	{
		const auto ownerForm = CaptureFormProbeSnapshot(reinterpret_cast<TESForm*>(owner));
		const auto componentForm = CaptureFormProbeSnapshot(component);
		const auto context = CaptureWorkshopMaterialContextProbe();
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(owner),
			componentForm.formID ^
				context.currentWorkshop.workshop.formID ^
				(context.nearestWorkshop.formID << 1));
		if (!ShouldLogWorkshopMaterialProbe(key, 320))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=direct-component-count, source={}, owner={:016X}, ownerReadable={}, ownerForm={:08X}, ownerType={}, component={:016X}, componentReadable={}, componentForm={:08X}, componentType={}, includeLinked={}, baseCount={}, totalCount={}, applied={}, currentWorkshopHandle={:08X}, currentWorkshopReadable={}, currentWorkshop={:08X}, currentWorkshopType={}, currentLocationReadable={}, currentLocation={:08X}, currentLocationRemembered={}, nearestWorkshopReadable={}, nearestWorkshop={:08X}, nearestWorkshopType={}, nearestLocationReadable={}, nearestLocation={:08X}, nearestLocationRemembered={}",
			sourceName,
			reinterpret_cast<std::uintptr_t>(owner),
			ownerForm.readable,
			ownerForm.formID,
			ownerForm.formType,
			reinterpret_cast<std::uintptr_t>(component),
			componentForm.readable,
			componentForm.formID,
			componentForm.formType,
			includeLinked,
			baseCount,
			adjustment.totalCount,
			adjustment.applied,
			context.currentWorkshop.handle,
			context.currentWorkshop.workshop.readable,
			context.currentWorkshop.workshop.formID,
			context.currentWorkshop.workshop.formType,
			context.currentWorkshop.location.readable,
			context.currentWorkshop.location.formID,
			context.currentWorkshop.locationRemembered,
			context.nearestWorkshop.readable,
			context.nearestWorkshop.formID,
			context.nearestWorkshop.formType,
			context.nearestLocation.readable,
			context.nearestLocation.formID,
			context.nearestLocationRemembered);
	}

	std::int32_t HookedDirectComponentCount(
		void* owner,
		BGSComponent* component,
		bool includeLinked,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto baseCount = originalDirectComponentCount ?
			originalDirectComponentCount(owner, component, includeLinked) :
			0;
		const auto adjustment = ApplyRememberedWorkshopDirectComponentCount(
			owner,
			component,
			baseCount);
		LogDirectComponentCountProbe(
			sourceId,
			sourceName,
			owner,
			component,
			includeLinked,
			baseCount,
			adjustment);
		return adjustment.applied ? adjustment.totalCount : baseCount;
	}

	std::int32_t HookedDirectComponentCountSourceE1(void* owner, BGSComponent* component, bool includeLinked)
	{
		const auto& site = kDirectComponentCountCallSites[0];
		return HookedDirectComponentCount(owner, component, includeLinked, site.sourceId, site.label);
	}

	std::int32_t HookedDirectComponentCountSourceE2(void* owner, BGSComponent* component, bool includeLinked)
	{
		const auto& site = kDirectComponentCountCallSites[1];
		return HookedDirectComponentCount(owner, component, includeLinked, site.sourceId, site.label);
	}

	std::int32_t HookedDirectComponentCountSourceE3(void* owner, BGSComponent* component, bool includeLinked)
	{
		const auto& site = kDirectComponentCountCallSites[2];
		return HookedDirectComponentCount(owner, component, includeLinked, site.sourceId, site.label);
	}

	std::int32_t HookedDirectComponentCountSourceE4(void* owner, BGSComponent* component, bool includeLinked)
	{
		const auto& site = kDirectComponentCountCallSites[3];
		return HookedDirectComponentCount(owner, component, includeLinked, site.sourceId, site.label);
	}

	std::int32_t HookedDirectComponentCountSourceE5(void* owner, BGSComponent* component, bool includeLinked)
	{
		const auto& site = kDirectComponentCountCallSites[4];
		return HookedDirectComponentCount(owner, component, includeLinked, site.sourceId, site.label);
	}

	TESObjectREFR* ResolveActiveRememberedWorkshop()
	{
		const auto currentWorkshop = CaptureCurrentWorkshopProbe();
		if (currentWorkshop.workshop.readable &&
			currentWorkshop.workshop.formID != 0 &&
			currentWorkshop.locationRemembered)
		{
			if (auto* workshop = TESForm::GetFormByID<TESObjectREFR>(
				currentWorkshop.workshop.formID))
			{
				return workshop;
			}
		}

		auto* player = PlayerCharacter::GetSingleton();
		auto* nearestWorkshop = TryFindNearestValidWorkshop(player);
		auto* nearestLocation = nearestWorkshop ? nearestWorkshop->GetCurrentLocation() : nullptr;
		if (nearestLocation &&
			HasRememberedLootManWorkshopForLocation(nearestLocation->formID))
		{
			return nearestWorkshop;
		}

		return nullptr;
	}

	struct SelectedWorkshopRecipeProbeSnapshot
	{
		std::uint16_t selectedRow = 0;
		std::uint32_t menuResult = 0;
		std::uintptr_t menuNode = 0;
		BGSConstructibleObject* recipe = nullptr;
		FormProbeSnapshot recipeForm;
		bool readable = false;
		bool sehFailed = false;
	};

	struct SelectedWorkshopRecipeProbeContext
	{
		SelectedWorkshopRecipeProbeSnapshot snapshot;
	};

	void CaptureSelectedWorkshopRecipeProbeCall(void* opaque)
	{
		auto* context = static_cast<SelectedWorkshopRecipeProbeContext*>(opaque);
		static REL::Relocation<GetWorkshopMenuNodeFn> getWorkshopMenuNode{
			ID::Workshop::GetSelectedWorkshopMenuNode
		};

		auto* selectedRow = Workshop::GetCurrentRow();
		if (!selectedRow || !getWorkshopMenuNode)
		{
			return;
		}

		context->snapshot.selectedRow = *selectedRow;
		std::uint32_t menuResult = 0;
		auto* menuNode = getWorkshopMenuNode(context->snapshot.selectedRow, &menuResult);
		context->snapshot.menuResult = menuResult;
		context->snapshot.menuNode = reinterpret_cast<std::uintptr_t>(menuNode);
		if (!menuNode)
		{
			return;
		}

		context->snapshot.readable = true;
		context->snapshot.recipe = menuNode->recipe;
		FillFormProbeSnapshot(context->snapshot.recipeForm, context->snapshot.recipe);
	}

	SelectedWorkshopRecipeProbeSnapshot CaptureSelectedWorkshopRecipeProbe()
	{
		SelectedWorkshopRecipeProbeContext context;
		if (!ExecuteSehCallSafe(&CaptureSelectedWorkshopRecipeProbeCall, &context))
		{
			context.snapshot.sehFailed = true;
		}
		return context.snapshot;
	}

	struct WorkshopMenuRecipeProbeContext
	{
		std::uint32_t row = 0;
		std::uint32_t menuResult = 0;
		SelectedWorkshopRecipeProbeSnapshot snapshot;
	};

	void CaptureWorkshopMenuRecipeProbeCall(void* opaque)
	{
		auto* context = static_cast<WorkshopMenuRecipeProbeContext*>(opaque);
		static REL::Relocation<GetWorkshopMenuNodeFn> getWorkshopMenuNode{
			ID::Workshop::GetSelectedWorkshopMenuNode
		};
		if (!getWorkshopMenuNode)
		{
			return;
		}

		context->snapshot.selectedRow = static_cast<std::uint16_t>(context->row);
		std::uint32_t menuResult = context->menuResult;
		auto* menuNode = getWorkshopMenuNode(
			context->snapshot.selectedRow,
			&menuResult);
		context->snapshot.menuResult = menuResult;
		context->snapshot.menuNode = reinterpret_cast<std::uintptr_t>(menuNode);
		if (!menuNode)
		{
			return;
		}

		context->snapshot.readable = true;
		context->snapshot.recipe = menuNode->recipe;
		FillFormProbeSnapshot(context->snapshot.recipeForm, context->snapshot.recipe);
	}

	SelectedWorkshopRecipeProbeSnapshot CaptureWorkshopMenuRecipeProbe(
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		WorkshopMenuRecipeProbeContext context;
		context.row = row;
		context.menuResult = menuResult;
		if (!ExecuteSehCallSafe(&CaptureWorkshopMenuRecipeProbeCall, &context))
		{
			context.snapshot.sehFailed = true;
		}
		return context.snapshot;
	}

	struct WorkshopRecipePointerProbeContext
	{
		BGSConstructibleObject* recipe = nullptr;
		SelectedWorkshopRecipeProbeSnapshot snapshot;
	};

	void CaptureWorkshopRecipePointerProbeCall(void* opaque)
	{
		auto* context = static_cast<WorkshopRecipePointerProbeContext*>(opaque);
		if (!context->recipe)
		{
			return;
		}

		context->snapshot.readable = true;
		context->snapshot.recipe = context->recipe;
		FillFormProbeSnapshot(context->snapshot.recipeForm, context->recipe);
	}

	SelectedWorkshopRecipeProbeSnapshot CaptureWorkshopRecipePointerProbe(
		BGSConstructibleObject* recipe)
	{
		WorkshopRecipePointerProbeContext context;
		context.recipe = recipe;
		if (!ExecuteSehCallSafe(&CaptureWorkshopRecipePointerProbeCall, &context))
		{
			context.snapshot.sehFailed = true;
		}
		return context.snapshot;
	}

	std::int32_t NonNegativeWorkshopComponentCount(
		TESObjectREFR* workshop,
		BGSComponent* component,
		bool includeLinked)
	{
		if (!originalDirectComponentCount || !workshop || !component)
		{
			return 0;
		}

		return std::max<std::int32_t>(
			originalDirectComponentCount(workshop, component, includeLinked),
			0);
	}

	struct WorkshopMaterialComponentRemoval
	{
		BGSComponent* component = nullptr;
		std::uint32_t requestedCount = 0;
		std::int32_t baseCount = 0;
		std::int32_t lootManCount = 0;
		std::uint32_t consumeFromLootMan = 0;
	};

	struct WorkshopResourceStatusEvaluation
	{
		TESObjectREFR* currentWorkshop = nullptr;
		BGSLocation* currentLocation = nullptr;
		TESObjectREFR* lootManWorkshop = nullptr;
		std::vector<WorkshopMaterialComponentRemoval> componentRemovals;
		std::uint32_t requiredItemCount = 0;
		std::uint32_t satisfiedItemCount = 0;
		std::uint32_t lootManBackedItemCount = 0;
		FormProbeSnapshot missingForm;
		std::uint32_t missingRequiredCount = 0;
		std::int32_t missingBaseCount = 0;
		std::int32_t missingLootManCount = 0;
		std::int32_t missingTotalCount = 0;
		FormProbeSnapshot unsupportedForm;
		bool evaluated = false;
		bool allSatisfied = false;
		bool applied = false;
		bool sehFailed = false;
	};

	struct PendingWorkshopBuildConsumptionContext
	{
		TESFormID currentWorkshopId = 0;
		TESFormID currentLocationId = 0;
		TESFormID lootManWorkshopId = 0;
		TESFormID recipeId = 0;
		Clock::time_point createdAt{};
		std::uint32_t remainingConsumeCalls = 0;
		bool active = false;
	};

	thread_local PendingWorkshopBuildConsumptionContext pendingWorkshopBuildConsumption;

	struct WorkshopResourceStatusEvaluationContext
	{
		SelectedWorkshopRecipeProbeSnapshot selectedRecipe;
		TESObjectREFR* ownerWorkshop = nullptr;
		bool requireOwnerWorkshop = false;
		WorkshopResourceStatusEvaluation evaluation;
	};

	void EvaluateWorkshopResourceStatusCall(void* opaque)
	{
		auto* context = static_cast<WorkshopResourceStatusEvaluationContext*>(opaque);
		auto& evaluation = context->evaluation;
		auto* recipe = context->selectedRecipe.recipe;
		if (!recipe ||
			!context->selectedRecipe.recipeForm.readable ||
			!originalDirectComponentCount)
		{
			return;
		}

		auto* currentWorkshop = context->ownerWorkshop ?
			ResolveActiveRememberedWorkshopForOwner(context->ownerWorkshop) :
			nullptr;
		if (!currentWorkshop && !context->requireOwnerWorkshop)
		{
			currentWorkshop = ResolveActiveRememberedWorkshop();
		}
		if (!currentWorkshop)
		{
			return;
		}

		if (context->requireOwnerWorkshop)
		{
			const auto ownerSnapshot = CaptureFormProbeSnapshot(context->ownerWorkshop);
			if (!ownerSnapshot.readable ||
				ownerSnapshot.formID == 0 ||
				currentWorkshop->formID != ownerSnapshot.formID)
			{
				return;
			}
		}

		auto* currentLocation = currentWorkshop->GetCurrentLocation();
		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(currentLocation);
		if (!lootManWorkshop || lootManWorkshop == currentWorkshop)
		{
			return;
		}

		evaluation.currentWorkshop = currentWorkshop;
		evaluation.currentLocation = currentLocation;
		evaluation.lootManWorkshop = lootManWorkshop;

		auto* requiredItems = recipe->requiredItems;
		if (!requiredItems)
		{
			evaluation.evaluated = true;
			evaluation.allSatisfied = true;
			return;
		}

		evaluation.requiredItemCount = requiredItems->size();
		evaluation.evaluated = true;
		if (requiredItems->empty())
		{
			evaluation.allSatisfied = true;
			return;
		}

		for (const auto& requiredItem : *requiredItems)
		{
			auto* requiredForm = requiredItem.first;
			const auto requiredCount = requiredItem.second.i;
			if (requiredCount == 0)
			{
				++evaluation.satisfiedItemCount;
				continue;
			}

			FormProbeSnapshot requiredFormSnapshot;
			FillFormProbeSnapshot(requiredFormSnapshot, requiredForm);
			if (!requiredFormSnapshot.readable ||
				requiredFormSnapshot.formType != static_cast<std::uint32_t>(ENUM_FORM_ID::kCMPO))
			{
				evaluation.unsupportedForm = requiredFormSnapshot;
				return;
			}

			auto* component = requiredForm->As<BGSComponent>();
			if (!component)
			{
				evaluation.unsupportedForm = requiredFormSnapshot;
				return;
			}

			const auto baseCount = NonNegativeWorkshopComponentCount(
				currentWorkshop,
				component,
				true);
			const auto lootManCount = NonNegativeWorkshopComponentCount(
				lootManWorkshop,
				component,
				false);
			const auto totalCount = SaturatingAddNonNegative(baseCount, lootManCount);
			const auto totalAvailable = static_cast<std::uint32_t>(totalCount);
			if (totalAvailable < requiredCount)
			{
				evaluation.missingForm = requiredFormSnapshot;
				evaluation.missingRequiredCount = requiredCount;
				evaluation.missingBaseCount = baseCount;
				evaluation.missingLootManCount = lootManCount;
				evaluation.missingTotalCount = totalCount;
				return;
			}

			const auto availableBase = static_cast<std::uint32_t>(baseCount);
			const auto availableLootMan = static_cast<std::uint32_t>(lootManCount);
			const auto deficit = requiredCount > availableBase ?
				requiredCount - availableBase :
				0u;
			const auto consumeFromLootMan = std::min(deficit, availableLootMan);

			++evaluation.satisfiedItemCount;
			if (consumeFromLootMan > 0)
			{
				++evaluation.lootManBackedItemCount;
				evaluation.componentRemovals.push_back(WorkshopMaterialComponentRemoval{
					component,
					static_cast<std::uint32_t>(requiredCount),
					baseCount,
					lootManCount,
					consumeFromLootMan
				});
			}
		}

		evaluation.allSatisfied = true;
	}

	WorkshopResourceStatusEvaluation EvaluateWorkshopResourceStatus(
		const SelectedWorkshopRecipeProbeSnapshot& selectedRecipe,
		TESObjectREFR* ownerWorkshop = nullptr,
		bool requireOwnerWorkshop = false)
	{
		WorkshopResourceStatusEvaluationContext context;
		context.selectedRecipe = selectedRecipe;
		context.ownerWorkshop = ownerWorkshop;
		context.requireOwnerWorkshop = requireOwnerWorkshop;
		if (!ExecuteSehCallSafe(&EvaluateWorkshopResourceStatusCall, &context))
		{
			context.evaluation.sehFailed = true;
		}
		return context.evaluation;
	}

	bool ShouldTrackPendingWorkshopBuildConsumption(std::uint32_t sourceId)
	{
		return sourceId == kWorkshopBuildResourceCheckConfirmSourceId ||
		       sourceId == kWorkshopBuildResourceCheckConsumePrecheckSourceId;
	}

	bool ShouldConsumeWorkshopBuildDeficitsImmediately(std::uint32_t sourceId)
	{
		// The consume-precheck site is followed by consume-component source F4,
		// so it only seeds pending context for that downstream consume path.
		return sourceId == kWorkshopBuildResourceCheckConfirmSourceId;
	}

	void UpdatePendingWorkshopBuildConsumption(
		std::uint32_t sourceId,
		const SelectedWorkshopRecipeProbeSnapshot& recipeProbe,
		bool originalResult,
		bool adjustedResult,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		if (!ShouldTrackPendingWorkshopBuildConsumption(sourceId))
		{
			return;
		}

		pendingWorkshopBuildConsumption = {};
		if (originalResult ||
			!adjustedResult ||
			!evaluation.applied ||
			evaluation.lootManBackedItemCount == 0 ||
			!evaluation.currentWorkshop ||
			!evaluation.currentLocation ||
			!evaluation.lootManWorkshop)
		{
			return;
		}

		pendingWorkshopBuildConsumption.currentWorkshopId = evaluation.currentWorkshop->formID;
		pendingWorkshopBuildConsumption.currentLocationId = evaluation.currentLocation->formID;
		pendingWorkshopBuildConsumption.lootManWorkshopId = evaluation.lootManWorkshop->formID;
		pendingWorkshopBuildConsumption.recipeId = recipeProbe.recipeForm.formID;
		pendingWorkshopBuildConsumption.createdAt = Clock::now();
		pendingWorkshopBuildConsumption.remainingConsumeCalls =
			std::max<std::uint32_t>(evaluation.requiredItemCount + 4, 1);
		pendingWorkshopBuildConsumption.active = true;
	}

	bool HasPendingWorkshopBuildConsumption(
		TESObjectREFR* currentWorkshop,
		BGSLocation* currentLocation,
		TESObjectREFR* lootManWorkshop)
	{
		if (!pendingWorkshopBuildConsumption.active ||
			!currentWorkshop ||
			!currentLocation ||
			!lootManWorkshop)
		{
			return false;
		}
		if (pendingWorkshopBuildConsumption.createdAt.time_since_epoch().count() != 0 &&
			Clock::now() - pendingWorkshopBuildConsumption.createdAt > std::chrono::seconds(5))
		{
			pendingWorkshopBuildConsumption = {};
			return false;
		}

		return pendingWorkshopBuildConsumption.currentWorkshopId == currentWorkshop->formID &&
		       pendingWorkshopBuildConsumption.currentLocationId == currentLocation->formID &&
		       pendingWorkshopBuildConsumption.lootManWorkshopId == lootManWorkshop->formID;
	}

	void NotePendingWorkshopBuildConsumptionCall(TESObjectREFR* currentWorkshop)
	{
		if (!pendingWorkshopBuildConsumption.active ||
			!currentWorkshop ||
			pendingWorkshopBuildConsumption.currentWorkshopId != currentWorkshop->formID)
		{
			return;
		}

		if (pendingWorkshopBuildConsumption.remainingConsumeCalls > 0)
		{
			--pendingWorkshopBuildConsumption.remainingConsumeCalls;
		}
		if (pendingWorkshopBuildConsumption.remainingConsumeCalls == 0)
		{
			pendingWorkshopBuildConsumption = {};
		}
	}

	void ConsumeWorkshopBuildDeficits(
		const char* sourceName,
		const SelectedWorkshopRecipeProbeSnapshot& recipeProbe,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		if (!evaluation.applied ||
			evaluation.componentRemovals.empty() ||
			!evaluation.lootManWorkshop)
		{
			return;
		}

		const bool canRemove = originalRemoveComponents != nullptr;
		std::uint32_t totalConsumeFromLootMan = 0;
		for (const auto& removal : evaluation.componentRemovals)
		{
			if (!removal.component || removal.consumeFromLootMan == 0)
			{
				continue;
			}

			totalConsumeFromLootMan = static_cast<std::uint32_t>(std::min<std::uint64_t>(
				static_cast<std::uint64_t>(totalConsumeFromLootMan) + removal.consumeFromLootMan,
				static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
			if (canRemove)
			{
				originalRemoveComponents(
					evaluation.lootManWorkshop,
					removal.component,
					removal.consumeFromLootMan,
					false,
					nullptr,
					false,
					0,
					nullptr);
			}
		}

		const auto* firstRemoval = evaluation.componentRemovals.empty() ?
			nullptr :
			&evaluation.componentRemovals.front();
		REX::INFO(
			"Native workshop material probe: kind=build-deficit-consume, source={}, recipe={:08X}, removalCount={}, totalConsumeFromLootMan={}, firstRemovalComponent={:08X}, firstRemovalRequested={}, firstRemovalBaseCount={}, firstRemovalLootManCount={}, firstRemovalConsumeFromLootMan={}, applied={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}",
			sourceName,
			recipeProbe.recipeForm.formID,
			evaluation.componentRemovals.size(),
			totalConsumeFromLootMan,
			firstRemoval && firstRemoval->component ? firstRemoval->component->formID : 0,
			firstRemoval ? firstRemoval->requestedCount : 0,
			firstRemoval ? firstRemoval->baseCount : 0,
			firstRemoval ? firstRemoval->lootManCount : 0,
			firstRemoval ? firstRemoval->consumeFromLootMan : 0,
			canRemove && totalConsumeFromLootMan > 0,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0);
	}

	void LogWorkshopResourceStatus(
		std::uint32_t sourceId,
		const char* sourceName,
		std::uint32_t originalStatus,
		std::uint32_t adjustedStatus,
		const SelectedWorkshopRecipeProbeSnapshot& selectedRecipe,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(selectedRecipe.recipe),
			selectedRecipe.recipeForm.formID ^
				(evaluation.currentLocation ? evaluation.currentLocation->formID : 0) ^
				evaluation.missingForm.formID ^
				evaluation.unsupportedForm.formID ^
				(originalStatus << 24) ^
				(adjustedStatus << 16));
		if (!ShouldLogWorkshopResourceStatus(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=resource-status, source={}, originalStatus={}, adjustedStatus={}, applied={}, selectedRow={}, menuResult={}, menuNode={:016X}, menuReadable={}, recipe={:016X}, recipeReadable={}, recipeForm={:08X}, recipeType={}, requiredItems={}, satisfiedItems={}, lootManBackedItems={}, evaluated={}, sehFailed={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, missingForm={:08X}, missingRequired={}, missingBaseCount={}, missingLootManCount={}, missingTotalCount={}, unsupportedForm={:08X}, unsupportedType={}",
			sourceName,
			originalStatus,
			adjustedStatus,
			evaluation.applied,
			selectedRecipe.selectedRow,
			selectedRecipe.menuResult,
			selectedRecipe.menuNode,
			selectedRecipe.readable,
			reinterpret_cast<std::uintptr_t>(selectedRecipe.recipe),
			selectedRecipe.recipeForm.readable,
			selectedRecipe.recipeForm.formID,
			selectedRecipe.recipeForm.formType,
			evaluation.requiredItemCount,
			evaluation.satisfiedItemCount,
			evaluation.lootManBackedItemCount,
			evaluation.evaluated,
			selectedRecipe.sehFailed || evaluation.sehFailed,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			evaluation.missingForm.formID,
			evaluation.missingRequiredCount,
			evaluation.missingBaseCount,
			evaluation.missingLootManCount,
			evaluation.missingTotalCount,
			evaluation.unsupportedForm.formID,
			evaluation.unsupportedForm.formType);
	}

	std::uint32_t HookedWorkshopResourceStatus(
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto originalStatus = originalWorkshopResourceStatus ?
			originalWorkshopResourceStatus() :
			0u;

		const auto selectedRecipe = CaptureSelectedWorkshopRecipeProbe();
		auto evaluation = EvaluateWorkshopResourceStatus(selectedRecipe);
		auto adjustedStatus = originalStatus;
		if (originalStatus == kWorkshopResourceStatusMissingResources &&
			evaluation.evaluated &&
			evaluation.allSatisfied &&
			evaluation.lootManBackedItemCount > 0)
		{
			adjustedStatus = 0;
			evaluation.applied = true;
		}

		LogWorkshopResourceStatus(
			sourceId,
			sourceName,
			originalStatus,
			adjustedStatus,
			selectedRecipe,
			evaluation);
		return adjustedStatus;
	}

	std::uint32_t HookedWorkshopResourceStatusSourceF1()
	{
		const auto& site = kWorkshopResourceStatusCallSites[0];
		return HookedWorkshopResourceStatus(site.sourceId, site.label);
	}

	std::uint32_t HookedWorkshopResourceStatusSourceF2()
	{
		const auto& site = kWorkshopResourceStatusCallSites[1];
		return HookedWorkshopResourceStatus(site.sourceId, site.label);
	}

	void LogWorkshopMenuAvailability(
		std::uint32_t sourceId,
		const char* sourceName,
		std::uint32_t row,
		std::uint32_t inputMenuResult,
		std::uint32_t beforeOut,
		std::uint32_t originalOut,
		std::uint32_t adjustedOut,
		bool result,
		bool adjusted,
		const SelectedWorkshopRecipeProbeSnapshot& selectedRecipe,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(selectedRecipe.recipe),
			selectedRecipe.recipeForm.formID ^
				(evaluation.currentLocation ? evaluation.currentLocation->formID : 0) ^
				(beforeOut << 24) ^
				(originalOut << 16) ^
				(adjustedOut << 8) ^
				(result ? 0x80u : 0u) ^
				(adjusted ? 0x40u : 0u));
		if (!ShouldLogWorkshopMenuAvailability(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=menu-availability, source={}, row={}, inputMenuResult={}, beforeOut={}, originalOut={}, adjustedOut={}, result={}, adjusted={}, selectedRow={}, menuResult={}, menuNode={:016X}, menuReadable={}, recipe={:016X}, recipeReadable={}, recipeForm={:08X}, recipeType={}, requiredItems={}, satisfiedItems={}, lootManBackedItems={}, evaluated={}, allSatisfied={}, sehFailed={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, missingForm={:08X}, missingRequired={}, missingBaseCount={}, missingLootManCount={}, missingTotalCount={}, unsupportedForm={:08X}, unsupportedType={}",
			sourceName,
			row,
			inputMenuResult,
			beforeOut,
			originalOut,
			adjustedOut,
			result,
			adjusted,
			selectedRecipe.selectedRow,
			selectedRecipe.menuResult,
			selectedRecipe.menuNode,
			selectedRecipe.readable,
			reinterpret_cast<std::uintptr_t>(selectedRecipe.recipe),
			selectedRecipe.recipeForm.readable,
			selectedRecipe.recipeForm.formID,
			selectedRecipe.recipeForm.formType,
			evaluation.requiredItemCount,
			evaluation.satisfiedItemCount,
			evaluation.lootManBackedItemCount,
			evaluation.evaluated,
			evaluation.allSatisfied,
			selectedRecipe.sehFailed || evaluation.sehFailed,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			evaluation.missingForm.formID,
			evaluation.missingRequiredCount,
			evaluation.missingBaseCount,
			evaluation.missingLootManCount,
			evaluation.missingTotalCount,
			evaluation.unsupportedForm.formID,
			evaluation.unsupportedForm.formType);
	}

	bool HookedWorkshopMenuAvailability(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto beforeOut = outValue ? *outValue : 0;
		auto selectedRecipe = CaptureWorkshopMenuRecipeProbe(row, menuResult);
		const bool result = originalWorkshopMenuAvailability ?
			originalWorkshopMenuAvailability(outValue, row, menuResult) :
			false;
		const auto originalOut = outValue ? *outValue : 0;
		auto evaluation = EvaluateWorkshopResourceStatus(selectedRecipe);
		auto adjustedOut = originalOut;
		bool adjusted = false;
		if (result &&
			outValue &&
			originalOut == 0 &&
			evaluation.evaluated &&
			evaluation.allSatisfied &&
			evaluation.lootManBackedItemCount > 0)
		{
			*outValue = 1;
			adjustedOut = *outValue;
			adjusted = true;
			evaluation.applied = true;
		}

		LogWorkshopMenuAvailability(
			sourceId,
			sourceName,
			row,
			menuResult,
			beforeOut,
			originalOut,
			adjustedOut,
			result,
			adjusted,
			selectedRecipe,
			evaluation);
		return result;
	}

	bool HookedWorkshopMenuAvailabilitySource91(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		const auto& site = kWorkshopMenuAvailabilityCallSites[0];
		return HookedWorkshopMenuAvailability(
			outValue,
			row,
			menuResult,
			site.sourceId,
			site.label);
	}

	bool HookedWorkshopMenuAvailabilitySource92(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		const auto& site = kWorkshopMenuAvailabilityCallSites[1];
		return HookedWorkshopMenuAvailability(
			outValue,
			row,
			menuResult,
			site.sourceId,
			site.label);
	}

	bool HookedWorkshopMenuAvailabilitySource93(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		const auto& site = kWorkshopMenuAvailabilityCallSites[2];
		return HookedWorkshopMenuAvailability(
			outValue,
			row,
			menuResult,
			site.sourceId,
			site.label);
	}

	bool HookedWorkshopMenuAvailabilitySource94(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		const auto& site = kWorkshopMenuAvailabilityCallSites[3];
		return HookedWorkshopMenuAvailability(
			outValue,
			row,
			menuResult,
			site.sourceId,
			site.label);
	}

	bool HookedWorkshopMenuAvailabilitySource95(
		std::uint32_t* outValue,
		std::uint32_t row,
		std::uint32_t menuResult)
	{
		const auto& site = kWorkshopMenuAvailabilityCallSites[4];
		return HookedWorkshopMenuAvailability(
			outValue,
			row,
			menuResult,
			site.sourceId,
			site.label);
	}

	void LogWorkshopCheckAndSetPlacement(
		std::uint32_t sourceId,
		const char* sourceName,
		const char* stage,
		WorkshopMenu* menu,
		const SelectedWorkshopRecipeProbeSnapshot& selectedRecipe,
		const WorkshopResourceStatusEvaluation& evaluation,
		const PlacementItemProbeSnapshot& placement)
	{
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(menu),
			selectedRecipe.recipeForm.formID ^
				(evaluation.currentLocation ? evaluation.currentLocation->formID : 0) ^
				placement.placementHandle ^
				(placement.dataPlacementHandle << 1) ^
				(stage[0] == 'a' ? 0x80000000u : 0u));
		if (!ShouldLogWorkshopCheckAndSetPlacement(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=check-placement, source={}, stage={}, menu={:016X}, selectedRow={}, menuResult={}, recipe={:08X}, recipeReadable={}, evaluated={}, allSatisfied={}, lootManBackedItems={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, placementHandle={:08X}, placementResolved={}, placementRef={:08X}, placementRefReadable={}, placementBase={:08X}, placementBaseType={}, dataHandle={:08X}, dataResolved={}, dataRef={:08X}, dataRefReadable={}, dataBase={:08X}, dataBaseType={}, dataIsSet={}, dataDropProxyCount={}, dataBodyCount={}, sehFailed={}",
			sourceName,
			stage,
			reinterpret_cast<std::uintptr_t>(menu),
			selectedRecipe.selectedRow,
			selectedRecipe.menuResult,
			selectedRecipe.recipeForm.formID,
			selectedRecipe.recipeForm.readable,
			evaluation.evaluated,
			evaluation.allSatisfied,
			evaluation.lootManBackedItemCount,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			placement.placementHandle,
			placement.placementHandleResolved,
			placement.placementRef.formID,
			placement.placementRef.readable,
			placement.placementBase.formID,
			placement.placementBase.formType,
			placement.dataPlacementHandle,
			placement.dataPlacementHandleResolved,
			placement.dataPlacementRef.formID,
			placement.dataPlacementRef.readable,
			placement.dataPlacementBase.formID,
			placement.dataPlacementBase.formType,
			placement.dataIsSet,
			placement.dataDropProxyCount,
			placement.dataBodyCount,
			selectedRecipe.sehFailed || evaluation.sehFailed || placement.sehFailed);
	}

	void HookedWorkshopCheckAndSetPlacement(
		WorkshopMenu* menu,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto beforeRecipe = CaptureSelectedWorkshopRecipeProbe();
		const auto beforeEvaluation = EvaluateWorkshopResourceStatus(beforeRecipe);
		const auto beforePlacement = CapturePlacementItemProbe();
		LogWorkshopCheckAndSetPlacement(
			sourceId,
			sourceName,
			"before",
			menu,
			beforeRecipe,
			beforeEvaluation,
			beforePlacement);

		if (originalWorkshopCheckAndSetPlacement)
		{
			originalWorkshopCheckAndSetPlacement(menu);
		}

		const auto afterRecipe = CaptureSelectedWorkshopRecipeProbe();
		const auto afterEvaluation = EvaluateWorkshopResourceStatus(afterRecipe);
		const auto afterPlacement = CapturePlacementItemProbe();
		LogWorkshopCheckAndSetPlacement(
			sourceId,
			sourceName,
			"after",
			menu,
			afterRecipe,
			afterEvaluation,
			afterPlacement);
	}

	void HookedWorkshopCheckAndSetPlacementSourceA5(WorkshopMenu* menu)
	{
		const auto& site = kWorkshopCheckAndSetPlacementCallSites[0];
		HookedWorkshopCheckAndSetPlacement(
			menu,
			site.sourceId,
			site.label);
	}

	void HookedWorkshopCheckAndSetPlacementSourceA6(WorkshopMenu* menu)
	{
		const auto& site = kWorkshopCheckAndSetPlacementCallSites[1];
		HookedWorkshopCheckAndSetPlacement(
			menu,
			site.sourceId,
			site.label);
	}

	void HookedWorkshopCheckAndSetPlacementSourceA7(WorkshopMenu* menu)
	{
		const auto& site = kWorkshopCheckAndSetPlacementCallSites[2];
		HookedWorkshopCheckAndSetPlacement(
			menu,
			site.sourceId,
			site.label);
	}

	void HookedWorkshopCheckAndSetPlacementSourceA8(WorkshopMenu* menu)
	{
		const auto& site = kWorkshopCheckAndSetPlacementCallSites[3];
		HookedWorkshopCheckAndSetPlacement(
			menu,
			site.sourceId,
			site.label);
	}

	void LogWorkshopPlacementTransition(
		std::uint32_t sourceId,
		const char* sourceName,
		const char* kind,
		bool forward,
		bool resultKnown,
		bool result,
		void* context,
		const SelectedWorkshopRecipeProbeSnapshot& before,
		const SelectedWorkshopRecipeProbeSnapshot& after,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		const auto contextKey =
			before.recipeForm.formID ^
			(after.recipeForm.formID << 1) ^
			(evaluation.currentLocation ? evaluation.currentLocation->formID : 0) ^
			(result ? 0x80000000u : 0u) ^
			(resultKnown ? 0x40000000u : 0u);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(context),
			contextKey);
		if (!ShouldLogWorkshopPlacementTransition(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind={}, source={}, forward={}, resultKnown={}, result={}, context={:016X}, beforeRow={}, beforeMenuResult={}, beforeNode={:016X}, beforeRecipe={:08X}, beforeRecipeReadable={}, afterRow={}, afterMenuResult={}, afterNode={:016X}, afterRecipe={:08X}, afterRecipeReadable={}, evaluated={}, allSatisfied={}, lootManBackedItems={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, missingForm={:08X}, missingRequired={}, missingBaseCount={}, missingLootManCount={}, missingTotalCount={}, unsupportedForm={:08X}, unsupportedType={}",
			kind,
			sourceName,
			forward,
			resultKnown,
			result,
			reinterpret_cast<std::uintptr_t>(context),
			before.selectedRow,
			before.menuResult,
			before.menuNode,
			before.recipeForm.formID,
			before.recipeForm.readable,
			after.selectedRow,
			after.menuResult,
			after.menuNode,
			after.recipeForm.formID,
			after.recipeForm.readable,
			evaluation.evaluated,
			evaluation.allSatisfied,
			evaluation.lootManBackedItemCount,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			evaluation.missingForm.formID,
			evaluation.missingRequiredCount,
			evaluation.missingBaseCount,
			evaluation.missingLootManCount,
			evaluation.missingTotalCount,
			evaluation.unsupportedForm.formID,
			evaluation.unsupportedForm.formType);
	}

	void LogWorkshopPlacementState(
		std::uint32_t sourceId,
		const char* sourceName,
		std::uint32_t stageId,
		const char* stage,
		bool allowPlacement,
		bool createPreview,
		void* context,
		const SelectedWorkshopRecipeProbeSnapshot& selectedRecipe,
		const WorkshopResourceStatusEvaluation& evaluation,
		const PlacementItemProbeSnapshot& placement)
	{
		const auto contextKey =
			selectedRecipe.recipeForm.formID ^
			(stageId << 24) ^
			placement.placementHandle ^
			(placement.dataPlacementHandle << 1) ^
			(placement.dataIsSet ? 0x40000000u : 0u);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(context),
			contextKey);
		if (!ShouldLogWorkshopPlacementState(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=placement-state, source={}, stage={}, allowPlacement={}, createPreview={}, context={:016X}, selectedRow={}, menuResult={}, recipe={:08X}, recipeReadable={}, evaluated={}, allSatisfied={}, lootManBackedItems={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, placementHandlePtr={:016X}, placementHandle={:08X}, placementResolved={}, placementRef={:08X}, placementRefReadable={}, placementBase={:08X}, placementBaseType={}, placementDataPtr={:016X}, dataReadable={}, dataHandle={:08X}, dataResolved={}, dataRef={:08X}, dataRefReadable={}, dataBase={:08X}, dataBaseType={}, dataIsSet={}, dataMustSnap={}, dataAnythingIsGround={}, dataDropProxyCount={}, dataBodyCount={}, sehFailed={}",
			sourceName,
			stage,
			allowPlacement,
			createPreview,
			reinterpret_cast<std::uintptr_t>(context),
			selectedRecipe.selectedRow,
			selectedRecipe.menuResult,
			selectedRecipe.recipeForm.formID,
			selectedRecipe.recipeForm.readable,
			evaluation.evaluated,
			evaluation.allSatisfied,
			evaluation.lootManBackedItemCount,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			placement.placementHandlePtr,
			placement.placementHandle,
			placement.placementHandleResolved,
			placement.placementRef.formID,
			placement.placementRef.readable,
			placement.placementBase.formID,
			placement.placementBase.formType,
			placement.placementDataPtr,
			placement.dataReadable,
			placement.dataPlacementHandle,
			placement.dataPlacementHandleResolved,
			placement.dataPlacementRef.formID,
			placement.dataPlacementRef.readable,
			placement.dataPlacementBase.formID,
			placement.dataPlacementBase.formType,
			placement.dataIsSet,
			placement.dataMustSnap,
			placement.dataAnythingIsGround,
			placement.dataDropProxyCount,
			placement.dataBodyCount,
			placement.sehFailed);
	}

	bool HookedWorkshopMenuSelect(
		bool forward,
		void* context,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto before = CaptureSelectedWorkshopRecipeProbe();
		const bool result = originalWorkshopMenuSelect ?
			originalWorkshopMenuSelect(forward, context) :
			false;
		const auto after = CaptureSelectedWorkshopRecipeProbe();
		const auto evaluation = EvaluateWorkshopResourceStatus(after);
		LogWorkshopPlacementTransition(
			sourceId,
			sourceName,
			"menu-select",
			forward,
			true,
			result,
			context,
			before,
			after,
			evaluation);
		return result;
	}

	bool HookedWorkshopMenuSelectSourceA1(bool forward, void* context)
	{
		const auto& site = kWorkshopMenuSelectCallSites[0];
		return HookedWorkshopMenuSelect(
			forward,
			context,
			site.sourceId,
			site.label);
	}

	bool HookedWorkshopMenuSelectSourceA2(bool forward, void* context)
	{
		const auto& site = kWorkshopMenuSelectCallSites[1];
		return HookedWorkshopMenuSelect(
			forward,
			context,
			site.sourceId,
			site.label);
	}

	void HookedWorkshopStartPlacement(
		void* menuContext,
		bool allowPlacement,
		bool createPreview,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto selectedRecipe = CaptureSelectedWorkshopRecipeProbe();
		const auto evaluation = EvaluateWorkshopResourceStatus(selectedRecipe);
		const auto placementBefore = CapturePlacementItemProbe();
		LogWorkshopPlacementState(
			sourceId,
			sourceName,
			1,
			"before",
			allowPlacement,
			createPreview,
			menuContext,
			selectedRecipe,
			evaluation,
			placementBefore);
		LogWorkshopPlacementTransition(
			sourceId,
			sourceName,
			"placement-start",
			allowPlacement,
			false,
			createPreview,
			menuContext,
			selectedRecipe,
			selectedRecipe,
			evaluation);

		if (originalWorkshopStartPlacement)
		{
			originalWorkshopStartPlacement(menuContext, allowPlacement, createPreview);
		}

		const auto afterRecipe = CaptureSelectedWorkshopRecipeProbe();
		const auto afterEvaluation = EvaluateWorkshopResourceStatus(afterRecipe);
		const auto placementAfter = CapturePlacementItemProbe();
		LogWorkshopPlacementState(
			sourceId,
			sourceName,
			2,
			"after",
			allowPlacement,
			createPreview,
			menuContext,
			afterRecipe,
			afterEvaluation,
			placementAfter);
	}

	void HookedWorkshopStartPlacementSourceA3(
		void* menuContext,
		bool allowPlacement,
		bool createPreview)
	{
		const auto& site = kWorkshopStartPlacementCallSites[0];
		HookedWorkshopStartPlacement(
			menuContext,
			allowPlacement,
			createPreview,
			site.sourceId,
			site.label);
	}

	void HookedWorkshopStartPlacementSourceA4(
		void* menuContext,
		bool allowPlacement,
		bool createPreview)
	{
		const auto& site = kWorkshopStartPlacementCallSites[1];
		HookedWorkshopStartPlacement(
			menuContext,
			allowPlacement,
			createPreview,
			site.sourceId,
			site.label);
	}

	void LogWorkshopBuildResourceCheck(
		std::uint32_t sourceId,
		const char* sourceName,
		BGSConstructibleObject* recipe,
		TESObjectREFR* owner,
		void* scratchList,
		bool scaleRequiredCount,
		bool originalResult,
		bool adjustedResult,
		const SelectedWorkshopRecipeProbeSnapshot& recipeProbe,
		const WorkshopResourceStatusEvaluation& evaluation)
	{
		const auto ownerForm = CaptureFormProbeSnapshot(owner);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(recipe),
			recipeProbe.recipeForm.formID ^
				ownerForm.formID ^
				(evaluation.currentLocation ? evaluation.currentLocation->formID : 0) ^
				(originalResult ? 0x40000000u : 0u) ^
				(adjustedResult ? 0x80000000u : 0u));
		if (!ShouldLogWorkshopBuildResourceCheck(key))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=build-resource-check, source={}, recipe={:016X}, recipeReadable={}, recipeForm={:08X}, recipeType={}, owner={:016X}, ownerReadable={}, ownerForm={:08X}, ownerType={}, scratchList={:016X}, scaleRequiredCount={}, originalResult={}, adjustedResult={}, applied={}, requiredItems={}, satisfiedItems={}, lootManBackedItems={}, evaluated={}, allSatisfied={}, sehFailed={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}, missingForm={:08X}, missingRequired={}, missingBaseCount={}, missingLootManCount={}, missingTotalCount={}, unsupportedForm={:08X}, unsupportedType={}",
			sourceName,
			reinterpret_cast<std::uintptr_t>(recipe),
			recipeProbe.recipeForm.readable,
			recipeProbe.recipeForm.formID,
			recipeProbe.recipeForm.formType,
			reinterpret_cast<std::uintptr_t>(owner),
			ownerForm.readable,
			ownerForm.formID,
			ownerForm.formType,
			reinterpret_cast<std::uintptr_t>(scratchList),
			scaleRequiredCount,
			originalResult,
			adjustedResult,
			evaluation.applied,
			evaluation.requiredItemCount,
			evaluation.satisfiedItemCount,
			evaluation.lootManBackedItemCount,
			evaluation.evaluated,
			evaluation.allSatisfied,
			recipeProbe.sehFailed || evaluation.sehFailed,
			evaluation.currentWorkshop ? evaluation.currentWorkshop->formID : 0,
			evaluation.currentLocation ? evaluation.currentLocation->formID : 0,
			evaluation.lootManWorkshop ? evaluation.lootManWorkshop->formID : 0,
			evaluation.missingForm.formID,
			evaluation.missingRequiredCount,
			evaluation.missingBaseCount,
			evaluation.missingLootManCount,
			evaluation.missingTotalCount,
			evaluation.unsupportedForm.formID,
			evaluation.unsupportedForm.formType);
	}

	bool HookedWorkshopBuildResourceCheck(
		BGSConstructibleObject* recipe,
		TESObjectREFR* owner,
		void* scratchList,
		bool scaleRequiredCount,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const bool originalResult = originalWorkshopBuildResourceCheck ?
			originalWorkshopBuildResourceCheck(recipe, owner, scratchList, scaleRequiredCount) :
			false;

		const auto recipeProbe = CaptureWorkshopRecipePointerProbe(recipe);
		auto evaluation = EvaluateWorkshopResourceStatus(recipeProbe, owner);
		auto adjustedResult = originalResult;
		if (!originalResult &&
			evaluation.evaluated &&
			evaluation.allSatisfied &&
			evaluation.lootManBackedItemCount > 0)
		{
			adjustedResult = true;
			evaluation.applied = true;
		}
		UpdatePendingWorkshopBuildConsumption(
			sourceId,
			recipeProbe,
			originalResult,
			adjustedResult,
			evaluation);
		if (ShouldConsumeWorkshopBuildDeficitsImmediately(sourceId) && evaluation.applied)
		{
			ConsumeWorkshopBuildDeficits(sourceName, recipeProbe, evaluation);
			pendingWorkshopBuildConsumption = {};
		}

		LogWorkshopBuildResourceCheck(
			sourceId,
			sourceName,
			recipe,
			owner,
			scratchList,
			scaleRequiredCount,
			originalResult,
			adjustedResult,
			recipeProbe,
			evaluation);
		return adjustedResult;
	}

	bool HookedWorkshopBuildResourceCheckPlacement(
		BGSConstructibleObject* recipe,
		TESObjectREFR* owner,
		void* scratchList,
		bool scaleRequiredCount)
	{
		const auto& site = kWorkshopBuildResourceCheckCallSites[0];
		return HookedWorkshopBuildResourceCheck(
			recipe,
			owner,
			scratchList,
			scaleRequiredCount,
			site.sourceId,
			site.label);
	}

	bool HookedWorkshopBuildResourceCheckConfirm(
		BGSConstructibleObject* recipe,
		TESObjectREFR* owner,
		void* scratchList,
		bool scaleRequiredCount)
	{
		const auto& site = kWorkshopBuildResourceCheckCallSites[1];
		return HookedWorkshopBuildResourceCheck(
			recipe,
			owner,
			scratchList,
			scaleRequiredCount,
			site.sourceId,
			site.label);
	}

	bool HookedWorkshopBuildResourceCheckConsumePrecheck(
		BGSConstructibleObject* recipe,
		TESObjectREFR* owner,
		void* scratchList,
		bool scaleRequiredCount)
	{
		const auto& site = kWorkshopBuildResourceCheckCallSites[2];
		return HookedWorkshopBuildResourceCheck(
			recipe,
			owner,
			scratchList,
			scaleRequiredCount,
			site.sourceId,
			site.label);
	}

	std::uint32_t SaturatingRequiredComponentCount(
		std::uint32_t itemCount,
		std::int32_t componentCount)
	{
		if (componentCount <= 0)
		{
			return 0;
		}

		const auto product =
			static_cast<std::uint64_t>(itemCount) *
			static_cast<std::uint64_t>(componentCount);
		return static_cast<std::uint32_t>(std::min<std::uint64_t>(
			product,
			static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
	}

	struct WorkshopMaterialConsumptionPlan
	{
		TESObjectREFR* currentWorkshop = nullptr;
		BGSLocation* currentLocation = nullptr;
		TESObjectREFR* lootManWorkshop = nullptr;
		std::vector<WorkshopMaterialComponentRemoval> componentRemovals;
		std::int32_t baseCount = 0;
		std::int32_t lootManCount = 0;
		std::uint32_t requestedCount = 0;
		std::uint32_t consumeFromLootMan = 0;
		bool targetIsDirectItem = false;
		bool applied = false;
	};

	WorkshopMaterialConsumptionPlan BuildRememberedWorkshopMaterialConsumptionPlan(
		TESObjectREFR* owner,
		TESForm* form,
		std::uint32_t requestedCount,
		bool includeLinked,
		bool allowDirectItem)
	{
		WorkshopMaterialConsumptionPlan plan;
		plan.requestedCount = requestedCount;

		if (!originalDirectComponentCount ||
			!owner ||
			!form ||
			requestedCount == 0 ||
			!IsReadableFormType(owner, ENUM_FORM_ID::kREFR))
		{
			return plan;
		}

		const auto targetForm = CaptureFormProbeSnapshot(form);
		if (!targetForm.readable)
		{
			return plan;
		}

		const bool targetIsComponent =
			targetForm.formType == static_cast<std::uint32_t>(ENUM_FORM_ID::kCMPO);
		const bool targetIsDirectItem =
			allowDirectItem &&
			targetForm.formType == static_cast<std::uint32_t>(ENUM_FORM_ID::kMISC);
		if (!targetIsComponent && !targetIsDirectItem)
		{
			return plan;
		}

		auto* currentWorkshop = ResolveActiveRememberedWorkshopForOwner(owner);
		if (!currentWorkshop)
		{
			return plan;
		}

		auto* currentLocation = currentWorkshop->GetCurrentLocation();
		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(currentLocation);
		if (!lootManWorkshop || lootManWorkshop == currentWorkshop)
		{
			return plan;
		}

		plan.currentWorkshop = currentWorkshop;
		plan.currentLocation = currentLocation;
		plan.lootManWorkshop = lootManWorkshop;
		plan.targetIsDirectItem = targetIsDirectItem;

		auto addComponentRemoval = [&](BGSComponent* component, std::uint32_t componentRequestedCount)
		{
			if (!component || componentRequestedCount == 0)
			{
				return;
			}

			const auto baseCount = NonNegativeWorkshopComponentCount(
				currentWorkshop,
				component,
				includeLinked);
			const auto lootManCount = NonNegativeWorkshopComponentCount(
				lootManWorkshop,
				component,
				false);
			const auto availableBase = static_cast<std::uint32_t>(baseCount);
			const auto availableLootMan = static_cast<std::uint32_t>(lootManCount);
			const auto deficit = componentRequestedCount > availableBase ?
				componentRequestedCount - availableBase :
				0u;
			const auto consumeFromLootMan = std::min(deficit, availableLootMan);

			plan.baseCount = SaturatingAddNonNegative(plan.baseCount, baseCount);
			plan.lootManCount = SaturatingAddNonNegative(plan.lootManCount, lootManCount);
			plan.consumeFromLootMan = static_cast<std::uint32_t>(std::min<std::uint64_t>(
				static_cast<std::uint64_t>(plan.consumeFromLootMan) + consumeFromLootMan,
				static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));

			if (consumeFromLootMan == 0)
			{
				return;
			}

			plan.componentRemovals.push_back(WorkshopMaterialComponentRemoval{
				component,
				componentRequestedCount,
				baseCount,
				lootManCount,
				consumeFromLootMan
			});
		};

		if (targetIsComponent)
		{
			addComponentRemoval(static_cast<BGSComponent*>(form), requestedCount);
		}
		else
		{
			auto* misc = form->As<TESObjectMISC>();
			if (misc && misc->componentData)
			{
				for (auto& [componentForm, componentValue] : *misc->componentData)
				{
					auto* component = componentForm ? componentForm->As<BGSComponent>() : nullptr;
					addComponentRemoval(
						component,
						SaturatingRequiredComponentCount(requestedCount, componentValue.i));
				}
			}
		}

		plan.applied = plan.consumeFromLootMan > 0 && !plan.componentRemovals.empty();
		return plan;
	}

	void LogWorkshopMaterialConsumption(
		std::uint32_t sourceId,
		const char* sourceName,
		TESObjectREFR* owner,
		TESForm* form,
		bool includeLinked,
		const WorkshopMaterialConsumptionPlan& plan)
	{
		const auto ownerForm = CaptureFormProbeSnapshot(owner);
		const auto targetForm = CaptureFormProbeSnapshot(form);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(owner),
			targetForm.formID ^ plan.requestedCount);
		if (!ShouldLogWorkshopMaterialConsumption(key))
		{
			return;
		}

		const auto* firstRemoval = plan.componentRemovals.empty() ?
			nullptr :
			&plan.componentRemovals.front();
		REX::INFO(
			"Native workshop material probe: kind=component-consume, source={}, owner={:016X}, ownerReadable={}, ownerForm={:08X}, ownerType={}, target={:016X}, targetReadable={}, targetForm={:08X}, targetType={}, targetIsDirectItem={}, includeLinked={}, requestedCount={}, baseCount={}, lootManCount={}, consumeFromLootMan={}, componentRemovalCount={}, firstRemovalComponent={:08X}, firstRemovalRequested={}, firstRemovalBaseCount={}, firstRemovalLootManCount={}, firstRemovalConsumeFromLootMan={}, applied={}, currentWorkshop={:08X}, currentLocation={:08X}, lootManWorkshop={:08X}",
			sourceName,
			reinterpret_cast<std::uintptr_t>(owner),
			ownerForm.readable,
			ownerForm.formID,
			ownerForm.formType,
			reinterpret_cast<std::uintptr_t>(form),
			targetForm.readable,
			targetForm.formID,
			targetForm.formType,
			plan.targetIsDirectItem,
			includeLinked,
			plan.requestedCount,
			plan.baseCount,
			plan.lootManCount,
			plan.consumeFromLootMan,
			plan.componentRemovals.size(),
			firstRemoval && firstRemoval->component ? firstRemoval->component->formID : 0,
			firstRemoval ? firstRemoval->requestedCount : 0,
			firstRemoval ? firstRemoval->baseCount : 0,
			firstRemoval ? firstRemoval->lootManCount : 0,
			firstRemoval ? firstRemoval->consumeFromLootMan : 0,
			plan.applied,
			plan.currentWorkshop ? plan.currentWorkshop->formID : 0,
			plan.currentLocation ? plan.currentLocation->formID : 0,
			plan.lootManWorkshop ? plan.lootManWorkshop->formID : 0);
	}

	void HookedWorkshopConsumeComponent(
		TESForm* form,
		std::uint32_t count,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		auto* currentWorkshop = ResolveActiveRememberedWorkshop();
		auto* currentLocation = currentWorkshop ? currentWorkshop->GetCurrentLocation() : nullptr;
		auto* lootManWorkshop = GetRememberedLootManWorkshopForLocation(currentLocation);
		const auto allowDirectItem = HasPendingWorkshopBuildConsumption(
			currentWorkshop,
			currentLocation,
			lootManWorkshop);
		const auto plan = BuildRememberedWorkshopMaterialConsumptionPlan(
			currentWorkshop,
			form,
			count,
			true,
			allowDirectItem);

		if (originalWorkshopConsumeComponent)
		{
			originalWorkshopConsumeComponent(form, count);
		}

		if (plan.consumeFromLootMan > 0 &&
			plan.lootManWorkshop &&
			originalRemoveComponents)
		{
			if (!plan.componentRemovals.empty())
			{
				for (const auto& removal : plan.componentRemovals)
				{
					if (!removal.component || removal.consumeFromLootMan == 0)
					{
						continue;
					}

					originalRemoveComponents(
						plan.lootManWorkshop,
						removal.component,
						removal.consumeFromLootMan,
						false,
						nullptr,
						false,
						0,
						nullptr);
				}
			}
			else
			{
				originalRemoveComponents(
					plan.lootManWorkshop,
					form,
					plan.consumeFromLootMan,
					false,
					nullptr,
					false,
					0,
					nullptr);
			}
		}

		LogWorkshopMaterialConsumption(
			sourceId,
			sourceName,
			currentWorkshop,
			form,
			true,
			plan);
		NotePendingWorkshopBuildConsumptionCall(currentWorkshop);
	}

	void HookedWorkshopConsumeComponentSourceF3(TESForm* form, std::uint32_t count)
	{
		const auto& site = kWorkshopConsumeComponentCallSites[0];
		HookedWorkshopConsumeComponent(
			form,
			count,
			site.sourceId,
			site.label);
	}

	void HookedWorkshopConsumeComponentSourceF4(TESForm* form, std::uint32_t count)
	{
		const auto& site = kWorkshopConsumeComponentCallSites[1];
		HookedWorkshopConsumeComponent(
			form,
			count,
			site.sourceId,
			site.label);
	}

	void HookedRemoveComponents(
		TESObjectREFR* owner,
		TESForm* form,
		std::uint32_t count,
		bool includeLinked,
		void* extraData,
		bool allowFallback,
		std::uint32_t uiMessageId,
		void* uiMessageContext,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto plan = BuildRememberedWorkshopMaterialConsumptionPlan(
			owner,
			form,
			count,
			includeLinked,
			false);

		if (originalRemoveComponents)
		{
			originalRemoveComponents(
				owner,
				form,
				count,
				includeLinked,
				extraData,
				allowFallback,
				uiMessageId,
				uiMessageContext);

			if (plan.consumeFromLootMan > 0 && plan.lootManWorkshop)
			{
				if (!plan.componentRemovals.empty())
				{
					for (const auto& removal : plan.componentRemovals)
					{
						if (!removal.component || removal.consumeFromLootMan == 0)
						{
							continue;
						}

						originalRemoveComponents(
							plan.lootManWorkshop,
							removal.component,
							removal.consumeFromLootMan,
							includeLinked,
							extraData,
							allowFallback,
							uiMessageId,
							uiMessageContext);
					}
				}
				else
				{
					originalRemoveComponents(
						plan.lootManWorkshop,
						form,
						plan.consumeFromLootMan,
						includeLinked,
						extraData,
						allowFallback,
						uiMessageId,
						uiMessageContext);
				}
			}
		}

		LogWorkshopMaterialConsumption(
			sourceId,
			sourceName,
			owner,
			form,
			includeLinked,
			plan);
	}

	void HookedRemoveComponentsSourceF1(
		TESObjectREFR* owner,
		TESForm* form,
		std::uint32_t count,
		bool includeLinked,
		void* extraData,
		bool allowFallback,
		std::uint32_t uiMessageId,
		void* uiMessageContext)
	{
		const auto& site = kRemoveComponentsCallSites[0];
		HookedRemoveComponents(
			owner,
			form,
			count,
			includeLinked,
			extraData,
			allowFallback,
			uiMessageId,
			uiMessageContext,
			site.sourceId,
			site.label);
	}

	void HookedRemoveComponentsSourceF2(
		TESObjectREFR* owner,
		TESForm* form,
		std::uint32_t count,
		bool includeLinked,
		void* extraData,
		bool allowFallback,
		std::uint32_t uiMessageId,
		void* uiMessageContext)
	{
		const auto& site = kRemoveComponentsCallSites[1];
		HookedRemoveComponents(
			owner,
			form,
			count,
			includeLinked,
			extraData,
			allowFallback,
			uiMessageId,
			uiMessageContext,
			site.sourceId,
			site.label);
	}

	void LogWorkshopObjectCountProbe(
		void* scriptContext,
		TESForm* form,
		bool includeLinked,
		float* outValue,
		bool result,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto targetForm = CaptureFormProbeSnapshot(form);
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(scriptContext),
			targetForm.formID);
		if (!ShouldLogWorkshopMaterialProbe(key, 224))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=workshop-object-count, source={}, scriptContext={:016X}, target={:016X}, targetReadable={}, targetForm={:08X}, targetType={}, includeLinked={}, result={}, outValue={}",
			sourceName,
			reinterpret_cast<std::uintptr_t>(scriptContext),
			reinterpret_cast<std::uintptr_t>(form),
			targetForm.readable,
			targetForm.formID,
			targetForm.formType,
			includeLinked,
			result,
			outValue ? *outValue : 0.0f);
	}

	bool HookedWorkshopObjectCount(void* scriptContext, TESForm* form, bool includeLinked, float* outValue)
	{
		const auto& site = kWorkshopObjectCountPapyrusCallSite;
		const bool result = originalWorkshopObjectCount ?
			originalWorkshopObjectCount(scriptContext, form, includeLinked, outValue) :
			false;
		LogWorkshopObjectCountProbe(
			scriptContext,
			form,
			includeLinked,
			outValue,
			result,
			site.sourceId,
			site.label);
		return result;
	}

	void LogCurrentWorkshopObjectCountProbe(
		TESForm* form,
		std::uint32_t count,
		std::uint32_t sourceId,
		const char* sourceName)
	{
		const auto targetForm = CaptureFormProbeSnapshot(form);
		const auto context = CaptureWorkshopMaterialContextProbe();
		const auto key = MakePointerProbeKey(
			sourceId,
			reinterpret_cast<std::uintptr_t>(form),
			targetForm.formID ^
				context.currentWorkshop.workshop.formID ^
				(context.nearestWorkshop.formID << 1));
		if (!ShouldLogWorkshopMaterialProbe(key, 256))
		{
			return;
		}

		REX::INFO(
			"Native workshop material probe: kind=current-workshop-object-count, source={}, target={:016X}, targetReadable={}, targetForm={:08X}, targetType={}, count={}, currentWorkshopHandle={:08X}, currentWorkshopReadable={}, currentWorkshop={:08X}, currentWorkshopType={}, currentLocationReadable={}, currentLocation={:08X}, currentLocationRemembered={}, nearestWorkshopReadable={}, nearestWorkshop={:08X}, nearestWorkshopType={}, nearestLocationReadable={}, nearestLocation={:08X}, nearestLocationRemembered={}",
			sourceName,
			reinterpret_cast<std::uintptr_t>(form),
			targetForm.readable,
			targetForm.formID,
			targetForm.formType,
			count,
			context.currentWorkshop.handle,
			context.currentWorkshop.workshop.readable,
			context.currentWorkshop.workshop.formID,
			context.currentWorkshop.workshop.formType,
			context.currentWorkshop.location.readable,
			context.currentWorkshop.location.formID,
			context.currentWorkshop.locationRemembered,
			context.nearestWorkshop.readable,
			context.nearestWorkshop.formID,
			context.nearestWorkshop.formType,
			context.nearestLocation.readable,
			context.nearestLocation.formID,
			context.nearestLocationRemembered);
	}

	std::uint32_t HookedCurrentWorkshopObjectCount(TESForm* form)
	{
		const auto& site = kCurrentWorkshopObjectCountCallSite;
		const auto count = originalCurrentWorkshopObjectCount ?
			originalCurrentWorkshopObjectCount(form) :
			0;
		LogCurrentWorkshopObjectCountProbe(form, count, site.sourceId, site.label);
		return count;
	}

	void RememberWorkshopSupplyLink(
		std::monostate,
		TESForm* targetLocationForm,
		TESObjectREFR* lootManWorkshop,
		BSFixedString prefix)
	{
		const auto prefixText = prefix.c_str();
		auto* targetLocation = targetLocationForm ? targetLocationForm->As<BGSLocation>() : nullptr;
		if (!targetLocation || !lootManWorkshop)
		{
			REX::WARN(
				"[Papyrus] {}      [ Failed to remember native workshop supply link ] targetLocation={:08X}, lootManWorkshop={:08X}",
				prefixText,
				targetLocationForm ? targetLocationForm->formID : 0,
				lootManWorkshop ? lootManWorkshop->formID : 0);
			return;
		}

		{
			std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock);
			rememberedWorkshopSupplyLinks[targetLocation->formID] = lootManWorkshop->formID;
		}

		auto* lootManLocation = lootManWorkshop->GetCurrentLocation();
		REX::INFO(
			"[Papyrus] {}      [ Remembered native workshop supply link ] targetLocation={:08X}, lootManLocation={:08X}, lootManWorkshop={:08X}",
			prefixText,
			targetLocation->formID,
			lootManLocation ? lootManLocation->formID : 0,
			lootManWorkshop->formID);
	}

	void ForgetWorkshopSupplyLink(std::monostate, TESForm* targetLocationForm, BSFixedString prefix)
	{
		const auto prefixText = prefix.c_str();
		auto* targetLocation = targetLocationForm ? targetLocationForm->As<BGSLocation>() : nullptr;
		if (!targetLocation)
		{
			REX::WARN(
				"[Papyrus] {}      [ Failed to forget native workshop supply link ] targetLocation={:08X}",
				prefixText,
				targetLocationForm ? targetLocationForm->formID : 0);
			return;
		}

		bool removed = false;
		{
			std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock);
			removed = rememberedWorkshopSupplyLinks.erase(targetLocation->formID) > 0;
		}

		REX::INFO(
			"[Papyrus] {}      [ Forgot native workshop supply link ] targetLocation={:08X}, removed={}",
			prefixText,
			targetLocation->formID,
			removed);
	}

	void LogWorkshopSupplyDiagnostics(
		std::monostate,
		TESObjectREFR* targetWorkshop,
		TESObjectREFR* lootManWorkshop,
		BSFixedString prefix)
	{
		const auto prefixText = prefix.c_str();
		auto* targetLocation = targetWorkshop ? targetWorkshop->GetCurrentLocation() : nullptr;
		auto* lootManLocation = lootManWorkshop ? lootManWorkshop->GetCurrentLocation() : nullptr;
		auto* workshopCaravanKeyword = GetNativeWorkshopCaravanKeyword();

		REX::INFO(
			"[Papyrus] {}      [ Native workshop supply diagnostics ] targetWorkshop={:08X}, targetLocation={:08X}, lootManWorkshop={:08X}, lootManLocation={:08X}, workshopCaravanKeyword={:08X}, nativeLinkedLocationScan=disabled-after-ctd",
			prefixText,
			targetWorkshop ? targetWorkshop->formID : 0,
			targetLocation ? targetLocation->formID : 0,
			lootManWorkshop ? lootManWorkshop->formID : 0,
			lootManLocation ? lootManLocation->formID : 0,
			workshopCaravanKeyword ? workshopCaravanKeyword->formID : 0);
	}

	void InstallWorkbenchSharedContainerHooks()
	{
		static std::once_flag installOnce;
		std::call_once(installOnce, []()
		{
			const std::array<PopulateLinkedWorkshopContainersFn, kPopulateLinkedWorkshopContainerCallSites.size()> hooks{
				&HookedPopulateLinkedWorkshopContainers,
				&HookedPopulateLinkedWorkshopContainers,
				&HookedPopulateLinkedWorkshopContainers,
			};

			if (InstallDirectCallHookFamily(
					kPopulateLinkedWorkshopContainerCallSites,
					hooks,
					originalPopulateLinkedWorkshopContainers,
					"workshop-shared-container.populate-linked"))
			{
				REX::INFO("Installed native shared workshop container hooks");
			}
		});
	}

	void InstallWorkshopMaterialProbeHooks()
	{
		static std::once_flag installOnce;
		std::call_once(installOnce, []()
		{
			bool allInstalled = true;

			const std::array<RebuildWorkshopSupplyFn, kRebuildWorkshopSupplyCallSites.size()> rebuildHooks{
				&HookedRebuildWorkshopSupplySourceA1,
				&HookedRebuildWorkshopSupplySourceA2,
				&HookedRebuildWorkshopSupplySourceA3,
				&HookedRebuildWorkshopSupplySourceA4,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kRebuildWorkshopSupplyCallSites,
				rebuildHooks,
				originalRebuildWorkshopSupply,
				"workshop-material.rebuild-supply");

			const std::array<ComponentCountHelperFn, kComponentCountHelperCallSites.size()> componentCountHooks{
				&HookedComponentCountPapyrus,
				&HookedComponentCountWorkbenchUi,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kComponentCountHelperCallSites,
				componentCountHooks,
				originalComponentCountHelper,
				"workshop-material.component-count");

			const std::array<DirectComponentCountFn, kDirectComponentCountCallSites.size()> directComponentHooks{
				&HookedDirectComponentCountSourceE1,
				&HookedDirectComponentCountSourceE2,
				&HookedDirectComponentCountSourceE3,
				&HookedDirectComponentCountSourceE4,
				&HookedDirectComponentCountSourceE5,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kDirectComponentCountCallSites,
				directComponentHooks,
				originalDirectComponentCount,
				"workshop-material.direct-component-count");

			const std::array<WorkshopResourceStatusFn, kWorkshopResourceStatusCallSites.size()> resourceStatusHooks{
				&HookedWorkshopResourceStatusSourceF1,
				&HookedWorkshopResourceStatusSourceF2,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kWorkshopResourceStatusCallSites,
				resourceStatusHooks,
				originalWorkshopResourceStatus,
				"workshop-material.resource-status");

			const std::array<WorkshopMenuAvailabilityFn, kWorkshopMenuAvailabilityCallSites.size()> menuAvailabilityHooks{
				&HookedWorkshopMenuAvailabilitySource91,
				&HookedWorkshopMenuAvailabilitySource92,
				&HookedWorkshopMenuAvailabilitySource93,
				&HookedWorkshopMenuAvailabilitySource94,
				&HookedWorkshopMenuAvailabilitySource95,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kWorkshopMenuAvailabilityCallSites,
				menuAvailabilityHooks,
				originalWorkshopMenuAvailability,
				"workshop-menu.availability");

			const std::array<WorkshopCheckAndSetPlacementFn, kWorkshopCheckAndSetPlacementCallSites.size()> checkAndSetPlacementHooks{
				&HookedWorkshopCheckAndSetPlacementSourceA5,
				&HookedWorkshopCheckAndSetPlacementSourceA6,
				&HookedWorkshopCheckAndSetPlacementSourceA7,
				&HookedWorkshopCheckAndSetPlacementSourceA8,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kWorkshopCheckAndSetPlacementCallSites,
				checkAndSetPlacementHooks,
				originalWorkshopCheckAndSetPlacement,
				"workshop-menu.check-placement");

			const std::array<WorkshopMenuSelectFn, kWorkshopMenuSelectCallSites.size()> menuSelectHooks{
				&HookedWorkshopMenuSelectSourceA1,
				&HookedWorkshopMenuSelectSourceA2,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kWorkshopMenuSelectCallSites,
				menuSelectHooks,
				originalWorkshopMenuSelect,
				"workshop-menu.select");

			const std::array<WorkshopStartPlacementFn, kWorkshopStartPlacementCallSites.size()> startPlacementHooks{
				&HookedWorkshopStartPlacementSourceA3,
				&HookedWorkshopStartPlacementSourceA4,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kWorkshopStartPlacementCallSites,
				startPlacementHooks,
				originalWorkshopStartPlacement,
				"workshop-menu.start-placement");

			const std::array<WorkshopBuildResourceCheckFn, kWorkshopBuildResourceCheckCallSites.size()> buildResourceCheckHooks{
				&HookedWorkshopBuildResourceCheckPlacement,
				&HookedWorkshopBuildResourceCheckConfirm,
				&HookedWorkshopBuildResourceCheckConsumePrecheck,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kWorkshopBuildResourceCheckCallSites,
				buildResourceCheckHooks,
				originalWorkshopBuildResourceCheck,
				"workshop-material.build-resource-check");

			const std::array<RemoveComponentsFn, kRemoveComponentsCallSites.size()> removeComponentHooks{
				&HookedRemoveComponentsSourceF1,
				&HookedRemoveComponentsSourceF2,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kRemoveComponentsCallSites,
				removeComponentHooks,
				originalRemoveComponents,
				"workshop-material.remove-components");

			const std::array<WorkshopConsumeComponentFn, kWorkshopConsumeComponentCallSites.size()> consumeComponentHooks{
				&HookedWorkshopConsumeComponentSourceF3,
				&HookedWorkshopConsumeComponentSourceF4,
			};
			allInstalled &= InstallDirectCallHookFamily(
				kWorkshopConsumeComponentCallSites,
				consumeComponentHooks,
				originalWorkshopConsumeComponent,
				"workshop-material.consume-component");

			allInstalled &= InstallDirectCallHookSite(
				kWorkshopObjectCountPapyrusCallSite,
				&HookedWorkshopObjectCount,
				originalWorkshopObjectCount,
				"workshop-material.object-count.papyrus");

			allInstalled &= InstallDirectCallHookSite(
				kCurrentWorkshopObjectCountCallSite,
				&HookedCurrentWorkshopObjectCount,
				originalCurrentWorkshopObjectCount,
				"workshop-material.object-count.current-workshop");

			if (allInstalled)
			{
				REX::INFO("Installed native workshop material probe hooks");
			}
			else
			{
				REX::WARN("Installed native workshop material probe hooks with one or more skipped families");
			}
		});
	}

}
