import fs from "fs-extra";
import path from "node:path";
import { globSync } from "glob";
import type { BuildMode } from "./build-mode.js";
import { hashFile } from "./content-hash.js";
import type { Config } from "./config.js";
import { createFileProgress } from "./progress.js";

export type DeployMode = BuildMode;
export type DeployLang = "en" | "ja";
const deployLangs: DeployLang[] = ["en", "ja"];

export interface SyncDeployOpts {
	mode: DeployMode;
	lang: DeployLang;
	withPapyrus?: boolean;
	fullSync?: boolean;
}

export interface SyncDeployResult {
	copied: number;
	removed: number;
	skipped: number;
	total: number;
}

interface DeployEntry {
	src: string;
	destRelative: string;
	hash: string;
}

interface DeployManifestFile {
	destRelative: string;
	srcHash: string;
}

interface DeployManifest {
	version: number;
	mode: DeployMode;
	lang: DeployLang;
	generatedAt: string;
	files: DeployManifestFile[];
}

interface DeployTargetDirectory {
	type: "directory";
	src: string;
	destPrefix: string;
}

interface DeployTargetFile {
	type: "file";
	src: string;
	destRelative: string;
	required?: boolean;
}

type DeployTarget = DeployTargetDirectory | DeployTargetFile;

function normalizeRelativePath(relativePath: string): string {
	return path.normalize(relativePath).replace(/\\/g, "/");
}

function computeHash(filePath: string): string {
	return hashFile(filePath);
}

function readManifest(filePath: string): DeployManifest | null {
	if (!fs.existsSync(filePath)) {
		return null;
	}

	try {
		const parsed = fs.readJsonSync(filePath) as Partial<DeployManifest>;
		if (!Array.isArray(parsed.files)) {
			return null;
		}
		const files: DeployManifestFile[] = [];
		for (const file of parsed.files) {
			if (typeof file?.destRelative !== "string" || typeof file?.srcHash !== "string") {
				return null;
			}
			files.push({
				destRelative: normalizeRelativePath(file.destRelative),
				srcHash: file.srcHash,
			});
		}

		if (parsed.mode !== "product") {
			return null;
		}
		if (parsed.lang !== "en" && parsed.lang !== "ja") {
			return null;
		}

		return {
			version: typeof parsed.version === "number" ? parsed.version : 1,
			mode: parsed.mode,
			lang: parsed.lang,
			generatedAt: typeof parsed.generatedAt === "string" ? parsed.generatedAt : "",
			files,
		};
	} catch {
		return null;
	}
}

function readPreviousEntries(config: Config, mode: DeployMode): Map<string, string> {
	const previous = new Map<string, string>();

	// Switching locales should also retire files from the previous locale deploy,
	// so stale cleanup needs to consider every manifest for the active mode.
	for (const lang of deployLangs) {
		const manifest = readManifest(resolveDeployManifestPath(config, mode, lang));
		for (const item of manifest?.files ?? []) {
			previous.set(item.destRelative, item.srcHash);
		}
	}

	return previous;
}

function buildTargets(config: Config, mode: DeployMode, lang: DeployLang, withPapyrus: boolean): DeployTarget[] {
	const filesRoot = path.join(config.buildTempDir, "files");
	const targets: DeployTarget[] = [
		{
			type: "directory",
			src: path.join(filesRoot, "resources", "common"),
			destPrefix: "",
		},
		{
			type: "directory",
			src: path.join(filesRoot, "resources", lang),
			destPrefix: "",
		},
		{
			type: "file",
			src: path.join(filesRoot, "dll", mode, "lootman.dll"),
			destRelative: normalizeRelativePath(path.join("F4SE", "Plugins", "lootman.dll")),
			required: true,
		},
	];

	if (withPapyrus) {
		targets.push({
			type: "directory",
			src: path.join(filesRoot, "papyrus", mode, "binary"),
			destPrefix: "Scripts",
		});
	}

	return targets;
}

