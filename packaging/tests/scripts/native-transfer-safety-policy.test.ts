import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

// An assertion that pins one spelling of a mutation only rules out that spelling, so
// the checks below that have to rule out a whole shape compare code with its comments
// stripped and its whitespace collapsed. That turns "these tokens appear somewhere in
// the function" into "the function is this expression", which is what a mutation has
// to break rather than route around.
function stripComments(source: string): string {
	return source.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/[^\n]*/g, "");
}

function code(source: string): string {
	return stripComments(source).replace(/\s+/g, " ").trim();
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

	it("gates the flora activation cue on the produce probe and keeps the plain-activator yield assumed", () => {
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
		// (harvestable eggs, caps stashes, noodle cups) and undercounts successes. No
		// later pass resolves it either, which is why the caller counts the attempt
		// instead of waiting for evidence that cannot arrive.
		expect(body).toContain("bool activationYielded = true;");
		expect(body).toContain("reportOutcome(ActivationOutcome::kAttemptedUnobservable);");
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
		// destination or settings failure never reaches attempt accounting.
		expect(body).toContain("reportOutcome(ActivationOutcome::kNotAttempted);");
		expect(body.indexOf("reportOutcome(ActivationOutcome::kNotAttempted);")).toBeLessThan(
			body.indexOf("reason=no_produce_item"),
		);
	});

	it("classifies the activation policy by the produce item rather than the form type", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_loot_actions.cpp");
		const start = source.indexOf("ActivationPolicy GetActivationPolicy(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start, source.indexOf("bool TryLootActivationReference(", start));
		// The produce probe is the only observable yield, so a FLOR base without a
		// produce item is exactly as unobservable as a plain activator and has to be
		// classified with it. Keying the policy on the form type instead would hand such
		// a reference three activations it can never justify.
		expect(body).toContain("flora && flora->produceItem");
		expect(body).toContain("? ActivationPolicy::kFloraProbe");
		expect(body).toContain(": ActivationPolicy::kPlainActivator;");
		expect(code(body)).toContain(
			"return flora && flora->produceItem ? ActivationPolicy::kFloraProbe : ActivationPolicy::kPlainActivator;",
		);

		// The classifier and the activation have to resolve the produce item the same
		// way, and the failure worth catching is the disagreement itself rather than
		// either site in isolation. If TryLootActivationReference stops deriving
		// expectedItem from the base form, every assertion about the classifier still
		// passes while the collection loop grants a plant three flora attempts and then
		// runs the unobservable plain path against it. So pin the shared resolution in
		// both bodies and the derivation that has to follow it.
		const floraResolution = "auto* flora = baseObject ? baseObject->As<TESFlora>() : nullptr;";
		expect(body).toContain(floraResolution);
		const activationStart = source.indexOf("bool TryLootActivationReference(");
		expect(activationStart).toBeGreaterThanOrEqual(0);
		const activationBody = source.slice(
			activationStart,
			source.indexOf("inline constexpr float kNearbyContainerTrapProbeRadius", activationStart),
		);
		expect(activationBody).toContain(floraResolution);
		expect(activationBody).toContain("expectedItem = flora ? flora->produceItem : nullptr;");
	});

	it("keeps activation attempt accounting bounded, identity-checked and free of settle machinery", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_state.cpp");
		const header = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_internal.h");
		expect(source).toContain("std::unordered_map<std::uint64_t, ActivationAttemptEntry> activationAttempts");
		// Unlike the world-reference marker this one must expire, so a plant that
		// legitimately respawns mid-session is looted again.
		expect(source).toContain("CleanupStaleActivationAttemptsLocked");
		expect(source).toContain("kActivationAttemptCleanupMaxPerPass");
		expect(header).toContain("bool IsActivationHeld(const RE::TESObjectREFR* ref, ActivationPolicy policy);");
		expect(header).toContain("bool RecordActivationAttempt(RE::TESObjectREFR* ref, ActivationPolicy policy);");
		expect(header).toContain("void ClearActivationAttempts(RE::TESObjectREFR* ref);");

		// The cross-pass settle machinery is gone on purpose. ActivationOutcome::kYielded
		// is unreachable without a TESFlora produce item, so for a plain activator
		// "the collector handed the reference back" was never evidence of a missing
		// delivery and the entry could only ever ratchet upwards.
		for (const removed of [
			"awaitingEvidencePassId",
			"BeginActivationYieldPass",
			"MarkActivationAwaitingYieldEvidence",
			"SettleActivationYieldEvidence",
			// A retry token is what let a gate rejection later in the same pass consume
			// the retry the query had just granted, without activating anything.
			"retryGranted",
		]) {
			expect(source).not.toContain(removed);
			expect(header).not.toContain(removed);
		}

		const holdStart = source.indexOf("bool IsActivationHeld(");
		expect(holdStart).toBeGreaterThanOrEqual(0);
		const hold = source.slice(holdStart, source.indexOf("bool RecordActivationAttempt(", holdStart));
		// The hold query must stay a pure read. A query that mutated the entry is the
		// defect this replaced: it handed back a retry, moved updatedAt and dropped the
		// count, all before any of the validity gates or the capacity gate had had their
		// say, so a pass that never activated anything could still burn the retry.
		// Binding the map through a const reference is what makes the compiler agree.
		expect(hold).toContain("const auto& attempts = activationAttempts;");
		expect(hold).toContain("const auto it = attempts.find(key);");
		expect(hold).not.toMatch(/it->second\.[A-Za-z]+\s*=[^=]/);
		expect(hold).not.toContain("= now;");
		expect(hold).not.toContain("activationAttempts.erase");
		expect(hold).not.toContain("attempts.erase");
		// Recycled handles must not inherit another reference's history.
		expect(hold).toContain("if (it->second.formID != formId)");
		// The hold is a level and not an edge: at or above the policy limit, and still
		// inside the interval the recorded backoff level asks for.
		expect(hold).toContain("if (it->second.attempts < GetActivationAttemptLimit(policy))");
		expect(hold).toContain(
			"return (now - it->second.updatedAt) < GetActivationHold(policy, it->second.backoffLevel);",
		);
		// The pins above name `it->second` and `= now;`, and a write through any other
		// accessor sidesteps both of them: `activationAttempts.at(key).updatedAt =
		// Clock::now();` satisfies every one of them and still moves the entry forward on
		// a read. Pin the shape instead. The mutable map may be named exactly once in the
		// whole body, on the const binding, and nothing in the body may assign to a member
		// of anything at all.
		const holdCode = code(hold);
		expect(holdCode.split("activationAttempts").length - 1).toBe(1);
		expect(holdCode).toContain("const auto& attempts = activationAttempts;");
		expect(holdCode).not.toMatch(/\.[A-Za-z_][A-Za-z0-9_]*\s*=[^=]/);
		expect(holdCode).not.toMatch(/->[A-Za-z_][A-Za-z0-9_]*\s*=[^=]/);

		const resetStart = source.indexOf("void ResetTransientState()");
		expect(resetStart).toBeGreaterThanOrEqual(0);
		const reset = source.slice(resetStart);
		expect(reset).toContain("activationAttempts.clear();");
		expect(reset).toContain("lastActivationAttemptCleanupAt = {};");
	});

	it("gives a plain activator exactly one attempt and a bounded, escalating hold", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_state.cpp");
		// One attempt, not a strike count. Every base form on the shipped
		// /include/activator list delivers its whole payload inside the first accepted
		// OnActivate, so a second activation can only add nothing or, for the one
		// unguarded entry, duplicate the item. Restoring a multi-attempt plain-activator
		// path also restores the regression this replaced, where an egg that really did
		// deliver was re-activated until it collected enough strikes to go silent.
		expect(source).toContain("inline constexpr std::uint32_t kPlainActivationAttemptLimit = 1;");
		expect(source).toContain("inline constexpr std::uint32_t kFloraActivationAttemptLimit = 3;");
		const limitStart = source.indexOf("std::uint32_t GetActivationAttemptLimit(");
		expect(limitStart).toBeGreaterThanOrEqual(0);
		const limit = source.slice(limitStart, source.indexOf("std::chrono::seconds GetActivationHold(", limitStart));
		expect(limit).toContain("policy == ActivationPolicy::kFloraProbe");
		expect(limit).toContain("? kFloraActivationAttemptLimit");
		expect(limit).toContain(": kPlainActivationAttemptLimit;");
		// The selector has to stay a single discriminator. Widening it to
		// `policy == kFloraProbe || policy == kPlainActivator` hands every plain activator
		// the flora limit of three while kPlainActivationAttemptLimit still reads 1 and
		// both branch arms still appear, so pinning the constant and the arms separately
		// proves nothing about what the function returns. Pin the whole function: it is
		// one expression and every token of it is load-bearing.
		expect(code(limit)).toBe(
			"std::uint32_t GetActivationAttemptLimit(ActivationPolicy policy) { return policy == ActivationPolicy::kFloraProbe ? kFloraActivationAttemptLimit : kPlainActivationAttemptLimit; }",
		);

		// The plain-activator base has to sit far above the 10s maximum pass interval and
		// far above any plausible Papyrus dispatch latency, and the ceiling has to stay a
		// ceiling: an unbounded doubling would strand a reference that becomes lootable
		// again for the rest of the session, while dropping the clamp altogether lets the
		// wait grow without limit.
		expect(source).toContain("inline constexpr auto kPlainActivationHold = std::chrono::seconds(300);");
		expect(source).toContain("inline constexpr auto kPlainActivationHoldMax = std::chrono::minutes(30);");
		// FLOR is unchanged: its produce probe is real evidence, so it keeps the shorter
		// three-strike policy.
		expect(source).toContain("inline constexpr auto kFloraActivationHold = std::chrono::seconds(45);");
		expect(source).toContain("inline constexpr auto kFloraActivationHoldMax = std::chrono::minutes(10);");
		const holdFnStart = source.indexOf("std::chrono::seconds GetActivationHold(");
		expect(holdFnStart).toBeGreaterThanOrEqual(0);
		const holdFn = source.slice(
			holdFnStart,
			source.indexOf("std::uint64_t GetRecentlyLootedWorldRefKey(", holdFnStart),
		);
		expect(holdFn).toContain("(std::int64_t{ 1 } << level)");
		expect(holdFn).toContain("floraProbe ? kFloraActivationHoldMax : kPlainActivationHoldMax");
		expect(holdFn).toContain("return scaled < ceiling ? scaled : ceiling;");
		expect(holdFn).toContain("kActivationBackoffLevelMax");
		// Every constant and the clamp itself can stay exactly as pinned above while the
		// ceiling is scaled after the duration cast (`... : kPlainActivationHoldMax) * 48`),
		// which restores the effectively unbounded hold this design replaced. The
		// function is the whole computation, so pin the whole computation.
		expect(code(holdFn)).toBe(
			"std::chrono::seconds GetActivationHold(ActivationPolicy policy, std::uint32_t backoffLevel) { const auto level = backoffLevel < kActivationBackoffLevelMax ? backoffLevel : kActivationBackoffLevelMax; const bool floraProbe = policy == ActivationPolicy::kFloraProbe; const auto scaled = (floraProbe ? kFloraActivationHold : kPlainActivationHold) * (std::int64_t{ 1 } << level); const auto ceiling = std::chrono::duration_cast<std::chrono::seconds>( floraProbe ? kFloraActivationHoldMax : kPlainActivationHoldMax); return scaled < ceiling ? scaled : ceiling; }",
		);
		// The stale timeout must outlast the longest hold of either policy, or an entry
		// sitting out its maximum backoff is culled mid-hold and handed back the full
		// per-pass activation rate, which defeats the backoff entirely.
		expect(source).toContain("inline constexpr auto kActivationAttemptStaleTimeout = std::chrono::minutes(60);");
	});

	it("counts one attempt, re-arms after an expired hold, and clears only on an observed yield", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_state.cpp");
		const recordStart = source.indexOf("bool RecordActivationAttempt(");
		expect(recordStart).toBeGreaterThanOrEqual(0);
		const record = source.slice(recordStart, source.indexOf("void ClearActivationAttempts(", recordStart));
		// A recycled key must not inherit another reference's attempts; GetOrCreate
		// resets the entry when the formID no longer matches.
		expect(record).toContain("auto& entry = GetOrCreateActivationAttemptEntryLocked(key, formId);");
		// The counter must actually advance and saturate at the limit, or the hold can
		// never arm and the whole gate is dead code.
		expect(record).toContain("if (entry.attempts < limit)");
		expect(record).toContain("++entry.attempts;");
		expect(record).toContain("entry.updatedAt = now;");
		expect(record).toContain("held = entry.attempts >= limit;");
		// Re-arm only for an attempt made after the previous hold had actually expired.
		// Without the elapsed check every recorded attempt would escalate, and without
		// the escalation a dead reference keeps costing one activation per base hold
		// forever, which with nearest-first ordering starves the loot behind it.
		expect(record).toContain("(now - entry.updatedAt) >= GetActivationHold(policy, entry.backoffLevel))");
		expect(record).toContain("? entry.backoffLevel + 1");
		expect(record).toContain("event=activation_attempt_recorded");
		// Both conjuncts, in one expression. Dropping `entry.attempts >= limit` leaves the
		// elapsed check pinned above intact while every single recorded attempt escalates
		// the backoff, so a flora entry is sitting out the ten-minute ceiling before it
		// has even spent its three attempts.
		expect(code(record)).toContain(
			"if (entry.attempts >= limit && (now - entry.updatedAt) >= GetActivationHold(policy, entry.backoffLevel))",
		);

		const clearStart = source.indexOf("void ClearActivationAttempts(");
		expect(clearStart).toBeGreaterThan(recordStart);
		const clear = source.slice(clearStart, source.indexOf("bool IsPapyrusObjectHandleAvailable(", clearStart));
		// An observed yield drops the entry outright, backoff level included. Without the
		// erase a reference that starts producing again stays held until the stale sweep.
		// The erase is identity-checked like every other operation on this map: handle
		// keys are recycled, so erasing by key alone lets one reference's yield hand a
		// different reference's entry back the full per-pass activation rate.
		expect(code(clear)).toContain(
			"const auto it = activationAttempts.find(key); if (it != activationAttempts.end() && it->second.formID == formId) { activationAttempts.erase(it); }",
		);
		// ...and the identity has to be read off the reference before the lock is taken. A
		// fault inside the guard's scope never runs the guard's destructor under /EHsc,
		// which leaves the activation map locked for the rest of the session.
		expect(clear).toContain("const auto formId = ref->formID;");
		expect(clear.indexOf("const auto formId = ref->formID;")).toBeLessThan(
			clear.indexOf("std::lock_guard<std::mutex> guard(activationAttemptLock);"),
		);
	});

	it("bounds the activation attempt map by examined entries and a hard ceiling", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_state.cpp");
		const cleanupStart = source.indexOf("std::size_t CleanupStaleActivationAttemptsLocked(");
		expect(cleanupStart).toBeGreaterThanOrEqual(0);
		const cleanup = source.slice(
			cleanupStart,
			source.indexOf("void EvictOldestActivationAttemptsLocked(", cleanupStart),
		);
		// The per-pass bound must count entries EXAMINED, not only entries removed: a
		// map full of young entries would otherwise be walked in full under the lock on
		// every pass and never shrink.
		expect(cleanup).toContain("++examinedCount;");
		expect(cleanup).toContain("examinedCount < kActivationAttemptCleanupMaxPerPassExamined");
		expect(cleanup).toContain("visitedBuckets < kActivationAttemptCleanupMaxPerPassBuckets");
		// ...and the walk must resume where the previous pass stopped, or everything
		// past the first bounded slice is never examined at all.
		expect(cleanup).toContain("activationAttemptCleanupBucket = (bucket + 1) % bucketCount;");

		// An age-based expiry cannot bound the map on its own, so back it with a hard
		// entry ceiling that evicts the least recently updated entry.
		expect(source).toContain("inline constexpr std::size_t kActivationAttemptMaxEntries = 512;");
		const evictStart = source.indexOf("void EvictOldestActivationAttemptsLocked(");
		const evict = source.slice(
			evictStart,
			source.indexOf("ActivationAttemptEntry& GetOrCreateActivationAttemptEntryLocked(", evictStart),
		);
		expect(evict).toContain("while (activationAttempts.size() >= kActivationAttemptMaxEntries)");
		expect(evict).toContain("it->second.updatedAt < oldest->second.updatedAt");
		expect(evict).toContain("activationAttempts.erase(oldest);");
	});

	it("holds only activation refs and counts an attempt only when one was actually made", () => {
		const source = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_nearby_looting.cpp");
		const start = source.indexOf("std::vector<std::int32_t> LootNearbyEnabledReferences(");
		expect(start).toBeGreaterThanOrEqual(0);
		const body = source.slice(start);
		// The world-reference suppression stays type-gated as before; the activation
		// hold is a separate gate for ACTI/FLOR only.
		expect(body).toContain("UsesWorldReferenceTransfer(actualFormType) && IsRecentlyLootedWorldRef(ref)");
		expect(body).toContain("auto activationPolicy = ActivationPolicy::kPlainActivator;");
		expect(body).toContain("IsActivationHeld(ref, activationPolicy)");
		// Arming the hold is not the same event as skipping a held reference, and a plain
		// activator arms on its first attempt, so this line rides along with ordinary
		// successful pickups. It must not read as a suppression.
		expect(body).toContain("event=activation_hold_armed reason=attempt_limit");
		expect(body).not.toContain("event=activation_held ");
		// The pure hold query has to stay ahead of the activation and ahead of
		// markProcessed, so a held reference pays neither the gate cost nor a slot of the
		// object and category budget.
		expect(body.indexOf("IsActivationHeld(ref, activationPolicy)")).toBeLessThan(
			body.indexOf("successful = TryLootActivationReference("),
		);
		expect(body.indexOf("IsActivationHeld(ref, activationPolicy)")).toBeLessThan(
			body.indexOf("auto markProcessed = [&]()"),
		);

		// The hold gate must carry the form-type conjunct itself. Without it the gate
		// leaks onto containers, actors and world refs.
		const holdGate = body.slice(
			body.indexOf("if (actualFormType == ENUM_FORM_ID::kACTI || actualFormType == ENUM_FORM_ID::kFLOR)"),
			body.indexOf("bool validForm = false;"),
		);
		expect(holdGate).toContain(
			"if (actualFormType == ENUM_FORM_ID::kACTI || actualFormType == ENUM_FORM_ID::kFLOR)",
		);
		expect(holdGate).toContain("activationPolicy = GetActivationPolicy(baseForm);");
		expect(holdGate).toContain("IsActivationHeld(ref, activationPolicy)");
		// The gate has to skip the reference, not merely ask about it. Deleting the
		// `continue` leaves every string and every ordering above true and activates held
		// references anyway, so pin the query together with the branch it controls.
		expect(code(holdGate)).toContain(
			"activationPolicy = GetActivationPolicy(baseForm); if (IsActivationHeld(ref, activationPolicy)) { continue; }",
		);

		// The admission check is not enough on its own. The MCM's forced call and the
		// timer-driven pass are not serialised, so a second pass can read the hold as
		// clear, stall, and activate after the first pass has already activated and armed
		// it - and TrapFloraThistle has no script-side guard at all, so a second
		// activation that lands before its destroyed flag does duplicates the item. The
		// per-object lock is what orders the two passes, so the hold is read again inside
		// it, immediately before the activation.
		const lockedBranch = body.slice(
			body.indexOf("if (!TryLockObject(ref))"),
			body.indexOf("else if (UsesWorldReferenceTransfer(actualFormType))"),
		);
		expect(code(lockedBranch)).toContain(
			"else if (actualFormType == ENUM_FORM_ID::kACTI || actualFormType == ENUM_FORM_ID::kFLOR) { if (IsActivationHeld(ref, activationPolicy)) { continue; } auto activationOutcome = ActivationOutcome::kNotAttempted;",
		);
		// ...and that re-read only orders anything if the other pass records its attempt
		// while it still holds the same object lock, so the record has to sit inside the
		// release guard's scope.
		const releaseGuardAt = body.indexOf("} releaseGuard{ ref->formID };");
		expect(releaseGuardAt).toBeGreaterThan(body.indexOf("if (!TryLockObject(ref))"));
		expect(body.indexOf("RecordActivationAttempt(ref, activationPolicy)")).toBeGreaterThan(releaseGuardAt);
		// Nothing here may settle, mark or stamp a pass any more: the collector handing a
		// plain activator back was never evidence about that reference.
		for (const removed of [
			"BeginActivationYieldPass",
			"MarkActivationAwaitingYieldEvidence",
			"SettleActivationYieldEvidence",
			"RecordActivationYieldOutcome",
			"IsSuppressedNoYieldActivationRef",
		]) {
			expect(source).not.toContain(removed);
		}

		const activationBranch = body.slice(
			body.indexOf("auto activationOutcome = ActivationOutcome::kNotAttempted;"),
			body.indexOf("else if (UsesWorldReferenceTransfer(actualFormType))"),
		);
		// An observed produce delivery is the only thing that clears the history.
		expect(activationBranch).toContain("if (activationOutcome == ActivationOutcome::kYielded)");
		expect(activationBranch).toContain("ClearActivationAttempts(ref);");
		// Attempt accounting must skip the pre-activation gate rejections. Dropping this
		// guard arms the hold on a capacity-rejected plant, and with a one-attempt policy
		// that silences the reference for the whole hold even though nothing was ever
		// activated.
		expect(activationBranch).toContain("else if (activationOutcome != ActivationOutcome::kNotAttempted)");
		expect(activationBranch).toContain("if (RecordActivationAttempt(ref, activationPolicy))");
		expect(activationBranch.indexOf("RecordActivationAttempt(ref, activationPolicy)")).toBeGreaterThan(
			activationBranch.indexOf("else if (activationOutcome != ActivationOutcome::kNotAttempted)"),
		);
		// The condition has to still govern the block. A stray semicolon after it
		// (`...kNotAttempted);`) keeps every string and every ordering above true while
		// making the recording unconditional, which is exactly the case the guard exists
		// to exclude. Pin the condition and the block it opens as one expression.
		expect(code(activationBranch)).toContain(
			"else if (activationOutcome != ActivationOutcome::kNotAttempted) { if (RecordActivationAttempt(ref, activationPolicy)) {",
		);
	});
});
