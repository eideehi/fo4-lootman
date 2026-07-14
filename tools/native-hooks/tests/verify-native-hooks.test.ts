import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
	defaultManifestPath,
	generateNativeHookHeader,
	projectRoot,
	readNativeHookManifest,
} from "../scripts/native-hook-addresses.js";
import { verifyNativeHooks } from "../scripts/verify-native-hooks.js";
import { createTempDir, removeTempDir } from "../../tests/helpers/temp-dir.js";

describe("native hook verification", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) removeTempDir(dir);
	});

	it("rejects self-consistent RVA, target, and context drift from resolver evidence", () => {
		for (const mutation of ["rva", "target", "context"] as const) {
			const root = createTempDir();
			dirs.push(root);
			const manifest = readNativeHookManifest(defaultManifestPath);
			const entry = manifest.entries.find((candidate) => candidate.discoveryStrategy.status === "proven");
			if (!entry?.sites) throw new Error("Checked-in manifest has no proven call site.");
			if (mutation === "rva") entry.sites[0].rva = "0x4D23D5";
			if (mutation === "target") entry.sites[0].expectedTargetRva = "0x494B41";
			if (mutation === "context") entry.sites[0].contextBytes = entry.sites[0].contextBytes?.replace(/^[0-9A-F]{2}/, "FF");
			const manifestPath = path.join(root, "tools/native-hooks/papyrus_lootman_hooks.addresses.json");
			fs.outputJsonSync(manifestPath, manifest, { spaces: 2 });
			fs.copySync(path.join(projectRoot, "tools/ghidra/reports"), path.join(root, "tools/ghidra/reports"));
			fs.outputFileSync(path.join(root, manifest.generatedHeader), generateNativeHookHeader(manifest));

			expect(() => verifyNativeHooks({
				projectRoot: root,
				manifestPath,
				checkSource: false,
			})).toThrow("Native hook resolver evidence disagrees with the manifest");
		}
	});
});
