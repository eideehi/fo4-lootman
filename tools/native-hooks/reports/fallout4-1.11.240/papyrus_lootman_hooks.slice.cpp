// Source slice generated for native hook address review.
// It keeps the address catalog include and direct-call install helpers near their call sites.

//   11 | #include <optional>
//   12 | #include <span>
//   13 | #include <unordered_map>
//   14 | #include <unordered_set>
//   15 | #include <vector>
//   16 |
//   17 | #include <REL/Relocation.h>
//   18 |
//   19 | #include "papyrus_lootman_hook_addresses.generated.h"
//   20 | #include "properties.h"
//   21 |
//   22 | namespace papyrus_lootman
//   23 | {
//   24 | 	using namespace RE;
//   25 |
//   26 | 	template <class OriginalFn, class HookFn>
//   27 | 	bool InstallDirectCallHookSite(
//   28 | 		const NativeHookCallSite& site,
//   29 | 		HookFn hook,
//   30 | 		OriginalFn& original,
//   31 | 		const char* family,
//   32 | 		const char* failurePolicyAction);
//   33 |
//   34 | 	struct NativeHookFeaturePolicy
//   35 | 	{
//   36 | 		const char* featureGroup;
//   37 | 		const char* failurePolicyAction;
//   38 | 	};
//   39 |
//   40 | 	inline constexpr NativeHookFeaturePolicy kEncounterZoneResetSuppressionPolicy{
//   41 | 		"encounter-zone.reset-suppression",
//   42 | 		"disable-encounter-zone-reset-suppression",
//   43 | 	};
//   44 | 	inline constexpr NativeHookFeaturePolicy kSharedWorkshopContainerPolicy{
//   45 | 		"workshop-shared-container",
//   46 | 		"disable-shared-workshop-container-augmentation",
//   47 | 	};
//   48 | 	inline constexpr NativeHookFeaturePolicy kWorkshopMaterialBehaviorPolicy{
//   49 | 		"workshop-material.behavior",
//   50 | 		"disable-native-workshop-material-feature",
//   51 | 	};
//   52 | 	inline constexpr NativeHookFeaturePolicy kWorkshopMaterialDiagnosticsPolicy{
//   53 | 		"workshop-material.diagnostics",
//   54 | 		"skip-optional-workshop-material-diagnostics",
//   55 | 	};
//   56 |
//   57 | 	struct ExtraCellDetachTimeCompat :
//   58 | 		public BSExtraData
//   59 | 	{
//   60 | 		static constexpr auto TYPE = EXTRA_DATA_TYPE::kCellDetachTime;
//   61 | 		std::uint32_t detachTime;
//   62 | 	};
//   63 |

// ...

