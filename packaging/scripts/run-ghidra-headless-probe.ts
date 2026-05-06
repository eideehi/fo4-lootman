import path from "node:path";
import { fileURLToPath } from "node:url";
import { projectRoot } from "./ghidra-headless.js";
import {
	parseGhidraHeadlessProbeArgs,
	runGhidraHeadlessProbe,
} from "./ghidra-headless.js";

function isCliEntry(): boolean {
	const arg = process.argv[1] ?? "";
	return path.basename(arg) === path.basename(fileURLToPath(import.meta.url));
}

if (isCliEntry()) {
	try {
		const result = await runGhidraHeadlessProbe(parseGhidraHeadlessProbeArgs(process.argv.slice(2)));
		console.log(`Generated ${path.relative(projectRoot, result.reportPath).replaceAll("\\", "/")}`);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
