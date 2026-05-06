// Source slice generated for native hook address review.
// It keeps the address catalog include and direct-call install helpers near their call sites.

//    9 | #include <mutex>
//   10 | #include <optional>
//   11 | #include <span>
//   12 | #include <unordered_map>
//   13 | #include <unordered_set>
//   14 |
//   15 | #include <REL/Relocation.h>
//   16 |
//   17 | #include "papyrus_lootman_hook_addresses.generated.h"
//   18 | #include "properties.h"
//   19 |
//   20 | namespace papyrus_lootman
//   21 | {
//   22 | 	using namespace RE;
//   23 |
//   24 | 	template <class OriginalFn, class HookFn>
//   25 | 	bool InstallDirectCallHookSite(
//   26 | 		const NativeHookCallSite& site,
//   27 | 		HookFn hook,
//   28 | 		OriginalFn& original,
//   29 | 		const char* family);
//   30 |
//   31 | 	struct ExtraCellDetachTimeCompat :
//   32 | 		public BSExtraData
//   33 | 	{
//   34 | 		static constexpr auto TYPE = EXTRA_DATA_TYPE::kCellDetachTime;
//   35 | 		std::uint32_t detachTime;
//   36 | 	};
//   37 |
//   38 | 	std::optional<std::uint32_t> GetCellDetachTime(const TESObjectCELL* cell)
//   39 | 	{
//   40 | 		if (!cell)
//   41 | 		{
//   42 | 			return std::nullopt;
//   43 | 		}
//   44 |
//   45 | 		if (auto* extraList = cell->extraList.get())
//   46 | 		{
//   47 | 			if (auto* detachTime = extraList->GetByType<ExtraCellDetachTimeCompat>())
//   48 | 			{
//   49 | 				return detachTime->detachTime;
//   50 | 			}
//   51 | 		}
//   52 |
//   53 | 		return std::nullopt;
//   54 | 	}
//   55 |
//   56 | 	using CheckCellBeforeEncounterZoneResetFn = bool (*)(BGSEncounterZone*, TESObjectCELL*);
//   57 | 	using CheckResetElapsedFromDetachTimeFn =
//   58 | 		bool (*)(std::uint32_t, std::uint32_t, bool);
//   59 |
//   60 | 	CheckCellBeforeEncounterZoneResetFn originalCheckCellBeforeEncounterZoneReset = nullptr;
//   61 | 	CheckResetElapsedFromDetachTimeFn checkResetElapsedFromDetachTime = nullptr;

// ...

//  124 | 					zone ? zone->gameData.resetTime : 0,
//  125 | 					cellDetachTime);
//  126 | 			}
//  127 | 		}
//  128 |
//  129 | 		return false;
//  130 | 	}
//  131 |
//  132 | 	void InstallEncounterZoneResetSuppressionHooks()
//  133 | 	{
//  134 | 		static std::once_flag installOnce;
//  135 | 		std::call_once(installOnce, []()
//  136 | 		{
//  137 | 			REL::Relocation<CheckResetElapsedFromDetachTimeFn> resetElapsedFromDetach{
//  138 | 				kEncounterZoneResetElapsedFromDetachId
//  139 | 			};
//  140 |
//  141 | 			if (!InstallDirectCallHookSite(
//  142 | 					kLoadChangeCellBeforeZoneResetCallSite,
//  143 | 					HookedCheckCellBeforeEncounterZoneReset,
//  144 | 					originalCheckCellBeforeEncounterZoneReset,
//  145 | 					"encounter-zone.reset-suppression"))
//  146 | 			{
//  147 | 				return;
//  148 | 			}
//  149 |
//  150 | 			checkResetElapsedFromDetachTime = resetElapsedFromDetach.get();
//  151 |
//  152 | 			REX::INFO("Installed encounter-zone reset suppression hook");
//  153 | 		});
//  154 | 	}
//  155 |
//  156 |
//  157 | 	using SharedWorkshopContainers = BSScrapArray<NiPointer<TESObjectREFR>>;
//  158 | 	using PopulateLinkedWorkshopContainersFn =
//  159 | 		void (*)(SharedWorkshopContainers*, BGSLocation*, bool);
//  160 | 	using RebuildWorkshopSupplyFn = void (*)(void*);
//  161 | 	using ComponentCountHelperFn = bool (*)(void*, std::int32_t*, TESForm*, bool);
//  162 | 	using DirectComponentCountFn = std::int32_t (*)(void*, BGSComponent*, bool);
//  163 | 	using WorkshopResourceStatusFn = std::uint32_t (*)();
//  164 | 	using GetWorkshopMenuNodeFn = Workshop::WorkshopMenuNode* (*)(std::uint16_t, std::uint32_t*);
//  165 | 	using WorkshopMenuAvailabilityFn = bool (*)(std::uint32_t*, std::uint32_t, std::uint32_t);
//  166 | 	using WorkshopMenuSelectFn = bool (*)(bool, void*);
//  167 | 	using WorkshopCheckAndSetPlacementFn = void (*)(WorkshopMenu*);
//  168 | 	using WorkshopStartPlacementFn = void (*)(void*, bool, bool);
//  169 | 	using WorkshopBuildResourceCheckFn = bool (*)(BGSConstructibleObject*, TESObjectREFR*, void*, bool);
//  170 | 	using WorkshopConsumeComponentFn = void (*)(TESForm*, std::uint32_t);
//  171 | 	using RemoveComponentsFn = void (*)(
//  172 | 		TESObjectREFR*,
//  173 | 		TESForm*,
//  174 | 		std::uint32_t,
//  175 | 		bool,
//  176 | 		void*,
//  177 | 		bool,

