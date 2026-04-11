import fs from "fs-extra";
import path from "node:path";
import { globSync } from "glob";
import { syncCollectedFiles, type CollectSyncResult } from "./collect-sync.js";
import { type Config, createConfig, isCliEntry } from "./config.js";

export function collectResources(config: Config): CollectSyncResult {
	const source = path.join(config.resourcesRoot, "lootman");
	const dest = path.join(config.buildTempDir, "files", "resources");
	const files = globSync("**/*", { cwd: source, nodir: true });

	console.log("Collecting resources...");
	const result = syncCollectedFiles(config, {
		manifestName: "collect-resources",
		destRoot: dest,
		mirrorRoot: true,
		progressLabel: "Collecting resources",
		entries: files.map((file) => ({
			relativePath: file,
			readContent: () => fs.readFileSync(path.join(source, file)),
		})),
	});
	console.log(`Resources collected. copied=${result.copied} removed=${result.removed} skipped=${result.skipped}`);
	return result;
}

if (isCliEntry("collect-resources")) {
	try {
		const config = createConfig();
		collectResources(config);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
