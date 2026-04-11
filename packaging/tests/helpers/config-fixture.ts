import path from "node:path";
import type { Config } from "../../scripts/config.js";

export function createTestConfig(root: string, overrides: Partial<Config> = {}): Config {
	const base: Config = {
		version: "9.9.9-test",
		isWsl: false,
		sevenzipPath: path.join(root, "bin", "7z.exe"),
		dllBuildDir: "commonlibf4-plugin/build/windows/x64/{mode}",
		wslStageDir: path.join(root, "wsl-stage"),
		projectRoot: path.join(root, "project"),
		fallout4Dir: path.join(root, "fallout4"),
		archive2Path: path.join(root, "fallout4", "Tools", "Archive2", "Archive2.exe"),
		papyrusCompilerPath: path.join(root, "fallout4", "Papyrus Compiler", "PapyrusCompiler.exe"),
		papyrusSourceDir: path.join(root, "fallout4", "Data", "Scripts", "Source"),
		papyrusFlagsPath: path.join(root, "fallout4", "Data", "Scripts", "Source", "Base", "Institute_Papyrus_Flags.flg"),
		papyrusImportDirs: [
			path.join(root, "fallout4", "Data", "Scripts", "Source", "Base"),
			path.join(root, "fallout4", "Data", "Scripts", "Source", "User"),
		],
		packagingRoot: path.join(root, "packaging"),
		resourcesRoot: path.join(root, "packaging", "resources"),
		templatesRoot: path.join(root, "packaging", "templates"),
		buildDirRoot: path.join(root, "packaging", "build"),
		buildTempDir: path.join(root, "packaging", "build", "9.9.9-test"),
		archiveName: "LootMan - 9.9.9-test",
	};

	return { ...base, ...overrides };
}
