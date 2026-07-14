import path from "node:path";
import { fileURLToPath } from "node:url";
import {
	defaultManifestPath,
	projectRoot,
	readNativeHookManifest,
	validateNativeHookManifest,
} from "./native-hook-addresses.js";
import { resolveNativeHookAddresses } from "./native-hook-address-resolution.js";

function isCliEntry(): boolean {
	const arg = process.argv[1] ?? "";
	return path.basename(arg) === path.basename(fileURLToPath(import.meta.url));
}

export interface VerifyNativeHookOptions {
	projectRoot?: string;
	manifestPath?: string;
	evidenceReportPath?: string;
	checkSource?: boolean;
}

export function verifyNativeHooks(options: VerifyNativeHookOptions = {}): void {
	const root = options.projectRoot ?? projectRoot;
	const manifestPath = options.manifestPath ?? defaultManifestPath;
	const manifest = readNativeHookManifest(manifestPath);
	const result = validateNativeHookManifest(manifest, {
		projectRoot: root,
		checkEvidencePaths: true,
		checkGeneratedHeader: true,
		checkSource: options.checkSource ?? true,
	});

	if (!result.valid) {
		throw new Error(result.errors.join("\n"));
	}

	const resolution = resolveNativeHookAddresses({
		projectRoot: root,
		manifestPath,
		evidenceReportPath: options.evidenceReportPath,
	});
	const changed = resolution.resolvedEntries.filter((entry) => entry.changed);
	if (changed.length > 0) {
		throw new Error(`Native hook resolver evidence disagrees with the manifest: ${changed.map((entry) => entry.id).join(", ")}`);
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