//  156 | 			evaluation.cellDetachTime.value_or(0),
//  157 | 			evaluation.cellDetachTime.has_value(),
//  158 | 			evaluation.locationCleared,
//  159 | 			evaluation.cellDetachResetElapsed);
//  160 |
//  161 | 		return !evaluation.suppress;
//  162 | 	}
//  163 |
//  164 | 	void InstallEncounterZoneResetSuppressionHooks()
//  165 | 	{
//  166 | 		static std::once_flag installOnce;
//  167 | 		std::call_once(installOnce, []()
//  168 | 		{
//  169 | 			REL::Relocation<CheckResetElapsedFromDetachTimeFn> resetElapsedFromDetach{
//  170 | 				kEncounterZoneResetElapsedFromDetachId
//  171 | 			};
//  172 |
//  173 | 			if (!InstallDirectCallHookSite(
//  174 | 					kLoadChangeCellBeforeZoneResetCallSite,
//  175 | 					HookedCheckCellBeforeEncounterZoneReset,
//  176 | 					originalCheckCellBeforeEncounterZoneReset,
//  177 | 					"encounter-zone.reset-suppression",
//  178 | 					kEncounterZoneResetSuppressionPolicy.failurePolicyAction))
//  179 | 			{
//  180 | 				REX::ERROR(
//  181 | 					"source=native component=native_hook event=feature_group_disabled feature_group={} family=encounter-zone.reset-suppression failure_policy_action={}",
//  182 | 					kEncounterZoneResetSuppressionPolicy.featureGroup,
//  183 | 					kEncounterZoneResetSuppressionPolicy.failurePolicyAction);
//  184 | 				return;
//  185 | 			}
//  186 |
//  187 | 			checkResetElapsedFromDetachTime = resetElapsedFromDetach.get();
//  188 |
//  189 | 			REX::INFO("source=native component=native_hook event=installed family=encounter-zone.reset-suppression");
//  190 | 		});
//  191 | 	}
//  192 |
//  193 |
//  194 | 	using SharedWorkshopContainers = BSScrapArray<NiPointer<TESObjectREFR>>;
//  195 | 	using PopulateLinkedWorkshopContainersFn =
//  196 | 		void (*)(SharedWorkshopContainers*, BGSLocation*, bool);
//  197 | 	using RebuildWorkshopSupplyFn = void (*)(void*);
//  198 | 	using ComponentCountHelperFn = bool (*)(void*, std::int32_t*, TESForm*, bool);
//  199 | 	using DirectComponentCountFn = std::int32_t (*)(void*, BGSComponent*, bool);
//  200 | 	using WorkshopResourceStatusFn = std::uint32_t (*)();
//  201 | 	using GetWorkshopMenuNodeFn = Workshop::WorkshopMenuNode* (*)(std::uint16_t, std::uint32_t*);
//  202 | 	using WorkshopMenuAvailabilityFn = bool (*)(std::uint32_t*, std::uint32_t, std::uint32_t);
//  203 | 	using WorkshopMenuSelectFn = bool (*)(bool, void*);
//  204 | 	using WorkshopCheckAndSetPlacementFn = void (*)(WorkshopMenu*);
//  205 | 	using WorkshopStartPlacementFn = void (*)(void*, bool, bool);
//  206 | 	using WorkshopBuildResourceCheckFn = bool (*)(BGSConstructibleObject*, TESObjectREFR*, void*, bool);
//  207 | 	using WorkshopConsumeComponentFn = void (*)(TESForm*, std::uint32_t);
//  208 | 	using RemoveComponentsFn = void (*)(
//  209 | 		TESObjectREFR*,

// ...

//  291 | 	{
//  292 | 		auto* context = static_cast<DirectCallContextReadContext*>(opaque);
//  293 | 		std::memcpy(
//  294 | 			context->destination,
//  295 | 			reinterpret_cast<const void*>(context->address),
//  296 | 			context->size);
//  297 | 	}
//  298 |
//  299 | 	std::optional<DirectCallSiteDecode> DecodeDirectCallSite(
//  300 | 		const NativeHookCallSite& site,
//  301 | 		const char* family,
//  302 | 		const char* failurePolicyAction)
//  303 | 	{
//  304 | 		REL::Relocation<std::uintptr_t> callSite{ REL::Offset(site.rva) };
//  305 | 		const auto address = callSite.address();
//  306 | 		DirectCallInstructionReadContext context{ address };
//  307 | 		if (!ExecuteSehCallSafe(&ReadDirectCallInstructionBytes, &context))
//  308 | 		{
//  309 | 			REX::ERROR(
//  310 | 				"source=native component=native_hook event=direct_call_hook_skipped reason=instruction_read_failed family={} site={} rva={:X} failure_policy_action={}",
//  311 | 				family,
//  312 | 				site.id,
//  313 | 				site.rva,
//  314 | 				failurePolicyAction);
//  315 | 			return std::nullopt;
//  316 | 		}
//  317 |
//  318 | 		if (context.bytes[0] != 0xE8)
//  319 | 		{
//  320 | 			REX::ERROR(
//  321 | 				"source=native component=native_hook event=direct_call_hook_skipped reason=unexpected_opcode family={} site={} rva={:X} failure_policy_action={} expected_opcode=E8 actual_opcode={:02X}",
//  322 | 				family,
//  323 | 				site.id,
//  324 | 				site.rva,
//  325 | 				failurePolicyAction,
//  326 | 				context.bytes[0]);
//  327 | 			return std::nullopt;
//  328 | 		}
//  329 |
//  330 | 		std::int32_t displacement = 0;
//  331 | 		std::memcpy(&displacement, context.bytes.data() + 1, sizeof(displacement));
//  332 | 		const auto targetAddress = static_cast<std::uintptr_t>(
//  333 | 			static_cast<std::intptr_t>(address + 5) +
//  334 | 			static_cast<std::intptr_t>(displacement));
//  335 | 		const auto moduleBase = address - site.rva;

