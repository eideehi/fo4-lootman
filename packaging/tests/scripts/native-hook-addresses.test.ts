import fs from "fs-extra";
import path from "node:path";
import { describe, expect, it } from "vitest";
import {
	defaultManifestPath,
	generateNativeHookHeader,
	getGeneratedHeaderPath,
	projectRoot,
	readNativeHookManifest,
	validateNativeHookManifest,
	type NativeHookAddressManifest,
} from "../../scripts/native-hook-addresses.js";

function cloneManifest(manifest: NativeHookAddressManifest): NativeHookAddressManifest {
	return JSON.parse(JSON.stringify(manifest)) as NativeHookAddressManifest;
}

describe("native hook address manifest", () => {
	it("validates the checked-in manifest", () => {
		const manifest = readNativeHookManifest(defaultManifestPath);
		const result = validateNativeHookManifest(manifest, {
			projectRoot,
			checkEvidencePaths: true,
			checkGeneratedHeader: false,
			checkSource: false,
		});

		expect(result.errors).toEqual([]);
		expect(result.valid).toBe(true);
	});

	it("fails when a call-site expected count is impossible", () => {
		const manifest = cloneManifest(readNativeHookManifest(defaultManifestPath));
		const entry = manifest.entries.find((candidate) => candidate.category === "call_site_rva");
		if (!entry) throw new Error("Fixture manifest has no call-site entry.");
		entry.expectedCount += 1;

		const result = validateNativeHookManifest(manifest, {
			projectRoot,
			checkEvidencePaths: true,
		});

		expect(result.valid).toBe(false);
		expect(result.errors.join("\n")).toContain("expectedCount");
	});

	it("fails when evidence paths are missing", () => {
		const manifest = cloneManifest(readNativeHookManifest(defaultManifestPath));
		manifest.entries[0].evidence = [];

		const result = validateNativeHookManifest(manifest, {
			projectRoot,
			checkEvidencePaths: true,
		});

		expect(result.valid).toBe(false);
		expect(result.errors.join("\n")).toContain("evidence");
	});

	it("fails when a raw layout offset is mislabeled as an RVA", () => {
		const manifest = cloneManifest(readNativeHookManifest(defaultManifestPath));
		const entry = manifest.entries.find((candidate) => candidate.id === "workshop_supply_owner.field_e0");
		if (!entry) throw new Error("Fixture manifest has no field_e0 entry.");
		entry.category = "function_rva";

		const result = validateNativeHookManifest(manifest, {
			projectRoot,
			checkEvidencePaths: true,
		});

		expect(result.valid).toBe(false);
		expect(result.errors.join("\n")).toContain("layout_offset");
	});

	it("fails when a direct call-site lacks expected target metadata", () => {
		const manifest = cloneManifest(readNativeHookManifest(defaultManifestPath));
		const entry = manifest.entries.find((candidate) => candidate.category === "call_site_rva");
		if (!entry) throw new Error("Fixture manifest has no call-site entry.");
		delete entry.expectedOriginalTargetGroup;

		const result = validateNativeHookManifest(manifest, {
			projectRoot,
			checkEvidencePaths: true,
		});

		expect(result.valid).toBe(false);
		expect(result.errors.join("\n")).toContain("expectedOriginalTargetGroup");
	});

	it("generates deterministic C++ and matches the checked-in header", () => {
		const manifest = readNativeHookManifest(defaultManifestPath);
		const first = generateNativeHookHeader(manifest);
		const second = generateNativeHookHeader(manifest);
		const headerPath = getGeneratedHeaderPath(manifest, projectRoot);

		expect(first).toBe(second);
		expect(fs.readFileSync(headerPath, "utf8")).toBe(first);
		expect(path.relative(projectRoot, headerPath).replaceAll("\\", "/")).toBe(manifest.generatedHeader);
	});
});
