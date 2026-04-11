import fs from "fs-extra";
import { globSync } from "glob";
import path from "node:path";
import type { Config } from "./config.js";
import { hashFile } from "./content-hash.js";
import { createFileProgress } from "./progress.js";

export function resolveWslBuildStagePath(config: Config, ...segments: string[]): string {
	return path.join(config.wslStageDir, "build", ...segments);
}


export interface SyncTreeOptions {
	preserveTopLevel?: string[];
	progressLabel?: string;
}

export function normalizeRelativePath(relativePath: string): string {
	return relativePath.replace(/\\/g, "/");
}

function isPreserved(relativePath: string, preserveTopLevel: Set<string>): boolean {
	const normalized = normalizeRelativePath(relativePath);
	const topLevel = normalized.split("/")[0];
	return preserveTopLevel.has(topLevel);
}

function sameFileByStat(sourcePath: string, destPath: string): boolean {
	if (!fs.existsSync(destPath) || !fs.statSync(destPath).isFile()) {
		return false;
	}

	const sourceStat = fs.statSync(sourcePath);
	const destStat = fs.statSync(destPath);
	if (sourceStat.size !== destStat.size) {
		return false;
	}

	const sourceMtimeSeconds = Math.trunc(sourceStat.mtimeMs / 1000);
	const destMtimeSeconds = Math.trunc(destStat.mtimeMs / 1000);
	if (sourceMtimeSeconds !== destMtimeSeconds) {
		return false;
	}

	if (Math.trunc(sourceStat.mtimeMs) === Math.trunc(destStat.mtimeMs)) {
		return true;
	}

	// Crossing from WSL ext4 to /mnt/c can drop sub-second precision. Fall back
	// to content hashes so we still detect same-size edits made within one second.
	return hashFile(sourcePath) === hashFile(destPath);
}

function removeEmptyDirectories(rootDir: string): void {
	if (!fs.existsSync(rootDir) || !fs.statSync(rootDir).isDirectory()) {
		return;
	}

	for (const entry of fs.readdirSync(rootDir)) {
		const fullPath = path.join(rootDir, entry);
		if (fs.statSync(fullPath).isDirectory()) {
			removeEmptyDirectories(fullPath);
		}
	}

	if (fs.readdirSync(rootDir).length === 0) {
		fs.removeSync(rootDir);
	}
}

export function syncTree(sourceDir: string, destDir: string, opts?: SyncTreeOptions): { copied: number; skipped: number; removed: number } {
	const preserveTopLevel = new Set(opts?.preserveTopLevel ?? []);
	const sourceFiles = globSync("**/*", { cwd: sourceDir, dot: true, nodir: true }).filter(
		(relativePath) => !isPreserved(relativePath, preserveTopLevel),
	);
	const sourceFileSet = new Set(sourceFiles.map(normalizeRelativePath));
	let copied = 0;
	let skipped = 0;
	let removed = 0;

	fs.mkdirsSync(destDir);
	const destFiles = globSync("**/*", { cwd: destDir, dot: true, nodir: true });
	const removeCandidates = destFiles.filter((relativePath) => {
		const normalized = normalizeRelativePath(relativePath);
		return !isPreserved(normalized, preserveTopLevel) && !sourceFileSet.has(normalized);
	});
	const progress = createFileProgress(sourceFiles.length + removeCandidates.length, opts?.progressLabel ?? "Syncing stage files");

	try {
		for (const relativePath of sourceFiles) {
			const sourcePath = path.join(sourceDir, relativePath);
			const destPath = path.join(destDir, relativePath);
			if (sameFileByStat(sourcePath, destPath)) {
				skipped++;
				progress.advance();
				continue;
			}

			fs.mkdirsSync(path.dirname(destPath));
			fs.copyFileSync(sourcePath, destPath);
			const sourceStat = fs.statSync(sourcePath);
			fs.utimesSync(destPath, sourceStat.atime, sourceStat.mtime);
			copied++;
			progress.advance();
		}

		for (const relativePath of removeCandidates) {
			fs.removeSync(path.join(destDir, relativePath));
			removed++;
			progress.advance();
		}
	} finally {
		progress.finish();
	}

	if (fs.existsSync(destDir)) {
		for (const entry of fs.readdirSync(destDir)) {
			if (preserveTopLevel.has(entry)) {
				continue;
			}
			removeEmptyDirectories(path.join(destDir, entry));
		}
	}

	return { copied, skipped, removed };
}

export function syncStageDir(stageDir: string, destDir: string): void {
	fs.removeSync(destDir);
	if (fs.existsSync(stageDir)) {
		fs.copySync(stageDir, destDir);
	}
}

