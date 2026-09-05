import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

describe("native transfer safety policy", () => {
	it("keeps the reusable runtime probe facility gated off by default", () => {
		const probeSource = readWorkspaceFile("commonlibf4-plugin/src/runtime_probe.cpp");
		const logSettingsSource = readWorkspaceFile("commonlibf4-plugin/src/log_settings.cpp");

		// The gate is a conjunction: the config flag alone must not enable
		// emission without the trace log level, and vice versa.
		expect(probeSource).toContain("log_settings::IsRuntimeProbeEnabled() &&");
		expect(probeSource).toContain("spdlog::level::trace");
		expect(logSettingsSource).toContain('find("runtimeProbe")');
		expect(logSettingsSource).toContain("std::atomic<bool> runtimeProbeEnabled = false");
		// Initialize() must start from the false default and publish the parsed
		// value through the atomic, not bypass it.
		expect(logSettingsSource).toContain("bool enableRuntimeProbe = false");
		expect(logSettingsSource).toContain("runtimeProbeEnabled.store(enableRuntimeProbe");
	});

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

	it("plays the deferred ammo cue only after the player-side delta probe", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_loot_actions.cpp");
		const start = source.indexOf("bool TryLootDeferredActivationAmmoReference(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start, source.indexOf("bool TryLootActivationReference(", start));
		// The cue must sit behind the verification, not behind the bare `activated`
		// check: an activation the engine accepts without depositing anything would
		// otherwise sound like a pickup.
		expect(body).toContain("if (playPickupSound && (observedPlayerDelta || !gotPlayerBefore || !gotPlayerAfter))");
		expect(body.indexOf("PlayPickUpSound")).toBeGreaterThan(
			body.indexOf("const bool gotPlayerAfter = TryGetReferenceItemCountSafe(player, object, playerAfter);"),
		);
	});

	it("gates the flora activation cue on the produce probe and defers the plain-activator verdict", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_loot_actions.cpp");
		const start = source.indexOf("bool TryLootActivationReference(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start, source.indexOf("inline constexpr float kNearbyContainerTrapProbeRadius", start));
		// The produce probe must not be gated on a caller wanting capacity or
		// notification data; it is the only evidence a flora activation yielded.
		expect(body).toContain("expectedItem && TryGetReferenceItemCountSafe(actionRef, expectedItem, afterCount);");
		expect(body).not.toContain("(capacity || notifyMovedItems) &&");
		// FLOR is the only synchronously verifiable case (the engine adds
		// TESFlora::produceItem inside the activation call), so only the produce
		// branch may decide a yield.
		expect(body).toContain(
			"activationYielded ? ActivationOutcome::kYielded : ActivationOutcome::kActivatedNoYield);",
		);
		// A plain activator delivers from an OnActivate script the VM dispatches
		// asynchronously, so it must reach the cue and the return with the yield still
		// assumed. Flipping this default silences every legitimate activator pickup
		// (harvestable eggs, caps stashes, noodle cups) and undercounts successes.
		expect(body).toContain("bool activationYielded = true;");
		expect(body).toContain("reportOutcome(ActivationOutcome::kAwaitingEvidence);");
		// Re-introducing a synchronous probe for plain activators is the exact defect
		// this design replaced: nothing readable here can observe an asynchronous
		// delivery, so any such probe classifies real pickups as no-yield.
		expect(body).not.toContain("TryGetDestinationInventoryEntryCountSafe");
		expect(body).not.toContain("TryIsActivationReferenceConsumedSafe");
		expect(body).toContain("if (playPickupSound && activationYielded)");
		expect(body.indexOf("PlayPickUpSound")).toBeGreaterThan(body.indexOf("bool activationYielded = true;"));
		// The function must report the yield, not an unconditional success, so a flora
		// activation the engine accepted without producing stops counting as a looted
		// object.
		expect(body).toContain("return activationYielded;");
		expect(body).not.toContain("return true;");
		// Every gate ahead of the activation has to report "not attempted", so a
		// destination or settings failure never reaches strike accounting.
		expect(body).toContain("reportOutcome(ActivationOutcome::kNotAttempted);");
		expect(body.indexOf("reportOutcome(ActivationOutcome::kNotAttempted);")).toBeLessThan(
			body.indexOf("reason=no_produce_item"),
		);
	});

	it("keeps activation no-yield suppression bounded and identity-checked", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_state.cpp");
		expect(source).toContain("std::unordered_map<std::uint64_t, NoYieldActivationEntry> noYieldActivationRefs");
		// Unlike the world-reference marker this one must expire, so a plant that
		// legitimately respawns mid-session is looted again.
		expect(source).toContain("kNoYieldActivationStrikeLimit");
		expect(source).toContain("kNoYieldActivationCooldown");
		expect(source).toContain("CleanupStaleNoYieldActivationsLocked");
		expect(source).toContain("kNoYieldActivationCleanupMaxPerPass");

		const suppressionStart = source.indexOf("bool IsSuppressedNoYieldActivationRef(");
		expect(suppressionStart).toBeGreaterThanOrEqual(0);
		const suppression = source.slice(
			suppressionStart,
			source.indexOf("bool RecordActivationYieldOutcome(", suppressionStart),
		);
		// Recycled handles must not inherit another reference's strikes.
		expect(suppression).toContain("if (it->second.formID != formId)");
		expect(suppression).toContain("noYieldActivationRefs.erase(it);");
		expect(suppression).toContain("it->second.strikes = kNoYieldActivationStrikeLimit - 1;");
		// The below-limit early-out is the only thing keeping a single transient miss
		// from suppressing the reference; without it the first recorded miss wins.
		expect(suppression).toContain("if (it->second.strikes < kNoYieldActivationStrikeLimit)");
		expect(source).toContain("inline constexpr std::uint32_t kNoYieldActivationStrikeLimit = 3;");
		// The retry interval must scale with the recorded backoff level rather than
		// being the base cooldown forever.
		expect(suppression).toContain("GetNoYieldActivationCooldown(it->second.backoffLevel)");
		expect(suppression).toContain("it->second.retryGranted = true;");

		const resetStart = source.indexOf("void ResetTransientState()");
		expect(resetStart).toBeGreaterThanOrEqual(0);
		const reset = source.slice(resetStart);
		expect(reset).toContain("noYieldActivationRefs.clear();");
		expect(reset).toContain("lastNoYieldActivationCleanupAt = {};");
	});

	it("clears on a yield, strikes on a miss, and arms suppression only at the limit", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_state.cpp");
		const start = source.indexOf("bool RecordActivationYieldOutcome(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start, source.indexOf("void MarkActivationAwaitingYieldEvidence(", start));
		// A yield must drop the entry outright. Without the erase, a reference that
		// starts producing again stays suppressed until the stale timeout.
		expect(body).toContain("if (yielded)");
		expect(body).toContain("noYieldActivationRefs.erase(key);");
		// The strike counter must actually advance, or suppression can never arm and
		// the whole gate is dead code.
		expect(body).toContain("++entry.strikes;");
		// Pin the comparison operator: ">" would demand a fourth miss and "> 0" would
		// suppress on the first one.
		expect(body).toContain("suppressed = entry.strikes >= kNoYieldActivationStrikeLimit;");
		// Re-arming after a granted retry that still yielded nothing must lengthen the
		// next cooldown, or a hopeless reference keeps costing one activation per fixed
		// interval and, with nearest-first ordering, starves farther loot.
		expect(body).toContain("if (suppressed && entry.retryGranted)");
		expect(body).toContain("entry.backoffLevel + 1");
		// A recorded verdict closes any pending cross-pass mark.
		expect(body).toContain("entry.awaitingEvidencePassId = 0;");
	});

	it("settles a plain-activator verdict only from a later pass", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_state.cpp");
		const markStart = source.indexOf("void MarkActivationAwaitingYieldEvidence(");
		const settleStart = source.indexOf("bool SettleActivationYieldEvidence(");
		expect(markStart).toBeGreaterThanOrEqual(0);
		expect(settleStart).toBeGreaterThan(markStart);
		const mark = source.slice(markStart, settleStart);
		const settle = source.slice(settleStart);
		expect(mark).toContain("entry.awaitingEvidencePassId = passId;");
		// The mark only becomes evidence when a different pass laid it down; dropping
		// the pass comparison lets one pass strike its own activation immediately.
		expect(settle).toContain("it->second.awaitingEvidencePassId != 0 &&");
		expect(settle).toContain("it->second.awaitingEvidencePassId != passId");
		// A recycled key must not inherit another reference's pending mark.
		expect(settle).toContain("it->second.formID == formId &&");
		// Settling consumes the mark and records a miss, never a clear.
		expect(settle).toContain("it->second.awaitingEvidencePassId = 0;");
		expect(settle).toContain("return RecordActivationYieldOutcome(ref, false);");
	});

	it("bounds the no-yield activation map by examined entries and a hard ceiling", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_state.cpp");
		// Exponential backoff, not a fixed cooldown: pin the base, the ceiling and the
		// doubling itself.
		expect(source).toContain("inline constexpr auto kNoYieldActivationCooldown = std::chrono::seconds(45);");
		expect(source).toContain("inline constexpr auto kNoYieldActivationCooldownMax = std::chrono::minutes(10);");
		expect(source).toContain("kNoYieldActivationCooldown * (std::int64_t{ 1 } << level)");
		// The stale timeout must outlast the longest cooldown, or an entry sitting out
		// its maximum backoff is culled mid-cooldown and handed back the full retry
		// rate, which defeats the backoff entirely.
		expect(source).toContain("inline constexpr auto kNoYieldActivationStaleTimeout = std::chrono::minutes(15);");

		const cleanupStart = source.indexOf("std::size_t CleanupStaleNoYieldActivationsLocked(");
		expect(cleanupStart).toBeGreaterThanOrEqual(0);
		const cleanup = source.slice(
			cleanupStart,
			source.indexOf("void EvictOldestNoYieldActivationsLocked(", cleanupStart),
		);
		// The per-pass bound must count entries EXAMINED, not only entries removed: a
		// map full of young entries would otherwise be walked in full under the lock on
		// every pass and never shrink.
		expect(cleanup).toContain("++examinedCount;");
		expect(cleanup).toContain("examinedCount < kNoYieldActivationCleanupMaxPerPassExamined");
		expect(cleanup).toContain("visitedBuckets < kNoYieldActivationCleanupMaxPerPassBuckets");
		// ...and the walk must resume where the previous pass stopped, or everything
		// past the first bounded slice is never examined at all.
		expect(cleanup).toContain("noYieldActivationCleanupBucket = (bucket + 1) % bucketCount;");

		// An age-based expiry cannot bound the map on its own, so back it with a hard
		// entry ceiling that evicts the least recently updated entry.
		expect(source).toContain("inline constexpr std::size_t kNoYieldActivationMaxEntries = 512;");
		const evictStart = source.indexOf("void EvictOldestNoYieldActivationsLocked(");
		const evict = source.slice(
			evictStart,
			source.indexOf("NoYieldActivationEntry& GetOrCreateNoYieldActivationEntryLocked(", evictStart),
		);
		expect(evict).toContain("while (noYieldActivationRefs.size() >= kNoYieldActivationMaxEntries)");
		expect(evict).toContain("it->second.updatedAt < oldest->second.updatedAt");
		expect(evict).toContain("noYieldActivationRefs.erase(oldest);");
	});

	it("suppresses only activation refs and records their yield outcome", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_nearby_looting.cpp");
		const start = source.indexOf("std::vector<std::int32_t> LootNearbyEnabledReferences(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start);
		// The world-reference suppression stays type-gated as before; the activation
		// suppression is a separate gate for ACTI/FLOR only.
		expect(body).toContain("UsesWorldReferenceTransfer(actualFormType) && IsRecentlyLootedWorldRef(ref)");
		expect(body).toContain("IsSuppressedNoYieldActivationRef(ref)");
		expect(body).toContain(
			"if (RecordActivationYieldOutcome(ref, activationOutcome == ActivationOutcome::kYielded))",
		);
		expect(body).toContain("event=activation_suppressed reason=no_yield_strike_limit");
		expect(body.indexOf("IsSuppressedNoYieldActivationRef(ref)")).toBeLessThan(
			body.indexOf("successful = TryLootActivationReference("),
		);

		// The activation gate must carry the form-type conjunct itself. Without it the
		// settle-and-suppress pair leaks onto containers, actors and world refs.
		const suppressionGate = body.slice(
			body.indexOf("if (actualFormType == ENUM_FORM_ID::kACTI || actualFormType == ENUM_FORM_ID::kFLOR)"),
			body.indexOf("bool validForm = false;"),
		);
		expect(suppressionGate).toContain(
			"if (actualFormType == ENUM_FORM_ID::kACTI || actualFormType == ENUM_FORM_ID::kFLOR)",
		);
		expect(suppressionGate).toContain("SettleActivationYieldEvidence(ref, activationPassId)");
		expect(suppressionGate).toContain("IsSuppressedNoYieldActivationRef(ref)");

		// One pass stamp per loot pass, so the pass that activates a reference cannot
		// read its own mark back as evidence.
		expect(body).toContain("const auto activationPassId = BeginActivationYieldPass();");
		expect(body).toContain("if (activationOutcome == ActivationOutcome::kAwaitingEvidence)");
		expect(body).toContain("MarkActivationAwaitingYieldEvidence(ref, activationPassId);");
		// Strike accounting must skip the pre-activation gate rejections. Dropping this
		// guard makes a capacity-rejected plant accumulate strikes and go silent for a
		// cooldown even though the player only had to free some carry weight.
		expect(body).toContain("else if (activationOutcome != ActivationOutcome::kNotAttempted)");
	});
});
