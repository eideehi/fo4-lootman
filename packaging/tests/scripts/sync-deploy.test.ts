import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { resolveDeployManifestPath, syncDeploy } from "../../scripts/sync-deploy.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

function seedBaseDeployArtifacts(config: ReturnType<typeof createTestConfig>, lang: "en" | "ja"): void {
	const filesRoot = path.join(config.buildTempDir, "files");
	fs.outputFileSync(path.join(filesRoot, "resources", "common", "LootMan", "messages.json"), "common");
	fs.outputFileSync(path.join(filesRoot, "resources", lang, "Interface", "Translations", `LootMan_${lang}.txt`), "lang");
	fs.outputFileSync(path.join(filesRoot, "dll", "product", "lootman.dll"), "dll");
}

describe("sync-deploy", () => {
	const dirs: string[] = [];

	afterEach(() => {
		vi.useRealTimers();
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("resolves manifest path by mode and language", () => {
		const config = createTestConfig("C:/tmp/root");
		const resolved = resolveDeployManifestPath(config, "product", "en");
		expect(resolved.replaceAll("\\", "/")).toContain("/packaging/build/cache/deploy/deployed-product-en.json");
	});

	it("copies deploy artifacts and writes manifest", () => {
		const root = createTempDir();
		dirs.push(root);
		vi.useFakeTimers();
		vi.setSystemTime(new Date("2026-01-01T00:00:00.000Z"));
		const config = createTestConfig(root);
		seedBaseDeployArtifacts(config, "en");

		const result = syncDeploy(config, { mode: "product", lang: "en" });
		const dataDir = path.join(config.fallout4Dir, "Data");
		const manifestPath = resolveDeployManifestPath(config, "product", "en");
		const manifest = fs.readJsonSync(manifestPath) as { files: unknown[]; generatedAt: string };

		expect(result).toEqual({ copied: 3, removed: 0, skipped: 0, total: 3 });
		expect(fs.readFileSync(path.join(dataDir, "LootMan", "messages.json"), "utf8")).toBe("common");
		expect(fs.readFileSync(path.join(dataDir, "Interface", "Translations", "LootMan_en.txt"), "utf8")).toBe("lang");
		expect(fs.readFileSync(path.join(dataDir, "F4SE", "Plugins", "lootman.dll"), "utf8")).toBe("dll");
		expect(manifest.files).toHaveLength(3);
		expect(manifest.generatedAt).toBe("2026-01-01T00:00:00.000Z");
	});

	it("skips unchanged files when manifest hashes match", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		seedBaseDeployArtifacts(config, "en");

		const first = syncDeploy(config, { mode: "product", lang: "en" });
		const second = syncDeploy(config, { mode: "product", lang: "en" });

		expect(first.copied).toBe(3);
		expect(second).toEqual({ copied: 0, removed: 0, skipped: 3, total: 3 });
	});

	it("copies all files again when fullSync is enabled", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		seedBaseDeployArtifacts(config, "ja");

		syncDeploy(config, { mode: "product", lang: "ja" });
		const result = syncDeploy(config, { mode: "product", lang: "ja", fullSync: true });

		expect(result.copied).toBe(3);
		expect(result.total).toBe(3);
	});

	it("removes stale files from previous manifest", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const dataDir = path.join(config.fallout4Dir, "Data");
		seedBaseDeployArtifacts(config, "en");

		const manifestPath = resolveDeployManifestPath(config, "product", "en");
		fs.outputJsonSync(manifestPath, {
			version: 1,
			mode: "product",
			lang: "en",
			generatedAt: "old",
			files: [
				{ destRelative: "Old/stale.txt", srcHash: "abc" },
			],
		});
		fs.outputFileSync(path.join(dataDir, "Old", "stale.txt"), "stale");

		const result = syncDeploy(config, { mode: "product", lang: "en" });
		expect(result.removed).toBe(1);
		expect(fs.existsSync(path.join(dataDir, "Old", "stale.txt"))).toBe(false);
	});

	it("removes stale locale files left by a previous deploy in another language", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const dataDir = path.join(config.fallout4Dir, "Data");

		seedBaseDeployArtifacts(config, "en");
		expect(syncDeploy(config, { mode: "product", lang: "en" })).toEqual({
			copied: 3,
			removed: 0,
			skipped: 0,
			total: 3,
		});

		seedBaseDeployArtifacts(config, "ja");
		const result = syncDeploy(config, { mode: "product", lang: "ja" });

		expect(result).toEqual({ copied: 1, removed: 1, skipped: 2, total: 3 });
		expect(fs.existsSync(path.join(dataDir, "Interface", "Translations", "LootMan_en.txt"))).toBe(false);
		expect(fs.readFileSync(path.join(dataDir, "Interface", "Translations", "LootMan_ja.txt"), "utf8")).toBe("lang");
	});

	it("throws when no deployable artifacts exist", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);

		expect(() => syncDeploy(config, { mode: "product", lang: "en" })).toThrow("No deployable artifacts found under");
	});

	it("throws when required DLL is missing", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const filesRoot = path.join(config.buildTempDir, "files");
		fs.outputFileSync(path.join(filesRoot, "resources", "common", "LootMan", "messages.json"), "common");

		expect(() => syncDeploy(config, { mode: "product", lang: "en" })).toThrow("Required DLL artifact not found:");
	});

	it("throws when withPapyrus is true and no papyrus binaries are present", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		seedBaseDeployArtifacts(config, "en");

		expect(() => syncDeploy(config, { mode: "product", lang: "en", withPapyrus: true })).toThrow(
			"Papyrus artifacts not found under",
		);
	});

	it("ignores invalid existing manifest and continues deployment", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		seedBaseDeployArtifacts(config, "en");

		const manifestPath = resolveDeployManifestPath(config, "product", "en");
		fs.outputFileSync(manifestPath, "{not-json");

		const result = syncDeploy(config, { mode: "product", lang: "en" });
		expect(result.copied).toBe(3);
		expect(result.removed).toBe(0);
	});
});
