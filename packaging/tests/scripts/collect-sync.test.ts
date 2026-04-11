import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { resolveCollectManifestPath, syncCollectedFiles } from "../../scripts/collect-sync.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("collect-sync", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("re-copies files when the destination is missing even if the manifest matches", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const destRoot = path.join(config.buildTempDir, "files", "custom");
		const entries = [
			{
				relativePath: "nested/file.txt",
				readContent: () => "payload",
			},
		];

		expect(syncCollectedFiles(config, { manifestName: "collect-sync-test", destRoot, entries })).toEqual({
			copied: 1,
			removed: 0,
			skipped: 0,
			total: 1,
		});

		fs.removeSync(path.join(destRoot, "nested", "file.txt"));

		expect(syncCollectedFiles(config, { manifestName: "collect-sync-test", destRoot, entries })).toEqual({
			copied: 1,
			removed: 0,
			skipped: 0,
			total: 1,
		});
	});

	it("ignores an invalid manifest and recopies files", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const destRoot = path.join(config.buildTempDir, "files", "custom");
		const manifestPath = resolveCollectManifestPath(config, "collect-sync-test");

		fs.outputFileSync(manifestPath, "{not-json");

		expect(syncCollectedFiles(config, {
			manifestName: "collect-sync-test",
			destRoot,
			entries: [
				{
					relativePath: "a.txt",
					readContent: () => "hello",
				},
			],
		})).toEqual({
			copied: 1,
			removed: 0,
			skipped: 0,
			total: 1,
		});

		expect(fs.readFileSync(path.join(destRoot, "a.txt"), "utf8")).toBe("hello");
	});

	it("removes stale files and prunes empty directories", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);
		const destRoot = path.join(config.buildTempDir, "files", "custom");

		expect(syncCollectedFiles(config, {
			manifestName: "collect-sync-test",
			destRoot,
			entries: [
				{
					relativePath: "nested/file.txt",
					readContent: () => "hello",
				},
			],
		})).toEqual({
			copied: 1,
			removed: 0,
			skipped: 0,
			total: 1,
		});

		expect(syncCollectedFiles(config, {
			manifestName: "collect-sync-test",
			destRoot,
			entries: [],
		})).toEqual({
			copied: 0,
			removed: 1,
			skipped: 0,
			total: 0,
		});

		expect(fs.existsSync(destRoot)).toBe(false);
	});
});
