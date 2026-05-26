import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

function extractPapyrusFunction(source: string, name: string): string {
	const match = new RegExp(`Function\\s+${name}\\([^\\n]*\\)([\\s\\S]*?)EndFunction`, "i").exec(source);
	expect(match, `missing Papyrus function ${name}`).not.toBeNull();
	return match![1]!;
}

function extractCppFunction(source: string, signature: string): string {
	const start = source.indexOf(signature);
	expect(start, `missing C++ function ${signature}`).toBeGreaterThanOrEqual(0);

	const braceStart = source.indexOf("{", start);
	expect(braceStart, `missing C++ function body for ${signature}`).toBeGreaterThanOrEqual(0);

	let depth = 0;
	for (let index = braceStart; index < source.length; index += 1) {
		const char = source[index];
		if (char === "{") {
			depth += 1;
		} else if (char === "}") {
			depth -= 1;
			if (depth === 0) {
				return source.slice(braceStart + 1, index);
			}
		}
	}

	throw new Error(`unterminated C++ function ${signature}`);
}

function extractEnclosingBlock(source: string, anchor: string): string {
	const anchorIndex = source.indexOf(anchor);
	expect(anchorIndex, `missing block anchor ${anchor}`).toBeGreaterThanOrEqual(0);

	const braceStart = source.lastIndexOf("{", anchorIndex);
	expect(braceStart, `missing opening brace before ${anchor}`).toBeGreaterThanOrEqual(0);

	let depth = 0;
	for (let index = braceStart; index < source.length; index += 1) {
		const char = source[index];
		if (char === "{") {
			depth += 1;
		} else if (char === "}") {
			depth -= 1;
			if (depth === 0) {
				return source.slice(braceStart + 1, index);
			}
		}
	}

	throw new Error(`unterminated block around ${anchor}`);
}

function orderedIndex(source: string, patterns: string[]): number[] {
	return patterns.map((pattern) => {
		const index = source.indexOf(pattern);
		expect(index, `missing ordered pattern ${pattern}`).toBeGreaterThanOrEqual(0);
		return index;
	});
}

function expectBefore(source: string, before: string, after: string): void {
	const beforeIndex = source.indexOf(before);
	const afterIndex = source.indexOf(after);
	expect(beforeIndex, `missing before pattern ${before}`).toBeGreaterThanOrEqual(0);
	expect(afterIndex, `missing after pattern ${after}`).toBeGreaterThanOrEqual(0);
	expect(beforeIndex, `${before} should appear before ${after}`).toBeLessThan(afterIndex);
}

function expectAllAfter(source: string, anchor: string, patterns: string[]): void {
	const anchorIndex = source.indexOf(anchor);
	expect(anchorIndex, `missing anchor pattern ${anchor}`).toBeGreaterThanOrEqual(0);
	for (const pattern of patterns) {
		const patternIndex = source.indexOf(pattern);
		expect(patternIndex, `missing pattern ${pattern}`).toBeGreaterThanOrEqual(0);
		expect(patternIndex, `${pattern} should appear after ${anchor}`).toBeGreaterThan(anchorIndex);
	}
}

