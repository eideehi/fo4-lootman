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

//  231 | 	{
//  232 | 		auto* context = static_cast<DirectCallInstructionReadContext*>(opaque);
//  233 | 		std::memcpy(
//  234 | 			context->bytes.data(),
//  235 | 			reinterpret_cast<const void*>(context->address),
//  236 | 			context->bytes.size());
//  237 | 	}
//  238 |
//  239 | 	std::optional<DirectCallSiteDecode> DecodeDirectCallSite(
//  240 | 		const NativeHookCallSite& site,
//  241 | 		const char* family)
//  242 | 	{
//  243 | 		REL::Relocation<std::uintptr_t> callSite{ REL::Offset(site.rva) };
//  244 | 		const auto address = callSite.address();
//  245 | 		DirectCallInstructionReadContext context{ address };
//  246 | 		if (!ExecuteSehCallSafe(&ReadDirectCallInstructionBytes, &context))
//  247 | 		{
//  248 | 			REX::ERROR(
//  249 | 				"Skipping native direct-call hook: family={}, site={}, rva={:X}, reason=instruction-read-failed",
//  250 | 				family,
//  251 | 				site.id,
//  252 | 				site.rva);
//  253 | 			return std::nullopt;
//  254 | 		}
//  255 |
//  256 | 		if (context.bytes[0] != 0xE8)
//  257 | 		{
//  258 | 			REX::ERROR(
//  259 | 				"Skipping native direct-call hook: family={}, site={}, rva={:X}, expectedOpcode=E8, actualOpcode={:02X}",
//  260 | 				family,
//  261 | 				site.id,
//  262 | 				site.rva,
//  263 | 				context.bytes[0]);
//  264 | 			return std::nullopt;
//  265 | 		}
//  266 |
//  267 | 		std::int32_t displacement = 0;
//  268 | 		std::memcpy(&displacement, context.bytes.data() + 1, sizeof(displacement));
//  269 | 		const auto targetAddress = static_cast<std::uintptr_t>(
//  270 | 			static_cast<std::intptr_t>(address + 5) +
//  271 | 			static_cast<std::intptr_t>(displacement));
//  272 | 		const auto moduleBase = address - site.rva;
//  273 | 		return DirectCallSiteDecode{
//  274 | 			&site,
//  275 | 			address,
//  276 | 			targetAddress,
//  277 | 			targetAddress - moduleBase,
//  278 | 		};
//  279 | 	}
//  280 |
//  281 | 	bool ValidateDirectCallSiteFamily(
//  282 | 		std::span<const NativeHookCallSite> sites,
//  283 | 		const char* family,
//  284 | 		bool requireSharedOriginalTarget)
//  285 | 	{
//  286 | 		if (sites.empty())
//  287 | 		{
//  288 | 			REX::ERROR("Skipping native direct-call hook family: family={}, reason=no-sites", family);
//  289 | 			return false;
//  290 | 		}
//  291 |
//  292 | 		std::optional<std::uintptr_t> expectedTargetAddress;
//  293 | 		std::optional<std::uintptr_t> expectedTargetRva;
//  294 | 		for (const auto& site : sites)
//  295 | 		{
//  296 | 			const auto decoded = DecodeDirectCallSite(site, family);
//  297 | 			if (!decoded)
//  298 | 			{
//  299 | 				return false;
//  300 | 			}
//  301 |
//  302 | 			if (!requireSharedOriginalTarget)
//  303 | 			{
//  304 | 				continue;
//  305 | 			}
//  306 |
//  307 | 			if (!expectedTargetAddress)
//  308 | 			{
//  309 | 				expectedTargetAddress = decoded->targetAddress;
//  310 | 				expectedTargetRva = decoded->targetRva;
//  311 | 				continue;
//  312 | 			}
//  313 |
//  314 | 			if (*expectedTargetAddress != decoded->targetAddress)
//  315 | 			{
//  316 | 				REX::ERROR(
//  317 | 					"Skipping native direct-call hook family: family={}, site={}, rva={:X}, originalTargetRva={:X}, expectedOriginalTargetRva={:X}",
//  318 | 					family,
//  319 | 					site.id,
//  320 | 					site.rva,
//  321 | 					decoded->targetRva,
//  322 | 					expectedTargetRva.value_or(0));
//  323 | 				return false;
//  324 | 			}
//  325 | 		}
//  326 |
//  327 | 		return true;
//  328 | 	}
//  329 |
//  330 | 	template <class OriginalFn, class HookFn>
//  331 | 	OriginalFn WriteValidatedDirectCallHook(
//  332 | 		const NativeHookCallSite& site,
//  333 | 		HookFn hook,
//  334 | 		const char* family)
//  335 | 	{
//  336 | 		const auto decoded = DecodeDirectCallSite(site, family);
//  337 | 		if (!decoded)
//  338 | 		{
//  339 | 			return OriginalFn{};
//  340 | 		}
//  341 |
//  342 | 		REL::Relocation<std::uintptr_t> callSite{ REL::Offset(site.rva) };
//  343 | 		const auto original = reinterpret_cast<OriginalFn>(callSite.write_call<5>(hook));
//  344 | 		REX::INFO(
//  345 | 			"Installed native direct-call hook: family={}, site={}, rva={:X}, originalTargetRva={:X}",
//  346 | 			family,
//  347 | 			site.id,
//  348 | 			site.rva,
//  349 | 			decoded->targetRva);
//  350 | 		return original;
//  351 | 	}
//  352 |
//  353 | 	template <class OriginalFn, class HookFn, std::size_t N>
//  354 | 	bool InstallDirectCallHookFamily(
//  355 | 		const std::array<NativeHookCallSite, N>& sites,
//  356 | 		const std::array<HookFn, N>& hooks,
//  357 | 		OriginalFn& original,
//  358 | 		const char* family)
//  359 | 	{
//  360 | 		if (!ValidateDirectCallSiteFamily(
//  361 | 				std::span<const NativeHookCallSite>(sites.data(), sites.size()),
//  362 | 				family,
//  363 | 				true))
//  364 | 		{
//  365 | 			REX::ERROR("Skipped native direct-call hook family: family={}", family);
//  366 | 			return false;
//  367 | 		}
//  368 |
//  369 | 		for (std::size_t index = 0; index < sites.size(); ++index)
//  370 | 		{
//  371 | 			const auto patchedOriginal = WriteValidatedDirectCallHook<OriginalFn>(
//  372 | 				sites[index],
//  373 | 				hooks[index],
//  374 | 				family);
//  375 | 			if (!patchedOriginal)
//  376 | 			{
//  377 | 				REX::ERROR(
//  378 | 					"Skipped native direct-call hook family after validation changed: family={}, site={}, rva={:X}",
//  379 | 					family,
//  380 | 					sites[index].id,
//  381 | 					sites[index].rva);
//  382 | 				return false;
//  383 | 			}
//  384 |
//  385 | 			if (!original)
//  386 | 			{
//  387 | 				original = patchedOriginal;
//  388 | 			}
//  389 | 			else if (original != patchedOriginal)
//  390 | 			{
//  391 | 				REX::WARN(
//  392 | 					"Unexpected native direct-call original target after patch: family={}, site={}, rva={:X}, original={:X}, expected={:X}",
//  393 | 					family,
//  394 | 					sites[index].id,
//  395 | 					sites[index].rva,
//  396 | 					reinterpret_cast<std::uintptr_t>(patchedOriginal),
//  397 | 					reinterpret_cast<std::uintptr_t>(original));
//  398 | 			}
//  399 | 		}
//  400 |
//  401 | 		return true;
//  402 | 	}
//  403 |
//  404 | 	template <class OriginalFn, class HookFn>
//  405 | 	bool InstallDirectCallHookSite(
//  406 | 		const NativeHookCallSite& site,
//  407 | 		HookFn hook,
//  408 | 		OriginalFn& original,
//  409 | 		const char* family)
//  410 | 	{
//  411 | 		if (!ValidateDirectCallSiteFamily(
//  412 | 				std::span<const NativeHookCallSite>(&site, 1),
//  413 | 				family,
//  414 | 				false))
//  415 | 		{
//  416 | 			REX::ERROR("Skipped native direct-call hook: family={}, site={}", family, site.id);
//  417 | 			return false;
//  418 | 		}
//  419 |
//  420 | 		const auto patchedOriginal = WriteValidatedDirectCallHook<OriginalFn>(
//  421 | 			site,
//  422 | 			hook,
//  423 | 			family);
//  424 | 		if (!patchedOriginal)
//  425 | 		{
//  426 | 			REX::ERROR("Skipped native direct-call hook after validation changed: family={}, site={}", family, site.id);
//  427 | 			return false;
//  428 | 		}
//  429 |
//  430 | 		original = patchedOriginal;
//  431 | 		return true;
//  432 | 	}
//  433 |
//  434 | 	struct FormProbeSnapshot
//  435 | 	{
//  436 | 		std::uintptr_t pointer = 0;
//  437 | 		TESFormID formID = 0;
//  438 | 		std::uint32_t formType = 0;
//  439 | 		bool readable = false;
//  440 | 	};
//  441 |
//  442 | 	struct FormProbeSnapshotContext
//  443 | 	{
//  444 | 		TESForm* form = nullptr;
//  445 | 		FormProbeSnapshot snapshot;
//  446 | 	};
//  447 |
//  448 | 	void CaptureFormProbeSnapshotCall(void* opaque)
//  449 | 	{
//  450 | 		auto* context = static_cast<FormProbeSnapshotContext*>(opaque);
//  451 | 		auto* form = context->form;
//  452 | 		if (!form)
//  453 | 		{
//  454 | 			return;
//  455 | 		}
//  456 |

