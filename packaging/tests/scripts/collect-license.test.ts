import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { collectLicense } from "../../scripts/collect-license.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("collect-license", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("copies LICENSE and EXCEPTIONS into build temp", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);

		fs.outputFileSync(path.join(config.projectRoot, "LICENSE"), "license");
		fs.outputFileSync(path.join(config.projectRoot, "EXCEPTIONS"), "exceptions");

		expect(collectLicense(config)).toEqual({
			copied: 2,
			removed: 0,
			skipped: 0,
			total: 2,
		});

		expect(fs.readFileSync(path.join(config.buildTempDir, "LICENSE"), "utf8")).toBe("license");
		expect(fs.readFileSync(path.join(config.buildTempDir, "EXCEPTIONS"), "utf8")).toBe("exceptions");
	});

	it("skips unchanged files and recopies only changed license files", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);

		fs.outputFileSync(path.join(config.projectRoot, "LICENSE"), "license");
		fs.outputFileSync(path.join(config.projectRoot, "EXCEPTIONS"), "exceptions");

		expect(collectLicense(config)).toEqual({
			copied: 2,
			removed: 0,
			skipped: 0,
			total: 2,
		});
		expect(collectLicense(config)).toEqual({
			copied: 0,
			removed: 0,
			skipped: 2,
			total: 2,
		});

		fs.writeFileSync(path.join(config.projectRoot, "LICENSE"), "license-v2");

		expect(collectLicense(config)).toEqual({
			copied: 1,
			removed: 0,
			skipped: 1,
			total: 2,
		});
		expect(fs.readFileSync(path.join(config.buildTempDir, "LICENSE"), "utf8")).toBe("license-v2");
	});
});
