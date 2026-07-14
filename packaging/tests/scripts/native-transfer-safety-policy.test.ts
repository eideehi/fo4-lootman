import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

describe("native transfer safety policy", () => {
	it("keeps world-reference suppression until transient state resets", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_state.cpp");
		expect(source).toContain("std::unordered_map<std::uint64_t, RecentlyLootedWorldRefEntry> recentlyLootedWorldRefs");
		expect(source).not.toContain("kRecentLootStaleTimeout");
		expect(source).toContain("if (it->second.formID != ref->formID)");
		expect(source).toContain("if (!ref->IsCreated())");
		expect(source).toContain("it->second.ref = ref;");
		expect(source).toContain("it->second.ref == ref");
		expect(source).toContain("recentlyLootedWorldRefs.erase(it);");
		expect(source).toContain("recentlyLootedWorldRefs.clear();");
	});

	it("restores the source only after verifying the destination add did not commit", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_transfer_ops.cpp");
		const start = source.indexOf("bool TryMoveInventoryItemPreservingStackExtraSafe(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start);
		expect(body).toContain("reason=dest_add_outcome_unknown");
		expect(body).toContain("event=instance_preserving_move_rolled_back");
		expect(body).toContain("reason=dest_add_and_source_restore_failed");
		// The source restore must be gated on a verified-unchanged destination count;
		// an unconditional restore after an indeterminate add can duplicate the extra.
		expect(body).toContain("gotDestBefore && gotDestAfter && destAfter == destBefore");
		const restoreAt = body.indexOf("TryAddInventoryItemSafe(src");
		expect(restoreAt).toBeGreaterThan(body.indexOf("destAfter == destBefore"));
	});

	it("finalizes world-reference pickup when the add succeeds but both count probes are unavailable", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_loot_actions.cpp");
		const start = source.indexOf("bool TryLootWorldReference(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start, source.indexOf("bool TryLootDeferredActivationAmmoReference(", start));
		expect(body).toContain("const bool observedDestIncrease = gotBefore && gotAfter && afterCount > beforeCount;");
		expect(body).toContain("const bool countUnavailable = !gotBefore && !gotAfter;");
		expect(body).toContain("if (observedDestIncrease || countUnavailable)");
		expect(body.indexOf("FinalizeWorldPickup")).toBeGreaterThan(body.indexOf("observedDestIncrease || countUnavailable"));
	});
});
