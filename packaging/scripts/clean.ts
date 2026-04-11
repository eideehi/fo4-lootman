import fs from "fs-extra";
import { type Config, createConfig, isCliEntry } from "./config.js";
import { resolveWslBuildStagePath } from "./wsl-stage.js";

export interface CleanOpts {
	all?: boolean;
	wslBuild?: boolean;
}

export function clean(config: Config, opts?: CleanOpts): void {
	if (opts?.all) {
		console.log(`Cleaning ${config.buildDirRoot} (including cache)`);
		fs.removeSync(config.buildDirRoot);
		if (config.isWsl) {
			console.log(`Cleaning ${config.wslStageDir}`);
			fs.removeSync(config.wslStageDir);
		}
		return;
	}

	if (opts?.wslBuild) {
		if (config.isWsl) {
			const wslBuildDir = resolveWslBuildStagePath(config);
			console.log(`Cleaning ${wslBuildDir}`);
			fs.removeSync(wslBuildDir);
		}
		return;
	} else {
		console.log(`Cleaning ${config.buildTempDir}`);
		fs.removeSync(config.buildTempDir);
	}
}

if (isCliEntry("clean")) {
	try {
		const config = createConfig();
		const all = process.argv.includes("--all");
		const wslBuild = process.argv.includes("--wsl-build");
		clean(config, { all, wslBuild });
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