describe("workshop runtime state policy", () => {
	const systemScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/System.psc");
	const lootManScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/LootMan.psc");
	const propertiesHeader = readWorkspaceFile("commonlibf4-plugin/src/properties.h");
	const propertiesSource = readWorkspaceFile("commonlibf4-plugin/src/properties.cpp");
	const papyrusBinding = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman.cpp");
	const papyrusInternal = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_internal.h");
	const hooksSource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_hooks.cpp");
	const stateSource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_state.cpp");
	const userGuide = readWorkspaceFile("docs/user-guide.md");

	it("mirrors lifecycle properties into native cached state and clears stale cache on initialize", () => {
		expect(propertiesHeader).toContain("is_installed");
		expect(propertiesHeader).toContain("is_initialized");
		expect(propertiesHeader).toContain("is_uninstalled");

		expect(propertiesSource).toContain('propertyName = "IsInstalled"');
		expect(propertiesSource).toContain("updates[is_installed] = GetBoolProperty(propertyName)");
		expect(propertiesSource).toContain('propertyName = "IsInitialized"');
		expect(propertiesSource).toContain("updates[is_initialized] = GetBoolProperty(propertyName)");
		expect(propertiesSource).toContain('propertyName = "IsUninstalled"');
		expect(propertiesSource).toContain("updates[is_uninstalled] = GetBoolProperty(propertyName)");

		const initialize = extractCppFunction(propertiesSource, "void Initialize()");
		expect(initialize).toContain("papyrusProperties.clear()");
		expect(initialize).toContain("lootManWorkshopRef = nullptr");
	});

	it("publishes initialized and uninstalled lifecycle state before native hooks need it", () => {
		const initialize = extractPapyrusFunction(systemScript, "Initialize");
		const initializeOrder = orderedIndex(initialize, [
			"properties.IsInitialized = true",
			"properties.IsNotInitialized = false",
			'LTMN2:LootMan.OnUpdateLootManProperty("")',
			"LogSystemEvent(\"initialize_completed\"",
		]);
		expect(initializeOrder).toEqual([...initializeOrder].sort((a, b) => a - b));

		const uninstall = extractPapyrusFunction(systemScript, "Uninstall");
		const uninstallOrder = orderedIndex(uninstall, [
			"properties.IsUninstalled = true",
			"properties.IsNotUninstalled = false",
			"properties.IsInstalled = false",
			"properties.IsInitialized = false",
			'LTMN2:LootMan.OnUpdateLootManProperty("")',
			'LTMN2:LootMan.ResetWorkshopRuntimeState("uninstall")',
			"properties.Stop()",
			"self.Stop()",
		]);
		expect(uninstallOrder).toEqual([...uninstallOrder].sort((a, b) => a - b));
	});

	it("exposes an explicit native workshop runtime reset to Papyrus", () => {
		expect(lootManScript).toContain(
			'Function ResetWorkshopRuntimeState(string context = "") global native',
		);
		expect(papyrusInternal).toMatch(/void ResetWorkshopRuntimeState\(\s*std::monostate,\s*RE::BSFixedString context\);/);
		expect(papyrusBinding).toContain('"ResetWorkshopRuntimeState"sv');
		expect(papyrusBinding).toContain("&ResetWorkshopRuntimeState");
		expect(hooksSource).toContain("void ResetWorkshopRuntimeState(std::monostate, BSFixedString context)");
		expect(hooksSource).toContain("ClearWorkshopRuntimeState(context.c_str())");
	});

	it("keeps workshop material hooks inert until lifecycle state and remembered links are active", () => {
		expect(hooksSource).toContain("std::atomic_bool hasRememberedWorkshopSupplyLinks");
		expect(hooksSource).toContain("bool HasRememberedWorkshopSupplyLinks()");

		const activeGate = extractCppFunction(hooksSource, "bool CanUseWorkshopMaterialAugmentation()");
		expect(activeGate).toContain("if (!HasRememberedWorkshopSupplyLinks())");
		expect(activeGate).toContain("properties::GetBool(properties::is_installed, false)");
		expect(activeGate).toContain("properties::GetBool(properties::is_initialized, false)");
		expect(activeGate).toContain("properties::GetBool(properties::is_uninstalled, false)");
		expectBefore(activeGate, "if (!HasRememberedWorkshopSupplyLinks())", "properties::GetBool(properties::is_installed, false)");

		const rememberLink = extractCppFunction(hooksSource, "void RememberWorkshopSupplyLink(");
		const rememberMutation = extractEnclosingBlock(
			rememberLink,
			"rememberedWorkshopSupplyLinks[targetLocation->formID] = lootManWorkshop->formID",
		);
		expect(rememberMutation).toContain("std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock)");
		expect(rememberMutation).toContain("hasRememberedWorkshopSupplyLinks.store(true");
		expectBefore(
			rememberLink,
			"rememberedWorkshopSupplyLinks[targetLocation->formID] = lootManWorkshop->formID",
			"hasRememberedWorkshopSupplyLinks.store(true",
		);

		const forgetLink = extractCppFunction(hooksSource, "void ForgetWorkshopSupplyLink(");
		const forgetMutation = extractEnclosingBlock(
			forgetLink,
			"removed = rememberedWorkshopSupplyLinks.erase(targetLocation->formID) > 0",
		);
		expect(forgetMutation).toContain("std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock)");
		expect(forgetLink).toContain("hasLinks = !rememberedWorkshopSupplyLinks.empty()");
		expect(forgetMutation).toContain("hasRememberedWorkshopSupplyLinks.store(hasLinks");
		expectBefore(
			forgetLink,
			"hasLinks = !rememberedWorkshopSupplyLinks.empty()",
			"hasRememberedWorkshopSupplyLinks.store(hasLinks",
		);

		const populateContainers = extractCppFunction(hooksSource, "void HookedPopulateLinkedWorkshopContainers(");
		expect(populateContainers).toContain("if (!originalPopulateLinkedWorkshopContainers)");
		expectBefore(populateContainers, "originalPopulateLinkedWorkshopContainers", "if (!CanUseWorkshopMaterialAugmentation())");
		expectAllAfter(populateContainers, "if (!CanUseWorkshopMaterialAugmentation())", [
			"LogSharedWorkshopContainerHookProbe",
			"AddRememberedLootManWorkshopSharedContainer",
		]);

		const componentCount = extractCppFunction(hooksSource, "bool HookedComponentCountHelper(");
		expectBefore(componentCount, "originalComponentCountHelper", "if (CanUseWorkshopMaterialAugmentation())");
		expectBefore(componentCount, "if (CanUseWorkshopMaterialAugmentation())", "ApplyRememberedWorkshopMaterialCount");

		const directCount = extractCppFunction(hooksSource, "std::int32_t HookedDirectComponentCount(");
		expectBefore(directCount, "originalDirectComponentCount", "if (!CanUseWorkshopMaterialAugmentation())");
		expectBefore(directCount, "if (!CanUseWorkshopMaterialAugmentation())", "ApplyRememberedWorkshopDirectComponentCount");

		const buildResourceCheck = extractCppFunction(hooksSource, "bool HookedWorkshopBuildResourceCheck(");
		expect(buildResourceCheck).toContain("if (!originalWorkshopBuildResourceCheck)");
		expectBefore(buildResourceCheck, "originalWorkshopBuildResourceCheck", "if (!CanUseWorkshopMaterialAugmentation())");
		expectAllAfter(buildResourceCheck, "if (!CanUseWorkshopMaterialAugmentation())", [
			"CaptureWorkshopRecipePointerProbe(recipe)",
			"EvaluateWorkshopResourceStatus(recipeProbe, owner)",
			"UpdatePendingWorkshopBuildConsumption",
		]);

		const consumeComponent = extractCppFunction(hooksSource, "void HookedWorkshopConsumeComponent(");
		expect(consumeComponent).toContain("if (!originalWorkshopConsumeComponent)");
		expectBefore(consumeComponent, "if (!CanUseWorkshopMaterialAugmentation())", "ResolveActiveRememberedWorkshop()");
		expectBefore(consumeComponent, "if (!CanUseWorkshopMaterialAugmentation())", "BuildRememberedWorkshopMaterialConsumptionPlan");

		const removeComponents = extractCppFunction(hooksSource, "void HookedRemoveComponents(");
		expectBefore(removeComponents, "if (!CanUseWorkshopMaterialAugmentation())", "BuildRememberedWorkshopMaterialConsumptionPlan");
	});

	it("defers recipe capture and resource evaluation until original menu results can be changed", () => {
		const resourceStatus = extractCppFunction(hooksSource, "std::uint32_t HookedWorkshopResourceStatus(");
		expect(resourceStatus).toContain("originalWorkshopResourceStatus");
		expect(resourceStatus).toContain("if (!CanUseWorkshopMaterialAugmentation())");
		expect(resourceStatus).toContain("if (originalStatus != kWorkshopResourceStatusMissingResources)");
		expect(resourceStatus).toContain("CaptureSelectedWorkshopRecipeProbe()");
		expect(resourceStatus).toContain("EvaluateWorkshopResourceStatus(selectedRecipe)");
		expect(resourceStatus.indexOf("originalWorkshopResourceStatus")).toBeLessThan(
			resourceStatus.indexOf("CaptureSelectedWorkshopRecipeProbe()"),
		);
		expect(resourceStatus.indexOf("if (!CanUseWorkshopMaterialAugmentation())")).toBeLessThan(
			resourceStatus.indexOf("CaptureSelectedWorkshopRecipeProbe()"),
		);
		expect(resourceStatus.indexOf("originalStatus != kWorkshopResourceStatusMissingResources")).toBeLessThan(
			resourceStatus.indexOf("EvaluateWorkshopResourceStatus(selectedRecipe)"),
		);

		const availability = extractCppFunction(hooksSource, "bool HookedWorkshopMenuAvailability(");
		expect(availability).toContain("originalWorkshopMenuAvailability");
		expect(availability).toContain("if (!CanUseWorkshopMaterialAugmentation())");
		expect(availability).toContain("if (!result || !outValue || originalOut != 0)");
		expect(availability).toContain("CaptureWorkshopMenuRecipeProbe(row, menuResult)");
		expect(availability).toContain("EvaluateWorkshopResourceStatus(selectedRecipe)");
		expect(availability.indexOf("originalWorkshopMenuAvailability")).toBeLessThan(
			availability.indexOf("CaptureWorkshopMenuRecipeProbe(row, menuResult)"),
		);
		expect(availability.indexOf("if (!CanUseWorkshopMaterialAugmentation())")).toBeLessThan(
			availability.indexOf("CaptureWorkshopMenuRecipeProbe(row, menuResult)"),
		);
		expect(availability.indexOf("originalOut != 0")).toBeLessThan(
			availability.indexOf("EvaluateWorkshopResourceStatus(selectedRecipe)"),
		);
	});

	it("preserves active linked-workshop augmentation paths after the cheap gates pass", () => {
		const populateContainers = extractCppFunction(hooksSource, "void HookedPopulateLinkedWorkshopContainers(");
		expectAllAfter(populateContainers, "if (!CanUseWorkshopMaterialAugmentation())", [
			"AddRememberedLootManWorkshopSharedContainer(containers, currentLocation)",
		]);

		const componentCount = extractCppFunction(hooksSource, "bool HookedComponentCountHelper(");
		expectAllAfter(componentCount, "if (CanUseWorkshopMaterialAugmentation())", [
			"ApplyRememberedWorkshopMaterialCount",
		]);

		const directCount = extractCppFunction(hooksSource, "std::int32_t HookedDirectComponentCount(");
		expectAllAfter(directCount, "if (!CanUseWorkshopMaterialAugmentation())", [
			"ApplyRememberedWorkshopDirectComponentCount",
		]);

		const resourceStatus = extractCppFunction(hooksSource, "std::uint32_t HookedWorkshopResourceStatus(");
		expectAllAfter(resourceStatus, "if (originalStatus != kWorkshopResourceStatusMissingResources)", [
			"adjustedStatus = 0",
			"evaluation.applied = true",
		]);

		const availability = extractCppFunction(hooksSource, "bool HookedWorkshopMenuAvailability(");
		expectAllAfter(availability, "if (!result || !outValue || originalOut != 0)", [
			"*outValue = 1",
			"evaluation.applied = true",
		]);

		const buildResourceCheck = extractCppFunction(hooksSource, "bool HookedWorkshopBuildResourceCheck(");
		expectAllAfter(buildResourceCheck, "if (originalResult)", [
			"adjustedResult = true",
			"UpdatePendingWorkshopBuildConsumption",
		]);

		const consumeComponent = extractCppFunction(hooksSource, "void HookedWorkshopConsumeComponent(");
		expectAllAfter(consumeComponent, "if (!CanUseWorkshopMaterialAugmentation())", [
			"BuildRememberedWorkshopMaterialConsumptionPlan",
			"originalRemoveComponents",
			"NotePendingWorkshopBuildConsumptionCall",
		]);

		const removeComponents = extractCppFunction(hooksSource, "void HookedRemoveComponents(");
		expectAllAfter(removeComponents, "if (!CanUseWorkshopMaterialAugmentation())", [
			"BuildRememberedWorkshopMaterialConsumptionPlan",
			"originalRemoveComponents",
			"plan.consumeFromLootMan > 0",
		]);
	});

	it("clears remembered links and diagnostics at preload and explicit reset boundaries", () => {
		expect(hooksSource).toContain("void ClearWorkshopRuntimeState(const char* context)");
		expect(hooksSource).toContain("std::atomic<std::uint64_t> workshopRuntimeStateGeneration");
		expect(hooksSource).toContain("std::uint64_t generation = 0");

		const clearRuntimeState = extractCppFunction(hooksSource, "void ClearWorkshopRuntimeState(const char* context)");
		const generationResetBumps = clearRuntimeState.match(/workshopRuntimeStateGeneration\.fetch_add/g) ?? [];
		expect(generationResetBumps.length).toBeGreaterThanOrEqual(2);
		const clearRememberedLinks = extractEnclosingBlock(clearRuntimeState, "rememberedWorkshopSupplyLinks.clear()");
		expect(clearRememberedLinks).toContain("std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock)");
		expect(clearRememberedLinks).toContain("hasRememberedWorkshopSupplyLinks.store(false");
		expect(clearRuntimeState).toContain("pendingWorkshopBuildConsumption = {}");
		expect(hooksSource).toContain("loggedWorkshopResourceStatusKeys.clear()");
		expect(hooksSource).toContain("loggedWorkshopMenuAvailabilityKeys.clear()");

		const currentPending = extractCppFunction(hooksSource, "bool HasCurrentPendingWorkshopBuildConsumption()");
		expect(currentPending).toContain("pendingWorkshopBuildConsumption.generation");
		expect(currentPending).toContain("pendingWorkshopBuildConsumption = {}");

		const updatePending = extractCppFunction(hooksSource, "void UpdatePendingWorkshopBuildConsumption(");
		expect(hooksSource).toContain("std::uint64_t runtimeStateGeneration,");
		expect(updatePending).toContain("pendingWorkshopBuildConsumption.generation = runtimeStateGeneration");

		const hasPending = extractCppFunction(hooksSource, "bool HasPendingWorkshopBuildConsumption(");
		expect(hasPending).toContain("HasCurrentPendingWorkshopBuildConsumption()");

		const notePending = extractCppFunction(hooksSource, "void NotePendingWorkshopBuildConsumptionCall(");
		expect(notePending).toContain("HasCurrentPendingWorkshopBuildConsumption()");

		const resetTransientState = extractCppFunction(stateSource, "void ResetTransientState()");
		expect(resetTransientState).toContain("ClearWorkshopRuntimeState(\"preload\")");
	});

	it("keeps diagnostic-only hooks behind the compile-time diagnostics gate", () => {
		expect(hooksSource).toContain("inline constexpr bool kVerboseWorkshopMaterialDiagnostics = false");

		const rebuildSupply = extractCppFunction(hooksSource, "void HookedRebuildWorkshopSupply(");
		expectBefore(rebuildSupply, "if (kVerboseWorkshopMaterialDiagnostics)", "LogRebuildWorkshopSupplyProbe");

		const checkPlacement = extractCppFunction(hooksSource, "void HookedWorkshopCheckAndSetPlacement(");
		expectBefore(checkPlacement, "if (!kVerboseWorkshopMaterialDiagnostics)", "CaptureSelectedWorkshopRecipeProbe()");
		expectBefore(checkPlacement, "if (!kVerboseWorkshopMaterialDiagnostics)", "EvaluateWorkshopResourceStatus(beforeRecipe)");
		expectBefore(checkPlacement, "if (!kVerboseWorkshopMaterialDiagnostics)", "CapturePlacementItemProbe()");

		const menuSelect = extractCppFunction(hooksSource, "bool HookedWorkshopMenuSelect(");
		expectBefore(menuSelect, "if (!kVerboseWorkshopMaterialDiagnostics)", "CaptureSelectedWorkshopRecipeProbe()");
		expectBefore(menuSelect, "if (!kVerboseWorkshopMaterialDiagnostics)", "EvaluateWorkshopResourceStatus(after)");

		const startPlacement = extractCppFunction(hooksSource, "void HookedWorkshopStartPlacement(");
		expectBefore(startPlacement, "if (!kVerboseWorkshopMaterialDiagnostics)", "CaptureSelectedWorkshopRecipeProbe()");
		expectBefore(startPlacement, "if (!kVerboseWorkshopMaterialDiagnostics)", "EvaluateWorkshopResourceStatus(selectedRecipe)");
		expectBefore(startPlacement, "if (!kVerboseWorkshopMaterialDiagnostics)", "CapturePlacementItemProbe()");

		const objectCount = extractCppFunction(hooksSource, "bool HookedWorkshopObjectCount(");
		expectBefore(objectCount, "if (kVerboseWorkshopMaterialDiagnostics)", "LogWorkshopObjectCountProbe");

		const currentObjectCount = extractCppFunction(hooksSource, "std::uint32_t HookedCurrentWorkshopObjectCount(");
		expectBefore(currentObjectCount, "if (kVerboseWorkshopMaterialDiagnostics)", "LogCurrentWorkshopObjectCountProbe");
	});

	it("documents the static ESP workshop menu removal limit", () => {
		expect(userGuide).toContain("static ESP workshop menu records");
		expect(userGuide).toContain("MCM uninstall clears LootMan runtime state");
		expect(userGuide).toMatch(/cannot remove those already-loaded\s+static workshop menu records/);
	});
});