// ...

// 3453 | 			prefixText,
// 3454 | 			targetWorkshop ? targetWorkshop->formID : 0,
// 3455 | 			targetLocation ? targetLocation->formID : 0,
// 3456 | 			lootManWorkshop ? lootManWorkshop->formID : 0,
// 3457 | 			lootManLocation ? lootManLocation->formID : 0,
// 3458 | 			workshopCaravanKeyword ? workshopCaravanKeyword->formID : 0);
// 3459 | 	}
// 3460 |
// 3461 | 	void InstallWorkbenchSharedContainerHooks()
// 3462 | 	{
// 3463 | 		static std::once_flag installOnce;
// 3464 | 		std::call_once(installOnce, []()
// 3465 | 		{
// 3466 | 			const std::array<PopulateLinkedWorkshopContainersFn, kPopulateLinkedWorkshopContainerCallSites.size()> hooks{
// 3467 | 				&HookedPopulateLinkedWorkshopContainers,
// 3468 | 				&HookedPopulateLinkedWorkshopContainers,
// 3469 | 				&HookedPopulateLinkedWorkshopContainers,
// 3470 | 			};
// 3471 |
// 3472 | 			if (InstallDirectCallHookFamily(
// 3473 | 					kPopulateLinkedWorkshopContainerCallSites,
// 3474 | 					hooks,
// 3475 | 					originalPopulateLinkedWorkshopContainers,
// 3476 | 					"workshop-shared-container.populate-linked"))
// 3477 | 			{
// 3478 | 				REX::INFO("Installed native shared workshop container hooks");
// 3479 | 			}
// 3480 | 		});
// 3481 | 	}
// 3482 |
// 3483 | 	void InstallWorkshopMaterialProbeHooks()
// 3484 | 	{
// 3485 | 		static std::once_flag installOnce;
// 3486 | 		std::call_once(installOnce, []()
// 3487 | 		{
// 3488 | 			bool allInstalled = true;
// 3489 |
// 3490 | 			const std::array<RebuildWorkshopSupplyFn, kRebuildWorkshopSupplyCallSites.size()> rebuildHooks{
// 3491 | 				&HookedRebuildWorkshopSupplySourceA1,
// 3492 | 				&HookedRebuildWorkshopSupplySourceA2,
// 3493 | 				&HookedRebuildWorkshopSupplySourceA3,
// 3494 | 				&HookedRebuildWorkshopSupplySourceA4,
// 3495 | 			};
// 3496 | 			allInstalled &= InstallDirectCallHookFamily(
// 3497 | 				kRebuildWorkshopSupplyCallSites,
// 3498 | 				rebuildHooks,
// 3499 | 				originalRebuildWorkshopSupply,
// 3500 | 				"workshop-material.rebuild-supply");
// 3501 |
// 3502 | 			const std::array<ComponentCountHelperFn, kComponentCountHelperCallSites.size()> componentCountHooks{
// 3503 | 				&HookedComponentCountPapyrus,
// 3504 | 				&HookedComponentCountWorkbenchUi,
// 3505 | 			};
// 3506 | 			allInstalled &= InstallDirectCallHookFamily(
// 3507 | 				kComponentCountHelperCallSites,
// 3508 | 				componentCountHooks,
// 3509 | 				originalComponentCountHelper,
// 3510 | 				"workshop-material.component-count");
// 3511 |
// 3512 | 			const std::array<DirectComponentCountFn, kDirectComponentCountCallSites.size()> directComponentHooks{
// 3513 | 				&HookedDirectComponentCountSourceE1,
// 3514 | 				&HookedDirectComponentCountSourceE2,
// 3515 | 				&HookedDirectComponentCountSourceE3,
// 3516 | 				&HookedDirectComponentCountSourceE4,
// 3517 | 				&HookedDirectComponentCountSourceE5,
// 3518 | 			};
// 3519 | 			allInstalled &= InstallDirectCallHookFamily(
// 3520 | 				kDirectComponentCountCallSites,
// 3521 | 				directComponentHooks,
// 3522 | 				originalDirectComponentCount,
// 3523 | 				"workshop-material.direct-component-count");
// 3524 |
// 3525 | 			const std::array<WorkshopResourceStatusFn, kWorkshopResourceStatusCallSites.size()> resourceStatusHooks{
// 3526 | 				&HookedWorkshopResourceStatusSourceF1,
// 3527 | 				&HookedWorkshopResourceStatusSourceF2,
// 3528 | 			};
// 3529 | 			allInstalled &= InstallDirectCallHookFamily(
// 3530 | 				kWorkshopResourceStatusCallSites,
// 3531 | 				resourceStatusHooks,
// 3532 | 				originalWorkshopResourceStatus,
// 3533 | 				"workshop-material.resource-status");
// 3534 |
// 3535 | 			const std::array<WorkshopMenuAvailabilityFn, kWorkshopMenuAvailabilityCallSites.size()> menuAvailabilityHooks{
// 3536 | 				&HookedWorkshopMenuAvailabilitySource91,
// 3537 | 				&HookedWorkshopMenuAvailabilitySource92,
// 3538 | 				&HookedWorkshopMenuAvailabilitySource93,
// 3539 | 				&HookedWorkshopMenuAvailabilitySource94,
// 3540 | 				&HookedWorkshopMenuAvailabilitySource95,
// 3541 | 			};
// 3542 | 			allInstalled &= InstallDirectCallHookFamily(
// 3543 | 				kWorkshopMenuAvailabilityCallSites,
// 3544 | 				menuAvailabilityHooks,
// 3545 | 				originalWorkshopMenuAvailability,
// 3546 | 				"workshop-menu.availability");
// 3547 |
// 3548 | 			const std::array<WorkshopCheckAndSetPlacementFn, kWorkshopCheckAndSetPlacementCallSites.size()> checkAndSetPlacementHooks{
// 3549 | 				&HookedWorkshopCheckAndSetPlacementSourceA5,
// 3550 | 				&HookedWorkshopCheckAndSetPlacementSourceA6,
// 3551 | 				&HookedWorkshopCheckAndSetPlacementSourceA7,
// 3552 | 				&HookedWorkshopCheckAndSetPlacementSourceA8,
// 3553 | 			};
// 3554 | 			allInstalled &= InstallDirectCallHookFamily(
// 3555 | 				kWorkshopCheckAndSetPlacementCallSites,
// 3556 | 				checkAndSetPlacementHooks,
// 3557 | 				originalWorkshopCheckAndSetPlacement,
// 3558 | 				"workshop-menu.check-placement");
// 3559 |
// 3560 | 			const std::array<WorkshopMenuSelectFn, kWorkshopMenuSelectCallSites.size()> menuSelectHooks{
// 3561 | 				&HookedWorkshopMenuSelectSourceA1,
// 3562 | 				&HookedWorkshopMenuSelectSourceA2,
// 3563 | 			};
// 3564 | 			allInstalled &= InstallDirectCallHookFamily(
// 3565 | 				kWorkshopMenuSelectCallSites,
// 3566 | 				menuSelectHooks,
// 3567 | 				originalWorkshopMenuSelect,
// 3568 | 				"workshop-menu.select");
// 3569 |
// 3570 | 			const std::array<WorkshopStartPlacementFn, kWorkshopStartPlacementCallSites.size()> startPlacementHooks{
// 3571 | 				&HookedWorkshopStartPlacementSourceA3,
// 3572 | 				&HookedWorkshopStartPlacementSourceA4,
// 3573 | 			};
// 3574 | 			allInstalled &= InstallDirectCallHookFamily(
// 3575 | 				kWorkshopStartPlacementCallSites,
// 3576 | 				startPlacementHooks,
// 3577 | 				originalWorkshopStartPlacement,
// 3578 | 				"workshop-menu.start-placement");
// 3579 |
// 3580 | 			const std::array<WorkshopBuildResourceCheckFn, kWorkshopBuildResourceCheckCallSites.size()> buildResourceCheckHooks{
// 3581 | 				&HookedWorkshopBuildResourceCheckPlacement,
// 3582 | 				&HookedWorkshopBuildResourceCheckConfirm,
// 3583 | 			};
// 3584 | 			allInstalled &= InstallDirectCallHookFamily(
// 3585 | 				kWorkshopBuildResourceCheckCallSites,
// 3586 | 				buildResourceCheckHooks,
// 3587 | 				originalWorkshopBuildResourceCheck,
// 3588 | 				"workshop-material.build-resource-check");
// 3589 |
// 3590 | 			const std::array<RemoveComponentsFn, kRemoveComponentsCallSites.size()> removeComponentHooks{
// 3591 | 				&HookedRemoveComponentsSourceF1,
// 3592 | 				&HookedRemoveComponentsSourceF2,
// 3593 | 			};
// 3594 | 			allInstalled &= InstallDirectCallHookFamily(
// 3595 | 				kRemoveComponentsCallSites,
// 3596 | 				removeComponentHooks,
// 3597 | 				originalRemoveComponents,
// 3598 | 				"workshop-material.remove-components");
// 3599 |
// 3600 | 			const std::array<WorkshopConsumeComponentFn, kWorkshopConsumeComponentCallSites.size()> consumeComponentHooks{
// 3601 | 				&HookedWorkshopConsumeComponentSourceF3,
// 3602 | 				&HookedWorkshopConsumeComponentSourceF4,
// 3603 | 			};
// 3604 | 			allInstalled &= InstallDirectCallHookFamily(
// 3605 | 				kWorkshopConsumeComponentCallSites,
// 3606 | 				consumeComponentHooks,
// 3607 | 				originalWorkshopConsumeComponent,
// 3608 | 				"workshop-material.consume-component");
// 3609 |
// 3610 | 			allInstalled &= InstallDirectCallHookSite(
// 3611 | 				kWorkshopObjectCountPapyrusCallSite,
// 3612 | 				&HookedWorkshopObjectCount,
// 3613 | 				originalWorkshopObjectCount,
// 3614 | 				"workshop-material.object-count.papyrus");
// 3615 |
// 3616 | 			allInstalled &= InstallDirectCallHookSite(
// 3617 | 				kCurrentWorkshopObjectCountCallSite,
// 3618 | 				&HookedCurrentWorkshopObjectCount,
// 3619 | 				originalCurrentWorkshopObjectCount,
// 3620 | 				"workshop-material.object-count.current-workshop");
// 3621 |
// 3622 | 			if (allInstalled)
// 3623 | 			{
// 3624 | 				REX::INFO("Installed native workshop material probe hooks");
// 3625 | 			}
// 3626 | 			else
// 3627 | 			{
// 3628 | 				REX::WARN("Installed native workshop material probe hooks with one or more skipped families");
// 3629 | 			}
// 3630 | 		});
// 3631 | 	}
// 3632 |
// 3633 | }
// 3634 |
