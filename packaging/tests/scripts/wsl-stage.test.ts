import fs from "fs-extra";
import { afterEach, describe, expect, it } from "vitest";
import path from "node:path";
import { resolveWslBuildStagePath, syncTree } from "../../scripts/wsl-stage.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("wsl-stage", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("build stage root ignores package version", () => {
		const root = "/tmp/root";
		const configA = createTestConfig(root, { version: "1.0.0", buildTempDir: path.join(root, "packaging", "build", "1.0.0") });
		const configB = createTestConfig(root, { version: "2.0.0", buildTempDir: path.join(root, "packaging", "build", "2.0.0") });

		expect(resolveWslBuildStagePath(configA, "dll", "commonlibf4-plugin")).toBe(
			resolveWslBuildStagePath(configB, "dll", "commonlibf4-plugin"),
		);
	});

	it("resolves build stage paths under wslStageDir", () => {
		const config = createTestConfig("/tmp/root");

		expect(resolveWslBuildStagePath(config, "dll", "commonlibf4-plugin")).toBe(
			path.join(config.wslStageDir, "build", "dll", "commonlibf4-plugin"),
		);
	});

	it("skips unchanged files when only sub-second mtime precision differs", () => {
		const root = createTempDir();
		dirs.push(root);
		const sourceDir = path.join(root, "source");
		const destDir = path.join(root, "dest");
		const sourceFile = path.join(sourceDir, "xmake.lua");
		const destFile = path.join(destDir, "xmake.lua");

		fs.outputFileSync(sourceFile, "set_project(\"lootman\")\n");
		fs.outputFileSync(destFile, "set_project(\"lootman\")\n");
		fs.utimesSync(sourceFile, new Date("2026-03-22T08:41:10.424Z"), new Date("2026-03-22T08:41:10.424Z"));
		fs.utimesSync(destFile, new Date("2026-03-22T08:41:10.000Z"), new Date("2026-03-22T08:41:10.000Z"));

		expect(syncTree(sourceDir, destDir)).toEqual({ copied: 0, skipped: 1, removed: 0 });
	});

	it("recopies same-size files changed within the same second", () => {
		const root = createTempDir();
		dirs.push(root);
		const sourceDir = path.join(root, "source");
		const destDir = path.join(root, "dest");
		const sourceFile = path.join(sourceDir, "xmake.lua");
		const destFile = path.join(destDir, "xmake.lua");

		fs.outputFileSync(sourceFile, "return 1\n");
		fs.outputFileSync(destFile, "return 2\n");
		fs.utimesSync(sourceFile, new Date("2026-03-22T08:41:10.424Z"), new Date("2026-03-22T08:41:10.424Z"));
		fs.utimesSync(destFile, new Date("2026-03-22T08:41:10.000Z"), new Date("2026-03-22T08:41:10.000Z"));

		expect(syncTree(sourceDir, destDir)).toEqual({ copied: 1, skipped: 0, removed: 0 });
		expect(fs.readFileSync(destFile, "utf8")).toBe("return 1\n");
	});
});
