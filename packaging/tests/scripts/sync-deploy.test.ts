import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { resolveDeployManifestPath, syncDeploy } from "../../scripts/sync-deploy.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";
import { hashFile } from "../../scripts/content-hash.js";

function seedBaseDeployArtifacts(config: ReturnType<typeof createTestConfig>, lang: "en" | "ja", mode: "product" | "debug" = "product"): void {
	const filesRoot = path.join(config.buildTempDir, "files");
	fs.outputFileSync(path.join(filesRoot, "resources", "common", "LootMan", "messages.json"), "common");
	fs.outputFileSync(path.join(filesRoot, "resources", lang, "Interface", "Translations", `LootMan_${lang}.txt`), "lang");
	fs.outputFileSync(path.join(filesRoot, "dll", mode, "lootman.dll"), "dll");
}

function seedSharedTranslationDeployArtifacts(config: ReturnType<typeof createTestConfig>, lang: "en" | "ja"): void {
	const filesRoot = path.join(config.buildTempDir, "files");
	fs.outputFileSync(path.join(filesRoot, "resources", "common", "Interface", "Translations", "LootMan_en.txt"), "common-en");
	fs.outputFileSync(path.join(filesRoot, "resources", "common", "Interface", "Translations", "LootMan_de.txt"), "common-de");
	fs.outputFileSync(path.join(filesRoot, "resources", "common", "Interface", "Translations", "LootMan_ptbr.txt"), "common-ptbr");
	fs.outputFileSync(path.join(filesRoot, "resources", lang, "LootMan.esp"), `${lang}-esp`);
	if (lang === "ja") {
		fs.outputFileSync(path.join(filesRoot, "resources", "ja", "Interface", "Translations", "LootMan_en.txt"), "ja-en-override");
	}
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
		fs.outputFileSync(path.join(dataDir, "Old", "stale.txt"), "stale");

		const manifestPath = resolveDeployManifestPath(config, "product", "en");
		fs.outputJsonSync(manifestPath, {
			version: 1,
			mode: "product",
			lang: "en",
			generatedAt: "old",
			files: [
				{ destRelative: "Old/stale.txt", srcHash: hashFile(path.join(dataDir, "Old", "stale.txt")) },
			],
		});
		const result = syncDeploy(config, { mode: "product", lang: "en" });
		expect(result.removed).toBe(1);
		expect(fs.existsSync(path.join(dataDir, "Old", "stale.txt"))).toBe(false);
	});

	it("retains a stale deployed file whose bytes no longer match its manifest", () => {
		const root = createTempDir(); dirs.push(root);
		const config = createTestConfig(root);
		const dataDir = path.join(config.fallout4Dir, "Data");
		seedBaseDeployArtifacts(config, "en");
		const stalePath = path.join(dataDir, "Old", "stale.txt");
		fs.outputFileSync(stalePath, "owned");
		fs.outputJsonSync(resolveDeployManifestPath(config, "product", "en"), {
			version: 1, mode: "product", lang: "en", generatedAt: "old",
			files: [{ destRelative: "Old/stale.txt", srcHash: hashFile(stalePath) }],
		});
		fs.writeFileSync(stalePath, "user-modified");

		const result = syncDeploy(config, { mode: "product", lang: "en" });
		expect(result.removed).toBe(0);
		expect(fs.readFileSync(stalePath, "utf8")).toBe("user-modified");
	});

	it("rejects an escaping manifest path before copying deploy artifacts", () => {
		const root = createTempDir(); dirs.push(root);
		const config = createTestConfig(root);
		seedBaseDeployArtifacts(config, "en");
		fs.outputJsonSync(resolveDeployManifestPath(config, "product", "en"), {
			version: 1, mode: "product", lang: "en", generatedAt: "old",
			files: [{ destRelative: "../outside.txt", srcHash: "unsafe" }],
		});

		expect(() => syncDeploy(config, { mode: "product", lang: "en" })).toThrow(/Unsafe packaging path/);
		expect(fs.existsSync(path.join(config.fallout4Dir, "Data", "F4SE", "Plugins", "lootman.dll"))).toBe(false);
	});

	it("does not remove previously deployed Papyrus scripts when deploying resources only", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const dataDir = path.join(config.fallout4Dir, "Data");
		seedBaseDeployArtifacts(config, "en");
		fs.outputFileSync(path.join(dataDir, "Old", "stale.txt"), "stale");

		const manifestPath = resolveDeployManifestPath(config, "product", "en");
		fs.outputJsonSync(manifestPath, {
			version: 1,
			mode: "product",
			lang: "en",
			generatedAt: "old",
			files: [
				{ destRelative: "Old/stale.txt", srcHash: hashFile(path.join(dataDir, "Old", "stale.txt")) },
				{ destRelative: "Scripts/ltmn2/mcm.pex", srcHash: "def" },
			],
		});
		fs.outputFileSync(path.join(dataDir, "Scripts", "ltmn2", "mcm.pex"), "papyrus");

		const result = syncDeploy(config, { mode: "product", lang: "en" });
		const manifest = fs.readJsonSync(manifestPath) as { files: Array<{ destRelative: string; srcHash: string }> };
		expect(result.removed).toBe(1);
		expect(fs.existsSync(path.join(dataDir, "Old", "stale.txt"))).toBe(false);
		expect(fs.readFileSync(path.join(dataDir, "Scripts", "ltmn2", "mcm.pex"), "utf8")).toBe("papyrus");
		expect(manifest.files).toContainEqual({ destRelative: "Scripts/ltmn2/mcm.pex", srcHash: "def" });
		expect(manifest.files).not.toContainEqual({ destRelative: "Old/stale.txt", srcHash: "abc" });
	});

	it("retains loose debug scripts across a resource-only deploy and removes stale scripts on the next Papyrus deploy", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const dataDir = path.join(config.fallout4Dir, "Data");
		const papyrusBinaryDir = path.join(config.buildTempDir, "files", "papyrus", "debug", "binary");
		seedBaseDeployArtifacts(config, "en", "debug");
		fs.outputFileSync(path.join(papyrusBinaryDir, "ltmn2", "mcm.pex"), "papyrus");

		const first = syncDeploy(config, { mode: "debug", lang: "en", withPapyrus: true });
		const manifestPath = resolveDeployManifestPath(config, "debug", "en");
		let manifest = fs.readJsonSync(manifestPath) as { files: Array<{ destRelative: string; srcHash: string }> };
		expect(first.copied).toBe(4);
		expect(fs.readFileSync(path.join(dataDir, "Scripts", "ltmn2", "mcm.pex"), "utf8")).toBe("papyrus");
		expect(manifest.files.map((file) => file.destRelative)).toContain("Scripts/ltmn2/mcm.pex");

		const second = syncDeploy(config, { mode: "debug", lang: "en" });
		manifest = fs.readJsonSync(manifestPath) as { files: Array<{ destRelative: string; srcHash: string }> };
		expect(second.removed).toBe(0);
		expect(fs.readFileSync(path.join(dataDir, "Scripts", "ltmn2", "mcm.pex"), "utf8")).toBe("papyrus");
		expect(manifest.files.map((file) => file.destRelative)).toContain("Scripts/ltmn2/mcm.pex");

		fs.removeSync(path.join(papyrusBinaryDir, "ltmn2", "mcm.pex"));
		fs.outputFileSync(path.join(papyrusBinaryDir, "ltmn2", "other.pex"), "papyrus-other");

		const third = syncDeploy(config, { mode: "debug", lang: "en", withPapyrus: true });
		manifest = fs.readJsonSync(manifestPath) as { files: Array<{ destRelative: string; srcHash: string }> };
		expect(third.removed).toBe(1);
		expect(fs.existsSync(path.join(dataDir, "Scripts", "ltmn2", "mcm.pex"))).toBe(false);
		expect(fs.readFileSync(path.join(dataDir, "Scripts", "ltmn2", "other.pex"), "utf8")).toBe("papyrus-other");
		expect(manifest.files.map((file) => file.destRelative)).not.toContain("Scripts/ltmn2/mcm.pex");
		expect(manifest.files.map((file) => file.destRelative)).toContain("Scripts/ltmn2/other.pex");
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

	it.each(["en", "ja"] as const)("copies shared MCM translations when deploying %s resources", (lang) => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const dataDir = path.join(config.fallout4Dir, "Data");

		seedSharedTranslationDeployArtifacts(config, lang);
		syncDeploy(config, { mode: "product", lang });

		expect(fs.readFileSync(path.join(dataDir, "Interface", "Translations", "LootMan_de.txt"), "utf8")).toBe("common-de");
		expect(fs.readFileSync(path.join(dataDir, "Interface", "Translations", "LootMan_ptbr.txt"), "utf8")).toBe("common-ptbr");
		expect(fs.readFileSync(path.join(dataDir, "LootMan.esp"), "utf8")).toBe(`${lang}-esp`);
		expect(fs.readFileSync(path.join(dataDir, "Interface", "Translations", "LootMan_en.txt"), "utf8")).toBe(
			lang === "ja" ? "ja-en-override" : "common-en",
		);
	});

	it("keeps the Japanese locale override authoritative across consecutive deploys", () => {
		const root = createTempDir(); dirs.push(root);
		const config = createTestConfig(root);
		const filesRoot = path.join(config.buildTempDir, "files", "resources");
		const relative = path.join("Interface", "Translations", "LootMan_en.txt");
		fs.outputFileSync(path.join(filesRoot, "common", relative), "common");
		fs.outputFileSync(path.join(filesRoot, "ja", relative), "japanese");
		fs.outputFileSync(path.join(config.buildTempDir, "files", "dll", "product", "lootman.dll"), "dll");

		syncDeploy(config, { mode: "product", lang: "ja" });
		const second = syncDeploy(config, { mode: "product", lang: "ja" });
		const deployed = path.join(config.fallout4Dir, "Data", relative);
		const manifest = fs.readJsonSync(resolveDeployManifestPath(config, "product", "ja")) as { files: Array<{ destRelative: string }> };
		expect(fs.readFileSync(deployed, "utf8")).toBe("japanese");
		expect(second.copied).toBe(0);
		expect(manifest.files.filter((file) => file.destRelative === relative.replaceAll("\\", "/"))).toHaveLength(1);
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

		expect(() => syncDeploy(config, { mode: "product", lang: "en" })).toThrow("Required deploy artifact not found:");
	});

	it("throws when product Papyrus deployment has no BA2", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		seedBaseDeployArtifacts(config, "en");

		expect(() => syncDeploy(config, { mode: "product", lang: "en", withPapyrus: true })).toThrow("Required deploy artifact not found:");
	});

	it("deploys the product Papyrus BA2 instead of loose scripts", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		seedBaseDeployArtifacts(config, "en");
		fs.outputFileSync(path.join(config.buildTempDir, "files", "ba2", "product", "LootMan - Main.ba2"), "ba2");

		const result = syncDeploy(config, { mode: "product", lang: "en", withPapyrus: true });
		const dataDir = path.join(config.fallout4Dir, "Data");
		expect(result.copied).toBe(4);
		expect(fs.readFileSync(path.join(dataDir, "LootMan - Main.ba2"), "utf8")).toBe("ba2");
		expect(fs.existsSync(path.join(dataDir, "Scripts"))).toBe(false);
	});

	it("removes managed loose debug scripts when switching to a product deploy", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const filesRoot = path.join(config.buildTempDir, "files");
		const dataDir = path.join(config.fallout4Dir, "Data");
		seedBaseDeployArtifacts(config, "en", "debug");
		fs.outputFileSync(path.join(filesRoot, "papyrus", "debug", "binary", "LTMN2", "System.pex"), "debug-pex");
		syncDeploy(config, { mode: "debug", lang: "en", withPapyrus: true });

		seedBaseDeployArtifacts(config, "en", "product");
		fs.outputFileSync(path.join(filesRoot, "ba2", "product", "LootMan - Main.ba2"), "product-ba2");
		const result = syncDeploy(config, { mode: "product", lang: "en", withPapyrus: true });

		expect(result.removed).toBe(1);
		expect(fs.existsSync(path.join(dataDir, "Scripts", "LTMN2", "System.pex"))).toBe(false);
		expect(fs.readFileSync(path.join(dataDir, "LootMan - Main.ba2"), "utf8")).toBe("product-ba2");
	});

	it("removes the managed product BA2 when switching to a debug deploy", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const filesRoot = path.join(config.buildTempDir, "files");
		const dataDir = path.join(config.fallout4Dir, "Data");
		seedBaseDeployArtifacts(config, "en", "product");
		fs.outputFileSync(path.join(filesRoot, "ba2", "product", "LootMan - Main.ba2"), "product-ba2");
		syncDeploy(config, { mode: "product", lang: "en", withPapyrus: true });

		seedBaseDeployArtifacts(config, "en", "debug");
		fs.outputFileSync(path.join(filesRoot, "papyrus", "debug", "binary", "LTMN2", "System.pex"), "debug-pex");
		const result = syncDeploy(config, { mode: "debug", lang: "en", withPapyrus: true });

		expect(result.removed).toBe(1);
		expect(fs.existsSync(path.join(dataDir, "LootMan - Main.ba2"))).toBe(false);
		expect(fs.readFileSync(path.join(dataDir, "Scripts", "LTMN2", "System.pex"), "utf8")).toBe("debug-pex");
	});

	it("throws when debug Papyrus deployment has no loose binaries", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		seedBaseDeployArtifacts(config, "en", "debug");

		expect(() => syncDeploy(config, { mode: "debug", lang: "en", withPapyrus: true })).toThrow("Papyrus artifacts not found under");
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
