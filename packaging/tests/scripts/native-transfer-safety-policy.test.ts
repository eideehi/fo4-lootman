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

	it("keeps a world reference when the available post-add count proves the destination is empty", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_loot_actions.cpp");
		const start = source.indexOf("bool TryLootWorldReference(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start, source.indexOf("bool TryLootDeferredActivationAmmoReference(", start));
		expect(body).toContain("const bool observedDestIncrease = gotBefore && gotAfter && afterCount > beforeCount;");
		expect(body).toContain("!gotAfter || (!gotBefore && afterCount > 0)");
		const successBranch = body.slice(
			body.indexOf("if (observedDestIncrease || verificationInconclusive)"),
			body.indexOf('REX::WARN(\n\t\t\t"source=native component=loot_nearby event=world_transfer_verification_failed'),
		);
		expect(successBranch).toContain("if (observedDestIncrease || verificationInconclusive)");
		expect(successBranch).toContain("FinalizeWorldPickup(std::monostate{}, ref);");
		expect(successBranch).toContain("return true;");
		expect(body).toContain("got_before={}");
		expect(body).toContain("got_after={}");
	});

	it("charges locked-container early exits to the object and category budgets", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_nearby_looting.cpp");
		const start = source.indexOf("std::vector<std::int32_t> LootNearbyEnabledReferences(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start);
		const invalidCapacity = body.slice(
			body.indexOf("if (capacity.enabled && !capacity.valid)"),
			body.indexOf("if (!TryUnlockContainerForLooting("),
		);
		const unlockFailure = body.slice(
			body.indexOf("if (!TryUnlockContainerForLooting("),
			body.indexOf("movedStacks = TransferLootableInventoryItemsImpl("),
		);
		expect(invalidCapacity).toContain("markProcessed();");
		expect(unlockFailure).toContain("markProcessed();");
	});

	it("does not account unverified deferred ammo as delivered to a non-player destination", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_loot_actions.cpp");
		const start = source.indexOf("bool TryLootDeferredActivationAmmoReference(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start, source.indexOf("bool TryLootActivationReference(", start));
		const skippedRelay = body.slice(
			body.indexOf("if (movedCount > 0 && dest != player && !observedPlayerDelta)"),
			body.indexOf("if (movedCount > 0 && dest != player && observedPlayerDelta)"),
		);
		expect(skippedRelay).toContain("movedCount = 0;");
		expect(body.indexOf("movedCount = 0;", body.indexOf("deferred_activation_relay_skipped"))).toBeLessThan(
			body.indexOf("if (movedCount > 0 && capacity)"),
		);
	});
});
