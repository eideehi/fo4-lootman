import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { buildDll, parseArgs, resolveBuildSteps } from "../../scripts/build-dll.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

function seedWslDllProject(config: ReturnType<typeof createTestConfig>): { projectDir: string; submoduleDir: string } {
	const projectDir = path.join(config.projectRoot, "commonlibf4-plugin");
	const submoduleDir = path.join(projectDir, "lib", "commonlibf4");
	fs.outputFileSync(path.join(projectDir, "xmake.lua"), "set_project(\"lootman\")\n");
	fs.outputFileSync(path.join(projectDir, "src", "main.cpp"), "int main() { return 0; }\n");
	fs.outputFileSync(path.join(submoduleDir, "xmake.lua"), "target(\"commonlibf4\")\n");
	return { projectDir, submoduleDir };
}

function createWslBuildRunner(config: ReturnType<typeof createTestConfig>) {
	return vi.fn().mockImplementation(async (...args) => {
		if (args[1][0] === "f") {
			fs.outputFileSync(
				path.join(config.wslStageDir, "build", "dll", "commonlibf4-plugin", ".xmake", "configured.txt"),
				"configured",
			);
		}
		if (args[1][0] === "build") {
			fs.outputFileSync(
				path.join(config.wslStageDir, "build", "dll", "commonlibf4-plugin", "build", "windows", "x64", "releasedbg", "lootman.dll"),
				"dll",
			);
		}
		return { exitCode: 0, stdout: "", stderr: "" };
	});
}

