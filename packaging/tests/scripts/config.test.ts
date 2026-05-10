import fs from "fs-extra";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, it, vi } from "vitest";
import {
	buildPapyrusImportDirs,
	checkDir,
	checkFile,
	createConfig,
	isCliEntry,
	requireEnv,
} from "../../scripts/config.js";
import * as windowsPath from "../../scripts/windows-path.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

const packagingRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const repositoryRoot = path.resolve(packagingRoot, "..");

describe("config", () => {
	const dirs: string[] = [];
	const originalArgv = [...process.argv];
	const originalEnv = { ...process.env };

	afterEach(() => {
		process.argv = [...originalArgv];
		process.env = { ...originalEnv };
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("checkDir returns dir when present", () => {
		const root = createTempDir();
		dirs.push(root);
		expect(checkDir(root)).toBe(root);
	});

	it("checkDir throws for missing directory", () => {
		expect(() => checkDir("Z:/does/not/exist")).toThrow("Directory not found:");
	});

	it("checkFile returns file when present", () => {
		const root = createTempDir();
		dirs.push(root);
		const file = path.join(root, "a.txt");
		fs.writeFileSync(file, "x");
		expect(checkFile(file)).toBe(file);
	});

	it("checkFile throws for missing file", () => {
		expect(() => checkFile("Z:/does/not/exist.txt")).toThrow("File not found:");
	});

	it("requireEnv returns env var value", () => {
		process.env.TEST_ENV = "value";
		expect(requireEnv("TEST_ENV")).toBe("value");
	});

	it("requireEnv throws when empty or missing", () => {
		delete process.env.TEST_ENV;
		expect(() => requireEnv("TEST_ENV")).toThrow("TEST_ENV is not defined in environment");
		process.env.TEST_ENV = "";
		expect(() => requireEnv("TEST_ENV")).toThrow("TEST_ENV is not defined in environment");
	});

	it("isCliEntry accepts ts/js script names", () => {
		process.argv[1] = "/tmp/cli.ts";
		expect(isCliEntry("cli")).toBe(true);
		process.argv[1] = "/tmp/cli.js";
		expect(isCliEntry("cli")).toBe(true);
		process.argv[1] = "/tmp/other.ts";
		expect(isCliEntry("cli")).toBe(false);
	});

	it("buildPapyrusImportDirs returns Base then User", () => {
		const dirs = buildPapyrusImportDirs("/papyrus");
		expect(dirs.map((dir) => path.basename(dir))).toEqual(["Base", "User"]);
		expect(dirs[0].replaceAll("\\", "/")).toContain("/papyrus/Base");
		expect(dirs[1].replaceAll("\\", "/")).toContain("/papyrus/User");
	});

	it("createConfig rejects legacy parent game directory env without FO4_GAME_DIR", () => {
		const legacyGameDirEnv = ["STEAM", "GAME", "DIR"].join("_");

		delete process.env.FO4_GAME_DIR;
		process.env[legacyGameDirEnv] = "/tmp/steamapps/common";

		expect(() => createConfig()).toThrow("FO4_GAME_DIR is not defined in environment");
	});

	it("createConfig resolves required fields from environment and filesystem", () => {
		const root = createTempDir();
		dirs.push(root);
		const gameDir = path.join(root, "steamapps", "common", "Fallout 4");
		const projectRoot = path.join(root, "project");
		const sevenzipPath = path.join(root, "bin", "7z.exe");

		fs.mkdirsSync(projectRoot);
		fs.outputFileSync(sevenzipPath, "");
		fs.outputFileSync(path.join(gameDir, "Tools", "Archive2", "Archive2.exe"), "");
		fs.outputFileSync(path.join(gameDir, "Papyrus Compiler", "PapyrusCompiler.exe"), "");
		fs.mkdirsSync(path.join(gameDir, "Data", "Scripts", "Source", "Base"));
		fs.mkdirsSync(path.join(gameDir, "Data", "Scripts", "Source", "User"));
		fs.outputFileSync(path.join(gameDir, "Data", "Scripts", "Source", "Base", "Institute_Papyrus_Flags.flg"), "");

		process.env.FO4_GAME_DIR = gameDir;
		process.env.SEVENZIP_PATH = sevenzipPath;
		process.env.PROJECT_ROOT = projectRoot;
		delete process.env.DLL_BUILD_DIR;
		delete process.env.WSL_STAGE_DIR;

		const detectWslSpy = vi.spyOn(windowsPath, "detectWsl").mockReturnValue(false);

		const config = createConfig();
		expect(detectWslSpy).toHaveBeenCalled();
		expect(config.projectRoot).toBe(projectRoot);
		expect(config.fallout4Dir).toBe(gameDir);
		expect(config.archive2Path).toBe(path.join(gameDir, "Tools", "Archive2", "Archive2.exe"));
		expect(config.isWsl).toBe(false);
		expect(config.sevenzipPath).toBe(sevenzipPath);
		expect(config.wslStageDir).toBe(path.resolve("/mnt/c/tmp/lootman-wsl-build"));
		expect(config.papyrusFlagsPath).toBe(path.join(gameDir, "Data", "Scripts", "Source", "Base", "Institute_Papyrus_Flags.flg"));
		expect(config.papyrusImportDirs.map((dir) => path.basename(dir))).toEqual(["Base", "User"]);
		expect(config.archiveName).toContain(config.version);
		expect(config.buildTempDir.replaceAll("\\", "/")).toContain(`/build/${config.version}`);
	});

	it("createConfig respects WSL stage override", () => {
		const root = createTempDir();
		dirs.push(root);
		const gameDir = path.join(root, "steamapps", "common", "Fallout 4");
		const projectRoot = path.join(root, "project");
		const sevenzipPath = path.join(root, "bin", "7z.exe");

		fs.mkdirsSync(projectRoot);
		fs.outputFileSync(sevenzipPath, "");
		fs.outputFileSync(path.join(gameDir, "Tools", "Archive2", "Archive2.exe"), "");
		fs.outputFileSync(path.join(gameDir, "Papyrus Compiler", "PapyrusCompiler.exe"), "");
		fs.mkdirsSync(path.join(gameDir, "Data", "Scripts", "Source", "Base"));
		fs.mkdirsSync(path.join(gameDir, "Data", "Scripts", "Source", "User"));
		fs.outputFileSync(path.join(gameDir, "Data", "Scripts", "Source", "Base", "Institute_Papyrus_Flags.flg"), "");

		process.env.FO4_GAME_DIR = gameDir;
		process.env.SEVENZIP_PATH = sevenzipPath;
		process.env.PROJECT_ROOT = projectRoot;
		process.env.WSL_STAGE_DIR = path.join(root, "stage");
		vi.spyOn(windowsPath, "detectWsl").mockReturnValue(true);

		const config = createConfig();

		expect(config.isWsl).toBe(true);
		expect(config.wslStageDir).toBe(path.join(root, "stage"));
	});

	it("resolves relative PROJECT_ROOT from packaging/.env location", () => {
		const root = createTempDir();
		dirs.push(root);
		const gameDir = path.join(root, "steamapps", "common", "Fallout 4");
		const sevenzipPath = path.join(root, "bin", "7z.exe");

		fs.outputFileSync(sevenzipPath, "");
		fs.outputFileSync(path.join(gameDir, "Tools", "Archive2", "Archive2.exe"), "");
		fs.outputFileSync(path.join(gameDir, "Papyrus Compiler", "PapyrusCompiler.exe"), "");
		fs.mkdirsSync(path.join(gameDir, "Data", "Scripts", "Source", "Base"));
		fs.mkdirsSync(path.join(gameDir, "Data", "Scripts", "Source", "User"));
		fs.outputFileSync(path.join(gameDir, "Data", "Scripts", "Source", "Base", "Institute_Papyrus_Flags.flg"), "");

		process.env.FO4_GAME_DIR = gameDir;
		process.env.SEVENZIP_PATH = sevenzipPath;
		process.env.PROJECT_ROOT = "../";
		vi.spyOn(windowsPath, "detectWsl").mockReturnValue(false);

		const config = createConfig();

		expect(config.projectRoot).toBe(repositoryRoot);
	});

	it("createConfig throws when SEVENZIP_PATH does not exist", () => {
		const root = createTempDir();
		dirs.push(root);
		const gameDir = path.join(root, "steamapps", "common", "Fallout 4");
		const projectRoot = path.join(root, "project");

		fs.mkdirsSync(projectRoot);
		fs.outputFileSync(path.join(gameDir, "Tools", "Archive2", "Archive2.exe"), "");
		fs.outputFileSync(path.join(gameDir, "Papyrus Compiler", "PapyrusCompiler.exe"), "");
		fs.mkdirsSync(path.join(gameDir, "Data", "Scripts", "Source", "Base"));
		fs.mkdirsSync(path.join(gameDir, "Data", "Scripts", "Source", "User"));

		process.env.FO4_GAME_DIR = gameDir;
		process.env.SEVENZIP_PATH = path.join(root, "bin", "missing-7z.exe");
		process.env.PROJECT_ROOT = projectRoot;
		vi.spyOn(windowsPath, "detectWsl").mockReturnValue(false);

		expect(() => createConfig()).toThrow("File not found:");
	});
});
