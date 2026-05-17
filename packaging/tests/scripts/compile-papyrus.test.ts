import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { hashFile } from "../../scripts/content-hash.js";
import {
	buildPapyrusImportSearchDirs,
	buildPapyrusPpjImportDirs,
	buildPapyrusPpjWindowsImportDirs,
	compilePapyrus,
	deployPex,
	formatResolvedPapyrusImports,
	getPapyrusOverlayDir,
	getResolvedRequiredPapyrusImports,
	parseArgs,
	prepareF4SEOverlay,
	pruneStaleCachedPex,
	resolveImportScriptPath,
	toScriptName,
	verifyPapyrusImportSymbols,
} from "../../scripts/compile-papyrus.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

function seedRequiredPapyrusSymbols(importDir: string): void {
	fs.outputFileSync(
		path.join(importDir, "Math.psc"),
		[
			"ScriptName Math",
			"Function LogicalAnd()",
			"EndFunction",
			"Function LogicalXor()",
			"EndFunction",
		].join("\n"),
	);
	fs.outputFileSync(
		path.join(importDir, "ObjectReference.psc"),
		[
			"ScriptName ObjectReference",
			"Function GetDisplayName()",
			"EndFunction",
			"Function GetInventoryWeight()",
			"EndFunction",
		].join("\n"),
	);
	fs.outputFileSync(
		path.join(importDir, "ScriptObject.psc"),
		[
			"ScriptName ScriptObject",
			"Function RegisterForExternalEvent()",
			"EndFunction",
			"Function UnregisterForExternalEvent()",
			"EndFunction",
		].join("\n"),
	);
	fs.outputFileSync(
		path.join(importDir, "MiscObject.psc"),
		[
			"ScriptName MiscObject",
			"struct MiscComponent",
			"int value",
			"endStruct",
		].join("\n"),
	);
}

function seedCompileLayout(root: string) {
	const config = createTestConfig(root);
	const sourceDir = path.join(config.buildTempDir, "files", "papyrus", "product", "source");
	const modeCacheDir = path.join(config.buildDirRoot, "cache", "papyrus", "binary", "product");
	const hashesPath = path.join(config.buildDirRoot, "cache", "papyrus", "script-hashes-product.json");
	const f4seSourceDir = path.join(config.papyrusSourceDir, "F4SE");
	const importsUser = config.papyrusImportDirs[1];

	fs.outputFileSync(path.join(config.templatesRoot, "papyrus.ppj"), "FLAGS=__FLAGS__\n__OUTPUT_DIR__\n__IS_PRODUCT__\n__IMPORTS__\n__SCRIPTS__");
	fs.outputFileSync(path.join(sourceDir, "LTMN", "Test.psc"), "ScriptName LTMN:Test");
	fs.outputFileSync(path.join(f4seSourceDir, "OverlayOnly.psc"), "ScriptName OverlayOnly");
	seedRequiredPapyrusSymbols(importsUser);

	return { config, sourceDir, modeCacheDir, hashesPath };
}

