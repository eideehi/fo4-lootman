import path from "node:path";
import { fileURLToPath } from "node:url";
import {
	defaultManifestPath,
	projectRoot,
	readNativeHookManifest,
	validateNativeHookManifest,
} from "./native-hook-addresses.js";

function isCliEntry(): boolean {
	const arg = process.argv[1] ?? "";
	return path.basename(arg) === path.basename(fileURLToPath(import.meta.url));
}

export function verifyNativeHooks(): void {
	const manifest = readNativeHookManifest(defaultManifestPath);
	const result = validateNativeHookManifest(manifest, {
		projectRoot,
		checkEvidencePaths: true,
		checkGeneratedHeader: true,
		checkSource: true,
	});

	if (!result.valid) {
		throw new Error(result.errors.join("\n"));
	}
}

if (isCliEntry()) {
	try {
		verifyNativeHooks();
		console.log("Native hook address manifest verified.");
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
