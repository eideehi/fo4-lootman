import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
	generateNativeHookHeader,
	type NativeHookAddressManifest,
} from "../../scripts/native-hook-addresses.js";
import { resolveNativeHookAddresses } from "../../scripts/native-hook-address-resolution.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

function createFixtureManifest(reportPath = "tools/ghidra/reports/proven-call.txt"): NativeHookAddressManifest {
	return {
		schemaVersion: 1,
		targetRuntime: "Fallout4 Test 1.2.3",
		sourceFile: "commonlibf4-plugin/src/papyrus_lootman_hooks.cpp",
		generatedHeader: "commonlibf4-plugin/src/papyrus_lootman_hook_addresses.generated.h",
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
	const manifestPath = path.join(root, "tools", "native-hooks", "papyrus_lootman_hooks.addresses.json");
	const headerPath = path.join(root, manifest.generatedHeader);
	const reportPath = path.join(root, "tools", "ghidra", "reports", "proven-call.txt");

	fs.outputJsonSync(manifestPath, manifest, { spaces: 2 });
	fs.outputFileSync(headerPath, generateNativeHookHeader(manifest));
	fs.outputFileSync(reportPath, reportText);
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
		"Target 140456000",
		"Instructions:",
		"  140456789: CALL 0x140123000",
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
			{
				id: "fixture.proven_call",
				targetAbsoluteAddress: "0x140123000",
				candidateRvas: ["0x456789"],
				changed: true,
			},
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
			exactReport().replace("  140456789: CALL 0x140123000", "  140456789: JMP 0x140123000"),
		);

		expect(() => resolveNativeHookAddresses({ projectRoot: root, manifestPath }))
			.toThrow("no CALL 0x140123000 instruction line was found");
	});
});
