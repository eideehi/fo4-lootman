import "dotenv/config";
import fs from "fs-extra";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { detectWsl } from "./windows-path.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const packagingRoot = path.resolve(__dirname, "..");

export function checkDir(dir: string): string {
	if (!fs.existsSync(dir) || !fs.statSync(dir).isDirectory()) {
		throw new Error(`Directory not found: ${dir}`);
	}
	return dir;
}

export function checkFile(file: string): string {
	if (!fs.existsSync(file) || !fs.statSync(file).isFile()) {
		throw new Error(`File not found: ${file}`);
	}
	return file;
}

export function requireEnv(name: string): string {
	const value = process.env[name];
	if (value === undefined || value === "") {
		throw new Error(`${name} is not defined in environment`);
	}
	return value;
}

export function isCliEntry(scriptBaseName: string): boolean {
	const arg = process.argv[1] ?? "";
	const base = path.basename(arg);
	return base === `${scriptBaseName}.ts` || base === `${scriptBaseName}.js`;
}

export interface Config {
	version: string;
	isWsl: boolean;
	sevenzipPath: string;
	dllBuildDir: string;
	wslStageDir: string;
	projectRoot: string;
	fallout4Dir: string;
	archive2Path: string;
	papyrusCompilerPath: string;
	papyrusSourceDir: string;
	papyrusFlagsPath: string;
	papyrusImportDirs: string[];
	packagingRoot: string;
	resourcesRoot: string;
	templatesRoot: string;
	buildDirRoot: string;
	buildTempDir: string;
	archiveName: string;
}

export function buildPapyrusImportDirs(papyrusSourceDir: string): string[] {
	// Import dirs exclude Source/F4SE because compile-papyrus uses an overlay copy.
	// compile-papyrus reorders these roots when emitting PPJ imports because PPJ
	// resolves duplicate script names by the first matching import directory.
	return [
		path.resolve(papyrusSourceDir, "Base"),
		path.resolve(papyrusSourceDir, "User"),
	];
}

export function createConfig(): Config {
	// Required environment variables
	const steamGameDir = requireEnv("STEAM_GAME_DIR");
	const sevenzipPath = checkFile(path.resolve(requireEnv("SEVENZIP_PATH")));
	const dllBuildDir = process.env.DLL_BUILD_DIR ?? "commonlibf4-plugin/build/windows/x64/{mode}";
	const wslStageDir = path.resolve(process.env.WSL_STAGE_DIR ?? "/mnt/c/tmp/lootman-wsl-build");
	const isWsl = detectWsl();

	// Read version from package.json
	const packageJson = fs.readJsonSync(path.join(packagingRoot, "package.json"));
	const version: string = packageJson.version;

	// Resolve paths
	const projectRoot = checkDir(path.resolve(process.env.PROJECT_ROOT ?? path.join(packagingRoot, "..")));
	const fallout4Dir = checkDir(path.resolve(steamGameDir, "Fallout 4"));
	const archive2Path = checkFile(path.resolve(fallout4Dir, "Tools", "Archive2", "Archive2.exe"));
	const papyrusCompilerPath = checkFile(path.resolve(fallout4Dir, "Papyrus Compiler", "PapyrusCompiler.exe"));
	const papyrusSourceDir = checkDir(path.resolve(fallout4Dir, "Data", "Scripts", "Source"));
	const papyrusFlagsPath = checkFile(path.resolve(papyrusSourceDir, "Base", "Institute_Papyrus_Flags.flg"));
	const papyrusImportDirs = buildPapyrusImportDirs(papyrusSourceDir);

	const resourcesRoot = checkDir(path.join(packagingRoot, "resources"));
	const templatesRoot = checkDir(path.join(packagingRoot, "templates"));
	const buildDirRoot = path.join(packagingRoot, "build");
	const buildTempDir = path.join(buildDirRoot, version);
	const archiveName = `LootMan - ${version}`;

	return {
		version,
		isWsl,
		sevenzipPath,
		dllBuildDir,
		wslStageDir,
		projectRoot,
		fallout4Dir,
		archive2Path,
		papyrusCompilerPath,
		papyrusSourceDir,
		papyrusFlagsPath,
		papyrusImportDirs,
		packagingRoot,
		resourcesRoot,
		templatesRoot,
		buildDirRoot,
		buildTempDir,
		archiveName,
	};
}
