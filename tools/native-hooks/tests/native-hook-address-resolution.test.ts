import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
	generateNativeHookHeader,
	type NativeHookAddressManifest,
} from "../scripts/native-hook-addresses.js";
import { resolveNativeHookAddresses } from "../scripts/native-hook-address-resolution.js";
import { createTempDir, removeTempDir } from "../../tests/helpers/temp-dir.js";

function createFixtureManifest(reportPath = "tools/ghidra/reports/proven-call.txt"): NativeHookAddressManifest {
	return {
		schemaVersion: 1,
		targetRuntime: "Fallout4 Test 1.2.3",
		sourceFile: "commonlibf4-plugin/src/papyrus_lootman_hooks.cpp",
		generatedHeader: "commonlibf4-plugin/src/papyrus_lootman_hook_addresses.generated.h",
		resolutionEvidenceReport: "tools/ghidra/reports/fallout4-1.11.240/proven-call-site-evidence.txt",
		entries: [
			{
				id: "fixture.proven_call",
				cppName: "kFixtureProvenCallSite",
				category: "call_site_rva",
				expectedCount: 1,
				expectedInstructionKind: "call_rel32",
				expectedOriginalTargetGroup: "fixture.target",
				evidence: [reportPath],
				discoveryStrategy: {
					status: "proven",
					summary: "Fixture proof resolves one call reference.",
					proof: {
						kind: "ghidra_reference_report",
						report: reportPath,
						targetAbsoluteAddress: "0x140123000",
						referenceType: "UNCONDITIONAL_CALL",
					},
				},
				sites: [
					{
						id: "fixture.proven-call.primary",
						rva: "0x1111",
						sourceId: "0xA1",
						label: "fixture.proven-call.primary",
					},
				],
			},
			{
				id: "fixture.unproven_call",
				cppName: "kFixtureUnprovenCallSite",
				category: "call_site_rva",
				expectedCount: 1,
				expectedInstructionKind: "call_rel32",
				expectedOriginalTargetGroup: "fixture.unproven",
				evidence: [reportPath],
				discoveryStrategy: {
					status: "unproven",
					summary: "Fixture remains manual.",
				},
				sites: [
					{
						id: "fixture.unproven-call.primary",
						rva: "0x2222",
						sourceId: "0xA2",
						label: "fixture.unproven-call.primary",
					},
				],
			},
		],
	};
}

function writeFixtureProject(root: string, manifest: NativeHookAddressManifest, reportText: string): string {
	return writeFixtureProjectWithReports(root, manifest, {
		"tools/ghidra/reports/proven-call.txt": reportText,
	});
}

function writeFixtureProjectWithReports(
	root: string,
	manifest: NativeHookAddressManifest,
	reports: Record<string, string>,
	headerManifest = manifest,
): string {
	const manifestPath = path.join(root, "tools", "native-hooks", "papyrus_lootman_hooks.addresses.json");
	const headerPath = path.join(root, manifest.generatedHeader);

	fs.outputJsonSync(manifestPath, manifest, { spaces: 2 });
	fs.outputFileSync(headerPath, generateNativeHookHeader(headerManifest));
	for (const [reportPath, text] of Object.entries(reports)) {
		fs.outputFileSync(path.join(root, reportPath), text);
	}
	const evidenceText = Object.values(reports).join("\n");
	fs.outputFileSync(path.join(root, "tools/ghidra/reports/fallout4-1.11.240/proven-call-site-evidence.txt"), evidenceText);
	return manifestPath;
}

function exactReport(): string {
	return [
		"Program: Fallout4.exe",
		"Image base: 140000000",
		"",
		"================================================================================",
		"Target 140123000",
		"References to entry:",
		"  143000000 -> 140123000 type=DATA",
		"  140456789 -> 140123000 type=UNCONDITIONAL_CALL",
		"",
		"Outgoing references from function body:",
		"",
		"Instructions:",
		"  140123000: RET",
		"",
		"================================================================================",
		"Target 140456789",
		"Instructions:",
		"  140456789: [E8 72 C8 CC FF] CALL 0x140123000",
		"  14045678e: [48 8B C1] MOV RAX,RCX",
		"",
	].join("\n");
}

