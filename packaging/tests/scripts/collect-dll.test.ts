import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { collectDll, parseArgs, resolveDllPath } from "../../scripts/collect-dll.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("collect-dll", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("parseArgs defaults to product", () => {
		expect(parseArgs(["node", "script"])).toBe("product");
	});

	it("parseArgs validates product-only mode", () => {
		expect(parseArgs(["--mode=product"])).toBe("product");
		expect(() => parseArgs(["--mode=debug"])).toThrow('Invalid --mode value: "debug" (expected "product")');
	});

	it("resolveDllPath interpolates mode placeholder", () => {
		const resolved = resolveDllPath("C:/root", "build/{mode}", "releasedbg");
		expect(resolved.replaceAll("\\", "/")).toContain("C:/root/build/releasedbg/lootman.dll");
	});

	it("collects product dll from releasedbg build", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const src = path.join(config.projectRoot, "commonlibf4-plugin", "build", "windows", "x64", "releasedbg", "lootman.dll");
		const out = path.join(config.buildTempDir, "files", "dll", "product", "lootman.dll");

		fs.outputFileSync(src, "product-dll");
		expect(collectDll(config, "product")).toEqual({
			copied: 1,
			removed: 0,
			skipped: 0,
			total: 1,
		});

		expect(fs.readFileSync(out, "utf8")).toBe("product-dll");
	});

	it("skips unchanged dlls and recopies when the dll changes", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const src = path.join(config.projectRoot, "commonlibf4-plugin", "build", "windows", "x64", "releasedbg", "lootman.dll");
		const out = path.join(config.buildTempDir, "files", "dll", "product", "lootman.dll");

		fs.outputFileSync(src, "product-dll");

		expect(collectDll(config, "product")).toEqual({
			copied: 1,
			removed: 0,
			skipped: 0,
			total: 1,
		});
		expect(collectDll(config, "product")).toEqual({
			copied: 0,
			removed: 0,
			skipped: 1,
			total: 1,
		});

		fs.writeFileSync(src, "product-dll-v2");

		expect(collectDll(config, "product")).toEqual({
			copied: 1,
			removed: 0,
			skipped: 0,
			total: 1,
		});
		expect(fs.readFileSync(out, "utf8")).toBe("product-dll-v2");
	});

	it("throws when source dll is missing", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);

		expect(() => collectDll(config, "product")).toThrow("DLL source file not found:");
	});
});
