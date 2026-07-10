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
		expect(source).toContain("it->second.ref == ref && it->second.formID == ref->formID");
		expect(source).toContain("recentlyLootedWorldRefs.erase(it);");
		expect(source).toContain("recentlyLootedWorldRefs.clear();");
	});

	it("does not reuse instance extra after an indeterminate destination add", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_transfer_ops.cpp");
		const start = source.indexOf("bool TryMoveInventoryItemPreservingStackExtraSafe(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start);
		expect(body).toContain("reason=dest_add_outcome_unknown");
		expect(body).not.toContain("TryAddInventoryItemSafe(src");
	});
});
