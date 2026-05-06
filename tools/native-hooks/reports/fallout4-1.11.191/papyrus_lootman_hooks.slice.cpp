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

// 3502 | 			prefixText,
// 3503 | 			targetWorkshop ? targetWorkshop->formID : 0,
// 3504 | 			targetLocation ? targetLocation->formID : 0,
// 3505 | 			lootManWorkshop ? lootManWorkshop->formID : 0,
// 3506 | 			lootManLocation ? lootManLocation->formID : 0,
// 3507 | 			workshopCaravanKeyword ? workshopCaravanKeyword->formID : 0);
// 3508 | 	}
// 3509 |
// 3510 | 	void InstallWorkbenchSharedContainerHooks()
// 3511 | 	{
// 3512 | 		static std::once_flag installOnce;
// 3513 | 		std::call_once(installOnce, []()
// 3514 | 		{
// 3515 | 			const std::array<PopulateLinkedWorkshopContainersFn, kPopulateLinkedWorkshopContainerCallSites.size()> hooks{
// 3516 | 				&HookedPopulateLinkedWorkshopContainers,
// 3517 | 				&HookedPopulateLinkedWorkshopContainers,
// 3518 | 				&HookedPopulateLinkedWorkshopContainers,
// 3519 | 			};
// 3520 |
// 3521 | 			if (InstallDirectCallHookFamily(
// 3522 | 					kPopulateLinkedWorkshopContainerCallSites,
// 3523 | 					hooks,
// 3524 | 					originalPopulateLinkedWorkshopContainers,
// 3525 | 					"workshop-shared-container.populate-linked"))
// 3526 | 			{
// 3527 | 				REX::INFO("Installed native shared workshop container hooks");
// 3528 | 			}
// 3529 | 		});
// 3530 | 	}
// 3531 |
// 3532 | 	void InstallWorkshopMaterialProbeHooks()
// 3533 | 	{
// 3534 | 		static std::once_flag installOnce;
// 3535 | 		std::call_once(installOnce, []()
// 3536 | 		{
// 3537 | 			bool allInstalled = true;
// 3538 |
// 3539 | 			const std::array<RebuildWorkshopSupplyFn, kRebuildWorkshopSupplyCallSites.size()> rebuildHooks{
// 3540 | 				&HookedRebuildWorkshopSupplySourceA1,
// 3541 | 				&HookedRebuildWorkshopSupplySourceA2,
// 3542 | 				&HookedRebuildWorkshopSupplySourceA3,
// 3543 | 				&HookedRebuildWorkshopSupplySourceA4,
// 3544 | 			};
// 3545 | 			allInstalled &= InstallDirectCallHookFamily(
// 3546 | 				kRebuildWorkshopSupplyCallSites,
// 3547 | 				rebuildHooks,
// 3548 | 				originalRebuildWorkshopSupply,
// 3549 | 				"workshop-material.rebuild-supply");
// 3550 |
// 3551 | 			const std::array<ComponentCountHelperFn, kComponentCountHelperCallSites.size()> componentCountHooks{
// 3552 | 				&HookedComponentCountPapyrus,
// 3553 | 				&HookedComponentCountWorkbenchUi,
// 3554 | 			};
// 3555 | 			allInstalled &= InstallDirectCallHookFamily(
// 3556 | 				kComponentCountHelperCallSites,
// 3557 | 				componentCountHooks,
// 3558 | 				originalComponentCountHelper,
// 3559 | 				"workshop-material.component-count");
// 3560 |
// 3561 | 			const std::array<DirectComponentCountFn, kDirectComponentCountCallSites.size()> directComponentHooks{
// 3562 | 				&HookedDirectComponentCountSourceE1,
// 3563 | 				&HookedDirectComponentCountSourceE2,
// 3564 | 				&HookedDirectComponentCountSourceE3,
// 3565 | 				&HookedDirectComponentCountSourceE4,
// 3566 | 				&HookedDirectComponentCountSourceE5,
// 3567 | 			};
// 3568 | 			allInstalled &= InstallDirectCallHookFamily(
// 3569 | 				kDirectComponentCountCallSites,
// 3570 | 				directComponentHooks,
// 3571 | 				originalDirectComponentCount,
// 3572 | 				"workshop-material.direct-component-count");
// 3573 |
// 3574 | 			const std::array<WorkshopResourceStatusFn, kWorkshopResourceStatusCallSites.size()> resourceStatusHooks{
// 3575 | 				&HookedWorkshopResourceStatusSourceF1,
// 3576 | 				&HookedWorkshopResourceStatusSourceF2,
// 3577 | 			};
// 3578 | 			allInstalled &= InstallDirectCallHookFamily(
// 3579 | 				kWorkshopResourceStatusCallSites,
// 3580 | 				resourceStatusHooks,
// 3581 | 				originalWorkshopResourceStatus,
// 3582 | 				"workshop-material.resource-status");
// 3583 |
// 3584 | 			const std::array<WorkshopMenuAvailabilityFn, kWorkshopMenuAvailabilityCallSites.size()> menuAvailabilityHooks{
// 3585 | 				&HookedWorkshopMenuAvailabilitySource<0>,
// 3586 | 				&HookedWorkshopMenuAvailabilitySource<1>,
// 3587 | 				&HookedWorkshopMenuAvailabilitySource<2>,
// 3588 | 				&HookedWorkshopMenuAvailabilitySource<3>,
// 3589 | 				&HookedWorkshopMenuAvailabilitySource<4>,
// 3590 | 				&HookedWorkshopMenuAvailabilitySource<5>,
// 3591 | 				&HookedWorkshopMenuAvailabilitySource<6>,
// 3592 | 				&HookedWorkshopMenuAvailabilitySource<7>,
// 3593 | 				&HookedWorkshopMenuAvailabilitySource<8>,
// 3594 | 				&HookedWorkshopMenuAvailabilitySource<9>,
// 3595 | 				&HookedWorkshopMenuAvailabilitySource<10>,
// 3596 | 				&HookedWorkshopMenuAvailabilitySource<11>,
// 3597 | 				&HookedWorkshopMenuAvailabilitySource<12>,
// 3598 | 				&HookedWorkshopMenuAvailabilitySource<13>,
// 3599 | 				&HookedWorkshopMenuAvailabilitySource<14>,
// 3600 | 				&HookedWorkshopMenuAvailabilitySource<15>,
// 3601 | 				&HookedWorkshopMenuAvailabilitySource<16>,
// 3602 | 				&HookedWorkshopMenuAvailabilitySource<17>,
// 3603 | 				&HookedWorkshopMenuAvailabilitySource<18>,
// 3604 | 			};
// 3605 | 			allInstalled &= InstallDirectCallHookFamily(
// 3606 | 				kWorkshopMenuAvailabilityCallSites,
// 3607 | 				menuAvailabilityHooks,
// 3608 | 				originalWorkshopMenuAvailability,
// 3609 | 				"workshop-menu.availability");
// 3610 |
// 3611 | 			const std::array<WorkshopCheckAndSetPlacementFn, kWorkshopCheckAndSetPlacementCallSites.size()> checkAndSetPlacementHooks{
// 3612 | 				&HookedWorkshopCheckAndSetPlacementSourceA5,
// 3613 | 				&HookedWorkshopCheckAndSetPlacementSourceA6,
// 3614 | 				&HookedWorkshopCheckAndSetPlacementSourceA7,
// 3615 | 				&HookedWorkshopCheckAndSetPlacementSourceA8,
// 3616 | 			};
// 3617 | 			allInstalled &= InstallDirectCallHookFamily(
// 3618 | 				kWorkshopCheckAndSetPlacementCallSites,
// 3619 | 				checkAndSetPlacementHooks,
// 3620 | 				originalWorkshopCheckAndSetPlacement,
// 3621 | 				"workshop-menu.check-placement");
// 3622 |
// 3623 | 			const std::array<WorkshopMenuSelectFn, kWorkshopMenuSelectCallSites.size()> menuSelectHooks{
// 3624 | 				&HookedWorkshopMenuSelectSourceA1,
// 3625 | 				&HookedWorkshopMenuSelectSourceA2,
// 3626 | 			};
// 3627 | 			allInstalled &= InstallDirectCallHookFamily(
// 3628 | 				kWorkshopMenuSelectCallSites,
// 3629 | 				menuSelectHooks,
// 3630 | 				originalWorkshopMenuSelect,
// 3631 | 				"workshop-menu.select");
// 3632 |
// 3633 | 			const std::array<WorkshopStartPlacementFn, kWorkshopStartPlacementCallSites.size()> startPlacementHooks{
// 3634 | 				&HookedWorkshopStartPlacementSourceA3,
// 3635 | 				&HookedWorkshopStartPlacementSourceA4,
// 3636 | 				&HookedWorkshopStartPlacementSourceA9,
// 3637 | 				&HookedWorkshopStartPlacementSourceAA,
// 3638 | 				&HookedWorkshopStartPlacementSourceAB,
// 3639 | 				&HookedWorkshopStartPlacementSourceAC,
// 3640 | 				&HookedWorkshopStartPlacementSourceAD,
// 3641 | 			};
// 3642 | 			allInstalled &= InstallDirectCallHookFamily(
// 3643 | 				kWorkshopStartPlacementCallSites,
// 3644 | 				startPlacementHooks,
// 3645 | 				originalWorkshopStartPlacement,
// 3646 | 				"workshop-menu.start-placement");
// 3647 |
// 3648 | 			const std::array<WorkshopBuildResourceCheckFn, kWorkshopBuildResourceCheckCallSites.size()> buildResourceCheckHooks{
// 3649 | 				&HookedWorkshopBuildResourceCheckPlacement,
// 3650 | 				&HookedWorkshopBuildResourceCheckConfirm,
// 3651 | 				&HookedWorkshopBuildResourceCheckConsumePrecheck,
// 3652 | 			};
// 3653 | 			allInstalled &= InstallDirectCallHookFamily(
// 3654 | 				kWorkshopBuildResourceCheckCallSites,
// 3655 | 				buildResourceCheckHooks,
// 3656 | 				originalWorkshopBuildResourceCheck,
// 3657 | 				"workshop-material.build-resource-check");
// 3658 |
// 3659 | 			const std::array<RemoveComponentsFn, kRemoveComponentsCallSites.size()> removeComponentHooks{
// 3660 | 				&HookedRemoveComponentsSourceF1,
// 3661 | 				&HookedRemoveComponentsSourceF2,
// 3662 | 			};
// 3663 | 			allInstalled &= InstallDirectCallHookFamily(
// 3664 | 				kRemoveComponentsCallSites,
// 3665 | 				removeComponentHooks,
// 3666 | 				originalRemoveComponents,
// 3667 | 				"workshop-material.remove-components");
// 3668 |
// 3669 | 			const std::array<WorkshopConsumeComponentFn, kWorkshopConsumeComponentCallSites.size()> consumeComponentHooks{
// 3670 | 				&HookedWorkshopConsumeComponentSourceF3,
// 3671 | 				&HookedWorkshopConsumeComponentSourceF4,
// 3672 | 			};
// 3673 | 			allInstalled &= InstallDirectCallHookFamily(
// 3674 | 				kWorkshopConsumeComponentCallSites,
// 3675 | 				consumeComponentHooks,
// 3676 | 				originalWorkshopConsumeComponent,
// 3677 | 				"workshop-material.consume-component");
// 3678 |
// 3679 | 			allInstalled &= InstallDirectCallHookSite(
// 3680 | 				kWorkshopObjectCountPapyrusCallSite,
// 3681 | 				&HookedWorkshopObjectCount,
// 3682 | 				originalWorkshopObjectCount,
// 3683 | 				"workshop-material.object-count.papyrus");
// 3684 |
// 3685 | 			allInstalled &= InstallDirectCallHookSite(
// 3686 | 				kCurrentWorkshopObjectCountCallSite,
// 3687 | 				&HookedCurrentWorkshopObjectCount,
// 3688 | 				originalCurrentWorkshopObjectCount,
// 3689 | 				"workshop-material.object-count.current-workshop");
// 3690 |
// 3691 | 			if (allInstalled)
// 3692 | 			{
// 3693 | 				REX::INFO("Installed native workshop material probe hooks");
// 3694 | 			}
// 3695 | 			else
// 3696 | 			{
// 3697 | 				REX::WARN("Installed native workshop material probe hooks with one or more skipped families");
// 3698 | 			}
// 3699 | 		});
// 3700 | 	}
// 3701 |
// 3702 | }
// 3703 |
