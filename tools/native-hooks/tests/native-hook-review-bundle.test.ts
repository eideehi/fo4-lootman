import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
	generateNativeHookHeader,
	type NativeHookAddressEntry,
	type NativeHookAddressManifest,
} from "../scripts/native-hook-addresses.js";
import { generateNativeHookReviewBundle } from "../scripts/native-hook-review-bundle.js";
import { createTempDir, removeTempDir } from "../../tests/helpers/temp-dir.js";

const IMAGE_BASE = 0x140000000;

interface CandidateBundleEntry {
	id: string;
	candidateRvas: { rva: string }[];
	instructionWindowAddresses: string[];
	proofReadiness: {
		status: string;
		targetAbsoluteAddress?: string;
		targetReport?: { selectedReferenceCount: number };
		missingDirectCallSites?: string[];
		extraSameTargetReferences?: string[];
		targetCandidates?: { targetAbsoluteAddress: string }[];
	};
	unresolvedItems: unknown[];
}

function absoluteFromRva(rva: string): string {
	return (IMAGE_BASE + Number.parseInt(rva.slice(2), 16)).toString(16);
}

function createCallEntry(
	id: string,
	cppName: string,
	rva: string,
	reportPath: string,
	sourceId: string,
): NativeHookAddressEntry {
	return {
		id,
		cppName,
		category: "call_site_rva",
		expectedCount: 1,
		expectedInstructionKind: "call_rel32",
		expectedOriginalTargetGroup: `${id}.target`,
		evidence: [reportPath],
		discoveryStrategy: {
			status: "unproven",
			summary: "Fixture resolver is intentionally unresolved.",
		},
		sites: [
			{
				id: `${id}.primary`,
				rva,
				sourceId,
				label: `${id}.primary`,
			},
		],
	};
}

function targetReport(
	target: string,
	references: string[],
	directCalls: Array<{ source: string; target: string }>,
): string {
	const targetBody = [
		"Program: Fallout4.exe",
		"Image base: 140000000",
		"",
		"================================================================================",
		`Target ${target}`,
		"References to entry:",
		...references.map((reference) => `  ${reference} -> ${target} type=UNCONDITIONAL_CALL`),
		"",
		"Instructions:",
		`  ${target}: RET`,
	];
	const directCallBodies = directCalls.flatMap((call) => [
		"",
		"================================================================================",
		`Target ${call.source}`,
		"Instructions:",
		`  ${call.source}: CALL 0x${call.target}`,
	]);
	return [...targetBody, ...directCallBodies, ""].join("\n");
}

function instructionWindowReport(source: string, target: string): string {
	return [
		"Program: Fallout4.exe",
		"Image base: 140000000",
		"",
		"================================================================================",
		`Target ${source}`,
		"Instructions:",
		`  ${source}: CALL 0x${target}`,
		"",
	].join("\n");
}

function rediscoveryReport(source: string): string {
	return [
		"Program: Fallout4.exe",
		"Image base: 140000000",
		"",
		"================================================================================",
		`Target ${source}`,
		"Instructions:",
		`  ${source}: MOV RAX,RBX`,
		"",
	].join("\n");
}

function conflictingTargetReport(source: string): string {
	return [
		"Program: Fallout4.exe",
		"Image base: 140000000",
		"",
		"================================================================================",
		`Target ${source}`,
		"References to entry:",
		`  ${source} -> 140009100 type=UNCONDITIONAL_CALL`,
		"",
		"Instructions:",
		`  ${source}: CALL 0x140009000`,
		"",
	].join("\n");
}