describe("build-dll", () => {
	const dirs: string[] = [];

	afterEach(() => {
		vi.restoreAllMocks();
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("parseArgs defaults to product", () => {
		expect(parseArgs([])).toEqual({ mode: "product" });
	});

	it("parseArgs validates product-only mode", () => {
		expect(parseArgs(["--mode=product"])).toEqual({ mode: "product" });
		expect(() => parseArgs(["--mode=debug"])).toThrow('Invalid mode: debug. Must be "product".');
	});

	it("resolveBuildSteps returns product commands", () => {
		expect(resolveBuildSteps()).toEqual([
			{ type: "argv", file: "xmake", args: ["f", "-m", "releasedbg", "-y"] },
			{ type: "argv", file: "xmake", args: ["build", "-y"] },
		]);
		expect(resolveBuildSteps(false)).toEqual([
			{ type: "argv", file: "xmake", args: ["build", "-y"] },
		]);
	});

	it("runs all steps when build succeeds", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const execaFn = vi.fn().mockResolvedValue({ exitCode: 0 });

		await buildDll(config, { mode: "product", execaFn });

		expect(execaFn).toHaveBeenCalledTimes(2);
		expect(execaFn).toHaveBeenNthCalledWith(
			1,
			"xmake",
			["f", "-m", "releasedbg", "-y"],
			expect.objectContaining({
				cwd: expect.stringContaining("commonlibf4-plugin"),
				reject: false,
				stdio: "pipe",
			}),
		);
	});

	it("uses Windows exe runner when config is WSL", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		seedWslDllProject(config);
		fs.outputFileSync(
			path.join(config.wslStageDir, "build", "dll", "commonlibf4-plugin", ".xmake", "cache.txt"),
			"cache",
		);
		const readSubmoduleCommitFn = vi.fn().mockReturnValue("commonlibf4-commit");
		const runWindowsExeFn = createWslBuildRunner(config);
		const execaFn = vi.fn();

		await buildDll(config, { mode: "product", execaFn, readSubmoduleCommitFn, runWindowsExeFn });

		expect(execaFn).not.toHaveBeenCalled();
		expect(runWindowsExeFn).toHaveBeenCalledTimes(2);
		expect(runWindowsExeFn).toHaveBeenNthCalledWith(
			1,
			"xmake",
			["f", "-m", "releasedbg", "-y"],
			expect.objectContaining({
				cwd: expect.stringContaining(path.join("wsl-stage", "build", "dll", "commonlibf4-plugin")),
				execaFn,
				stdio: "pipe",
			}),
		);
		expect(
			fs.readFileSync(path.join(config.projectRoot, "commonlibf4-plugin", "build", "windows", "x64", "releasedbg", "lootman.dll"), "utf8"),
		).toBe("dll");
		expect(
			fs.readFileSync(path.join(config.wslStageDir, "build", "dll", "commonlibf4-plugin", ".xmake", "cache.txt"), "utf8"),
		).toBe("cache");
	});

	it("throws with command details when a step fails", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const execaFn = vi.fn().mockResolvedValue({ exitCode: 2 });

		let thrown: unknown;
		try {
			await buildDll(config, { mode: "product", execaFn });
		} catch (error) {
			thrown = error;
		}

		expect(thrown).toBeInstanceOf(Error);
		expect((thrown as Error).message).toContain("DLL build failed (mode: product).");
		expect((thrown as Error).message).toContain("Command: xmake f -m releasedbg -y");
		expect((thrown as Error).message).toContain("Working directory:");
	});

	it("skips xmake configure when staged WSL project state is unchanged", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		seedWslDllProject(config);
		const readSubmoduleCommitFn = vi.fn().mockReturnValue("commonlibf4-commit");
		const runWindowsExeFn = createWslBuildRunner(config);

		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });
		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });

		expect(runWindowsExeFn).toHaveBeenCalledTimes(3);
		expect(runWindowsExeFn).toHaveBeenNthCalledWith(
			1,
			"xmake",
			["f", "-m", "releasedbg", "-y"],
			expect.any(Object),
		);
		expect(runWindowsExeFn).toHaveBeenNthCalledWith(
			2,
			"xmake",
			["build", "-y"],
			expect.any(Object),
		);
		expect(runWindowsExeFn).toHaveBeenNthCalledWith(
			3,
			"xmake",
			["build", "-y"],
			expect.any(Object),
		);
	});

	it("restores missing staged root files even when the stage state still matches", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		seedWslDllProject(config);
		const stagedRootXmake = path.join(config.wslStageDir, "build", "dll", "commonlibf4-plugin", "xmake.lua");
		const readSubmoduleCommitFn = vi.fn().mockReturnValue("commonlibf4-commit");
		const runWindowsExeFn = createWslBuildRunner(config);

		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });
		fs.removeSync(stagedRootXmake);
		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });

		expect(runWindowsExeFn).toHaveBeenCalledTimes(4);
		expect(fs.existsSync(stagedRootXmake)).toBe(true);
		expect(runWindowsExeFn).toHaveBeenNthCalledWith(
			3,
			"xmake",
			["f", "-m", "releasedbg", "-y"],
			expect.any(Object),
		);
	});

	it("restores missing staged commonlibf4 files even when the stage state still matches", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		seedWslDllProject(config);
		const stagedCommonLibXmake = path.join(
			config.wslStageDir,
			"build",
			"dll",
			"commonlibf4-plugin",
			"lib",
			"commonlibf4",
			"xmake.lua",
		);
		const readSubmoduleCommitFn = vi.fn().mockReturnValue("commonlibf4-commit");
		const runWindowsExeFn = createWslBuildRunner(config);

		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });
		fs.removeSync(stagedCommonLibXmake);
		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });

		expect(runWindowsExeFn).toHaveBeenCalledTimes(4);
		expect(fs.existsSync(stagedCommonLibXmake)).toBe(true);
		expect(runWindowsExeFn).toHaveBeenNthCalledWith(
			3,
			"xmake",
			["f", "-m", "releasedbg", "-y"],
			expect.any(Object),
		);
	});

	it("restages when a tracked source file changes after the first build", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		const { projectDir } = seedWslDllProject(config);
		const stagedSubmoduleSentinel = path.join(
			config.wslStageDir,
			"build",
			"dll",
			"commonlibf4-plugin",
			"lib",
			"commonlibf4",
			"sentinel.txt",
		);
		const readSubmoduleCommitFn = vi.fn().mockReturnValue("commonlibf4-commit");
		const runWindowsExeFn = createWslBuildRunner(config);

		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });
		fs.outputFileSync(stagedSubmoduleSentinel, "preserved");
		fs.outputFileSync(path.join(projectDir, "src", "main.cpp"), "int main() { return 42; }\n");
		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });

		expect(runWindowsExeFn).toHaveBeenCalledTimes(4);
		expect(fs.readFileSync(stagedSubmoduleSentinel, "utf8")).toBe("preserved");
		expect(runWindowsExeFn).toHaveBeenNthCalledWith(
			3,
			"xmake",
			["f", "-m", "releasedbg", "-y"],
			expect.any(Object),
		);
	});

	it("restages when a new source file is added", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		const { projectDir } = seedWslDllProject(config);
		const readSubmoduleCommitFn = vi.fn().mockReturnValue("commonlibf4-commit");
		const runWindowsExeFn = createWslBuildRunner(config);

		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });
		fs.outputFileSync(path.join(projectDir, "src", "extra.cpp"), "int extra() { return 7; }\n");
		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });

		expect(runWindowsExeFn).toHaveBeenCalledTimes(4);
		expect(runWindowsExeFn).toHaveBeenNthCalledWith(
			3,
			"xmake",
			["f", "-m", "releasedbg", "-y"],
			expect.any(Object),
		);
	});

	it("ignores non-input files when deciding whether to reuse the staged DLL project", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		const { projectDir } = seedWslDllProject(config);
		const readSubmoduleCommitFn = vi.fn().mockReturnValue("commonlibf4-commit");
		const runWindowsExeFn = createWslBuildRunner(config);

		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });
		fs.outputFileSync(path.join(projectDir, "README.md"), "notes\n");
		fs.outputFileSync(path.join(projectDir, "build", "ignored.txt"), "ignored\n");
		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });

		expect(runWindowsExeFn).toHaveBeenCalledTimes(3);
		expect(runWindowsExeFn).toHaveBeenNthCalledWith(
			3,
			"xmake",
			["build", "-y"],
			expect.any(Object),
		);
	});

	it("restages when the commonlibf4 submodule commit changes", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		seedWslDllProject(config);
		const stagedSubmoduleSentinel = path.join(
			config.wslStageDir,
			"build",
			"dll",
			"commonlibf4-plugin",
			"lib",
			"commonlibf4",
			"sentinel.txt",
		);
		let submoduleCommit = "commonlibf4-commit-a";
		const readSubmoduleCommitFn = vi.fn(() => submoduleCommit);
		const runWindowsExeFn = createWslBuildRunner(config);

		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });
		fs.outputFileSync(stagedSubmoduleSentinel, "removed");
		submoduleCommit = "commonlibf4-commit-b";
		await buildDll(config, { mode: "product", readSubmoduleCommitFn, runWindowsExeFn });

		expect(runWindowsExeFn).toHaveBeenCalledTimes(4);
		expect(fs.existsSync(stagedSubmoduleSentinel)).toBe(false);
		expect(runWindowsExeFn).toHaveBeenNthCalledWith(
			3,
			"xmake",
			["f", "-m", "releasedbg", "-y"],
			expect.any(Object),
		);
	});
});