// ...

//  235 | 	{
//  236 | 		auto* context = static_cast<DirectCallInstructionReadContext*>(opaque);
//  237 | 		std::memcpy(
//  238 | 			context->bytes.data(),
//  239 | 			reinterpret_cast<const void*>(context->address),
//  240 | 			context->bytes.size());
//  241 | 	}
//  242 |
//  243 | 	std::optional<DirectCallSiteDecode> DecodeDirectCallSite(
//  244 | 		const NativeHookCallSite& site,
//  245 | 		const char* family)
//  246 | 	{
//  247 | 		REL::Relocation<std::uintptr_t> callSite{ REL::Offset(site.rva) };
//  248 | 		const auto address = callSite.address();
//  249 | 		DirectCallInstructionReadContext context{ address };
//  250 | 		if (!ExecuteSehCallSafe(&ReadDirectCallInstructionBytes, &context))
//  251 | 		{
//  252 | 			REX::ERROR(
//  253 | 				"Skipping native direct-call hook: family={}, site={}, rva={:X}, reason=instruction-read-failed",
//  254 | 				family,
//  255 | 				site.id,
//  256 | 				site.rva);
//  257 | 			return std::nullopt;
//  258 | 		}
//  259 |
//  260 | 		if (context.bytes[0] != 0xE8)
//  261 | 		{
//  262 | 			REX::ERROR(
//  263 | 				"Skipping native direct-call hook: family={}, site={}, rva={:X}, expectedOpcode=E8, actualOpcode={:02X}",
//  264 | 				family,
//  265 | 				site.id,
//  266 | 				site.rva,
//  267 | 				context.bytes[0]);
//  268 | 			return std::nullopt;
//  269 | 		}
//  270 |
//  271 | 		std::int32_t displacement = 0;
//  272 | 		std::memcpy(&displacement, context.bytes.data() + 1, sizeof(displacement));
//  273 | 		const auto targetAddress = static_cast<std::uintptr_t>(
//  274 | 			static_cast<std::intptr_t>(address + 5) +
//  275 | 			static_cast<std::intptr_t>(displacement));
//  276 | 		const auto moduleBase = address - site.rva;
//  277 | 		return DirectCallSiteDecode{
//  278 | 			&site,
//  279 | 			address,
//  280 | 			targetAddress,
//  281 | 			targetAddress - moduleBase,
//  282 | 		};
//  283 | 	}
//  284 |
//  285 | 	bool ValidateDirectCallSiteFamily(
//  286 | 		std::span<const NativeHookCallSite> sites,
//  287 | 		const char* family,
//  288 | 		bool requireSharedOriginalTarget)
//  289 | 	{
//  290 | 		if (sites.empty())
//  291 | 		{
//  292 | 			REX::ERROR("Skipping native direct-call hook family: family={}, reason=no-sites", family);
//  293 | 			return false;
//  294 | 		}
//  295 |
//  296 | 		std::optional<std::uintptr_t> expectedTargetAddress;
//  297 | 		std::optional<std::uintptr_t> expectedTargetRva;
//  298 | 		for (const auto& site : sites)
//  299 | 		{
//  300 | 			const auto decoded = DecodeDirectCallSite(site, family);
//  301 | 			if (!decoded)
//  302 | 			{
//  303 | 				return false;
//  304 | 			}
//  305 |
//  306 | 			if (!requireSharedOriginalTarget)
//  307 | 			{
//  308 | 				continue;
//  309 | 			}
//  310 |
//  311 | 			if (!expectedTargetAddress)
//  312 | 			{
//  313 | 				expectedTargetAddress = decoded->targetAddress;
//  314 | 				expectedTargetRva = decoded->targetRva;
//  315 | 				continue;
//  316 | 			}
//  317 |
//  318 | 			if (*expectedTargetAddress != decoded->targetAddress)
//  319 | 			{
//  320 | 				REX::ERROR(
//  321 | 					"Skipping native direct-call hook family: family={}, site={}, rva={:X}, originalTargetRva={:X}, expectedOriginalTargetRva={:X}",
//  322 | 					family,
//  323 | 					site.id,
//  324 | 					site.rva,
//  325 | 					decoded->targetRva,
//  326 | 					expectedTargetRva.value_or(0));
//  327 | 				return false;
//  328 | 			}
//  329 | 		}
//  330 |
//  331 | 		return true;
//  332 | 	}
//  333 |
//  334 | 	template <class OriginalFn, class HookFn>
//  335 | 	OriginalFn WriteValidatedDirectCallHook(
//  336 | 		const NativeHookCallSite& site,
//  337 | 		HookFn hook,
//  338 | 		const char* family)
//  339 | 	{
//  340 | 		const auto decoded = DecodeDirectCallSite(site, family);
//  341 | 		if (!decoded)
//  342 | 		{
//  343 | 			return OriginalFn{};
//  344 | 		}
//  345 |
//  346 | 		REL::Relocation<std::uintptr_t> callSite{ REL::Offset(site.rva) };
//  347 | 		const auto original = reinterpret_cast<OriginalFn>(callSite.write_call<5>(hook));
//  348 | 		REX::INFO(
//  349 | 			"Installed native direct-call hook: family={}, site={}, rva={:X}, originalTargetRva={:X}",
//  350 | 			family,
//  351 | 			site.id,
//  352 | 			site.rva,
//  353 | 			decoded->targetRva);
//  354 | 		return original;
//  355 | 	}
//  356 |
//  357 | 	template <class OriginalFn, class HookFn, std::size_t N>
//  358 | 	bool InstallDirectCallHookFamily(
//  359 | 		const std::array<NativeHookCallSite, N>& sites,
//  360 | 		const std::array<HookFn, N>& hooks,
//  361 | 		OriginalFn& original,
//  362 | 		const char* family)
//  363 | 	{
//  364 | 		if (!ValidateDirectCallSiteFamily(
//  365 | 				std::span<const NativeHookCallSite>(sites.data(), sites.size()),
//  366 | 				family,
//  367 | 				true))
//  368 | 		{
//  369 | 			REX::ERROR("Skipped native direct-call hook family: family={}", family);
//  370 | 			return false;
//  371 | 		}
//  372 |
//  373 | 		for (std::size_t index = 0; index < sites.size(); ++index)
//  374 | 		{
//  375 | 			const auto patchedOriginal = WriteValidatedDirectCallHook<OriginalFn>(
//  376 | 				sites[index],
//  377 | 				hooks[index],
//  378 | 				family);
//  379 | 			if (!patchedOriginal)
//  380 | 			{
//  381 | 				REX::ERROR(
//  382 | 					"Skipped native direct-call hook family after validation changed: family={}, site={}, rva={:X}",
//  383 | 					family,
//  384 | 					sites[index].id,
//  385 | 					sites[index].rva);
//  386 | 				return false;
//  387 | 			}
//  388 |
//  389 | 			if (!original)
//  390 | 			{
//  391 | 				original = patchedOriginal;
//  392 | 			}
//  393 | 			else if (original != patchedOriginal)
//  394 | 			{
//  395 | 				REX::WARN(
//  396 | 					"Unexpected native direct-call original target after patch: family={}, site={}, rva={:X}, original={:X}, expected={:X}",
//  397 | 					family,
//  398 | 					sites[index].id,
//  399 | 					sites[index].rva,
//  400 | 					reinterpret_cast<std::uintptr_t>(patchedOriginal),
//  401 | 					reinterpret_cast<std::uintptr_t>(original));
//  402 | 			}
//  403 | 		}
//  404 |
//  405 | 		return true;
//  406 | 	}
//  407 |
//  408 | 	template <class OriginalFn, class HookFn>
//  409 | 	bool InstallDirectCallHookSite(
//  410 | 		const NativeHookCallSite& site,
//  411 | 		HookFn hook,
//  412 | 		OriginalFn& original,
//  413 | 		const char* family)
//  414 | 	{
//  415 | 		if (!ValidateDirectCallSiteFamily(
//  416 | 				std::span<const NativeHookCallSite>(&site, 1),
//  417 | 				family,
//  418 | 				false))
//  419 | 		{
//  420 | 			REX::ERROR("Skipped native direct-call hook: family={}, site={}", family, site.id);
//  421 | 			return false;
//  422 | 		}
//  423 |
//  424 | 		const auto patchedOriginal = WriteValidatedDirectCallHook<OriginalFn>(
//  425 | 			site,
//  426 | 			hook,
//  427 | 			family);
//  428 | 		if (!patchedOriginal)
//  429 | 		{
//  430 | 			REX::ERROR("Skipped native direct-call hook after validation changed: family={}, site={}", family, site.id);
//  431 | 			return false;
//  432 | 		}
//  433 |
//  434 | 		original = patchedOriginal;
//  435 | 		return true;
//  436 | 	}
//  437 |
//  438 | 	struct FormProbeSnapshot
//  439 | 	{
//  440 | 		std::uintptr_t pointer = 0;
//  441 | 		TESFormID formID = 0;
//  442 | 		std::uint32_t formType = 0;
//  443 | 		bool readable = false;
//  444 | 	};
//  445 |
//  446 | 	struct FormProbeSnapshotContext
//  447 | 	{
//  448 | 		TESForm* form = nullptr;
//  449 | 		FormProbeSnapshot snapshot;
//  450 | 	};
//  451 |
//  452 | 	void CaptureFormProbeSnapshotCall(void* opaque)
//  453 | 	{
//  454 | 		auto* context = static_cast<FormProbeSnapshotContext*>(opaque);
//  455 | 		auto* form = context->form;
//  456 | 		if (!form)
//  457 | 		{
//  458 | 			return;
//  459 | 		}
//  460 |

