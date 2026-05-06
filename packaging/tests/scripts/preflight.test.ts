import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { runPreflight } from "../../scripts/preflight.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

function seedTool(root: string, tool: string, platform = "win32", includeShim = true): void {
	fs.outputJsonSync(path.join(root, "node_modules", tool, "package.json"), { name: tool });
	if (!includeShim) {
		return;
	}

	const shimName = platform === "win32" ? `${tool}.CMD` : tool;
	fs.outputFileSync(path.join(root, "node_modules", ".bin", shimName), "");
}

describe("preflight", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("passes for build when tsx artifacts exist in packaging", () => {
		const root = createTempDir();
		dirs.push(root);
		seedTool(root, "tsx");

		expect(() => runPreflight("build", { cwd: root, packagingRoot: root, platform: "win32" })).not.toThrow();
	});

	it("passes for native hook utility aliases when tsx artifacts exist in packaging", () => {
		const root = createTempDir();
		dirs.push(root);
		seedTool(root, "tsx");

		expect(() => runPreflight("generate:native-hooks", { cwd: root, packagingRoot: root, platform: "win32" })).not.toThrow();
		expect(() => runPreflight("generate:native-hook-bundle", { cwd: root, packagingRoot: root, platform: "win32" })).not.toThrow();
		expect(() => runPreflight("verify:native-hooks", { cwd: root, packagingRoot: root, platform: "win32" })).not.toThrow();
	});

	it("fails with a packaging directory hint when cwd is wrong", () => {
		const root = createTempDir();
		dirs.push(root);
		seedTool(root, "tsx");
		const elsewhere = path.join(root, "..", "other-dir");

		expect(() => runPreflight("build", { cwd: elsewhere, packagingRoot: root, platform: "win32" })).toThrow(
			"Set-Location packaging",
		);
	});

	it("fails for build when tsx package artifacts are missing", () => {
		const root = createTempDir();
		dirs.push(root);

		expect(() => runPreflight("build", { cwd: root, packagingRoot: root, platform: "win32" })).toThrow(
			"node_modules/tsx/package.json",
		);
	});

	it("fails for build when tsx shim is missing", () => {
		const root = createTempDir();
		dirs.push(root);
		seedTool(root, "tsx", "win32", false);

		expect(() => runPreflight("build", { cwd: root, packagingRoot: root, platform: "win32" })).toThrow(
			"node_modules/.bin/tsx.CMD",
		);
	});

	it("fails for test when vitest artifacts are missing", () => {
		const root = createTempDir();
		dirs.push(root);

		expect(() => runPreflight("test:watch", { cwd: root, packagingRoot: root, platform: "win32" })).toThrow(
			"node_modules/vitest/package.json",
		);
	});

	it("passes for test aliases when vitest artifacts exist", () => {
		const root = createTempDir();
		dirs.push(root);
		seedTool(root, "vitest");

		expect(() => runPreflight("test:watch", { cwd: root, packagingRoot: root, platform: "win32" })).not.toThrow();
	});
});
