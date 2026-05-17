import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { appendWindowsWildcard, makeFomod, removeStaleLooseFomodFiles } from "../../scripts/make-fomod.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("make-fomod", () => {
	const dirs: string[] = [];

	afterEach(() => {
		vi.restoreAllMocks();
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("creates archive command with expected output path", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const execaFn = vi.fn().mockResolvedValue({ stdout: "done", stderr: "", exitCode: 0 });
		const outFile = path.join(config.packagingRoot, "dist", `${config.archiveName}.7z`);
		const staleModuleConfig = path.join(config.packagingRoot, "dist", "ModuleConfig.xml");
		const staleInfo = path.join(config.packagingRoot, "dist", "info.xml");

		fs.outputFileSync(outFile, "old-archive");
		fs.outputFileSync(staleModuleConfig, "old module config");
		fs.outputFileSync(staleInfo, "old info");
		fs.outputFileSync(path.join(config.buildTempDir, "files", "papyrus", "product", "source", "LTMN2", "System.psc"), "ScriptName LTMN2:System");
		await makeFomod(config, { execaFn });

		expect(fs.existsSync(outFile)).toBe(false);
		expect(fs.existsSync(staleModuleConfig)).toBe(false);
		expect(fs.existsSync(staleInfo)).toBe(false);
		expect(execaFn).toHaveBeenCalledWith(
			config.sevenzipPath,
			["a", outFile, `${config.buildTempDir}/*`],
			{ reject: false },
		);
	});

	it("removes stale loose FOMOD metadata from dist", () => {
		const root = createTempDir();
		dirs.push(root);
		const outDir = path.join(root, "packaging", "dist");
		const archive = path.join(outDir, "LootMan - 9.9.9-test.7z");

		fs.outputFileSync(path.join(outDir, "ModuleConfig.xml"), "stale module config");
		fs.outputFileSync(path.join(outDir, "info.xml"), "stale info");
		fs.outputFileSync(archive, "archive");
		removeStaleLooseFomodFiles(outDir);

		expect(fs.existsSync(path.join(outDir, "ModuleConfig.xml"))).toBe(false);
		expect(fs.existsSync(path.join(outDir, "info.xml"))).toBe(false);
		expect(fs.readFileSync(archive, "utf8")).toBe("archive");
	});

	it("throws when 7zip returns non-zero code", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const execaFn = vi.fn().mockResolvedValue({ stdout: "x", stderr: "err", exitCode: 2 });

		await expect(makeFomod(config, { execaFn })).rejects.toThrow(
			"7-Zip failed to create FOMOD archive. Check output above.",
		);
	});

	it("fails before 7zip when archived F4SE overlay sources are present", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const execaFn = vi.fn().mockResolvedValue({ stdout: "done", stderr: "", exitCode: 0 });

		fs.outputFileSync(path.join(config.buildTempDir, "files", "papyrus", "product", "overlay", "f4se", "Actor.psc"), "ScriptName Actor");

		await expect(makeFomod(config, { execaFn })).rejects.toThrow(
			"Forbidden Papyrus source artifacts found in distributable archive input.",
		);
		expect(execaFn).not.toHaveBeenCalled();
	});

	it("fails before 7zip when other official Papyrus source artifacts are present", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const execaFn = vi.fn().mockResolvedValue({ stdout: "done", stderr: "", exitCode: 0 });

		for (const file of [
			"files/papyrus/product/source/Actor.psc",
			"files/papyrus/product/source/Base/Actor.psc",
			"files/papyrus/product/source/F4SE/ObjectReference.psc",
			"files/papyrus/product/source/User/MCM.psc",
			"files/papyrus/product/source/product-papyrus.ppj",
			"files/papyrus/product/source/Institute_Papyrus_Flags.flg",
			"files/Scripts/Source/Base/Actor.psc",
		]) {
			fs.outputFileSync(path.join(config.buildTempDir, file), "source");
		}

		await expect(makeFomod(config, { execaFn })).rejects.toThrow("These files must not be packaged:");
		expect(execaFn).not.toHaveBeenCalled();
	});

	it("formats Windows wildcard paths without duplicate separators", () => {
		expect(appendWindowsWildcard("C:\\tmp\\build")).toBe("C:\\tmp\\build\\*");
		expect(appendWindowsWildcard("C:\\tmp\\build\\")).toBe("C:\\tmp\\build\\*");
	});

	it("uses Windows exe runner when config is WSL", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		const outFile = path.join(config.packagingRoot, "dist", `${config.archiveName}.7z`);
		fs.outputFileSync(path.join(config.buildTempDir, "files", "demo.txt"), "demo");
		const runWindowsExeFn = vi.fn().mockResolvedValue({ stdout: "done", stderr: "", exitCode: 0 });
		const toWindowsPathFn = vi.fn((value: string) => `WIN:${value}`);

		await makeFomod(config, { runWindowsExeFn, toWindowsPathFn });

		expect(runWindowsExeFn).toHaveBeenCalledWith(
			`WIN:${config.sevenzipPath}`,
			[
				"a",
				`WIN:${outFile}`,
				`WIN:${config.buildTempDir}\\*`,
			],
			expect.objectContaining({
				windowsCwd: `WIN:${config.buildTempDir}`,
				windowsShell: "powershell",
			}),
		);
	});
});
