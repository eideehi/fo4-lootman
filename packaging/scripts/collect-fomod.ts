import fs from "fs-extra";
import path from "node:path";
import { globSync } from "glob";
import { syncCollectedFiles, type CollectSyncResult } from "./collect-sync.js";
import { type Config, createConfig, isCliEntry } from "./config.js";
import { replaceText } from "./replace-text.js";

export function collectFomod(config: Config): CollectSyncResult {
	const fomodSrc = path.join(config.resourcesRoot, "fomod");
	const outDir = path.join(config.buildTempDir, "fomod");
	const files = globSync("**/*", { cwd: fomodSrc, nodir: true });

	console.log("Collecting FOMOD files...");
	const result = syncCollectedFiles(config, {
		manifestName: "collect-fomod",
		destRoot: outDir,
		mirrorRoot: true,
		progressLabel: "Collecting FOMOD files",
		entries: files.map((file) => ({
			relativePath: file,
			readContent: () => {
				const srcPath = path.join(fomodSrc, file);
				if (file.replaceAll("\\", "/") !== "info.xml") {
					return fs.readFileSync(srcPath);
				}

				return replaceText(fs.readFileSync(srcPath, "utf8"), [
					{ key: "__MOD_VERSION__", value: config.version },
				]);
			},
		})),
	});
	console.log(`FOMOD files collected. copied=${result.copied} removed=${result.removed} skipped=${result.skipped}`);
	return result;
}

if (isCliEntry("collect-fomod")) {
	try {
		const config = createConfig();
		collectFomod(config);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
