import fs from "fs-extra";
import path from "node:path";
import { glob } from "glob";
import { execa } from "execa";
import { DEFAULT_BUILD_MODE, parseBuildModeArg, type BuildMode } from "./build-mode.js";
import { hashFile } from "./content-hash.js";
import { type Config, createConfig, isCliEntry } from "./config.js";
import { createFileProgress, runWhile } from "./progress.js";
import { replace } from "./replace-text.js";
import { runWindowsExe } from "./windows-exec.js";
import { toWindowsPath } from "./windows-path.js";

export function toScriptName(relativePath: string): string {
	return relativePath
		.replace(/\.(psc|pex)$/i, "")
		.replace(/[\\/]/g, ":")
		.toLowerCase();
}

export async function deployPex(
	cacheDir: string,
	outDir: string,
	validScripts: Record<string, string>,
): Promise<void> {
	fs.removeSync(outDir);
	fs.mkdirsSync(outDir);

	if (!fs.existsSync(cacheDir)) return;

	const pexFiles = await glob("**/*.pex", { cwd: cacheDir });
	const progress = createFileProgress(pexFiles.length, "Deploying Papyrus binaries");
	try {
		for (const file of pexFiles) {
			const scriptName = toScriptName(file);

			if (Object.hasOwn(validScripts, scriptName)) {
				fs.copySync(path.join(cacheDir, file), path.join(outDir, file));
			}
			progress.advance();
		}
	} finally {
		progress.finish();
	}
}

export async function pruneStaleCachedPex(
	cacheDir: string,
	validScripts: Record<string, string>,
): Promise<void> {
	if (!fs.existsSync(cacheDir)) return;

	const pexFiles = await glob("**/*.pex", { cwd: cacheDir });
	for (const file of pexFiles) {
		const scriptName = toScriptName(file);
		if (!Object.hasOwn(validScripts, scriptName)) {
			fs.removeSync(path.join(cacheDir, file));
		}
	}
}

export interface CompilePapyrusOpts {
	execaFn?: typeof execa;
	mode?: BuildMode;
	runWindowsExeFn?: typeof runWindowsExe;
	toWindowsPathFn?: typeof toWindowsPath;
}

interface RequiredPapyrusSymbol {
	scriptFile: string;
	needle: string;
	label: string;
}

const REQUIRED_PAPYRUS_SYMBOLS: RequiredPapyrusSymbol[] = [
	{ scriptFile: "Math.psc", needle: "LogicalAnd", label: "Math.LogicalAnd" },
	{ scriptFile: "Math.psc", needle: "LogicalXor", label: "Math.LogicalXor" },
	{ scriptFile: "ObjectReference.psc", needle: "GetDisplayName", label: "ObjectReference.GetDisplayName" },
	{ scriptFile: "ObjectReference.psc", needle: "GetInventoryWeight", label: "ObjectReference.GetInventoryWeight" },
	{ scriptFile: "ScriptObject.psc", needle: "RegisterForExternalEvent", label: "ScriptObject.RegisterForExternalEvent" },
	{ scriptFile: "ScriptObject.psc", needle: "UnregisterForExternalEvent", label: "ScriptObject.UnregisterForExternalEvent" },
	{ scriptFile: "MiscObject.psc", needle: "struct MiscComponent", label: "MiscObject.MiscComponent" },
];

export function resolveImportScriptPath(scriptFile: string, importDirs: string[]): string | null {
	// PPJ import resolution is first-listed wins, so search from the front and
	// return the first matching file in priority order.
	for (const dir of importDirs) {
		const filePath = path.join(dir, scriptFile);
		if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
			return filePath;
		}
	}
	return null;
}

export function getResolvedRequiredPapyrusImports(importDirs: string[]): Record<string, string | null> {
	const scripts = Array.from(new Set(REQUIRED_PAPYRUS_SYMBOLS.map((item) => item.scriptFile)));
	const resolved: Record<string, string | null> = {};
	for (const script of scripts) {
		resolved[script] = resolveImportScriptPath(script, importDirs);
	}
	return resolved;
}

