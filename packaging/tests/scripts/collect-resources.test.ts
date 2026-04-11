import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { collectResources } from "../../scripts/collect-resources.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("collect-resources", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("replaces destination with copied resources", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const source = path.join(config.resourcesRoot, "lootman");
		const dest = path.join(config.buildTempDir, "files", "resources");

		fs.outputFileSync(path.join(source, "common", "a.txt"), "from-source");
		fs.outputFileSync(path.join(source, "common", "b.txt"), "other-source");
		fs.outputFileSync(path.join(dest, "old.txt"), "old");

		expect(collectResources(config)).toEqual({
			copied: 2,
			removed: 1,
			skipped: 0,
			total: 2,
		});

		expect(fs.existsSync(path.join(dest, "old.txt"))).toBe(false);
		expect(fs.readFileSync(path.join(dest, "common", "a.txt"), "utf8")).toBe("from-source");
		expect(fs.readFileSync(path.join(dest, "common", "b.txt"), "utf8")).toBe("other-source");
	});

	it("skips unchanged files, recopies changed files, and removes deleted ones", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const source = path.join(config.resourcesRoot, "lootman");
		const dest = path.join(config.buildTempDir, "files", "resources");

		fs.outputFileSync(path.join(source, "common", "a.txt"), "a1");
		fs.outputFileSync(path.join(source, "common", "b.txt"), "b1");

		expect(collectResources(config)).toEqual({
			copied: 2,
			removed: 0,
			skipped: 0,
			total: 2,
		});
		expect(collectResources(config)).toEqual({
			copied: 0,
			removed: 0,
			skipped: 2,
			total: 2,
		});

		fs.writeFileSync(path.join(source, "common", "b.txt"), "b2");

		expect(collectResources(config)).toEqual({
			copied: 1,
			removed: 0,
			skipped: 1,
			total: 2,
		});
		expect(fs.readFileSync(path.join(dest, "common", "b.txt"), "utf8")).toBe("b2");

		fs.removeSync(path.join(source, "common", "a.txt"));

		expect(collectResources(config)).toEqual({
			copied: 0,
			removed: 1,
			skipped: 1,
			total: 1,
		});
		expect(fs.existsSync(path.join(dest, "common", "a.txt"))).toBe(false);
	});
});
