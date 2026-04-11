import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { undeploy } from "../../scripts/undeploy.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("undeploy", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("removes existing targets and matched papyrus pex files", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const dataDir = path.join(config.fallout4Dir, "Data");

		fs.mkdirsSync(path.join(dataDir, "LootMan"));
		fs.outputFileSync(path.join(dataDir, "F4SE", "Plugins", "lootman.dll"), "dll");
		fs.outputFileSync(path.join(dataDir, "F4SE", "Plugins", "lootman.ini"), "ini");
		fs.outputFileSync(path.join(dataDir, "Scripts", "LTMN", "Nested", "A.pex"), "pex");

		const result = undeploy(config);

		expect(result.removed).toContain("LootMan");
		expect(result.removed).toContain("F4SE/Plugins/lootman.dll");
		expect(result.removed).toContain("F4SE/Plugins/lootman.ini");
		expect(result.removed).toContain("Scripts/LTMN");
		expect(result.skipped).toContain("Scripts/LTMN/**/*.pex");
		expect(fs.existsSync(path.join(dataDir, "LootMan"))).toBe(false);
		expect(fs.existsSync(path.join(dataDir, "F4SE", "Plugins", "lootman.dll"))).toBe(false);
		expect(fs.existsSync(path.join(dataDir, "F4SE", "Plugins", "lootman.ini"))).toBe(false);
		expect(fs.existsSync(path.join(dataDir, "Scripts", "LTMN", "Nested", "A.pex"))).toBe(false);
	});

	it("marks non-existent targets and patterns as skipped", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);

		const result = undeploy(config);

		expect(result.removed).toEqual([]);
		expect(result.skipped).toContain("LootMan");
		expect(result.skipped).toContain("Scripts/LTMN/**/*.pex");
		expect(result.skipped).toContain("Scripts/LTMN2/**/*.pex");
	});
});
