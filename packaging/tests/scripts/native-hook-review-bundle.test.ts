import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
	generateNativeHookHeader,
	type NativeHookAddressManifest,
} from "../../scripts/native-hook-addresses.js";
import { generateNativeHookReviewBundle } from "../../scripts/native-hook-review-bundle.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

function createFixtureManifest(): NativeHookAddressManifest {
	return {
		schemaVersion: 1,
		targetRuntime: "Fallout4 Test 1.2.3",
		sourceFile: "commonlibf4-plugin/src/papyrus_lootman_hooks.cpp",
		generatedHeader: "commonlibf4-plugin/src/papyrus_lootman_hook_addresses.generated.h",
		entries: [
			{
				id: "fixture.direct_call",
				cppName: "kFixtureCallSites",
				category: "call_site_rva",
				expectedCount: 1,
				expectedInstructionKind: "call_rel32",
				expectedOriginalTargetGroup: "fixture.target",
				evidence: ["tools/ghidra/reports/fixture-window.txt"],
				discoveryStrategy: {
					status: "unproven",
					summary: "Fixture resolver is intentionally unresolved.",
				},
				sites: [
					{
						id: "fixture.direct-call.primary",
						rva: "0x1234",
						sourceId: "0xA1",
						label: "fixture.direct-call.primary",
					},
				],
			},
			{
				id: "fixture.layout",
				cppName: "kFixtureLayoutOffset",
				category: "layout_offset",
				expectedCount: 1,
				value: "0xE0",
				evidence: ["tools/ghidra/reports/fixture-window.txt"],
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
		const reportPath = path.join(root, "tools", "ghidra", "reports", "fixture-window.txt");

		fs.outputJsonSync(manifestPath, manifest, { spaces: 2 });
		fs.outputFileSync(headerPath, generateNativeHookHeader(manifest));
		fs.outputFileSync(reportPath, "Target 00001234\n  00001234: CALL 00005678\n");
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
		const candidates = fs.readJsonSync(paths.candidates);
		const sourceSlice = fs.readFileSync(paths.sourceSlice, "utf8");

		expect(markdown).toContain("# LootMan Native Hook Address Review Bundle");
		expect(markdown).toContain("## Candidate RVAs");
		expect(markdown).toContain("fixture.direct-call.primary=0x1234");
		expect(markdown).toContain("## Instruction Windows");
		expect(markdown).toContain("tools/ghidra/reports/fixture-window.txt");
		expect(markdown).toContain("## Unresolved Items Checklist");
		expect(candidates.entries[0].candidateRvas[0].rva).toBe("0x1234");
		expect(candidates.entries[0].instructionWindowAddresses).toEqual(["0x1234"]);
		expect(sourceSlice).toContain("DecodeDirectCallSite");
		expect(sourceSlice).toContain("InstallWorkshopMaterialProbeHooks");
		expect(sourceSlice.split("\n").filter((line) => /[ \t]+$/u.test(line))).toEqual([]);
	});
});
