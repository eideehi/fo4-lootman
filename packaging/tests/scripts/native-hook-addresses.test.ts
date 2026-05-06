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

	it("keeps workshop supply owner fields as diagnostic layout offsets", () => {
		const manifest = readNativeHookManifest(defaultManifestPath);
		const entries = manifest.entries.filter((candidate) => candidate.id.startsWith("workshop_supply_owner."));

		expect(entries.map((entry) => entry.id).sort()).toEqual([
			"workshop_supply_owner.field_2f8",
			"workshop_supply_owner.field_e0",
			"workshop_supply_owner.field_e8",
			"workshop_supply_owner.field_f8",
		]);
		for (const entry of entries) {
			expect(entry.category).toBe("layout_offset");
			expect(entry.addressLibrary).toBeUndefined();
			expect(entry.discoveryStrategy.summary).toContain("Raw diagnostic workshop supply owner layout read");
		}
	});

	it("fails when Address Library metadata is attached to a direct call site", () => {
		const manifest = cloneManifest(readNativeHookManifest(defaultManifestPath));
		const entry = manifest.entries.find((candidate) => candidate.category === "call_site_rva");
		if (!entry) throw new Error("Fixture manifest has no call-site entry.");
		entry.addressLibrary = {
			id: "123",
			offset: "0x1234",
		};

		const result = validateNativeHookManifest(manifest, {
			projectRoot,
			checkEvidencePaths: true,
		});

		expect(result.valid).toBe(false);
		expect(result.errors.join("\n")).toContain("addressLibrary must not be set for call-site entries");
	});

	it("fails when Address Library metadata is attached to layout offsets or constants", () => {
		for (const category of ["layout_offset", "constant"] as const) {
			const manifest = cloneManifest(readNativeHookManifest(defaultManifestPath));
			const entry = manifest.entries.find((candidate) => candidate.category === category);
			if (!entry) throw new Error(`Fixture manifest has no ${category} entry.`);
			entry.addressLibrary = {
				id: "123",
				offset: "0x1234",
			};

			const result = validateNativeHookManifest(manifest, {
				projectRoot,
				checkEvidencePaths: true,
			});

			expect(result.valid).toBe(false);
			expect(result.errors.join("\n")).toContain("addressLibrary is only allowed for function_rva and global_rva entries");
		}
	});

	it("fails when an exact Address Library offset does not match the retained RVA", () => {
		const manifest = cloneManifest(readNativeHookManifest(defaultManifestPath));
		const entry = manifest.entries.find((candidate) => candidate.id === "encounter_zone.reset_elapsed_from_detach_time");
		if (!entry) throw new Error("Fixture manifest has no reset elapsed entry.");
		entry.addressLibrary = {
			id: "2200355",
			offset: "0x4D2E21",
		};

		const result = validateNativeHookManifest(manifest, {
			projectRoot,
			checkEvidencePaths: true,
		});

		expect(result.valid).toBe(false);
		expect(result.errors.join("\n")).toContain("addressLibrary.offset must match");
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
		expect(first).toContain("#include <REL/ID.h>");
		expect(first).toContain("inline constexpr REL::ID kEncounterZoneResetElapsedFromDetachId{ 2200355 };");
		expect(first).toContain("inline constexpr REL::ID kWorkshopCaravanKeywordGlobalId{ 4797310 };");
		expect(first).toContain("Address Library: RE::ID::Workshop::GetSelectedWorkshopMenuNode @ 0x389A80");
		expect(path.relative(projectRoot, headerPath).replaceAll("\\", "/")).toBe(manifest.generatedHeader);
	});
});