function createFixtureManifest(): NativeHookAddressManifest {
	return {
		schemaVersion: 1,
		targetRuntime: "Fallout4 Test 1.2.3",
		sourceFile: "commonlibf4-plugin/src/papyrus_lootman_hooks.cpp",
		generatedHeader: "commonlibf4-plugin/src/papyrus_lootman_hook_addresses.generated.h",
		entries: [
			createCallEntry("fixture.ready_call", "kFixtureReadyCallSites", "0x1234", "tools/ghidra/reports/fixture-ready.txt", "0xA1"),
			createCallEntry(
				"fixture.missing_window",
				"kFixtureMissingWindowCallSites",
				"0x2234",
				"tools/ghidra/reports/fixture-missing-window.txt",
				"0xA2",
			),
			createCallEntry(
				"fixture.missing_allrefs",
				"kFixtureMissingAllrefsCallSites",
				"0x3234",
				"tools/ghidra/reports/fixture-missing-allrefs.txt",
				"0xA3",
			),
			createCallEntry(
				"fixture.extra_reference",
				"kFixtureExtraReferenceCallSites",
				"0x4234",
				"tools/ghidra/reports/fixture-extra-reference.txt",
				"0xA4",
			),
			createCallEntry("fixture.rediscovery", "kFixtureRediscoveryCallSites", "0x5234", "tools/ghidra/reports/fixture-rediscovery.txt", "0xA5"),
			createCallEntry(
				"fixture.conflicting_targets",
				"kFixtureConflictingTargetsCallSites",
				"0x6234",
				"tools/ghidra/reports/fixture-conflicting-targets.txt",
				"0xA6",
			),
			{
				id: "fixture.layout",
				cppName: "kFixtureLayoutOffset",
				category: "layout_offset",
				expectedCount: 1,
				value: "0xE0",
				evidence: ["tools/ghidra/reports/fixture-ready.txt"],
				discoveryStrategy: {
					status: "manual",
					summary: "Fixture layout offset is not an executable address.",
				},
			},
		],
	};
}

