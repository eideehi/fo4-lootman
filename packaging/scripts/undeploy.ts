import fs from "fs-extra";
import { globSync } from "glob";
import path from "node:path";
import { type Config, createConfig, isCliEntry } from "./config.js";

const translationLanguageCodes = ["en", "fr", "it", "de", "es", "pl", "ptbr", "ru", "cn", "ja"];

const targets = [
	"LootMan",
	"MCM/Config/LootMan",
	"Meshes/SetDressing/SteamerTrunk/LootManTrunkClean.nif",
	"Meshes/SetDressing/SteamerTrunk/LootManTrunkDirty.nif",
	...translationLanguageCodes.map((lang) => `Interface/Translations/LootMan_${lang}.txt`),
	"LootMan.esp",
	"F4SE/Plugins/lootman.dll",
	"F4SE/Plugins/lootman.ini",
	"LootMan - Main.ba2",
	"Scripts/LTMN",
	"Scripts/LTMN2",
	"Scripts/Source/User/LTMN2",
];

const papyrusPexPatterns = [
	"Scripts/LTMN/**/*.pex",
	"Scripts/LTMN2/**/*.pex",
];

export function undeploy(config: Config): { removed: string[]; skipped: string[] } {
	const dataDir = path.join(config.fallout4Dir, "Data");
	const removed: string[] = [];
	const skipped: string[] = [];

	console.log("Removing LootMan files from game directory...");

	for (const target of targets) {
		const fullPath = path.join(dataDir, target);
		if (fs.existsSync(fullPath)) {
			fs.removeSync(fullPath);
			console.log(`  Removed: ${target}`);
			removed.push(target);
		} else {
			console.log(`  Skipped (not found): ${target}`);
			skipped.push(target);
		}
	}

	for (const pattern of papyrusPexPatterns) {
		const matches = globSync(pattern, { cwd: dataDir, nodir: true, nocase: true });
		if (matches.length === 0) {
			console.log(`  Skipped (not found): ${pattern}`);
			skipped.push(pattern);
			continue;
		}

		for (const file of matches) {
			const relativePath = path.normalize(file).replace(/\\/g, "/");
			const fullPath = path.join(dataDir, file);
			if (!fs.existsSync(fullPath)) {
				continue;
			}

			fs.removeSync(fullPath);
			console.log(`  Removed: ${relativePath}`);
			removed.push(relativePath);
		}
	}

	console.log("Undeploy complete.");
	return { removed, skipped };
}

if (isCliEntry("undeploy")) {
	try {
		const config = createConfig();
		undeploy(config);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
