import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

/** Slice a C++ free function body from its signature up to the start of the next known function. */
function sliceBetween(source: string, startNeedle: string, endNeedle: string): string {
	const start = source.indexOf(startNeedle);
	expect(start, `missing ${startNeedle}`).toBeGreaterThanOrEqual(0);
	const end = source.indexOf(endNeedle, start + startNeedle.length);
	expect(end, `missing ${endNeedle} after ${startNeedle}`).toBeGreaterThan(start);
	return source.slice(start, end);
}

function countOccurrences(haystack: string, needle: string): number {
	let count = 0;
	let index = haystack.indexOf(needle);
	while (index !== -1) {
		count += 1;
		index = haystack.indexOf(needle, index + needle.length);
	}
	return count;
}

describe("nameless item looting policy", () => {
	const validationSource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_validation.cpp");
	const transferSource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_inventory_transfer.cpp");
	const nearbySource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_nearby_looting.cpp");

	it("rejects a nameless base form on the inventory loot path", () => {
		// Creature equipment (SkinFeralGhoul and the other skins) ships with no FULL field and leaves the
		// non-playable flag clear, so IsPlayable does not catch it; without this gate the same object is
		// rejected on the ground but looted out of a corpse.
		const gate = sliceBetween(
			validationSource,
			"bool IsValidInventoryItem(",
			"bool IsLegendaryOnlyExceptionArmor(",
		);
		expect(gate).toContain("if (TryIsNamedFormSafe(form, named) && !named)");
		// The rejection has to be a real early-out, not just a probe whose result is discarded.
		const rejectAt = gate.indexOf("if (TryIsNamedFormSafe(form, named) && !named)");
		expect(gate.slice(rejectAt)).toContain("return false;");
		// Ordered after the cheap flag tests so they short-circuit ahead of the name read on the hot path.
		// Pin their existence first: indexOf returns -1 for a deleted test, and rejectAt > -1 would still
		// satisfy the ordering comparison, so the ordering assertions alone cannot prove they are there.
		expect(gate).toContain("if (info.dropped) return false;");
		expect(gate).toContain("info.questItem");
		expect(rejectAt).toBeGreaterThan(gate.indexOf("if (info.dropped) return false;"));
		expect(rejectAt).toBeGreaterThan(gate.indexOf("info.questItem"));
	});

	it("treats an unreadable name as named so a faulting probe never deletes loot", () => {
		const helper = sliceBetween(validationSource, "bool TryIsNamedFormSafe(", "bool IsValidInventoryItem(");
		expect(helper).toContain("outResult = !TESFullName::GetFullName(*form).empty();");
		// Own SEH frame: the enclosing TryIsLootableInventoryItemSafe guard would turn a fault here into
		// "not lootable", which is the opposite of the conservative bias inconclusive probes must keep.
		expect(helper).toContain("__except (SehFilterRecoverable(GetExceptionCode()))");
		const exceptBlock = sliceBetween(helper, "__except (SehFilterRecoverable(GetExceptionCode()))", "#else");
		expect(exceptBlock).toContain("return false;");
		// A failed probe must not write a verdict into outResult.
		expect(exceptBlock).not.toContain("outResult");

		// The caller starts from "named" and only rejects when the probe actually succeeded.
		const gate = sliceBetween(
			validationSource,
			"bool IsValidInventoryItem(",
			"bool IsLegendaryOnlyExceptionArmor(",
		);
		expect(gate).toContain("bool named = true;");
		expect(gate).toContain("if (TryIsNamedFormSafe(form, named) && !named)");
		expect(gate).not.toContain("!TryIsNamedFormSafe(");
	});

	it("keeps the world-path nameless rejection in IsValidObject", () => {
		const world = sliceBetween(validationSource, "bool IsValidObject(", "bool IsAllowedUniqueItem(");
		// Both exits keep the check: the no-extra-list fast path and the final gate.
		expect(countOccurrences(world, "ref->GetDisplayFullName();")).toBe(2);
		expect(countOccurrences(world, "if (!name || strlen(name) == 0)")).toBe(2);
	});

	it("keeps the name gate off the shared form path used by the world scan", () => {
		// IsValidForm and IsLootableForm are shared with the nearby world scan, which runs ACTI and FLOR
		// base forms through them. Fallout4.esm alone ships 685 playable activators with no FULL field, so a
		// name gate there would disable activator looting outright.
		const validForm = sliceBetween(validationSource, "bool IsValidForm(", "bool TryIsValidFormSafe(");
		expect(validForm).not.toContain("TryIsNamedFormSafe");
		expect(validForm).not.toContain("GetFullName");
		const lootableForm = sliceBetween(validationSource, "bool IsLootableForm(", "bool TryIsLootableFormSafe(");
		expect(lootableForm).not.toContain("TryIsNamedFormSafe");
		expect(lootableForm).not.toContain("GetFullName");

		// The helper stays confined to its definition plus the single inventory-item call site.
		expect(countOccurrences(validationSource, "TryIsNamedFormSafe(")).toBe(2);
	});

	it("keeps IsValidInventoryItem reachable only from the two inventory scans", () => {
		// Reachability is what makes the narrow placement safe: IsValidInventoryItem runs only inside
		// TryIsLootableInventoryItemSafe, whose callers are HasLootableItem and the transfer loop.
		// Definition plus the two branches of the SEH wrapper (_MSC_VER and the portable fallback).
		expect(countOccurrences(validationSource, "IsValidInventoryItem(")).toBe(3);
		const safeWrapper = sliceBetween(
			validationSource,
			"bool TryIsLootableInventoryItemSafe(",
			"bool TryGetStackExtraSafe(",
		);
		expect(countOccurrences(safeWrapper, "IsValidInventoryItem(form, info, matchCache)")).toBe(2);
		expect(transferSource).toContain("TryIsLootableInventoryItemSafe(form, stackInfo, props, &matchCache, lootableStack)");
		// The world scan never reaches the inventory-item gate directly.
		expect(nearbySource).not.toContain("IsValidInventoryItem");
		expect(nearbySource).not.toContain("TryIsLootableInventoryItemSafe");
		expect(nearbySource).not.toContain("TryIsNamedFormSafe");
	});
});
