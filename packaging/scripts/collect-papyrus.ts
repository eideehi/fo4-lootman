import fs from "fs-extra";
import path from "node:path";
import { glob } from "glob";
import { syncCollectedFiles, type CollectSyncResult } from "./collect-sync.js";
import { type Config, createConfig, isCliEntry } from "./config.js";

export async function collectPapyrus(config: Config): Promise<CollectSyncResult> {
	const srcDir = path.join(config.projectRoot, "papyrus", "Scripts", "Source", "User");
	const outDir = path.join(config.buildTempDir, "files", "papyrus", "product", "source");

	console.log("Collecting Papyrus sources...");
	const files = await glob("**/*.psc", { cwd: srcDir });
	const result = syncCollectedFiles(config, {
		manifestName: "collect-papyrus",
		destRoot: outDir,
		mirrorRoot: true,
		progressLabel: "Collecting Papyrus sources",
		entries: files.map((file) => ({
			relativePath: file,
			readContent: () => fs.readFileSync(path.join(srcDir, file)),
		})),
	});
	console.log(`Papyrus sources collected. copied=${result.copied} removed=${result.removed} skipped=${result.skipped}`);
	return result;
}

if (isCliEntry("collect-papyrus")) {
	try {
		const config = createConfig();
		await collectPapyrus(config);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
