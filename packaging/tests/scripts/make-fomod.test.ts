import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { appendWindowsWildcard, makeFomod } from "../../scripts/make-fomod.js";
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

		fs.outputFileSync(outFile, "old-archive");
		await makeFomod(config, { execaFn });

		expect(fs.existsSync(outFile)).toBe(false);
		expect(execaFn).toHaveBeenCalledWith(
			config.sevenzipPath,
			["a", outFile, `${config.buildTempDir}/*`],
			{ reject: false },
		);
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
