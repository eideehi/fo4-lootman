import fs from "fs-extra";
import path from "node:path";
import { syncCollectedFiles, type CollectSyncResult } from "./collect-sync.js";
import { type Config, createConfig, isCliEntry } from "./config.js";

export function collectLicense(config: Config): CollectSyncResult {
	const files = ["LICENSE", "EXCEPTIONS"];

	console.log("Collecting license files...");
	const result = syncCollectedFiles(config, {
		manifestName: "collect-license",
		destRoot: config.buildTempDir,
		progressLabel: "Collecting license files",
		entries: files.map((file) => ({
			relativePath: file,
			readContent: () => fs.readFileSync(path.join(config.projectRoot, file)),
		})),
	});
	console.log(`License files collected. copied=${result.copied} removed=${result.removed} skipped=${result.skipped}`);
	return result;
}

if (isCliEntry("collect-license")) {
	try {
		const config = createConfig();
		collectLicense(config);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
