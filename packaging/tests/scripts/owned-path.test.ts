import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { assertOwnedBuildRoot, ensureOwnedBuildRoot, resolveOwnedPath } from "../../scripts/owned-path.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("owned packaging paths", () => {
	const dirs: string[] = [];
	afterEach(() => { for (const dir of dirs.splice(0)) removeTempDir(dir); });

	it.each(["", ".", "../escape", "a/../../escape", "/absolute", "C:/drive", "C:\\drive", "//server/share", "\\\\server\\share"])(
		"rejects unsafe relative path %j",
		(relativePath) => {
			const root = createTempDir(); dirs.push(root);
			expect(() => resolveOwnedPath(path.join(root, "owned"), relativePath)).toThrow(/Unsafe packaging path/);
		},
	);

	it("uses path-component containment instead of sibling prefixes", () => {
		const root = createTempDir(); dirs.push(root);
		const owned = path.join(root, "build");
		expect(resolveOwnedPath(owned, "nested/file.txt")).toBe(path.join(owned, "nested", "file.txt"));
		expect(() => resolveOwnedPath(owned, "../build-other/file.txt")).toThrow(/Unsafe packaging path/);
	});

	it("rejects symbolic-link destinations", () => {
		const root = createTempDir(); dirs.push(root);
		const owned = path.join(root, "owned");
		fs.mkdirsSync(owned);
		fs.symlinkSync(path.join(root, "outside"), path.join(owned, "linked"));
		expect(() => resolveOwnedPath(owned, "linked/file.txt")).toThrow(/symbolic link/);
	});

	it("requires an intact project ownership marker", () => {
		const root = createTempDir(); dirs.push(root);
		const owned = path.join(root, "owned");
		fs.mkdirsSync(owned);
		expect(() => assertOwnedBuildRoot(owned)).toThrow(/ownership marker/);
		ensureOwnedBuildRoot(owned);
		expect(() => assertOwnedBuildRoot(owned)).not.toThrow();
		fs.writeFileSync(path.join(owned, ".lootman-owned.json"), "{}");
		expect(() => assertOwnedBuildRoot(owned)).toThrow(/identity mismatch/);
	});
});
