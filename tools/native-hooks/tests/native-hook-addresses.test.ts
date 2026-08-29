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
} from "../scripts/native-hook-addresses.js";
import { resolveNativeHookAddresses } from "../scripts/native-hook-address-resolution.js";

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
		expect(manifest.resolutionEvidenceReport).toBe(
			"tools/ghidra/reports/fallout4-1.11.240/proven-call-site-evidence.txt",
		);
	});

	it("keeps the checked-in 1.11.240 manifest resolved against its declared evidence", () => {
		const result = resolveNativeHookAddresses();

		expect(result.resolvedEntries).toHaveLength(15);
		expect(result.resolvedEntries.every((entry) => !entry.changed)).toBe(true);
		expect(result.resolvedEntries.flatMap((entry) => entry.sites ?? []).every((site) => !site.changed)).toBe(true);
	});

	it("keeps the durable 1.11.240 report manifest identical to production", () => {
		const production = fs.readFileSync(defaultManifestPath, "utf8");
		const report = fs.readFileSync(
			path.join(projectRoot, "tools/native-hooks/reports/fallout4-1.11.240/manifest.json"),
			"utf8",
		);

		expect(report).toBe(production);
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

	it("keeps direct component count as an explicit multi-site proof", () => {
		const manifest = readNativeHookManifest(defaultManifestPath);
		const entry = manifest.entries.find((candidate) => candidate.id === "workshop_material.direct_component_count");
		if (!entry) throw new Error("Fixture manifest has no direct component count entry.");
		const proof = entry.discoveryStrategy.proof;
		if (!proof) throw new Error("Direct component count has no proof.");

		expect(entry.discoveryStrategy.status).toBe("proven");
		expect(entry.expectedCount).toBe(5);
		expect(proof.targetAbsoluteAddress).toBe("0x140507D30");
		expect(proof.sites?.map((site) => site.siteId)).toEqual(entry.sites?.map((site) => site.id));
		expect(proof.sites?.map((site) => site.absoluteAddress)).toEqual([
			"0x1403BC71D",
			"0x14039F5AF",
			"0x140B32E7B",
			"0x140B37828",
			"0x140B2D13E",
		]);
		expect(proof.excludedReferences).toEqual([
			{
				absoluteAddress: "0x140507A05",
				reason: "Component helper internal fallback path, not one of the five direct component count hook sites; re-proven for Fallout4 1.11.240.",
			},
		]);
	});

	it("keeps component count helper as an explicit multi-site proof", () => {
		const manifest = readNativeHookManifest(defaultManifestPath);
		const entry = manifest.entries.find((candidate) => candidate.id === "workshop_material.component_count_helper");
		if (!entry) throw new Error("Fixture manifest has no component count helper entry.");
		const proof = entry.discoveryStrategy.proof;
		if (!proof) throw new Error("Component count helper has no proof.");

		expect(entry.discoveryStrategy.status).toBe("proven");
		expect(entry.expectedCount).toBe(2);
		expect(proof.report).toBe("tools/ghidra/reports/fallout4-1.11.240/fo4-component-count-helper-target-functions.txt");
		expect(proof.instructionReports).toEqual([
			"tools/ghidra/reports/fallout4-1.11.240/fo4-component-count-helper-call-windows.txt",
		]);
		expect(proof.targetAbsoluteAddress).toBe("0x140507990");
		expect(proof.sites?.map((site) => site.siteId)).toEqual(entry.sites?.map((site) => site.id));
		expect(proof.sites?.map((site) => site.absoluteAddress)).toEqual([
			"0x14059BF5A",
			"0x14117550B",
		]);
		expect(proof.excludedReferences).toBeUndefined();
	});

	it("keeps workshop menu select as an explicit multi-site proof", () => {
		const manifest = readNativeHookManifest(defaultManifestPath);
		const entry = manifest.entries.find((candidate) => candidate.id === "workshop_menu.select");
		if (!entry) throw new Error("Fixture manifest has no workshop menu select entry.");
		const proof = entry.discoveryStrategy.proof;
		if (!proof) throw new Error("Workshop menu select has no proof.");

		expect(entry.discoveryStrategy.status).toBe("proven");
		expect(entry.expectedCount).toBe(2);
		expect(proof.report).toBe("tools/ghidra/reports/fallout4-1.11.240/fo4-menu-select-target-functions.txt");
		expect(proof.instructionReports).toEqual([
			"tools/ghidra/reports/fallout4-1.11.240/fo4-menu-select-call-windows.txt",
		]);
		expect(proof.targetAbsoluteAddress).toBe("0x1403970E0");
		expect(proof.sites?.map((site) => site.siteId)).toEqual(entry.sites?.map((site) => site.id));
		expect(proof.sites?.map((site) => site.absoluteAddress)).toEqual([
			"0x140B2C69A",
			"0x140B2C957",
		]);
		expect(proof.excludedReferences).toBeUndefined();
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

	it("treats exact Address Library non-call-site entries as automated", () => {
		const manifest = readNativeHookManifest(defaultManifestPath);
		const ids = [
			"encounter_zone.reset_elapsed_from_detach_time",
			"workshop_shared_container.workshop_caravan_keyword_global",
			"workshop_material.current_workshop_handle_global",
			"workshop_menu.selected_menu_node_function",
			"workshop_menu.selected_row_global",
		];

		for (const id of ids) {
			const entry = manifest.entries.find((candidate) => candidate.id === id);
			if (!entry) throw new Error(`Fixture manifest has no ${id} entry.`);
			expect(entry.discoveryStrategy.status).toBe("automated");
			expect(entry.addressLibrary).toBeDefined();
		}
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

	it("requires versioned target and context evidence on every proven schema v2 call site", () => {
		for (const field of ["expectedTargetRva", "contextSignatureVersion", "contextBytes"] as const) {
			const manifest = cloneManifest(readNativeHookManifest(defaultManifestPath));
			manifest.schemaVersion = 2;
			const entry = manifest.entries.find((candidate) => candidate.category === "call_site_rva");
			if (!entry?.sites) throw new Error("Fixture manifest has no call sites.");
			delete entry.sites[0][field];

			const result = validateNativeHookManifest(manifest, { projectRoot, checkEvidencePaths: true });

			expect(result.valid).toBe(false);
			expect(result.errors.join("\n")).toContain(field);
		}
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
		expect(first).toContain("Address Library: RE::ID::Workshop::GetSelectedWorkshopMenuNode @ 0x389DB0");
		expect(path.relative(projectRoot, headerPath).replaceAll("\\", "/")).toBe(manifest.generatedHeader);
	});
});
