import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { collectPapyrus } from "../../scripts/collect-papyrus.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("collect-papyrus", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("copies all .psc files recursively", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const srcDir = path.join(config.projectRoot, "papyrus", "Scripts", "Source", "User");
		const outDir = path.join(config.buildTempDir, "files", "papyrus", "product", "source");

		fs.outputFileSync(path.join(srcDir, "Main.psc"), "ScriptName Main");
		fs.outputFileSync(path.join(srcDir, "Nested", "Other.psc"), "ScriptName Nested:Other");
		fs.outputFileSync(path.join(srcDir, "ignore.txt"), "ignore");
		fs.outputFileSync(path.join(outDir, "stale.psc"), "stale");

		expect(await collectPapyrus(config)).toEqual({
			copied: 2,
			removed: 1,
			skipped: 0,
			total: 2,
		});

		expect(fs.existsSync(path.join(outDir, "stale.psc"))).toBe(false);
		expect(fs.readFileSync(path.join(outDir, "Main.psc"), "utf8")).toContain("ScriptName Main");
		expect(fs.readFileSync(path.join(outDir, "Nested", "Other.psc"), "utf8")).toContain("Nested:Other");
		expect(fs.existsSync(path.join(outDir, "ignore.txt"))).toBe(false);
	});

	it("skips unchanged scripts, recopies changed scripts, and removes deleted scripts", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const srcDir = path.join(config.projectRoot, "papyrus", "Scripts", "Source", "User");
		const outDir = path.join(config.buildTempDir, "files", "papyrus", "product", "source");

		fs.outputFileSync(path.join(srcDir, "Main.psc"), "ScriptName Main");
		fs.outputFileSync(path.join(srcDir, "Nested", "Other.psc"), "ScriptName Nested:Other");

		expect(await collectPapyrus(config)).toEqual({
			copied: 2,
			removed: 0,
			skipped: 0,
			total: 2,
		});
		expect(await collectPapyrus(config)).toEqual({
			copied: 0,
			removed: 0,
			skipped: 2,
			total: 2,
		});

		fs.writeFileSync(path.join(srcDir, "Nested", "Other.psc"), "ScriptName Nested:Changed");

		expect(await collectPapyrus(config)).toEqual({
			copied: 1,
			removed: 0,
			skipped: 1,
			total: 2,
		});
		expect(fs.readFileSync(path.join(outDir, "Nested", "Other.psc"), "utf8")).toContain("Nested:Changed");

		fs.removeSync(path.join(srcDir, "Main.psc"));

		expect(await collectPapyrus(config)).toEqual({
			copied: 0,
			removed: 1,
			skipped: 1,
			total: 1,
		});
		expect(fs.existsSync(path.join(outDir, "Main.psc"))).toBe(false);
	});

	it("completes when no source scripts exist", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const srcDir = path.join(config.projectRoot, "papyrus", "Scripts", "Source", "User");
		const outDir = path.join(config.buildTempDir, "files", "papyrus", "product", "source");

		fs.mkdirsSync(srcDir);
		expect(await collectPapyrus(config)).toEqual({
			copied: 0,
			removed: 0,
			skipped: 0,
			total: 0,
		});

		expect(fs.existsSync(outDir)).toBe(false);
	});
});