function collectEntries(targets: DeployTarget[]): {
	entries: DeployEntry[];
	skippedTargets: number;
	requiredMissing: string[];
} {
	const entries: DeployEntry[] = [];
	let skippedTargets = 0;
	const requiredMissing: string[] = [];

	for (const target of targets) {
		if (target.type === "file") {
			if (!fs.existsSync(target.src) || !fs.statSync(target.src).isFile()) {
				if (target.required) {
					requiredMissing.push(target.src);
				}
				skippedTargets++;
				continue;
			}

			entries.push({
				src: target.src,
				destRelative: target.destRelative,
				hash: computeHash(target.src),
			});
			continue;
		}

		if (!fs.existsSync(target.src) || !fs.statSync(target.src).isDirectory()) {
			skippedTargets++;
			continue;
		}

		const files = globSync("**/*", { cwd: target.src, nodir: true });
		if (files.length === 0) {
			continue;
		}

		for (const relativePath of files) {
			const srcPath = path.join(target.src, relativePath);
			const destRelative = normalizeRelativePath(path.join(target.destPrefix, relativePath));
			entries.push({
				src: srcPath,
				destRelative,
				hash: computeHash(srcPath),
			});
		}
	}

	return { entries, skippedTargets, requiredMissing };
}

export function resolveDeployManifestPath(config: Config, mode: DeployMode, lang: DeployLang): string {
	return path.join(config.buildDirRoot, "cache", "deploy", `deployed-${mode}-${lang}.json`);
}

export function syncDeploy(config: Config, opts: SyncDeployOpts): SyncDeployResult {
	const withPapyrus = opts.withPapyrus ?? false;
	const fullSync = opts.fullSync ?? false;
	const targets = buildTargets(config, opts.mode, opts.lang, withPapyrus);
	const { entries, skippedTargets, requiredMissing } = collectEntries(targets);

	if (entries.length === 0) {
		const filesRoot = path.join(config.buildTempDir, "files");
		throw new Error([
			`No deployable artifacts found under ${filesRoot}.`,
			"Run `pnpm run deploy -- --build` or `pnpm run build` first, then retry deploy.",
			"If you want to deploy Papyrus scripts, pass --with-papyrus.",
		].join(" "));
	}
	if (requiredMissing.length > 0) {
		throw new Error(`Required DLL artifact not found: ${requiredMissing[0]}`);
	}
	if (withPapyrus) {
		const papyrusEntries = entries.filter((entry) => entry.destRelative.startsWith("Scripts/"));
		if (papyrusEntries.length === 0) {
			const papyrusDir = path.join(config.buildTempDir, "files", "papyrus", opts.mode, "binary");
			throw new Error(`Papyrus artifacts not found under ${papyrusDir}.`);
		}
	}

	const manifestPath = resolveDeployManifestPath(config, opts.mode, opts.lang);
	const previous = readPreviousEntries(config, opts.mode);

	const current = new Map<string, string>();
	for (const entry of entries) {
		current.set(entry.destRelative, entry.hash);
	}

	const dataDir = path.join(config.fallout4Dir, "Data");
	let copied = 0;
	let removed = 0;
	let unchanged = 0;
	const staleEntries = [...previous.keys()].filter((relativePath) => !current.has(relativePath));
	const progress = createFileProgress(entries.length + staleEntries.length, "Deploying files");

	try {
		for (const entry of entries) {
			const destPath = path.join(dataDir, entry.destRelative);
			if (!fullSync) {
				const previousHash = previous.get(entry.destRelative);
				if (previousHash === entry.hash && fs.existsSync(destPath)) {
					unchanged++;
					progress.advance();
					continue;
				}
			}

			fs.copySync(entry.src, destPath);
			copied++;
			progress.advance();
		}

		for (const relativePath of staleEntries) {
			const destPath = path.join(dataDir, relativePath);
			if (fs.existsSync(destPath)) {
				fs.removeSync(destPath);
				removed++;
			}
			progress.advance();
		}
	} finally {
		progress.finish();
	}

	const manifest: DeployManifest = {
		version: 1,
		mode: opts.mode,
		lang: opts.lang,
		generatedAt: new Date().toISOString(),
		files: entries.map((entry) => ({
			destRelative: entry.destRelative,
			srcHash: entry.hash,
		})),
	};

	fs.mkdirsSync(path.dirname(manifestPath));
	fs.writeJsonSync(manifestPath, manifest, { spaces: 2 });

	return {
		copied,
		removed,
		skipped: unchanged + skippedTargets,
		total: entries.length,
	};
}