export function formatResolvedPapyrusImports(resolved: Record<string, string | null>): string {
	return Object.entries(resolved)
		.map(([script, filePath]) => `- ${script}: ${filePath ?? "NOT FOUND"}`)
		.join("\n");
}

export function verifyPapyrusImportSymbols(importDirs: string[]): void {
	const missing: string[] = [];
	const resolvedImports = getResolvedRequiredPapyrusImports(importDirs);
	for (const [scriptFile, resolved] of Object.entries(resolvedImports)) {
		const related = REQUIRED_PAPYRUS_SYMBOLS.filter((item) => item.scriptFile === scriptFile);
		if (resolved === null) {
			for (const item of related) {
				missing.push(`${item.label} (${scriptFile} not found in import dirs)`);
			}
			continue;
		}

		const content = fs.readFileSync(resolved, "utf8");
		for (const item of related) {
			if (!content.includes(item.needle)) {
				missing.push(`${item.label} (missing in ${resolved})`);
			}
		}
	}

	if (missing.length > 0) {
		throw new Error([
			"Papyrus import verification failed.",
			"Resolved scripts:",
			formatResolvedPapyrusImports(resolvedImports),
			"Missing required symbols:",
			...missing.map((item) => `- ${item}`),
			"Ensure F4SE-extended Papyrus sources are installed and import priority is configured correctly.",
		].join("\n"));
	}
}

export function parseArgs(argv: string[]): { mode: BuildMode } {
	return { mode: parseBuildModeArg(argv) };
}

function resolveImportDirByName(importDirs: string[], dirName: string): string | null {
	for (const dir of importDirs) {
		if (path.basename(dir).toLowerCase() === dirName.toLowerCase()) {
			return dir;
		}
	}
	return null;
}

function getPapyrusF4SESourceDir(papyrusSourceDir: string): string {
	return path.resolve(papyrusSourceDir, "F4SE");
}

export async function prepareF4SEOverlay(sourceF4SEDir: string, overlayDir: string): Promise<void> {
	fs.removeSync(overlayDir);
	fs.mkdirsSync(overlayDir);
	const sourceFiles = await glob("**/*.psc", { cwd: sourceF4SEDir });
	const progress = createFileProgress(sourceFiles.length, "Staging F4SE overlay");
	try {
		for (const file of sourceFiles) {
			const srcPath = path.join(sourceF4SEDir, file);
			const destPath = path.join(overlayDir, file);
			fs.copySync(srcPath, destPath);
			progress.advance();
		}
	} finally {
		progress.finish();
	}
}

export function buildPapyrusPpjImportDirs(overlayDir: string, importDirs: string[]): string[] {
	const ordered: string[] = [".", overlayDir];
	const used = new Set<string>();
	const preferredOrder = ["User", "Base"];

	for (const name of preferredOrder) {
		const resolved = resolveImportDirByName(importDirs, name);
		if (resolved !== null) {
			ordered.push(resolved);
			used.add(resolved);
		}
	}

	for (const dir of importDirs) {
		if (!used.has(dir)) {
			ordered.push(dir);
		}
	}

	return ordered;
}

export function buildPapyrusImportSearchDirs(sourceDir: string, overlayDir: string, importDirs: string[]): string[] {
	return [
		sourceDir,
		...buildPapyrusPpjImportDirs(overlayDir, importDirs).filter((dir) => dir !== "."),
	];
}

export function buildPapyrusPpjWindowsImportDirs(
	config: Config,
	overlayDir: string,
	importDirs: string[],
	toWindowsPathFn: typeof toWindowsPath = toWindowsPath,
): string[] {
	return buildPapyrusPpjImportDirs(overlayDir, importDirs).map((dir) => {
		if (dir === ".") {
			return dir;
		}
		return config.isWsl ? toWindowsPathFn(dir, { isWsl: true }) : dir;
	});
}