// ...

//  379 | 		return DirectCallSiteDecode{
//  380 | 			&site,
//  381 | 			address,
//  382 | 			targetAddress,
//  383 | 			targetRva,
//  384 | 		};
//  385 | 	}
//  386 |
//  387 | 	bool ValidateDirectCallSiteFamily(
//  388 | 		std::span<const NativeHookCallSite> sites,
//  389 | 		const char* family,
//  390 | 		const char* failurePolicyAction,
//  391 | 		bool requireSharedOriginalTarget)
//  392 | 	{
//  393 | 		if (sites.empty())
//  394 | 		{
//  395 | 			REX::ERROR(
//  396 | 				"source=native component=native_hook event=direct_call_hook_family_skipped reason=no_sites family={} failure_policy_action={}",
//  397 | 				family,
//  398 | 				failurePolicyAction);
//  399 | 			return false;
//  400 | 		}
//  401 |
//  402 | 		std::optional<std::uintptr_t> expectedTargetAddress;
//  403 | 		std::optional<std::uintptr_t> expectedTargetRva;
//  404 | 		for (const auto& site : sites)
//  405 | 		{
//  406 | 			const auto decoded = DecodeDirectCallSite(site, family, failurePolicyAction);
//  407 | 			if (!decoded)
//  408 | 			{
//  409 | 				return false;
//  410 | 			}
//  411 |
//  412 | 			if (!requireSharedOriginalTarget)
//  413 | 			{
//  414 | 				continue;
//  415 | 			}
//  416 |
//  417 | 			if (!expectedTargetAddress)
//  418 | 			{
//  419 | 				expectedTargetAddress = decoded->targetAddress;
//  420 | 				expectedTargetRva = decoded->targetRva;
//  421 | 				continue;
//  422 | 			}
//  423 |
//  424 | 			if (*expectedTargetAddress != decoded->targetAddress)
//  425 | 			{
//  426 | 				REX::ERROR(
//  427 | 					"source=native component=native_hook event=direct_call_hook_family_skipped reason=unexpected_original_target family={} site={} rva={:X} original_target_rva={:X} expected_original_target_rva={:X} failure_policy_action={}",
//  428 | 					family,
//  429 | 					site.id,
//  430 | 					site.rva,
//  431 | 					decoded->targetRva,
//  432 | 					expectedTargetRva.value_or(0),
//  433 | 					failurePolicyAction);
//  434 | 				return false;
//  435 | 			}
//  436 | 		}
//  437 |
//  438 | 		return true;
//  439 | 	}
//  440 |
//  441 | 	template <class OriginalFn, class HookFn>
//  442 | 	OriginalFn WriteValidatedDirectCallHook(
//  443 | 		const NativeHookCallSite& site,
//  444 | 		HookFn hook,
//  445 | 		const char* family,
//  446 | 		const char* failurePolicyAction)
//  447 | 	{
//  448 | 		const auto decoded = DecodeDirectCallSite(site, family, failurePolicyAction);
//  449 | 		if (!decoded)
//  450 | 		{
//  451 | 			return OriginalFn{};
//  452 | 		}
//  453 |
//  454 | 		REL::Relocation<std::uintptr_t> callSite{ REL::Offset(site.rva) };
//  455 | 		const auto original = reinterpret_cast<OriginalFn>(callSite.write_call<5>(hook));
//  456 | 		REX::DEBUG(
//  457 | 			"source=native component=native_hook event=direct_call_hook_installed family={} site={} rva={:X} original_target_rva={:X} failure_policy_action={}",
//  458 | 			family,
//  459 | 			site.id,
//  460 | 			site.rva,
//  461 | 			decoded->targetRva,
//  462 | 			failurePolicyAction);
//  463 | 		return original;
//  464 | 	}
//  465 |
//  466 | 	template <std::size_t N>
//  467 | 	bool PrevalidateDirectCallHookFamily(
//  468 | 		const std::array<NativeHookCallSite, N>& sites,
//  469 | 		const char* family,
//  470 | 		const char* featureGroup,
//  471 | 		const char* failurePolicyAction)
//  472 | 	{
//  473 | 		if (ValidateDirectCallSiteFamily(
//  474 | 				std::span<const NativeHookCallSite>(sites.data(), sites.size()),
//  475 | 				family,
//  476 | 				failurePolicyAction,
//  477 | 				true))
//  478 | 		{
//  479 | 			return true;
//  480 | 		}
//  481 |
//  482 | 		REX::ERROR(
//  483 | 			"source=native component=native_hook event=feature_group_disabled feature_group={} family={} failure_policy_action={}",
//  484 | 			featureGroup,
//  485 | 			family,
//  486 | 			failurePolicyAction);
//  487 | 		return false;
//  488 | 	}
//  489 |
//  490 | 	template <class OriginalFn, class HookFn, std::size_t N>
//  491 | 	bool InstallDirectCallHookFamily(
//  492 | 		const std::array<NativeHookCallSite, N>& sites,
//  493 | 		const std::array<HookFn, N>& hooks,
//  494 | 		OriginalFn& original,
//  495 | 		const char* family,
//  496 | 		const char* failurePolicyAction)
//  497 | 	{
//  498 | 		if (!ValidateDirectCallSiteFamily(
//  499 | 				std::span<const NativeHookCallSite>(sites.data(), sites.size()),
//  500 | 				family,
//  501 | 				failurePolicyAction,
//  502 | 				true))
//  503 | 		{
//  504 | 			REX::ERROR(
//  505 | 				"source=native component=native_hook event=direct_call_hook_family_skipped reason=validation_failed family={} failure_policy_action={}",
//  506 | 				family,
//  507 | 				failurePolicyAction);
//  508 | 			return false;
//  509 | 		}
//  510 |
//  511 | 		for (std::size_t index = 0; index < sites.size(); ++index)
//  512 | 		{
//  513 | 			const auto patchedOriginal = WriteValidatedDirectCallHook<OriginalFn>(
//  514 | 				sites[index],
//  515 | 				hooks[index],
//  516 | 				family,
//  517 | 				failurePolicyAction);
//  518 | 			if (!patchedOriginal)
//  519 | 			{
//  520 | 				REX::ERROR(
//  521 | 					"source=native component=native_hook event=direct_call_hook_family_skipped reason=validation_changed_after_patch family={} site={} rva={:X} failure_policy_action={}",
//  522 | 					family,
//  523 | 					sites[index].id,
//  524 | 					sites[index].rva,
//  525 | 					failurePolicyAction);
//  526 | 				return false;
//  527 | 			}
//  528 |
//  529 | 			if (!original)
//  530 | 			{
//  531 | 				original = patchedOriginal;
//  532 | 			}
//  533 | 			else if (original != patchedOriginal)
//  534 | 			{
//  535 | 				REX::WARN(
//  536 | 					"source=native component=native_hook event=direct_call_original_target_unexpected family={} site={} rva={:X} original={:X} expected={:X} failure_policy_action={}",
//  537 | 					family,
//  538 | 					sites[index].id,
//  539 | 					sites[index].rva,
//  540 | 					reinterpret_cast<std::uintptr_t>(patchedOriginal),
//  541 | 					reinterpret_cast<std::uintptr_t>(original),
//  542 | 					failurePolicyAction);
//  543 | 			}
//  544 | 		}
//  545 |
//  546 | 		return true;
//  547 | 	}
//  548 |
//  549 | 	template <class OriginalFn, class HookFn>
//  550 | 	bool InstallDirectCallHookSite(
//  551 | 		const NativeHookCallSite& site,
//  552 | 		HookFn hook,
//  553 | 		OriginalFn& original,
//  554 | 		const char* family,
//  555 | 		const char* failurePolicyAction)
//  556 | 	{
//  557 | 		if (!ValidateDirectCallSiteFamily(
//  558 | 				std::span<const NativeHookCallSite>(&site, 1),
//  559 | 				family,
//  560 | 				failurePolicyAction,
//  561 | 				false))
//  562 | 		{
//  563 | 			REX::ERROR(
//  564 | 				"source=native component=native_hook event=direct_call_hook_skipped reason=validation_failed family={} site={} failure_policy_action={}",
//  565 | 				family,
//  566 | 				site.id,
//  567 | 				failurePolicyAction);
//  568 | 			return false;
//  569 | 		}
//  570 |
//  571 | 		const auto patchedOriginal = WriteValidatedDirectCallHook<OriginalFn>(
//  572 | 			site,
//  573 | 			hook,
//  574 | 			family,
//  575 | 			failurePolicyAction);
//  576 | 		if (!patchedOriginal)
//  577 | 		{
//  578 | 			REX::ERROR(
//  579 | 				"source=native component=native_hook event=direct_call_hook_skipped reason=validation_changed_after_patch family={} site={} failure_policy_action={}",
//  580 | 				family,
//  581 | 				site.id,
//  582 | 				failurePolicyAction);
//  583 | 			return false;
//  584 | 		}
//  585 |
//  586 | 		original = patchedOriginal;
//  587 | 		return true;
//  588 | 	}
//  589 |
//  590 | 	struct FormProbeSnapshot
//  591 | 	{
//  592 | 		std::uintptr_t pointer = 0;
//  593 | 		TESFormID formID = 0;
//  594 | 		std::uint32_t formType = 0;
//  595 | 		bool readable = false;
//  596 | 	};
//  597 |
//  598 | 	struct FormProbeSnapshotContext
//  599 | 	{
//  600 | 		TESForm* form = nullptr;
//  601 | 		FormProbeSnapshot snapshot;
//  602 | 	};
//  603 |
//  604 | 	void CaptureFormProbeSnapshotCall(void* opaque)
//  605 | 	{
//  606 | 		auto* context = static_cast<FormProbeSnapshotContext*>(opaque);
//  607 | 		auto* form = context->form;

