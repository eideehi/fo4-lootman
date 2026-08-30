import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

function sliceBetween(source: string, startMarker: string, endMarker: string): string {
	const start = source.indexOf(startMarker);
	expect(start, `missing ${startMarker}`).toBeGreaterThanOrEqual(0);
	const end = source.indexOf(endMarker, start + startMarker.length);
	expect(end, `missing ${endMarker}`).toBeGreaterThan(start);
	return source.slice(start, end);
}

function expectOwnerScopeEndsBeforeRaise(source: string, ownerMarker: string): void {
	const owner = source.indexOf(ownerMarker);
	const faultRaise = source.indexOf("RaiseMatchProbeException", owner);
	const ownerScopeEnd = source.lastIndexOf("\n\t\t}", faultRaise);
	expect(owner, `missing ${ownerMarker}`).toBeGreaterThanOrEqual(0);
	expect(ownerScopeEnd, `${ownerMarker} owner scope did not end`).toBeGreaterThan(owner);
	expect(faultRaise, `${ownerMarker} fault was not re-raised after scope exit`).toBeGreaterThan(ownerScopeEnd);
}

describe("native SEH snapshot ownership policy", () => {
	const matching = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_matching.cpp");
	const validation = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_validation.cpp");
	const diagnostics = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_diagnostics.cpp");
	const actorState = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_actor_state.cpp");
	const inventoryTransfer = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_inventory_transfer.cpp");
	const playerItems = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_player_items.cpp");
	const pluginBuild = readWorkspaceFile("commonlibf4-plugin/xmake.lua");
	const commonlibBuild = readWorkspaceFile("commonlibf4-plugin/lib/commonlibf4/lib/commonlib-shared/xmake.lua");

	it("keeps engine dereferences in RAII-free leaf SEH probes without changing the exception model", () => {
		const leafProbes = sliceBetween(matching, "bool TryReadFormIDSafe(", "[[noreturn]] void RaiseMatchProbeException(");
		const reRaise = sliceBetween(matching, "[[noreturn]] void RaiseMatchProbeException(", "bool MatchesAny(");
		const tryBodies = [...leafProbes.matchAll(/__try\s*\{([\s\S]*?)\}\s*__except/g)].map((match) => match[1]);
		expect(leafProbes.match(/\b__try\b/g)).toHaveLength(4);
		expect(tryBodies).toHaveLength(4);
		expect(leafProbes.match(/__except \(SehFilterRecoverable\(GetExceptionCode\(\)\)\)/g)).toHaveLength(4);
		expect(leafProbes.match(/bool TryHasKeywordSafe\(/g)).toHaveLength(2);
		expect(leafProbes).toContain("bool TryHasReferenceKeywordSafe(");
		for (const tryBody of tryBodies) {
			expect(tryBody).not.toMatch(/\b(?:std::|auto\b|\w+(?:Lock)?Guard\b)/);
		}
		expect(leafProbes).not.toContain("GetFormIDSet");
		expect(leafProbes).not.toContain("GetKeywordListRef");
		expect(reRaise).toContain("::RaiseException(exceptionCode, 0, 0, nullptr);");
		expect(reRaise.indexOf("std::terminate();")).toBeGreaterThan(reRaise.indexOf("::RaiseException"));
		expect(commonlibBuild).toContain('"/EHsc"');
		expect(pluginBuild).not.toContain('"/EHa"');
		expect(commonlibBuild).not.toContain('"/EHa"');
	});

	it("releases matching snapshot owners before re-raising a probe fault", () => {
		const matchesAny = sliceBetween(matching, "bool MatchesAny(", "bool MatchesAnyCached(");
		expect(matchesAny).not.toContain("form->formID");
		expect(matchesAny.indexOf("TryReadFormIDSafe")).toBeLessThan(matchesAny.indexOf("GetFormIDSet"));
		expect(matchesAny).toContain("TryHasKeywordSafe");
		const keywordOwner = matchesAny.indexOf("const auto keywords = injection_data::GetKeywordListRef(key);");
		const faultRaise = matchesAny.indexOf("RaiseMatchProbeException(exceptionCode);", keywordOwner);
		const ownerScopeEnd = matchesAny.lastIndexOf("\n\t\t}", faultRaise);
		expect(keywordOwner).toBeGreaterThanOrEqual(0);
		expect(ownerScopeEnd).toBeGreaterThan(keywordOwner);
		expect(faultRaise).toBeGreaterThan(ownerScopeEnd);
	});

	it("never inserts a match-cache result when probing faults", () => {
		const cached = sliceBetween(matching, "bool MatchesAnyCached(", "bool IsIncludedQuestItem(");
		expect(cached).not.toContain("form->formID");
		expect(cached.indexOf("TryReadFormIDSafe")).toBeLessThan(cached.indexOf("const auto cacheKey"));
		expect(cached.indexOf("RaiseMatchProbeException")).toBeLessThan(cached.indexOf("const auto cacheKey"));
		expect(cached.indexOf("const bool matched = MatchesAny(form, key);")).toBeLessThan(
			cached.indexOf("cache->results.emplace(cacheKey, matched);"),
		);
	});

	it("covers every direct validation and diagnostic injection-data snapshot site", () => {
		const validObject = sliceBetween(validation, "bool IsValidObject(", "bool IsAllowedUniqueItem(");
		const validForm = sliceBetween(validation, "bool IsValidForm(", "bool TryIsValidFormSafe(");
		const diagnosticReason = sliceBetween(
			diagnostics,
			"std::string DetermineDiagnosticReason(",
			"struct DiagnosticReasonProbeContext",
		);

		for (const source of [validObject, validForm, diagnosticReason]) {
			expect(source).not.toMatch(/GetFormIDSet\([^\n]+\)->/);
			expect(source).not.toMatch(/\*injection_data::GetKeywordListRef/);
		}
		expect(validForm.indexOf("TryReadFormIDSafe")).toBeLessThan(validForm.indexOf("GetFormIDSet"));
		expect(diagnosticReason.indexOf("TryReadFormIDSafe")).toBeLessThan(diagnosticReason.indexOf("GetFormIDSet"));
		expect(validForm).toContain("TryHasKeywordSafe");
		expect(validObject).toContain("TryHasReferenceKeywordSafe");
		expect(diagnosticReason).toContain("TryHasReferenceKeywordSafe");
		expectOwnerScopeEndsBeforeRaise(validObject, "const auto excludeKeywords =");
		expectOwnerScopeEndsBeforeRaise(validForm, "const auto excludedKeywords =");
		expectOwnerScopeEndsBeforeRaise(diagnosticReason, "const auto excludedKeywords =");
	});

	it("releases the quest-alias read lock before conservatively classifying a walk fault", () => {
		const leafProbe = sliceBetween(actorState, "bool TryWalkQuestAliasArraySafe(", "QuestAliasFlags GetQuestAliasFlags(");
		const getFlags = sliceBetween(actorState, "QuestAliasFlags GetQuestAliasFlags(", "bool IsEssential(");
		const tryBody = leafProbe.match(/__try\s*\{([\s\S]*?)\}\s*__except/)?.[1] ?? "";

		expect(leafProbe).toContain("__except (SehFilterRecoverable(GetExceptionCode()))");
		expect(tryBody).not.toMatch(/\b(?:std::|auto\b|\w+(?:Lock)?Guard\b)/);
		expect(getFlags).toContain("walkSucceeded = TryWalkQuestAliasArraySafe(context);");
		const owner = getFlags.indexOf("ReadLockGuard guard(extraData->aliasArrayLock);");
		const protectedResult = getFlags.indexOf("return QuestAliasFlags{ true, true };");
		const ownerScopeEnd = getFlags.lastIndexOf("\n\t\t}", protectedResult);
		expect(ownerScopeEnd).toBeGreaterThan(owner);
		expect(protectedResult).toBeGreaterThan(ownerScopeEnd);
		expect(getFlags).not.toContain("RaiseMatchProbeException");
		expect(getFlags).not.toContain("ExecuteSehCallSafe(&WalkQuestAliasArrayCall");
	});

	it("does not propagate match-probe faults through an inventory read-lock owner", () => {
		const safeMatch = sliceBetween(matching, "bool TryMatchesAnyCachedSafe(", "bool IsIncludedQuestItem(");
		const directTransfer = sliceBetween(inventoryTransfer, "std::int32_t TransferInventoryItemsImpl(", "std::int32_t TransferLootableInventoryItemsImpl(");

		expect(safeMatch).toContain("outMatched = MatchesAnyCached(form, key, cache);");
		expect(safeMatch).toContain("__except (SehFilterRecoverable(GetExceptionCode()))");
		expect(directTransfer.match(/TryMatchesAnyCachedSafe\(/g)).toHaveLength(2);
		expect(directTransfer).not.toContain("!MatchesAnyCached(form, injection_data::include_quest_item");
	});

	it("does not classify transfer protection raw under an inventory read-lock owner", () => {
		const leafProbe = playerItems.slice(
			playerItems.indexOf("bool TryGetPlayerTransferProtectedStackCountSafe("),
		);
		const tryBody = leafProbe.match(/__try\s*\{([\s\S]*?)\}\s*__except/)?.[1] ?? "";
		const directTransfer = sliceBetween(
			inventoryTransfer,
			"std::int32_t TransferInventoryItemsImpl(",
			"std::int32_t TransferLootableInventoryItemsImpl(",
		);
		const singleMove = sliceBetween(inventoryTransfer, "void MoveInventoryItem(", "void MoveInventoryItems(");

		expect(leafProbe.indexOf("bool TryGetPlayerTransferProtectedStackCountSafe(")).toBe(0);
		expect(leafProbe).toContain("__except (SehFilterRecoverable(GetExceptionCode()))");
		expect(tryBody).toContain("outProtectedCount = GetPlayerTransferProtectedStackCount(");
		expect(tryBody).not.toMatch(/\b(?:std::|auto\b|\w+(?:Lock)?Guard\b)/);
		for (const source of [directTransfer, singleMove]) {
			expect(source).toContain("ReadLockGuard guard(inventoryList->rwLock);");
			expect(source).toContain("TryGetPlayerTransferProtectedStackCountSafe(");
			expect(source).not.toMatch(/=\s*GetPlayerTransferProtectedStackCount\(/);
		}
	});
});