function createMultiSiteManifest(): NativeHookAddressManifest {
	const manifest = createFixtureManifest("tools/ghidra/reports/multi-site.txt");
	manifest.entries[0] = {
		id: "fixture.multi_site",
		cppName: "kFixtureMultiCallSites",
		category: "call_site_rva",
		expectedCount: 2,
		expectedInstructionKind: "call_rel32",
		expectedOriginalTargetGroup: "fixture.multi",
		evidence: ["tools/ghidra/reports/multi-site.txt"],
		discoveryStrategy: {
			status: "proven",
			summary: "Fixture proof resolves two explicit call references.",
			proof: {
				kind: "ghidra_reference_report",
				report: "tools/ghidra/reports/multi-site.txt",
				targetAbsoluteAddress: "0x140123000",
				referenceType: "UNCONDITIONAL_CALL",
				sites: [
					{
						siteId: "fixture.multi.site-b",
						absoluteAddress: "0x140456020",
					},
					{
						siteId: "fixture.multi.site-a",
						absoluteAddress: "0x140456010",
					},
				],
				excludedReferences: [
					{
						absoluteAddress: "0x140456030",
						reason: "Fixture same-target call is outside this hook family.",
					},
				],
			},
		},
		sites: [
			{
				id: "fixture.multi.site-a",
				rva: "0x1111",
				sourceId: "0xA1",
				label: "fixture.multi.site-a",
			},
			{
				id: "fixture.multi.site-b",
				rva: "0x2222",
				sourceId: "0xA2",
				label: "fixture.multi.site-b",
			},
		],
	};
	return manifest;
}

function multiSiteReport(): string {
	return [
		"Program: Fallout4.exe",
		"Image base: 140000000",
		"",
		"================================================================================",
		"Target 140123000",
		"References to entry:",
		"  140456010 -> 140123000 type=UNCONDITIONAL_CALL",
		"  140456020 -> 140123000 type=UNCONDITIONAL_CALL",
		"  140456030 -> 140123000 type=UNCONDITIONAL_CALL",
		"",
		"Instructions:",
		"  140123000: RET",
		"",
		"================================================================================",
		"Target 140456010",
		"Instructions:",
		"  140456010: [E8 EB CF CC FF] CALL 0x140123000",
		"  140456015: [48 8B C1] MOV RAX,RCX",
		"  140456018: [90] NOP",
		"",
		"================================================================================",
		"Target 140456020",
		"Instructions:",
		"  140456020: [E8 DB CF CC FF] CALL 0x140123000",
		"  140456025: [48 8B C2] MOV RAX,RDX",
		"  140456028: [90] NOP",
		"",
		"================================================================================",
		"Target 140456030",
		"Instructions:",
		"  140456030: [E8 CB CF CC FF] CALL 0x140123000",
		"  140456035: [48 8B C3] MOV RAX,RBX",
		"",
	].join("\n");
}

