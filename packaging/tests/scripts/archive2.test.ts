import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { createArchives, parseArgs } from "../../scripts/archive2.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("archive2", () => {
	const dirs: string[] = [];

	afterEach(() => {
		vi.restoreAllMocks();
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("parseArgs defaults to product mode", () => {
		expect(parseArgs([])).toEqual({ mode: "product" });
	});

	it("parseArgs rejects invalid mode", () => {
		expect(() => parseArgs(["--mode=debug"])).toThrow('Invalid mode: debug. Must be "product".');
	});

	it("creates BA2 using provided execa implementation", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const filesRoot = path.join(config.buildTempDir, "files");
		const papyrusBinary = path.join(filesRoot, "papyrus", "product", "binary");
		const meshesRoot = path.join(filesRoot, "resources", "common", "Meshes");
		const execaFn = vi.fn().mockResolvedValue({ stdout: "ok", stderr: "", exitCode: 0 });

		fs.outputFileSync(path.join(papyrusBinary, "Script.pex"), "pex");
		fs.outputFileSync(path.join(meshesRoot, "mesh.nif"), "mesh");

		await createArchives(config, { mode: "product", execaFn });

		const tmpBa2Dir = path.join(filesRoot, "ba2", "tmp");
		const ba2Path = path.join(filesRoot, "ba2", "product", "LootMan - Main.ba2");
		expect(fs.existsSync(tmpBa2Dir)).toBe(false);
		expect(fs.existsSync(path.dirname(ba2Path))).toBe(true);
		expect(execaFn).toHaveBeenCalledWith(
			config.archive2Path,
			[
				tmpBa2Dir,
				`-r=${tmpBa2Dir}`,
				`-c=${ba2Path}`,
			],
			{ reject: false },
		);
	});

	it("throws when archive2 command fails", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const filesRoot = path.join(config.buildTempDir, "files");
		const papyrusBinary = path.join(filesRoot, "papyrus", "product", "binary");
		const meshesRoot = path.join(filesRoot, "resources", "common", "Meshes");
		const execaFn = vi.fn().mockResolvedValue({ stdout: "bad", stderr: "err", exitCode: 1 });

		fs.outputFileSync(path.join(papyrusBinary, "Script.pex"), "pex");
		fs.outputFileSync(path.join(meshesRoot, "mesh.nif"), "mesh");

		await expect(createArchives(config, { mode: "product", execaFn })).rejects.toThrow(
			"Archive2 failed to create product BA2. Check output above.",
		);
	});

	it("converts archive paths for WSL execution", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		const filesRoot = path.join(config.buildTempDir, "files");
		const papyrusBinary = path.join(filesRoot, "papyrus", "product", "binary");
		const meshesRoot = path.join(filesRoot, "resources", "common", "Meshes");
		const tmpBa2Dir = path.join(filesRoot, "ba2", "tmp");
		const ba2Path = path.join(filesRoot, "ba2", "product", "LootMan - Main.ba2");
		const runWindowsExeFn = vi.fn().mockResolvedValue({ stdout: "ok", stderr: "", exitCode: 0 });
		const toWindowsPathFn = vi.fn((value: string) => `WIN:${value}`);

		fs.outputFileSync(path.join(papyrusBinary, "Script.pex"), "pex");
		fs.outputFileSync(path.join(meshesRoot, "mesh.nif"), "mesh");

		await createArchives(config, { mode: "product", runWindowsExeFn, toWindowsPathFn });

		expect(runWindowsExeFn).toHaveBeenCalledWith(
			`WIN:${config.archive2Path}`,
			[
				`WIN:${tmpBa2Dir}`,
				`-r=WIN:${tmpBa2Dir}`,
				`-c=WIN:${ba2Path}`,
			],
			expect.objectContaining({
				execaFn: expect.any(Function),
				windowsCwd: `WIN:${tmpBa2Dir}`,
				windowsShell: "powershell",
			}),
		);
	});
});
