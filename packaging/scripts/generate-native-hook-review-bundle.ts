import path from "node:path";
import { fileURLToPath } from "node:url";
import { projectRoot } from "./native-hook-addresses.js";
import {
	generateNativeHookReviewBundle,
	parseReviewBundleArgs,
} from "./native-hook-review-bundle.js";

function isCliEntry(): boolean {
	const arg = process.argv[1] ?? "";
	return path.basename(arg) === path.basename(fileURLToPath(import.meta.url));
}

if (isCliEntry()) {
	try {
		const paths = generateNativeHookReviewBundle(parseReviewBundleArgs(process.argv.slice(2)));
		console.log(`Generated ${path.relative(projectRoot, paths.markdown).replaceAll("\\", "/")}`);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
