import path from "node:path";
import { fileURLToPath } from "node:url";
import {
	defaultManifestPath,
	projectRoot,
	readNativeHookManifest,
	validateNativeHookManifest,
	writeGeneratedNativeHookHeader,
} from "./native-hook-addresses.js";

function isCliEntry(): boolean {
	const arg = process.argv[1] ?? "";
	return path.basename(arg) === path.basename(fileURLToPath(import.meta.url));
}

export function generateNativeHookAddresses(): string {
	const manifest = readNativeHookManifest(defaultManifestPath);
	const result = validateNativeHookManifest(manifest, {
		projectRoot,
		checkEvidencePaths: true,
		checkGeneratedHeader: false,
		checkSource: false,
	});
	if (!result.valid) {
		throw new Error(result.errors.join("\n"));
	}

	return writeGeneratedNativeHookHeader(manifest, projectRoot);
}

if (isCliEntry()) {
	try {
		const headerPath = generateNativeHookAddresses();
		console.log(`Generated ${path.relative(projectRoot, headerPath).replaceAll("\\", "/")}`);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
