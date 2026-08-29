import fs from "fs-extra";
import path from "node:path";
import { syncCollectedFiles, type CollectSyncResult } from "./collect-sync.js";
import { DEFAULT_BUILD_MODE, parseBuildModeArg, type BuildMode } from "./build-mode.js";
import { type Config, createConfig, isCliEntry } from "./config.js";

const dllName = "lootman.dll";

export type DllMode = BuildMode;

export function parseArgs(argv: string[]): DllMode {
	return parseBuildModeArg(argv);
}

export function resolveDllPath(projectRoot: string, dllBuildDir: string, mode: string): string {
	const buildDir = dllBuildDir.replace("{mode}", mode);
	return path.join(projectRoot, buildDir, dllName);
}

function collectSingleDll(config: Config, buildMode: string, outDir: string, manifestName: string): CollectSyncResult {
	const src = resolveDllPath(config.projectRoot, config.dllBuildDir, buildMode);
	if (!fs.existsSync(src)) {
		throw new Error(`DLL source file not found:\n- ${src}`);
	}

	return syncCollectedFiles(config, {
		manifestName,
		destRoot: outDir,
		mirrorRoot: true,
		progressLabel: "Collecting DLLs",
		entries: [
			{
				relativePath: dllName,
				readContent: () => fs.readFileSync(src),
			},
		],
	});
}

export function collectDll(config: Config, mode: DllMode = DEFAULT_BUILD_MODE): CollectSyncResult {
	const dllDirRoot = path.join(config.buildTempDir, "files", "dll");
	const outDir = path.join(dllDirRoot, mode);
	const buildMode = mode === "product" ? "releasedbg" : "debug";

	console.log(`Collecting DLLs (mode: ${mode})...`);
	const result = collectSingleDll(config, buildMode, outDir, `collect-dll-${mode}`);
	console.log(`DLL collection complete. copied=${result.copied} removed=${result.removed} skipped=${result.skipped}`);
	return result;
}

if (isCliEntry("collect-dll")) {
	try {
		const config = createConfig();
		const mode = parseArgs(process.argv.slice(2));
		collectDll(config, mode);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