describe("compile-papyrus helpers", () => {
	const dirs: string[] = [];

	afterEach(() => {
		vi.restoreAllMocks();
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("toScriptName normalizes extension, separator, and case", () => {
		expect(toScriptName("LTMN\\Test.psc")).toBe("ltmn:test");
		expect(toScriptName("LTMN/Test.pex")).toBe("ltmn:test");
	});

	it("deployPex copies only valid scripts", async () => {
		const root = createTempDir();
		dirs.push(root);
		const cacheDir = path.join(root, "cache");
		const outDir = path.join(root, "out");

		fs.outputFileSync(path.join(cacheDir, "LTMN", "Test.pex"), "valid");
		fs.outputFileSync(path.join(cacheDir, "LTMN", "Other.pex"), "invalid");
		await deployPex(cacheDir, outDir, { "ltmn:test": "hash" });

		expect(fs.readFileSync(path.join(outDir, "LTMN", "Test.pex"), "utf8")).toBe("valid");
		expect(fs.existsSync(path.join(outDir, "LTMN", "Other.pex"))).toBe(false);
	});

	it("deployPex returns early when cache dir does not exist", async () => {
		const root = createTempDir();
		dirs.push(root);
		const outDir = path.join(root, "out");

		await deployPex(path.join(root, "missing"), outDir, {});
		expect(fs.existsSync(outDir)).toBe(true);
	});

	it("pruneStaleCachedPex removes cached binaries without matching sources", async () => {
		const root = createTempDir();
		dirs.push(root);
		const cacheDir = path.join(root, "cache");

		fs.outputFileSync(path.join(cacheDir, "LTMN", "Test.pex"), "valid");
		fs.outputFileSync(path.join(cacheDir, "LTMN", "Debug.pex"), "stale");
		await pruneStaleCachedPex(cacheDir, { "ltmn:test": "hash" });

		expect(fs.existsSync(path.join(cacheDir, "LTMN", "Test.pex"))).toBe(true);
		expect(fs.existsSync(path.join(cacheDir, "LTMN", "Debug.pex"))).toBe(false);
	});

	it("resolveImportScriptPath prefers earlier import directory", () => {
		const root = createTempDir();
		dirs.push(root);
		const base = path.join(root, "Base");
		const user = path.join(root, "User");
		fs.outputFileSync(path.join(base, "Math.psc"), "base");
		fs.outputFileSync(path.join(user, "Math.psc"), "user");

		expect(resolveImportScriptPath("Math.psc", [user, base])).toBe(path.join(user, "Math.psc"));
		expect(resolveImportScriptPath("Missing.psc", [base, user])).toBeNull();
	});

	it("resolves and formats required import paths", () => {
		const root = createTempDir();
		dirs.push(root);
		const imports = path.join(root, "Imports");
		seedRequiredPapyrusSymbols(imports);

		const resolved = getResolvedRequiredPapyrusImports([imports]);
		expect(Object.keys(resolved).sort()).toEqual(["Math.psc", "MiscObject.psc", "ObjectReference.psc", "ScriptObject.psc"]);

		const formatted = formatResolvedPapyrusImports(resolved);
		expect(formatted).toContain("- Math.psc:");
		expect(formatted).toContain("- ScriptObject.psc:");
	});

	it("verifyPapyrusImportSymbols passes when all symbols exist", () => {
		const root = createTempDir();
		dirs.push(root);
		const imports = path.join(root, "Imports");
		seedRequiredPapyrusSymbols(imports);

		expect(() => verifyPapyrusImportSymbols([imports])).not.toThrow();
	});

	it("verifyPapyrusImportSymbols throws on missing symbols", () => {
		const root = createTempDir();
		dirs.push(root);
		const imports = path.join(root, "Imports");
		fs.outputFileSync(path.join(imports, "Math.psc"), "ScriptName Math");

		expect(() => verifyPapyrusImportSymbols([imports])).toThrow("Papyrus import verification failed.");
	});

	it("parseArgs defaults to product and validates mode", () => {
		expect(parseArgs([])).toEqual({ mode: "product" });
		expect(parseArgs(["--mode=product"])).toEqual({ mode: "product" });
		expect(() => parseArgs(["--mode=debug"])).toThrow('Invalid mode: debug. Must be "product".');
	});

	it("prepareF4SEOverlay recreates overlay and copies source files", async () => {
		const root = createTempDir();
		dirs.push(root);
		const sourceF4SE = path.join(root, "source");
		const overlay = path.join(root, "overlay");

		fs.outputFileSync(path.join(sourceF4SE, "A.psc"), "A");
		fs.outputFileSync(path.join(sourceF4SE, "Nested", "B.psc"), "B");
		fs.outputFileSync(path.join(overlay, "stale.txt"), "stale");

		await prepareF4SEOverlay(sourceF4SE, overlay);

		expect(fs.existsSync(path.join(overlay, "stale.txt"))).toBe(false);
		expect(fs.readFileSync(path.join(overlay, "A.psc"), "utf8")).toBe("A");
		expect(fs.readFileSync(path.join(overlay, "Nested", "B.psc"), "utf8")).toBe("B");
	});

	it("buildPapyrusPpjImportDirs orders overlay then User then Base", () => {
		const ordered = buildPapyrusPpjImportDirs("/overlay", ["/imports/Base", "/imports/Other", "/imports/User"]);
		expect(ordered).toEqual([".", "/overlay", "/imports/User", "/imports/Base", "/imports/Other"]);
	});

	it("buildPapyrusImportSearchDirs prioritizes source then overlay then User then Base", () => {
		const ordered = buildPapyrusImportSearchDirs("/source", "/overlay", ["/imports/Base", "/imports/Other", "/imports/User"]);
		expect(ordered).toEqual(["/source", "/overlay", "/imports/User", "/imports/Base", "/imports/Other"]);
	});

	it("buildPapyrusPpjWindowsImportDirs converts import directories for WSL", () => {
		const config = createTestConfig("/tmp/root", { isWsl: true });
		const toWindowsPathFn = vi.fn((value: string) => `WIN:${value}`);

		const ordered = buildPapyrusPpjWindowsImportDirs(config, "/overlay", ["/imports/Base", "/imports/User"], toWindowsPathFn);

		expect(ordered).toEqual([".", "WIN:/overlay", "WIN:/imports/User", "WIN:/imports/Base"]);
	});
});

describe("compilePapyrus", () => {
	const dirs: string[] = [];

	afterEach(() => {
		vi.restoreAllMocks();
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("throws when Source/F4SE directory is missing", async () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const sourceDir = path.join(config.buildTempDir, "files", "papyrus", "product", "source");

		fs.outputFileSync(path.join(config.templatesRoot, "papyrus.ppj"), "__SCRIPTS__");
		fs.outputFileSync(path.join(sourceDir, "A.psc"), "ScriptName A");
		seedRequiredPapyrusSymbols(config.papyrusImportDirs[1]);

		await expect(compilePapyrus(config)).rejects.toThrow("Papyrus F4SE source directory not found.");
	});

	it("compiles changed scripts, updates hash cache, and deploys pex", async () => {
		const root = createTempDir();
		dirs.push(root);
		const { config, modeCacheDir, hashesPath } = seedCompileLayout(root);
		let compiledProject = "";
		const execaFn = vi.fn().mockImplementation(async (_file: string, args: string[]) => {
			compiledProject = fs.readFileSync(args[0]!, "utf8");
			return { exitCode: 0, stdout: "compiled", stderr: "" };
		});
		const deployedPex = path.join(config.buildTempDir, "files", "papyrus", "product", "binary", "LTMN", "Test.pex");

		fs.outputFileSync(path.join(modeCacheDir, "LTMN", "Test.pex"), "compiled-binary");
		await compilePapyrus(config, { mode: "product", execaFn });

		const overlayDir = getPapyrusOverlayDir(config, "product");
		expect(fs.readFileSync(path.join(overlayDir, "OverlayOnly.psc"), "utf8")).toBe("ScriptName OverlayOnly");
		expect(fs.existsSync(path.join(config.buildTempDir, "files", "papyrus", "product", "overlay"))).toBe(false);
		expect(execaFn).toHaveBeenCalledTimes(1);
		expect(fs.existsSync(hashesPath)).toBe(true);
		expect(fs.existsSync(deployedPex)).toBe(true);
		expect(fs.existsSync(path.join(config.buildTempDir, "files", "papyrus", "product", "source", "product-papyrus.ppj"))).toBe(
			false,
		);
		expect(compiledProject).toContain("FLAGS=Institute_Papyrus_Flags.flg");
	});

	it("skips compiler execution when scripts and cache are unchanged", async () => {
		const root = createTempDir();
		dirs.push(root);
		const { config, sourceDir, modeCacheDir, hashesPath } = seedCompileLayout(root);
		const sourceScriptPath = path.join(sourceDir, "LTMN", "Test.psc");
		const scriptHash = hashFile(sourceScriptPath);
		const execaFn = vi.fn().mockResolvedValue({ exitCode: 0, stdout: "compiled", stderr: "" });

		fs.outputFileSync(path.join(modeCacheDir, "LTMN", "Test.pex"), "cached-binary");
		fs.outputFileSync(path.join(modeCacheDir, "LTMN", "Debug.pex"), "stale-binary");
		fs.outputJsonSync(hashesPath, { "ltmn:test": scriptHash });
		await compilePapyrus(config, { mode: "product", execaFn });

		expect(execaFn).not.toHaveBeenCalled();
		expect(fs.readJsonSync(hashesPath)).toEqual({ "ltmn:test": scriptHash });
		expect(fs.existsSync(path.join(modeCacheDir, "LTMN", "Debug.pex"))).toBe(false);
	});

	it("throws when compiler output reports failed scripts", async () => {
		const root = createTempDir();
		dirs.push(root);
		const { config } = seedCompileLayout(root);
		const execaFn = vi.fn().mockResolvedValue({ exitCode: 0, stdout: "1 failed", stderr: "" });

		await expect(compilePapyrus(config, { mode: "product", execaFn })).rejects.toThrow(
			"Product Papyrus compilation failed. Check compiler output above.",
		);
	});

	it("uses Windows exe runner and Windows paths on WSL", async () => {
		const root = createTempDir();
		dirs.push(root);
		const { config, sourceDir, modeCacheDir } = seedCompileLayout(root);
		config.isWsl = true;
		const overlayDir = getPapyrusOverlayDir(config, "product");
		let compiledProject = "";
		const runWindowsExeFn = vi.fn().mockImplementation(async (_file: string, args: string[]) => {
			compiledProject = fs.readFileSync(args[0]!.replace("WIN:", ""), "utf8");
			fs.outputFileSync(path.join(modeCacheDir, "LTMN", "Test.pex"), "compiled-binary");
			return { exitCode: 0, stdout: "compiled", stderr: "" };
		});
		const toWindowsPathFn = vi.fn((value: string) => `WIN:${value}`);

		await compilePapyrus(config, { mode: "product", runWindowsExeFn, toWindowsPathFn });

		expect(runWindowsExeFn).toHaveBeenCalledTimes(1);
		expect(runWindowsExeFn).toHaveBeenCalledWith(
			`WIN:${config.papyrusCompilerPath}`,
			[`WIN:${path.join(sourceDir, "product-papyrus.ppj")}`],
			expect.objectContaining({
				windowsCwd: `WIN:${sourceDir}`,
				windowsShell: "powershell",
				stdio: "pipe",
			}),
		);
		const deployedPex = path.join(config.buildTempDir, "files", "papyrus", "product", "binary", "LTMN", "Test.pex");
		expect(fs.readFileSync(deployedPex, "utf8")).toBe("compiled-binary");
		expect(toWindowsPathFn).toHaveBeenCalledWith(modeCacheDir, { isWsl: true });
		expect(toWindowsPathFn).toHaveBeenCalledWith(config.papyrusFlagsPath, { isWsl: true });
		expect(toWindowsPathFn).toHaveBeenCalledWith(sourceDir, { isWsl: true });
		expect(compiledProject).toContain(`FLAGS=WIN:${config.papyrusFlagsPath}`);
		expect(compiledProject).toContain(`<Import>WIN:${overlayDir}</Import>`);
		expect(compiledProject).toContain("<Script>ltmn:test</Script>");
	});
});