// ...

// 4010 | 			removed);
// 4011 | 	}
// 4012 |
// 4013 | 	void ResetWorkshopRuntimeState(std::monostate, BSFixedString context)
// 4014 | 	{
// 4015 | 		ClearWorkshopRuntimeState(context.c_str());
// 4016 | 	}
// 4017 |
// 4018 | 	void InstallWorkbenchSharedContainerHooks()
// 4019 | 	{
// 4020 | 		static std::once_flag installOnce;
// 4021 | 		std::call_once(installOnce, []()
// 4022 | 		{
// 4023 | 			const std::array<PopulateLinkedWorkshopContainersFn, kPopulateLinkedWorkshopContainerCallSites.size()> hooks{
// 4024 | 				&HookedPopulateLinkedWorkshopContainers,
// 4025 | 				&HookedPopulateLinkedWorkshopContainers,
// 4026 | 				&HookedPopulateLinkedWorkshopContainers,
// 4027 | 			};
// 4028 |
// 4029 | 			if (InstallDirectCallHookFamily(
// 4030 | 					kPopulateLinkedWorkshopContainerCallSites,
// 4031 | 					hooks,
// 4032 | 					originalPopulateLinkedWorkshopContainers,
// 4033 | 					"workshop-shared-container.populate-linked",
// 4034 | 					kSharedWorkshopContainerPolicy.failurePolicyAction))
// 4035 | 			{
// 4036 | 				sharedWorkshopContainerAugmentationInstalled.store(true, std::memory_order_release);
// 4037 | 				REX::INFO("source=native component=native_hook event=installed family=workshop-shared-container.populate-linked");
// 4038 | 			}
// 4039 | 			else
// 4040 | 			{
// 4041 | 				sharedWorkshopContainerAugmentationInstalled.store(false, std::memory_order_release);
// 4042 | 				REX::ERROR(
// 4043 | 					"source=native component=native_hook event=feature_group_disabled feature_group={} family=workshop-shared-container.populate-linked failure_policy_action={}",
// 4044 | 					kSharedWorkshopContainerPolicy.featureGroup,
// 4045 | 					kSharedWorkshopContainerPolicy.failurePolicyAction);
// 4046 | 			}
// 4047 | 		});
// 4048 | 	}
// 4049 |
// 4050 | 	void InstallWorkshopMaterialProbeHooks()
// 4051 | 	{
// 4052 | 		static std::once_flag installOnce;
// 4053 | 		std::call_once(installOnce, []()
// 4054 | 		{
// 4055 | 			const std::array<RebuildWorkshopSupplyFn, kRebuildWorkshopSupplyCallSites.size()> rebuildHooks{
// 4056 | 				&HookedRebuildWorkshopSupplySourceA1,
// 4057 | 				&HookedRebuildWorkshopSupplySourceA2,
// 4058 | 				&HookedRebuildWorkshopSupplySourceA3,
// 4059 | 				&HookedRebuildWorkshopSupplySourceA4,
// 4060 | 			};
// 4061 | 			const std::array<ComponentCountHelperFn, kComponentCountHelperCallSites.size()> componentCountHooks{
// 4062 | 				&HookedComponentCountPapyrus,
// 4063 | 				&HookedComponentCountWorkbenchUi,
// 4064 | 			};
// 4065 | 			const std::array<DirectComponentCountFn, kDirectComponentCountCallSites.size()> directComponentHooks{
// 4066 | 				&HookedDirectComponentCountSourceE1,
// 4067 | 				&HookedDirectComponentCountSourceE2,
// 4068 | 				&HookedDirectComponentCountSourceE3,
// 4069 | 				&HookedDirectComponentCountSourceE4,
// 4070 | 				&HookedDirectComponentCountSourceE5,
// 4071 | 			};
// 4072 | 			const std::array<WorkshopResourceStatusFn, kWorkshopResourceStatusCallSites.size()> resourceStatusHooks{
// 4073 | 				&HookedWorkshopResourceStatusSourceF1,
// 4074 | 				&HookedWorkshopResourceStatusSourceF2,
// 4075 | 			};
// 4076 | 			const std::array<WorkshopMenuAvailabilityFn, kWorkshopMenuAvailabilityCallSites.size()> menuAvailabilityHooks{
// 4077 | 				&HookedWorkshopMenuAvailabilitySource<0>,
// 4078 | 				&HookedWorkshopMenuAvailabilitySource<1>,
// 4079 | 				&HookedWorkshopMenuAvailabilitySource<2>,
// 4080 | 				&HookedWorkshopMenuAvailabilitySource<3>,
// 4081 | 				&HookedWorkshopMenuAvailabilitySource<4>,
// 4082 | 				&HookedWorkshopMenuAvailabilitySource<5>,
// 4083 | 				&HookedWorkshopMenuAvailabilitySource<6>,
// 4084 | 				&HookedWorkshopMenuAvailabilitySource<7>,
// 4085 | 				&HookedWorkshopMenuAvailabilitySource<8>,
// 4086 | 				&HookedWorkshopMenuAvailabilitySource<9>,