describe("native hook review bundle", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("generates manifest, candidate JSON, source slice, and markdown bundle", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifest = createFixtureManifest();
		const manifestPath = path.join(root, "tools", "native-hooks", "papyrus_lootman_hooks.addresses.json");
		const sourcePath = path.join(root, manifest.sourceFile);
		const headerPath = path.join(root, manifest.generatedHeader);
		const readySource = absoluteFromRva("0x1234");
		const missingWindowSource = absoluteFromRva("0x2234");
		const missingAllrefsSource = absoluteFromRva("0x3234");
		const extraReferenceSource = absoluteFromRva("0x4234");
		const rediscoverySource = absoluteFromRva("0x5234");
		const conflictingTargetSource = absoluteFromRva("0x6234");

		fs.outputJsonSync(manifestPath, manifest, { spaces: 2 });
		fs.outputFileSync(headerPath, generateNativeHookHeader(manifest));
		fs.outputFileSync(
			path.join(root, "tools", "ghidra", "reports", "fixture-ready.txt"),
			targetReport("140005678", [readySource], [{ source: readySource, target: "140005678" }]),
		);
		fs.outputFileSync(
			path.join(root, "tools", "ghidra", "reports", "fixture-missing-window.txt"),
			targetReport("140006000", [missingWindowSource], []),
		);
		fs.outputFileSync(
			path.join(root, "tools", "ghidra", "reports", "fixture-missing-allrefs.txt"),
			instructionWindowReport(missingAllrefsSource, "140007000"),
		);
		fs.outputFileSync(
			path.join(root, "tools", "ghidra", "reports", "fixture-extra-reference.txt"),
			targetReport("140008000", [extraReferenceSource, "140004244"], [{ source: extraReferenceSource, target: "140008000" }]),
		);
		fs.outputFileSync(
			path.join(root, "tools", "ghidra", "reports", "fixture-rediscovery.txt"),
			rediscoveryReport(rediscoverySource),
		);
		fs.outputFileSync(
			path.join(root, "tools", "ghidra", "reports", "fixture-conflicting-targets.txt"),
			conflictingTargetReport(conflictingTargetSource),
		);
		fs.outputFileSync(
			sourcePath,
			[
				"#include \"papyrus_lootman_hook_addresses.generated.h\"",
				"void DecodeDirectCallSite();",
				"void InstallDirectCallHookFamily();",
				"void InstallWorkshopMaterialProbeHooks();",
				"",
			].join("\n"),
		);

		const paths = generateNativeHookReviewBundle({
			projectRoot: root,
			manifestPath,
			outputRoot: path.join(root, "tools", "native-hooks", "reports"),
		});

		const markdown = fs.readFileSync(paths.markdown, "utf8");
			const candidates = fs.readJsonSync(paths.candidates) as { entries: CandidateBundleEntry[] };
			const sourceSlice = fs.readFileSync(paths.sourceSlice, "utf8");

		expect(markdown).toContain("# LootMan Native Hook Address Review Bundle");
		expect(markdown).toContain("## Candidate RVAs");
		expect(markdown).toContain("fixture.ready_call.primary=0x1234");
		expect(markdown).toContain("## Instruction Windows");
		expect(markdown).toContain("## Proof Readiness");
		expect(markdown).toContain("### ready_for_proof_metadata");
		expect(markdown).toContain("### needs_instruction_window_refresh");
		expect(markdown).toContain("### needs_target_allrefs_report");
		expect(markdown).toContain("### needs_exclusion_triage");
		expect(markdown).toContain("### needs_rediscovery");
		expect(markdown).toContain("tools/ghidra/reports/fixture-ready.txt");
		expect(markdown).toContain("## Manual Non-Executable Entries");
		expect(markdown).toContain("fixture.layout (layout_offset): value=0xE0");
		expect(markdown).toContain("## Unresolved Items Checklist");
		expect(markdown).not.toContain("fixture.layout: Discovery strategy is manual");
		expect(candidates.entries.every((entry: { proofReadiness?: unknown }) => entry.proofReadiness)).toBe(true);

			const entries = new Map(candidates.entries.map((entry) => [entry.id, entry]));
			const entry = (id: string): CandidateBundleEntry => {
				const found = entries.get(id);
				if (!found) {
					throw new Error(`Missing candidate entry: ${id}`);
				}
				return found;
			};
			expect(candidates.entries[0].candidateRvas[0].rva).toBe("0x1234");
			expect(candidates.entries[0].instructionWindowAddresses).toEqual(["0x1234"]);
			expect(entry("fixture.ready_call").proofReadiness.status).toBe("ready_for_proof_metadata");
			expect(entry("fixture.ready_call").proofReadiness.targetAbsoluteAddress).toBe("0x140005678");
			expect(entry("fixture.ready_call").proofReadiness.targetReport?.selectedReferenceCount).toBe(1);
			expect(entry("fixture.missing_window").proofReadiness.status).toBe("needs_instruction_window_refresh");
			expect(entry("fixture.missing_window").proofReadiness.missingDirectCallSites).toEqual(["0x140002234"]);
			expect(entry("fixture.missing_allrefs").proofReadiness.status).toBe("needs_target_allrefs_report");
			expect(entry("fixture.extra_reference").proofReadiness.status).toBe("needs_exclusion_triage");
			expect(entry("fixture.extra_reference").proofReadiness.extraSameTargetReferences).toEqual(["0x140004244"]);
			expect(entry("fixture.rediscovery").proofReadiness.status).toBe("needs_rediscovery");
			expect(entry("fixture.conflicting_targets").proofReadiness.status).toBe("needs_rediscovery");
			expect(entry("fixture.conflicting_targets").proofReadiness.targetCandidates?.map((
				candidate: { targetAbsoluteAddress: string },
			) => candidate.targetAbsoluteAddress)).toEqual(["0x140009000", "0x140009100"]);
			expect(entry("fixture.layout").proofReadiness.status).toBe("not_applicable");
			expect(entry("fixture.layout").unresolvedItems).toEqual([]);
		expect(sourceSlice).toContain("DecodeDirectCallSite");
		expect(sourceSlice).toContain("InstallWorkshopMaterialProbeHooks");
		expect(sourceSlice.split("\n").filter((line) => /[ \t]+$/u.test(line))).toEqual([]);
	});
});