export async function compilePapyrus(config: Config, opts?: CompilePapyrusOpts): Promise<void> {
	const execaImpl = opts?.execaFn ?? execa;
	const mode = opts?.mode ?? DEFAULT_BUILD_MODE;
	const runWindowsExeFn = opts?.runWindowsExeFn ?? runWindowsExe;
	const toWindowsPathFn = opts?.toWindowsPathFn ?? toWindowsPath;

	// Cache directories for incremental compilation
	const papyrusCacheDir = path.join(config.buildDirRoot, "cache", "papyrus");
	const modeCacheDir = path.join(papyrusCacheDir, "binary", mode);
	const scriptHashesPath = path.join(papyrusCacheDir, `script-hashes-${mode}.json`);

	// Ensure cache exists
	if (!fs.existsSync(scriptHashesPath)) {
		fs.mkdirsSync(path.dirname(scriptHashesPath));
		fs.writeJsonSync(scriptHashesPath, {});
	}
	const scriptHashes: Record<string, string> = fs.readJsonSync(scriptHashesPath);

	// Build output directories
	const papyrusDirRoot = path.join(config.buildTempDir, "files", "papyrus");
	const productDir = path.join(papyrusDirRoot, "product");
	const sourceDir = path.join(productDir, "source");
	const overlayRootDir = path.join(productDir, "overlay");
	const modeOutDir = path.join(papyrusDirRoot, mode, "binary");

	// PPJ template
	const papyrusProjectTemplate = path.join(config.templatesRoot, "papyrus.ppj");
	const modePpj = path.join(sourceDir, `${mode}-papyrus.ppj`);

	const f4seSourceDir = getPapyrusF4SESourceDir(config.papyrusSourceDir);
	if (!fs.existsSync(f4seSourceDir) || !fs.statSync(f4seSourceDir).isDirectory()) {
		throw new Error([
			"Papyrus F4SE source directory not found.",
			`Expected: ${f4seSourceDir}`,
			"Source/F4SE is required and must be prepared by developers.",
		].join("\n"));
	}
	// Remove legacy overlay location under source so it never gets compiled as scripts.
	fs.removeSync(path.join(sourceDir, "__overlay__"));
	const overlayDir = path.join(overlayRootDir, "f4se");
	await prepareF4SEOverlay(f4seSourceDir, overlayDir);

	const importSearchDirs = buildPapyrusImportSearchDirs(sourceDir, overlayDir, config.papyrusImportDirs);
	const resolvedImports = getResolvedRequiredPapyrusImports(importSearchDirs);
	verifyPapyrusImportSymbols(importSearchDirs);

	// Scan sources and compute hashes
	const newHashes: Record<string, string> = {};
	const scriptsToCompile = new Set<string>();

	const sourceFiles = await glob("**/*.psc", { cwd: sourceDir });
	for (const file of sourceFiles) {
		const filePath = path.join(sourceDir, file);
		const scriptName = toScriptName(file);
		const hash = hashFile(filePath);

		newHashes[scriptName] = hash;

		if (!Object.hasOwn(scriptHashes, scriptName) || scriptHashes[scriptName] !== hash) {
			scriptsToCompile.add(scriptName);
		}
	}

	await pruneStaleCachedPex(modeCacheDir, newHashes);

	// If cache artifacts are missing, recompile those scripts even when hashes match.
	const cachedScriptNames = new Set<string>();
	if (fs.existsSync(modeCacheDir) && fs.statSync(modeCacheDir).isDirectory()) {
		const cachedPexFiles = await glob("**/*.pex", { cwd: modeCacheDir });
		for (const file of cachedPexFiles) {
			cachedScriptNames.add(toScriptName(file));
		}
	}
	for (const scriptName of Object.keys(newHashes)) {
		if (!cachedScriptNames.has(scriptName)) {
			scriptsToCompile.add(scriptName);
		}
	}

	if (scriptsToCompile.size > 0) {
		let scripts = "";
		for (const scriptName of scriptsToCompile) {
			scripts += `\n<Script>${scriptName}</Script>`;
		}
		console.log("Compiling changed Papyrus scripts...");
		const isProduct = mode === "product";
		const compilerPath = config.isWsl
			? toWindowsPathFn(config.papyrusCompilerPath, { isWsl: true })
			: config.papyrusCompilerPath;
		const compilerOutputDir = config.isWsl
			? toWindowsPathFn(modeCacheDir, { isWsl: true })
			: modeCacheDir;
		const compilerFlagsPath = config.isWsl
			? toWindowsPathFn(config.papyrusFlagsPath, { isWsl: true })
			: path.basename(config.papyrusFlagsPath);
		const compilerCwd = config.isWsl
			? toWindowsPathFn(sourceDir, { isWsl: true })
			: sourceDir;
		const projectFile = path.join(sourceDir, `${mode}-papyrus.ppj`);
		const compilerProjectFile = config.isWsl
			? toWindowsPathFn(projectFile, { isWsl: true })
			: projectFile;
		const ppjImportDirs = buildPapyrusPpjWindowsImportDirs(config, overlayDir, config.papyrusImportDirs, toWindowsPathFn);
		replace(papyrusProjectTemplate, [
			{ key: "__FLAGS__", value: compilerFlagsPath },
			{ key: "__OUTPUT_DIR__", value: compilerOutputDir },
			{ key: /__IS_PRODUCT__/g, value: isProduct ? "true" : "false" },
			{ key: "__IMPORTS__", value: ppjImportDirs.map((dir) => `<Import>${dir}</Import>`).join("\n") },
			{ key: "__SCRIPTS__", value: scripts },
		], projectFile);

		console.log(`Compiling ${mode}...`);
		const result = await runWhile(`Running Papyrus compiler (${mode})`, () => config.isWsl
			? runWindowsExeFn(compilerPath, [compilerProjectFile], {
					execaFn: execaImpl,
					windowsCwd: compilerCwd,
					windowsShell: "powershell",
					stdio: "pipe",
			  })
			: execaImpl(config.papyrusCompilerPath, [projectFile], {
					cwd: sourceDir,
					reject: false,
					stdio: "pipe",
			  }));
		if (result.stdout) {
			console.log(result.stdout);
		}
		if (result.stderr) {
			console.error(result.stderr);
		}
		const exitCode = result.exitCode ?? (result.failed ? 1 : 0);
		const failedMatch = result.stdout.match(/(\d+) failed/);
		fs.removeSync(projectFile);
		if (exitCode !== 0 || (failedMatch && parseInt(failedMatch[1], 10) > 0)) {
			throw new Error([
				`${mode.charAt(0).toUpperCase() + mode.slice(1)} Papyrus compilation failed. Check compiler output above.`,
				`Compiler: ${compilerPath}`,
				`Project file: ${projectFile}`,
				...(config.isWsl ? [`Project file (Windows): ${compilerProjectFile}`] : []),
				`Working directory: ${sourceDir}`,
				`Exit code: ${exitCode}`,
				...(result.signal ? [`Signal: ${result.signal}`] : []),
				"Resolved scripts:",
				formatResolvedPapyrusImports(resolvedImports),
			].join("\n"));
		}
		console.log(`${mode.charAt(0).toUpperCase() + mode.slice(1)} compilation complete.`);
	} else {
		console.log("No Papyrus scripts changed, skipping compilation.");
	}
	fs.writeJsonSync(scriptHashesPath, newHashes);

	// Deploy from cache
	await deployPex(modeCacheDir, modeOutDir, newHashes);
	console.log("Papyrus binaries deployed.");
}

if (isCliEntry("compile-papyrus")) {
	try {
		const config = createConfig();
		const { mode } = parseArgs(process.argv.slice(2));
		await compilePapyrus(config, { mode });
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