// ...

// 4165 | 				kWorkshopMaterialBehaviorPolicy.failurePolicyAction);
// 4166 |
// 4167 | 			bool behaviorInstalled = false;
// 4168 | 			if (behaviorPrevalidated)
// 4169 | 			{
// 4170 | 				behaviorInstalled = true;
// 4171 | 				// Install consumption hooks before count/build allowance hooks so
// 4172 | 				// a mid-install failure cannot leave allowance active alone.
// 4173 | 				behaviorInstalled &= InstallDirectCallHookFamily(
// 4174 | 					kRemoveComponentsCallSites,
// 4175 | 					removeComponentHooks,
// 4176 | 					originalRemoveComponents,
// 4177 | 					"workshop-material.remove-components",
// 4178 | 					kWorkshopMaterialBehaviorPolicy.failurePolicyAction);
// 4179 | 				behaviorInstalled &= InstallDirectCallHookFamily(
// 4180 | 					kWorkshopConsumeComponentCallSites,
// 4181 | 					consumeComponentHooks,
// 4182 | 					originalWorkshopConsumeComponent,
// 4183 | 					"workshop-material.consume-component",
// 4184 | 					kWorkshopMaterialBehaviorPolicy.failurePolicyAction);
// 4185 | 				if (behaviorInstalled)
// 4186 | 				{
// 4187 | 					behaviorInstalled &= InstallDirectCallHookFamily(
// 4188 | 						kComponentCountHelperCallSites,
// 4189 | 						componentCountHooks,
// 4190 | 						originalComponentCountHelper,
// 4191 | 						"workshop-material.component-count",
// 4192 | 						kWorkshopMaterialBehaviorPolicy.failurePolicyAction);
// 4193 | 					behaviorInstalled &= InstallDirectCallHookFamily(
// 4194 | 						kDirectComponentCountCallSites,
// 4195 | 						directComponentHooks,
// 4196 | 						originalDirectComponentCount,
// 4197 | 						"workshop-material.direct-component-count",
// 4198 | 						kWorkshopMaterialBehaviorPolicy.failurePolicyAction);
// 4199 | 					behaviorInstalled &= InstallDirectCallHookFamily(
// 4200 | 						kWorkshopResourceStatusCallSites,
// 4201 | 						resourceStatusHooks,
// 4202 | 						originalWorkshopResourceStatus,
// 4203 | 						"workshop-material.resource-status",
// 4204 | 						kWorkshopMaterialBehaviorPolicy.failurePolicyAction);
// 4205 | 					behaviorInstalled &= InstallDirectCallHookFamily(
// 4206 | 						kWorkshopMenuAvailabilityCallSites,
// 4207 | 						menuAvailabilityHooks,
// 4208 | 						originalWorkshopMenuAvailability,
// 4209 | 						"workshop-menu.availability",
// 4210 | 						kWorkshopMaterialBehaviorPolicy.failurePolicyAction);
// 4211 | 					behaviorInstalled &= InstallDirectCallHookFamily(
// 4212 | 						kWorkshopBuildResourceCheckCallSites,
// 4213 | 						buildResourceCheckHooks,
// 4214 | 						originalWorkshopBuildResourceCheck,
// 4215 | 						"workshop-material.build-resource-check",
// 4216 | 						kWorkshopMaterialBehaviorPolicy.failurePolicyAction);
// 4217 | 				}
// 4218 | 			}
// 4219 | 			else
// 4220 | 			{
// 4221 | 				REX::ERROR(
// 4222 | 					"source=native component=native_hook event=feature_group_disabled reason=before_patching feature_group={} failure_policy_action={}",
// 4223 | 					kWorkshopMaterialBehaviorPolicy.featureGroup,
// 4224 | 					kWorkshopMaterialBehaviorPolicy.failurePolicyAction);
// 4225 | 			}
// 4226 |
// 4227 | 			bool optionalDiagnosticsInstalled = true;
// 4228 | 			optionalDiagnosticsInstalled &= InstallDirectCallHookFamily(
// 4229 | 				kRebuildWorkshopSupplyCallSites,
// 4230 | 				rebuildHooks,
// 4231 | 				originalRebuildWorkshopSupply,
// 4232 | 				"workshop-material.rebuild-supply",
// 4233 | 				kWorkshopMaterialDiagnosticsPolicy.failurePolicyAction);
// 4234 | 			optionalDiagnosticsInstalled &= InstallDirectCallHookFamily(
// 4235 | 				kWorkshopMenuSelectCallSites,
// 4236 | 				menuSelectHooks,
// 4237 | 				originalWorkshopMenuSelect,
// 4238 | 				"workshop-menu.select",
// 4239 | 				kWorkshopMaterialDiagnosticsPolicy.failurePolicyAction);
// 4240 | 			optionalDiagnosticsInstalled &= InstallDirectCallHookFamily(
// 4241 | 				kWorkshopCheckAndSetPlacementCallSites,
// 4242 | 				checkAndSetPlacementHooks,
// 4243 | 				originalWorkshopCheckAndSetPlacement,
// 4244 | 				"workshop-menu.check-placement",
// 4245 | 				kWorkshopMaterialDiagnosticsPolicy.failurePolicyAction);
// 4246 | 			optionalDiagnosticsInstalled &= InstallDirectCallHookFamily(
// 4247 | 				kWorkshopStartPlacementCallSites,
// 4248 | 				startPlacementHooks,
// 4249 | 				originalWorkshopStartPlacement,
// 4250 | 				"workshop-menu.start-placement",
// 4251 | 				kWorkshopMaterialDiagnosticsPolicy.failurePolicyAction);
// 4252 | 			optionalDiagnosticsInstalled &= InstallDirectCallHookSite(
// 4253 | 				kWorkshopObjectCountPapyrusCallSite,
// 4254 | 				&HookedWorkshopObjectCount,
// 4255 | 				originalWorkshopObjectCount,
// 4256 | 				"workshop-material.object-count.papyrus",
// 4257 | 				kWorkshopMaterialDiagnosticsPolicy.failurePolicyAction);
// 4258 | 			optionalDiagnosticsInstalled &= InstallDirectCallHookSite(
// 4259 | 				kCurrentWorkshopObjectCountCallSite,
// 4260 | 				&HookedCurrentWorkshopObjectCount,
// 4261 | 				originalCurrentWorkshopObjectCount,
// 4262 | 				"workshop-material.object-count.current-workshop",
// 4263 | 				kWorkshopMaterialDiagnosticsPolicy.failurePolicyAction);
// 4264 |
// 4265 | 			if (behaviorInstalled && optionalDiagnosticsInstalled)
// 4266 | 			{
// 4267 | 				REX::INFO("source=native component=native_hook event=installed feature_group=workshop_material behavior=installed diagnostics=installed");
// 4268 | 			}
// 4269 | 			else if (behaviorInstalled)
// 4270 | 			{
// 4271 | 				REX::WARN(
// 4272 | 					"source=native component=native_hook event=installed feature_group=workshop_material behavior=installed diagnostics=skipped failure_policy_action={}",
// 4273 | 					kWorkshopMaterialDiagnosticsPolicy.failurePolicyAction);
// 4274 | 			}
// 4275 | 			else if (optionalDiagnosticsInstalled)
// 4276 | 			{
// 4277 | 				REX::WARN(
// 4278 | 					"source=native component=native_hook event=installed feature_group=workshop_material behavior=disabled diagnostics=installed failure_policy_action={}",
// 4279 | 					kWorkshopMaterialBehaviorPolicy.failurePolicyAction);
// 4280 | 			}
// 4281 | 			else
// 4282 | 			{
// 4283 | 				REX::ERROR(
// 4284 | 					"source=native component=native_hook event=feature_group_disabled feature_group=workshop_material behavior_policy_action={} diagnostics_policy_action={}",
// 4285 | 					kWorkshopMaterialBehaviorPolicy.failurePolicyAction,
// 4286 | 					kWorkshopMaterialDiagnosticsPolicy.failurePolicyAction);
// 4287 | 			}
// 4288 | 		});
// 4289 | 	}
// 4290 |
// 4291 | }
// 4292 |
