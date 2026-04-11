import fs from "fs-extra";
import { globSync } from "glob";
import path from "node:path";
import { hashContent as computeContentHash } from "./content-hash.js";
import type { Config } from "./config.js";
import { createFileProgress } from "./progress.js";

export interface CollectSyncEntry {
	relativePath: string;
	readContent: () => Buffer | string;
}

export interface CollectSyncResult {
	copied: number;
	removed: number;
	skipped: number;
	total: number;
}

interface CollectManifestFile {
	relativePath: string;
	contentHash: string;
}

interface CollectManifest {
	version: number;
	generatedAt: string;
	files: CollectManifestFile[];
}

function normalizeRelativePath(relativePath: string): string {
	return path.normalize(relativePath).replace(/\\/g, "/");
}

function readManifest(filePath: string): CollectManifest | null {
	if (!fs.existsSync(filePath)) {
		return null;
	}

	try {
		const parsed = fs.readJsonSync(filePath) as Partial<CollectManifest>;
		if (!Array.isArray(parsed.files)) {
			return null;
		}

		const files: CollectManifestFile[] = [];
		for (const file of parsed.files) {
			if (typeof file?.relativePath !== "string" || typeof file?.contentHash !== "string") {
				return null;
			}

			files.push({
				relativePath: normalizeRelativePath(file.relativePath),
				contentHash: file.contentHash,
			});
		}

		return {
			version: typeof parsed.version === "number" ? parsed.version : 1,
			generatedAt: typeof parsed.generatedAt === "string" ? parsed.generatedAt : "",
			files,
		};
	} catch {
		return null;
	}
}

function isFile(filePath: string): boolean {
	return fs.existsSync(filePath) && fs.statSync(filePath).isFile();
}

function removeEmptyDirectories(startDir: string, stopDir: string): void {
	const root = path.resolve(stopDir);
	let current = path.resolve(startDir);

	while (current.startsWith(root) && current !== root) {
		if (!fs.existsSync(current) || !fs.statSync(current).isDirectory()) {
			current = path.dirname(current);
			continue;
		}

		if (fs.readdirSync(current).length > 0) {
			return;
		}

		fs.removeSync(current);
		current = path.dirname(current);
	}

	if (fs.existsSync(root) && fs.statSync(root).isDirectory() && fs.readdirSync(root).length === 0) {
		fs.removeSync(root);
	}
}

export function resolveCollectManifestPath(config: Config, manifestName: string): string {
	return path.join(config.buildDirRoot, "cache", "collect", `${manifestName}.json`);
}

export function syncCollectedFiles(
	config: Config,
	opts: {
		manifestName: string;
		destRoot: string;
		entries: CollectSyncEntry[];
		mirrorRoot?: boolean;
		progressLabel?: string;
	},
): CollectSyncResult {
	const manifestPath = resolveCollectManifestPath(config, opts.manifestName);
	const previousManifest = readManifest(manifestPath);
	const previous = new Map<string, string>();
	for (const item of previousManifest?.files ?? []) {
		previous.set(item.relativePath, item.contentHash);
	}

	const currentEntries = new Map<string, { content: Buffer | string; contentHash: string }>();
	for (const entry of opts.entries) {
		const relativePath = normalizeRelativePath(entry.relativePath);
		if (currentEntries.has(relativePath)) {
			throw new Error(`Duplicate collect output path: ${relativePath}`);
		}

		const content = entry.readContent();
		currentEntries.set(relativePath, {
			content,
			contentHash: computeContentHash(content),
		});
	}

	let copied = 0;
	let removed = 0;
	let skipped = 0;

	const sortedEntries = [...currentEntries.entries()].sort(([left], [right]) => left.localeCompare(right));
	const stalePaths = new Set<string>(previous.keys());
	if (opts.mirrorRoot && fs.existsSync(opts.destRoot) && fs.statSync(opts.destRoot).isDirectory()) {
		for (const relativePath of globSync("**/*", { cwd: opts.destRoot, nodir: true })) {
			stalePaths.add(normalizeRelativePath(relativePath));
		}
	}

	const staleCandidates = [...stalePaths].filter((relativePath) => !currentEntries.has(relativePath)).sort();
	const progress = createFileProgress(sortedEntries.length + staleCandidates.length, opts.progressLabel ?? "Syncing files");

	try {
		for (const [relativePath, entry] of sortedEntries) {
			const destPath = path.join(opts.destRoot, relativePath);
			const previousHash = previous.get(relativePath);
			if (previousHash === entry.contentHash && isFile(destPath)) {
				skipped++;
				progress.advance();
				continue;
			}

			if (fs.existsSync(destPath) && !fs.statSync(destPath).isFile()) {
				fs.removeSync(destPath);
			}

			fs.outputFileSync(destPath, entry.content);
			copied++;
			progress.advance();
		}

		for (const relativePath of staleCandidates) {
			const destPath = path.join(opts.destRoot, relativePath);
			if (fs.existsSync(destPath)) {
				fs.removeSync(destPath);
				removeEmptyDirectories(path.dirname(destPath), opts.destRoot);
				removed++;
			}
			progress.advance();
		}
	} finally {
		progress.finish();
	}

	removeEmptyDirectories(opts.destRoot, opts.destRoot);

	const manifest: CollectManifest = {
		version: 1,
		generatedAt: new Date().toISOString(),
		files: sortedEntries.map(([relativePath, entry]) => ({
			relativePath,
			contentHash: entry.contentHash,
		})),
	};

	fs.mkdirsSync(path.dirname(manifestPath));
	fs.writeJsonSync(manifestPath, manifest, { spaces: 2 });

	return {
		copied,
		removed,
		skipped,
		total: sortedEntries.length,
	};
}
