import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { collectFomod } from "../../scripts/collect-fomod.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("collect-fomod", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("copies fomod files and replaces version token", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { version: "1.2.3" });
		const source = path.join(config.resourcesRoot, "fomod");
		const outDir = path.join(config.buildTempDir, "fomod");

		fs.outputFileSync(path.join(source, "info.xml"), "<Version>__MOD_VERSION__</Version>");
		fs.outputFileSync(path.join(source, "module.xml"), "<Module />");
		fs.outputFileSync(path.join(outDir, "old.txt"), "stale");

		expect(collectFomod(config)).toEqual({
			copied: 2,
			removed: 1,
			skipped: 0,
			total: 2,
		});

		expect(fs.existsSync(path.join(outDir, "old.txt"))).toBe(false);
		expect(fs.readFileSync(path.join(outDir, "info.xml"), "utf8")).toContain("<Version>1.2.3</Version>");
		expect(fs.readFileSync(path.join(outDir, "module.xml"), "utf8")).toContain("<Module />");
	});

	it("skips unchanged fomod files and only updates transformed info.xml when the version changes", () => {
		const root = createTempDir();
		dirs.push(root);
		const buildTempDir = path.join(root, "packaging", "build", "shared");
		const configV1 = createTestConfig(root, { version: "1.2.3", buildTempDir });
		const configV2 = createTestConfig(root, { version: "2.0.0", buildTempDir });
		const source = path.join(configV1.resourcesRoot, "fomod");
		const outDir = path.join(buildTempDir, "fomod");

		fs.outputFileSync(path.join(source, "info.xml"), "<Version>__MOD_VERSION__</Version>");
		fs.outputFileSync(path.join(source, "module.xml"), "<Module />");

		expect(collectFomod(configV1)).toEqual({
			copied: 2,
			removed: 0,
			skipped: 0,
			total: 2,
		});
		expect(collectFomod(configV1)).toEqual({
			copied: 0,
			removed: 0,
			skipped: 2,
			total: 2,
		});

		expect(collectFomod(configV2)).toEqual({
			copied: 1,
			removed: 0,
			skipped: 1,
			total: 2,
		});

		expect(fs.readFileSync(path.join(outDir, "info.xml"), "utf8")).toContain("<Version>2.0.0</Version>");
		expect(fs.readFileSync(path.join(outDir, "module.xml"), "utf8")).toContain("<Module />");
	});
});