// ...

// 3556 | 			prefixText,
// 3557 | 			targetWorkshop ? targetWorkshop->formID : 0,
// 3558 | 			targetLocation ? targetLocation->formID : 0,
// 3559 | 			lootManWorkshop ? lootManWorkshop->formID : 0,
// 3560 | 			lootManLocation ? lootManLocation->formID : 0,
// 3561 | 			workshopCaravanKeyword ? workshopCaravanKeyword->formID : 0);
// 3562 | 	}
// 3563 |
// 3564 | 	void InstallWorkbenchSharedContainerHooks()
// 3565 | 	{
// 3566 | 		static std::once_flag installOnce;
// 3567 | 		std::call_once(installOnce, []()
// 3568 | 		{
// 3569 | 			const std::array<PopulateLinkedWorkshopContainersFn, kPopulateLinkedWorkshopContainerCallSites.size()> hooks{
// 3570 | 				&HookedPopulateLinkedWorkshopContainers,
// 3571 | 				&HookedPopulateLinkedWorkshopContainers,
// 3572 | 				&HookedPopulateLinkedWorkshopContainers,
// 3573 | 			};
// 3574 |
// 3575 | 			if (InstallDirectCallHookFamily(
// 3576 | 					kPopulateLinkedWorkshopContainerCallSites,
// 3577 | 					hooks,
// 3578 | 					originalPopulateLinkedWorkshopContainers,
// 3579 | 					"workshop-shared-container.populate-linked"))
// 3580 | 			{
// 3581 | 				REX::INFO("Installed native shared workshop container hooks");
// 3582 | 			}
// 3583 | 		});
// 3584 | 	}
// 3585 |
// 3586 | 	void InstallWorkshopMaterialProbeHooks()
// 3587 | 	{
// 3588 | 		static std::once_flag installOnce;
// 3589 | 		std::call_once(installOnce, []()
// 3590 | 		{
// 3591 | 			bool allInstalled = true;
// 3592 |
// 3593 | 			const std::array<RebuildWorkshopSupplyFn, kRebuildWorkshopSupplyCallSites.size()> rebuildHooks{
// 3594 | 				&HookedRebuildWorkshopSupplySourceA1,
// 3595 | 				&HookedRebuildWorkshopSupplySourceA2,
// 3596 | 				&HookedRebuildWorkshopSupplySourceA3,
// 3597 | 				&HookedRebuildWorkshopSupplySourceA4,
// 3598 | 			};
// 3599 | 			allInstalled &= InstallDirectCallHookFamily(
// 3600 | 				kRebuildWorkshopSupplyCallSites,
// 3601 | 				rebuildHooks,
// 3602 | 				originalRebuildWorkshopSupply,
// 3603 | 				"workshop-material.rebuild-supply");
// 3604 |
// 3605 | 			const std::array<ComponentCountHelperFn, kComponentCountHelperCallSites.size()> componentCountHooks{
// 3606 | 				&HookedComponentCountPapyrus,
// 3607 | 				&HookedComponentCountWorkbenchUi,
// 3608 | 			};
// 3609 | 			allInstalled &= InstallDirectCallHookFamily(
// 3610 | 				kComponentCountHelperCallSites,
// 3611 | 				componentCountHooks,
// 3612 | 				originalComponentCountHelper,
// 3613 | 				"workshop-material.component-count");
// 3614 |
// 3615 | 			const std::array<DirectComponentCountFn, kDirectComponentCountCallSites.size()> directComponentHooks{
// 3616 | 				&HookedDirectComponentCountSourceE1,
// 3617 | 				&HookedDirectComponentCountSourceE2,
// 3618 | 				&HookedDirectComponentCountSourceE3,
// 3619 | 				&HookedDirectComponentCountSourceE4,
// 3620 | 				&HookedDirectComponentCountSourceE5,
// 3621 | 			};
// 3622 | 			allInstalled &= InstallDirectCallHookFamily(
// 3623 | 				kDirectComponentCountCallSites,
// 3624 | 				directComponentHooks,
// 3625 | 				originalDirectComponentCount,
// 3626 | 				"workshop-material.direct-component-count");
// 3627 |
// 3628 | 			const std::array<WorkshopResourceStatusFn, kWorkshopResourceStatusCallSites.size()> resourceStatusHooks{
// 3629 | 				&HookedWorkshopResourceStatusSourceF1,
// 3630 | 				&HookedWorkshopResourceStatusSourceF2,
// 3631 | 			};
// 3632 | 			allInstalled &= InstallDirectCallHookFamily(
// 3633 | 				kWorkshopResourceStatusCallSites,
// 3634 | 				resourceStatusHooks,
// 3635 | 				originalWorkshopResourceStatus,
// 3636 | 				"workshop-material.resource-status");
// 3637 |
// 3638 | 			const std::array<WorkshopMenuAvailabilityFn, kWorkshopMenuAvailabilityCallSites.size()> menuAvailabilityHooks{
// 3639 | 				&HookedWorkshopMenuAvailabilitySource91,
// 3640 | 				&HookedWorkshopMenuAvailabilitySource92,
// 3641 | 				&HookedWorkshopMenuAvailabilitySource93,
// 3642 | 				&HookedWorkshopMenuAvailabilitySource94,
// 3643 | 				&HookedWorkshopMenuAvailabilitySource95,
// 3644 | 			};
// 3645 | 			allInstalled &= InstallDirectCallHookFamily(
// 3646 | 				kWorkshopMenuAvailabilityCallSites,
// 3647 | 				menuAvailabilityHooks,
// 3648 | 				originalWorkshopMenuAvailability,
// 3649 | 				"workshop-menu.availability");
// 3650 |
// 3651 | 			const std::array<WorkshopCheckAndSetPlacementFn, kWorkshopCheckAndSetPlacementCallSites.size()> checkAndSetPlacementHooks{
// 3652 | 				&HookedWorkshopCheckAndSetPlacementSourceA5,
// 3653 | 				&HookedWorkshopCheckAndSetPlacementSourceA6,
// 3654 | 				&HookedWorkshopCheckAndSetPlacementSourceA7,
// 3655 | 				&HookedWorkshopCheckAndSetPlacementSourceA8,
// 3656 | 			};
// 3657 | 			allInstalled &= InstallDirectCallHookFamily(
// 3658 | 				kWorkshopCheckAndSetPlacementCallSites,
// 3659 | 				checkAndSetPlacementHooks,
// 3660 | 				originalWorkshopCheckAndSetPlacement,
// 3661 | 				"workshop-menu.check-placement");
// 3662 |
// 3663 | 			const std::array<WorkshopMenuSelectFn, kWorkshopMenuSelectCallSites.size()> menuSelectHooks{
// 3664 | 				&HookedWorkshopMenuSelectSourceA1,
// 3665 | 				&HookedWorkshopMenuSelectSourceA2,
// 3666 | 			};
// 3667 | 			allInstalled &= InstallDirectCallHookFamily(
// 3668 | 				kWorkshopMenuSelectCallSites,
// 3669 | 				menuSelectHooks,
// 3670 | 				originalWorkshopMenuSelect,
// 3671 | 				"workshop-menu.select");
// 3672 |
// 3673 | 			const std::array<WorkshopStartPlacementFn, kWorkshopStartPlacementCallSites.size()> startPlacementHooks{
// 3674 | 				&HookedWorkshopStartPlacementSourceA3,
// 3675 | 				&HookedWorkshopStartPlacementSourceA4,
// 3676 | 				&HookedWorkshopStartPlacementSourceA9,
// 3677 | 				&HookedWorkshopStartPlacementSourceAA,
// 3678 | 				&HookedWorkshopStartPlacementSourceAB,
// 3679 | 				&HookedWorkshopStartPlacementSourceAC,
// 3680 | 				&HookedWorkshopStartPlacementSourceAD,
// 3681 | 			};
// 3682 | 			allInstalled &= InstallDirectCallHookFamily(
// 3683 | 				kWorkshopStartPlacementCallSites,
// 3684 | 				startPlacementHooks,
// 3685 | 				originalWorkshopStartPlacement,
// 3686 | 				"workshop-menu.start-placement");
// 3687 |
// 3688 | 			const std::array<WorkshopBuildResourceCheckFn, kWorkshopBuildResourceCheckCallSites.size()> buildResourceCheckHooks{
// 3689 | 				&HookedWorkshopBuildResourceCheckPlacement,
// 3690 | 				&HookedWorkshopBuildResourceCheckConfirm,
// 3691 | 				&HookedWorkshopBuildResourceCheckConsumePrecheck,
// 3692 | 			};
// 3693 | 			allInstalled &= InstallDirectCallHookFamily(
// 3694 | 				kWorkshopBuildResourceCheckCallSites,
// 3695 | 				buildResourceCheckHooks,
// 3696 | 				originalWorkshopBuildResourceCheck,
// 3697 | 				"workshop-material.build-resource-check");
// 3698 |
// 3699 | 			const std::array<RemoveComponentsFn, kRemoveComponentsCallSites.size()> removeComponentHooks{
// 3700 | 				&HookedRemoveComponentsSourceF1,
// 3701 | 				&HookedRemoveComponentsSourceF2,
// 3702 | 			};
// 3703 | 			allInstalled &= InstallDirectCallHookFamily(
// 3704 | 				kRemoveComponentsCallSites,
// 3705 | 				removeComponentHooks,
// 3706 | 				originalRemoveComponents,
// 3707 | 				"workshop-material.remove-components");
// 3708 |
// 3709 | 			const std::array<WorkshopConsumeComponentFn, kWorkshopConsumeComponentCallSites.size()> consumeComponentHooks{
// 3710 | 				&HookedWorkshopConsumeComponentSourceF3,
// 3711 | 				&HookedWorkshopConsumeComponentSourceF4,
// 3712 | 			};
// 3713 | 			allInstalled &= InstallDirectCallHookFamily(
// 3714 | 				kWorkshopConsumeComponentCallSites,
// 3715 | 				consumeComponentHooks,
// 3716 | 				originalWorkshopConsumeComponent,
// 3717 | 				"workshop-material.consume-component");
// 3718 |
// 3719 | 			allInstalled &= InstallDirectCallHookSite(
// 3720 | 				kWorkshopObjectCountPapyrusCallSite,
// 3721 | 				&HookedWorkshopObjectCount,
// 3722 | 				originalWorkshopObjectCount,
// 3723 | 				"workshop-material.object-count.papyrus");
// 3724 |
// 3725 | 			allInstalled &= InstallDirectCallHookSite(
// 3726 | 				kCurrentWorkshopObjectCountCallSite,
// 3727 | 				&HookedCurrentWorkshopObjectCount,
// 3728 | 				originalCurrentWorkshopObjectCount,
// 3729 | 				"workshop-material.object-count.current-workshop");
// 3730 |
// 3731 | 			if (allInstalled)
// 3732 | 			{
// 3733 | 				REX::INFO("Installed native workshop material probe hooks");
// 3734 | 			}
// 3735 | 			else
// 3736 | 			{
// 3737 | 				REX::WARN("Installed native workshop material probe hooks with one or more skipped families");
// 3738 | 			}
// 3739 | 		});
// 3740 | 	}
// 3741 |
// 3742 | }
// 3743 |