describe("native hook address resolution", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("resolves proven entries without writing by default", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifestPath = writeFixtureProject(root, createFixtureManifest(), exactReport());

		const result = resolveNativeHookAddresses({ projectRoot: root, manifestPath });
		const manifestAfter = fs.readJsonSync(manifestPath) as NativeHookAddressManifest;

		expect(result.wroteManifest).toBe(false);
		expect(result.resolvedEntries).toEqual([
			expect.objectContaining({
				id: "fixture.proven_call",
				targetAbsoluteAddress: "0x140123000",
				candidateRvas: ["0x456789"],
				changed: true,
				sites: [expect.objectContaining({
					siteId: "fixture.proven-call.primary",
					expectedTargetRva: "0x123000",
					contextBytes: "48 8B C1",
					contextInstructionCount: 1,
				})],
			}),
		]);
		expect(result.skippedEntries).toEqual(["fixture.unproven_call"]);
		expect(manifestAfter.entries[0].sites?.[0].rva).toBe("0x1111");
	});

	it("updates manifest and generated header only when --write is requested", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifestPath = writeFixtureProject(root, createFixtureManifest(), exactReport());

		const result = resolveNativeHookAddresses({ projectRoot: root, manifestPath, write: true });
		const manifestAfter = fs.readJsonSync(manifestPath) as NativeHookAddressManifest;
		const header = fs.readFileSync(path.join(root, manifestAfter.generatedHeader), "utf8");

		expect(result.wroteManifest).toBe(true);
		expect(manifestAfter.entries[0].sites?.[0].rva).toBe("0x456789");
		expect(header).toContain("0x456789");
	});

	it("refuses ambiguous proof reports before writing", () => {
		const root = createTempDir();
		dirs.push(root);
		const ambiguousReport = exactReport().replace(
			"  140456789 -> 140123000 type=UNCONDITIONAL_CALL",
			[
				"  140456789 -> 140123000 type=UNCONDITIONAL_CALL",
				"  140456999 -> 140123000 type=UNCONDITIONAL_CALL",
			].join("\n"),
		);
		const manifestPath = writeFixtureProject(root, createFixtureManifest(), ambiguousReport);

		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath, write: true }))
			.toThrow("expected 1 UNCONDITIONAL_CALL candidate, found 2");
		const manifestAfter = fs.readJsonSync(manifestPath) as NativeHookAddressManifest;
		expect(manifestAfter.entries[0].sites?.[0].rva).toBe("0x1111");
	});

	it("refuses candidates without a matching CALL instruction line", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifestPath = writeFixtureProject(
			root,
			createFixtureManifest(),
			exactReport().replace("[E8 72 C8 CC FF] CALL 0x140123000", "[E8 72 C8 CC FF] JMP 0x140123000"),
		);

		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath }))
			.toThrow("no CALL 0x140123000 instruction line was found");
	});

	it("refuses missing or malformed raw instruction bytes", () => {
		const root = createTempDir();
		dirs.push(root);
		const missingPath = writeFixtureProject(root, createFixtureManifest(), exactReport()
			.replace("[E8 72 C8 CC FF] ", "")
			.replace("  14045678e: [48 8B C1] MOV RAX,RCX", ""));
		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath: missingPath }))
			.toThrow("missing raw-byte instruction window");

		const malformedPath = writeFixtureProject(root, createFixtureManifest(), exactReport().replace(
			"[E8 72 C8 CC FF]", "[E8 72 C8 XX FF]",
		));
		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath: malformedPath }))
			.toThrow("malformed raw instruction bytes");
	});

	it("refuses unrecognized lines and non-contiguous instructions in evidence windows", () => {
		const malformedRoot = createTempDir();
		dirs.push(malformedRoot);
		const malformedPath = writeFixtureProject(malformedRoot, createFixtureManifest(), exactReport().replace(
			"  14045678e: [48 8B C1] MOV RAX,RCX",
			"  unexpected evidence format",
		));
		expect(() => resolveNativeHookAddresses({ projectRoot: malformedRoot, manifestPath: malformedPath }))
			.toThrow("unrecognized line inside Target 0x140456789");

		const gapRoot = createTempDir();
		dirs.push(gapRoot);
		const gapPath = writeFixtureProject(gapRoot, createFixtureManifest(), exactReport().replace(
			"14045678e: [48 8B C1]",
			"14045678f: [48 8B C1]",
		));
		expect(() => resolveNativeHookAddresses({ projectRoot: gapRoot, manifestPath: gapPath }))
			.toThrow("has no contiguous post-call instruction at 0x14045678E");
	});

	it("refuses an evidence report that is not declared by the manifest", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifestPath = writeFixtureProject(root, createFixtureManifest(), exactReport());
		const alternatePath = path.join(root, "tools/ghidra/reports/alternate.txt");
		fs.outputFileSync(alternatePath, exactReport());

		expect(() => resolveNativeHookAddresses({
			projectRoot: root,
			manifestPath,
			evidenceReportPath: alternatePath,
		})).toThrow("Evidence report must match manifest resolutionEvidenceReport");
	});

	it("independently rejects a raw CALL target mismatch", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifestPath = writeFixtureProject(root, createFixtureManifest(), exactReport().replace(
			"[E8 72 C8 CC FF]", "[E8 73 C8 CC FF]",
		));
		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath }))
			.toThrow("raw CALL target 0x140123001 does not match 0x140123000");
	});

	it("rejects a family whose post-call instruction contexts never become unique", () => {
		const root = createTempDir();
		dirs.push(root);
		const report = multiSiteReport().replace("[48 8B C2] MOV RAX,RDX", "[48 8B C1] MOV RAX,RCX");
		const manifestPath = writeFixtureProjectWithReports(root, createMultiSiteManifest(), {
			"tools/ghidra/reports/multi-site.txt": report,
		});
		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath }))
			.toThrow("no non-empty whole-instruction context uniquely identifies");
	});

	it("does not use instructions after a mid-window address gap to make contexts unique", () => {
		const root = createTempDir();
		dirs.push(root);
		const report = multiSiteReport()
			.replace("[48 8B C2] MOV RAX,RDX", "[48 8B C1] MOV RAX,RCX")
			.replace("  140456018: [90] NOP", "  140456019: [CC] INT3");
		const manifestPath = writeFixtureProjectWithReports(root, createMultiSiteManifest(), {
			"tools/ghidra/reports/multi-site.txt": report,
		});

		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath }))
			.toThrow("no non-empty whole-instruction context uniquely identifies fixture.multi.site-a");
	});

	it("resolves explicit multi-site proof without writing by default", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifestPath = writeFixtureProjectWithReports(root, createMultiSiteManifest(), {
			"tools/ghidra/reports/multi-site.txt": multiSiteReport(),
		});

		const result = resolveNativeHookAddresses({ projectRoot: root, manifestPath });
		const manifestAfter = fs.readJsonSync(manifestPath) as NativeHookAddressManifest;

		expect(result.wroteManifest).toBe(false);
		expect(result.resolvedEntries[0]).toEqual(expect.objectContaining({
			id: "fixture.multi_site",
			targetAbsoluteAddress: "0x140123000",
			candidateRvas: ["0x456010", "0x456020"],
			changed: true,
			sites: [
				expect.objectContaining({ siteId: "fixture.multi.site-a", rva: "0x456010", changed: true, contextBytes: "48 8B C1" }),
				expect.objectContaining({ siteId: "fixture.multi.site-b", rva: "0x456020", changed: true, contextBytes: "48 8B C2" }),
			],
		}));
		expect(manifestAfter.entries[0].sites?.map((site) => site.rva)).toEqual(["0x1111", "0x2222"]);
	});

	it("updates multi-site manifest entries by site id while preserving order and labels", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifestPath = writeFixtureProjectWithReports(root, createMultiSiteManifest(), {
			"tools/ghidra/reports/multi-site.txt": multiSiteReport(),
		});

		const result = resolveNativeHookAddresses({ projectRoot: root, manifestPath, write: true });
		const manifestAfter = fs.readJsonSync(manifestPath) as NativeHookAddressManifest;

		expect(result.wroteManifest).toBe(true);
		expect(manifestAfter.entries[0].sites).toEqual([
			{
				id: "fixture.multi.site-a",
				rva: "0x456010",
				sourceId: "0xA1",
				label: "fixture.multi.site-a",
				expectedTargetRva: "0x123000",
				contextSignatureVersion: 1,
				contextBytes: "48 8B C1",
			},
			{
				id: "fixture.multi.site-b",
				rva: "0x456020",
				sourceId: "0xA2",
				label: "fixture.multi.site-b",
				expectedTargetRva: "0x123000",
				contextSignatureVersion: 1,
				contextBytes: "48 8B C2",
			},
		]);
	});

	it("refuses multi-site proven entries without explicit proof sites", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifest = createMultiSiteManifest();
		delete manifest.entries[0].discoveryStrategy.proof?.sites;
		const manifestPath = writeFixtureProjectWithReports(root, manifest, {
			"tools/ghidra/reports/multi-site.txt": multiSiteReport(),
		}, createMultiSiteManifest());

		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath }))
			.toThrow("proof.sites is required for multi-site proven call-site entries");
	});

	it("refuses unknown explicit proof site ids", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifest = createMultiSiteManifest();
		const proof = manifest.entries[0].discoveryStrategy.proof;
		if (!proof?.sites) throw new Error("Fixture proof has no sites.");
		proof.sites[0].siteId = "fixture.multi.unknown";
		const manifestPath = writeFixtureProjectWithReports(root, manifest, {
			"tools/ghidra/reports/multi-site.txt": multiSiteReport(),
		}, createMultiSiteManifest());

		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath }))
			.toThrow("proof.sites must match manifest site ids");
	});

	it("refuses extra same-target references unless they are explicitly excluded", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifest = createMultiSiteManifest();
		delete manifest.entries[0].discoveryStrategy.proof?.excludedReferences;
		const manifestPath = writeFixtureProjectWithReports(root, manifest, {
			"tools/ghidra/reports/multi-site.txt": multiSiteReport(),
		});

		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath }))
			.toThrow("0x140456030 is an extra UNCONDITIONAL_CALL reference");
	});

	it("refuses excluded references that are absent from the target report", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifest = createMultiSiteManifest();
		const proof = manifest.entries[0].discoveryStrategy.proof;
		if (!proof?.excludedReferences) throw new Error("Fixture proof has no excluded references.");
		proof.excludedReferences[0].absoluteAddress = "0x140456040";
		const manifestPath = writeFixtureProjectWithReports(root, manifest, {
			"tools/ghidra/reports/multi-site.txt": multiSiteReport(),
		});

		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath }))
			.toThrow("excluded reference 0x140456040 was not found");
	});

	it("refuses explicit proof sites without matching CALL instruction lines", () => {
		const root = createTempDir();
		dirs.push(root);
		const manifestPath = writeFixtureProjectWithReports(root, createMultiSiteManifest(), {
			"tools/ghidra/reports/multi-site.txt": multiSiteReport().replace(
				"[E8 DB CF CC FF] CALL 0x140123000",
				"[E8 DB CF CC FF] CALL 0x140999000",
			),
		});

		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath }))
			.toThrow("no CALL 0x140123000 instruction line was found");
	});
});
