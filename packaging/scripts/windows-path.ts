import fs from "node:fs";
import { execFileSync } from "node:child_process";

interface FsLike {
	readFileSync(path: string, encoding: BufferEncoding): string;
	existsSync(path: string): boolean;
}

export function detectWsl(
	procVersionPath = "/proc/version",
	wslInteropPath = "/proc/sys/fs/binfmt_misc/WSLInterop",
	fsLike: FsLike = fs,
): boolean {
	try {
		const version = fsLike.readFileSync(procVersionPath, "utf8").toLowerCase();
		if (version.includes("microsoft") || version.includes("wsl")) {
			return true;
		}
	} catch {}

	try {
		return fsLike.existsSync(wslInteropPath);
	} catch {
		return false;
	}
}

export interface ToWindowsPathOptions {
	isWsl?: boolean;
	cache?: Map<string, string>;
	execFileSyncFn?: typeof execFileSync;
}

const defaultCache = new Map<string, string>();

export function toWindowsPath(filePath: string, opts?: ToWindowsPathOptions): string {
	const isWsl = opts?.isWsl ?? detectWsl();
	if (!isWsl) {
		return filePath;
	}

	const cache = opts?.cache ?? defaultCache;
	const cached = cache.get(filePath);
	if (cached !== undefined) {
		return cached;
	}

	const execFileSyncFn = opts?.execFileSyncFn ?? execFileSync;
	const output = execFileSyncFn("wslpath", ["-w", filePath], { encoding: "utf8" }).trim();
	cache.set(filePath, output);
	return output;
}
