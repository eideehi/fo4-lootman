import path from "node:path";
import { fileURLToPath } from "node:url";
import { projectRoot } from "./native-hook-addresses.js";
import {
	parseResolveNativeHookAddressArgs,
	resolveNativeHookAddresses,
} from "./native-hook-address-resolution.js";

function isCliEntry(): boolean {
	const arg = process.argv[1] ?? "";
	return path.basename(arg) === path.basename(fileURLToPath(import.meta.url));
}

if (isCliEntry()) {
	try {
		const result = resolveNativeHookAddresses(parseResolveNativeHookAddressArgs(process.argv.slice(2)));
		const mode = result.wroteManifest ? "Updated" : "Resolved";
		console.log(`${mode} ${result.resolvedEntries.length} proven native hook address entries.`);
		if (result.generatedHeader) {
			console.log(`Generated ${path.relative(projectRoot, result.generatedHeader).replaceAll("\\", "/")}`);
		}
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
