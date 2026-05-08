import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { runPreflight } from "../scripts/preflight.js";
import { createTempDir, removeTempDir } from "./helpers/temp-dir.js";

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

	it("passes for packaging commands when tsx artifacts exist at the repository root", () => {
		const root = createTempDir();
		dirs.push(root);
		seedTool(root, "tsx");

		expect(() => runPreflight("package:build", { cwd: root, projectRoot: root, platform: "win32" })).not.toThrow();
		expect(() => runPreflight("package:deploy", { cwd: root, projectRoot: root, platform: "win32" })).not.toThrow();
	});

	it("passes for native hook and ghidra commands when tsx artifacts exist at the repository root", () => {
		const root = createTempDir();
		dirs.push(root);
		seedTool(root, "tsx");

		expect(() => runPreflight("native-hooks:generate", { cwd: root, projectRoot: root, platform: "win32" })).not.toThrow();
		expect(() => runPreflight("native-hooks:bundle", { cwd: root, projectRoot: root, platform: "win32" })).not.toThrow();
		expect(() => runPreflight("native-hooks:resolve", { cwd: root, projectRoot: root, platform: "win32" })).not.toThrow();
		expect(() => runPreflight("native-hooks:verify", { cwd: root, projectRoot: root, platform: "win32" })).not.toThrow();
		expect(() => runPreflight("ghidra:probe", { cwd: root, projectRoot: root, platform: "win32" })).not.toThrow();
	});

	it("fails with a repository root hint when cwd is wrong", () => {
		const root = createTempDir();
		dirs.push(root);
		seedTool(root, "tsx");
		const elsewhere = path.join(root, "..", "other-dir");

		expect(() => runPreflight("package:build", { cwd: elsewhere, projectRoot: root, platform: "win32" })).toThrow(
			`Set-Location ${root}`,
		);
	});

	it("fails for packaging commands when tsx package artifacts are missing", () => {
		const root = createTempDir();
		dirs.push(root);

		expect(() => runPreflight("package:build", { cwd: root, projectRoot: root, platform: "win32" })).toThrow(
			"node_modules/tsx/package.json",
		);
	});

	it("fails for packaging commands when tsx shim is missing", () => {
		const root = createTempDir();
		dirs.push(root);
		seedTool(root, "tsx", "win32", false);

		expect(() => runPreflight("package:build", { cwd: root, projectRoot: root, platform: "win32" })).toThrow(
			"node_modules/.bin/tsx.CMD",
		);
	});

	it("fails for test when vitest artifacts are missing", () => {
		const root = createTempDir();
		dirs.push(root);

		expect(() => runPreflight("test:watch", { cwd: root, projectRoot: root, platform: "win32" })).toThrow(
			"node_modules/vitest/package.json",
		);
	});

	it("passes for test aliases when vitest artifacts exist", () => {
		const root = createTempDir();
		dirs.push(root);
		seedTool(root, "vitest");

		expect(() => runPreflight("test:watch", { cwd: root, projectRoot: root, platform: "win32" })).not.toThrow();
		expect(() => runPreflight("test:packaging", { cwd: root, projectRoot: root, platform: "win32" })).not.toThrow();
		expect(() => runPreflight("test:tools", { cwd: root, projectRoot: root, platform: "win32" })).not.toThrow();
	});
});
