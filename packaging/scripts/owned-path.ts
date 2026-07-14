import fs from "fs-extra";
import path from "node:path";

const markerName = ".lootman-owned.json";
const marker = { schemaVersion: 1, project: "fo4-lootman", purpose: "packaging-build-state" } as const;

function fail(message: string): never {
	throw new Error(`Unsafe packaging path: ${message}`);
}

function assertAbsoluteSafeRoot(root: string): string {
	if (!path.isAbsolute(root)) fail(`root is not absolute: ${root}`);
	const resolved = path.resolve(root);
	if (resolved === path.parse(resolved).root) fail(`root is too broad: ${root}`);
	return resolved;
}

function assertNoSymlinkFromExistingAncestor(target: string): void {
	const resolved = path.resolve(target);
	const parsed = path.parse(resolved);
	let walked = parsed.root;
	for (const part of resolved.slice(parsed.root.length).split(path.sep).filter(Boolean)) {
		walked = path.join(walked, part);
		try {
			if (fs.lstatSync(walked).isSymbolicLink()) fail(`symbolic link component: ${walked}`);
		} catch (error) {
			if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
		}
	}
}

export function resolveOwnedPath(root: string, relativePath: string): string {
	const safeRoot = assertAbsoluteSafeRoot(root);
	if (
		relativePath.length === 0 || relativePath === "." || path.isAbsolute(relativePath) ||
		/^[a-zA-Z]:/.test(relativePath) || relativePath.startsWith("\\\\") || relativePath.startsWith("//")
	) fail(`invalid relative path: ${relativePath || "<empty>"}`);
	const normalized = relativePath.replace(/\\/g, "/");
	if (normalized.split("/").some((part) => part === "" || part === "." || part === "..")) {
		fail(`invalid relative path: ${relativePath}`);
	}
	const resolved = path.resolve(safeRoot, ...normalized.split("/"));
	if (path.dirname(resolved) !== safeRoot && !resolved.startsWith(`${safeRoot}${path.sep}`)) {
		fail(`path escapes root: ${relativePath}`);
	}
	assertNoSymlinkFromExistingAncestor(safeRoot);
	assertNoSymlinkFromExistingAncestor(resolved);
	return resolved;
}

export function ensureOwnedBuildRoot(root: string): void {
	const safeRoot = assertAbsoluteSafeRoot(root);
	assertNoSymlinkFromExistingAncestor(safeRoot);
	fs.mkdirsSync(safeRoot);
	const markerPath = path.join(safeRoot, markerName);
	if (fs.existsSync(markerPath)) {
		assertOwnedBuildRoot(safeRoot);
		return;
	}
	fs.writeJsonSync(markerPath, marker, { spaces: 2 });
}

export function assertOwnedBuildRoot(root: string): void {
	const safeRoot = assertAbsoluteSafeRoot(root);
	assertNoSymlinkFromExistingAncestor(safeRoot);
	const markerPath = path.join(safeRoot, markerName);
	if (!fs.existsSync(markerPath) || !fs.lstatSync(markerPath).isFile() || fs.lstatSync(markerPath).isSymbolicLink()) {
		fail(`ownership marker missing or unsafe: ${markerPath}`);
	}
	let value: unknown;
	try { value = fs.readJsonSync(markerPath); } catch { fail(`ownership marker is corrupt: ${markerPath}`); }
	if (JSON.stringify(value) !== JSON.stringify(marker)) fail(`ownership marker identity mismatch: ${markerPath}`);
}
