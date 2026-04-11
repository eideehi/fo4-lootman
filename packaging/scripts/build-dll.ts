import fs from "fs-extra";
import { globSync } from "glob";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { execa } from "execa";
import { DEFAULT_BUILD_MODE, parseBuildModeArg, type BuildMode } from "./build-mode.js";
import { type Config, createConfig, isCliEntry } from "./config.js";
import { hashContent, hashFile } from "./content-hash.js";
import { runWhile } from "./progress.js";
import { runWindowsExe } from "./windows-exec.js";
import { normalizeRelativePath, resolveWslBuildStagePath, syncStageDir, syncTree } from "./wsl-stage.js";

interface ArgvBuildStep {
	type: "argv";
	file: string;
	args: string[];
}

export type BuildStep = ArgvBuildStep;

export interface BuildDllOpts {
	execaFn?: typeof execa;
	mode?: BuildMode;
	readSubmoduleCommitFn?: (submoduleDir: string) => string | null;
	runWindowsExeFn?: typeof runWindowsExe;
}

interface DllStageState {
	commonlibf4Commit: string;
	ownFilesHash: string;
	text: string;
}

const DLL_STAGE_STATE_VERSION = "v3";
const DLL_SUBMODULE_RELATIVE_PATH = "lib/commonlibf4";
// Track only build inputs. Project metadata and persistent build caches are
// intentionally excluded from the staged DLL fingerprint.
const DLL_FINGERPRINT_PATTERNS = ["xmake.lua", "src/**/*", "include/**/*"];

export function parseArgs(argv: string[]): { mode: BuildMode } {
	return { mode: parseBuildModeArg(argv) };
}

export function resolveBuildSteps(needsConfigure = true): BuildStep[] {
	return needsConfigure
		? [
				{ type: "argv", file: "xmake", args: ["f", "-m", "releasedbg", "-y"] },
				{ type: "argv", file: "xmake", args: ["build", "-y"] },
		  ]
		: [{ type: "argv", file: "xmake", args: ["build", "-y"] }];
}

function getBuildCwd(config: Config): string {
	return path.join(config.projectRoot, "commonlibf4-plugin");
}

function resolveDllBuildOutputDir(projectRoot: string, dllBuildDir: string, mode: string): string {
	return path.join(projectRoot, dllBuildDir.replace("{mode}", mode));
}

function resolveStagedDllBuildOutputDir(stageProjectDir: string, dllBuildDir: string, mode: string): string {
	const replaced = dllBuildDir.replace("{mode}", mode);
	const segments = replaced.split(/[\\/]+/);
	const relativeSegments = segments[0]?.toLowerCase() === "commonlibf4-plugin" ? segments.slice(1) : segments;
	return path.join(stageProjectDir, ...relativeSegments);
}

function tryReadGitOutput(projectRoot: string, args: string[]): string | null {
	try {
		return execFileSync("git", args, {
			cwd: projectRoot,
			encoding: "utf8",
			stdio: ["ignore", "pipe", "ignore"],
		}).trim();
	} catch {
		return null;
	}
}

function resolveDllFingerprintInputs(sourceDir: string): string[] {
	const matched = globSync(DLL_FINGERPRINT_PATTERNS, { cwd: sourceDir, dot: true, nodir: true });
	return [...new Set(matched.map(normalizeRelativePath))].sort();
}

function computeFingerprintForFiles(rootDir: string, relativePaths: string[]): string {
	const lines = relativePaths.map((relativePath) => {
		const filePath = path.join(rootDir, relativePath);
		return `${relativePath}:${hashFile(filePath)}`;
	});
	return hashContent(lines.join("\n"));
}

function tryReadGitHeadCommit(repoDir: string): string | null {
	return tryReadGitOutput(repoDir, ["rev-parse", "HEAD"]);
}

function formatDllStageState(ownFilesHash: string, commonlibf4Commit: string): DllStageState {
	return {
		ownFilesHash,
		commonlibf4Commit,
		text: [
			DLL_STAGE_STATE_VERSION,
			`own-files=${ownFilesHash}`,
			`commonlibf4=${commonlibf4Commit}`,
		].join("\n"),
	};
}

function parseDllStageState(text: string): DllStageState | null {
	const lines = text.split(/\r?\n/);
	if (lines[0] !== DLL_STAGE_STATE_VERSION) {
		return null;
	}

	const entries = new Map(
		lines.slice(1).map((line) => {
			const separatorIndex = line.indexOf("=");
			return separatorIndex >= 0 ? [line.slice(0, separatorIndex), line.slice(separatorIndex + 1)] : [line, ""];
		}),
	);
	const ownFilesHash = entries.get("own-files");
	const commonlibf4Commit = entries.get("commonlibf4");
	if (!ownFilesHash || !commonlibf4Commit) {
		return null;
	}

	return formatDllStageState(ownFilesHash, commonlibf4Commit);
}

function computeDllStageState(
	config: Config,
	readSubmoduleCommitFn: (submoduleDir: string) => string | null = tryReadGitHeadCommit,
): DllStageState | null {
	const sourceDir = getBuildCwd(config);
	const ownFiles = resolveDllFingerprintInputs(sourceDir);
	const ownFilesHash = computeFingerprintForFiles(sourceDir, ownFiles);
	const submoduleDir = path.join(sourceDir, DLL_SUBMODULE_RELATIVE_PATH);
	// The CommonLibF4 checkout is large, so track its checked-out commit instead
	// of hashing the full submodule contents on every WSL build.
	const submoduleCommit = readSubmoduleCommitFn(submoduleDir);

	if (submoduleCommit === null) {
		return null;
	}

	return formatDllStageState(ownFilesHash, submoduleCommit);
}

function getDllStageStatePath(stageDir: string): string {
	return path.join(stageDir, ".lootman-stage-state");
}

function accumulateSyncStats(
	stats: { copied: number; skipped: number; removed: number },
	next: { copied: number; skipped: number; removed: number },
): void {
	stats.copied += next.copied;
	stats.skipped += next.skipped;
	stats.removed += next.removed;
}

function stageDllProject(
	config: Config,
	readSubmoduleCommitFn?: (submoduleDir: string) => string | null,
): { stageDir: string; reused: boolean; copied: number; skipped: number; removed: number } {
	const sourceDir = getBuildCwd(config);
	const stageDir = resolveWslBuildStagePath(config, "dll", "commonlibf4-plugin");
	const stageSubmoduleDir = path.join(stageDir, DLL_SUBMODULE_RELATIVE_PATH);
	const sourceSubmoduleDir = path.join(sourceDir, DLL_SUBMODULE_RELATIVE_PATH);
	const statePath = getDllStageStatePath(stageDir);
	const nextState = computeDllStageState(config, readSubmoduleCommitFn);
	const previousState = fs.existsSync(statePath) ? parseDllStageState(fs.readFileSync(statePath, "utf8")) : null;
	const requiredRootFiles = [path.join(stageDir, "xmake.lua")];
	const requiredCommonLibFiles = [path.join(stageSubmoduleDir, "xmake.lua")];
	const hasRequiredRootFiles = requiredRootFiles.every((filePath) => fs.existsSync(filePath));
	const hasRequiredCommonLibFiles = requiredCommonLibFiles.every((filePath) => fs.existsSync(filePath));
	if (
		nextState !== null &&
		previousState !== null &&
		previousState.text === nextState.text &&
		hasRequiredRootFiles &&
		hasRequiredCommonLibFiles
	) {
		return { stageDir, reused: true, copied: 0, skipped: 0, removed: 0 };
	}

	const stats = { copied: 0, skipped: 0, removed: 0 };
	const shouldSyncRoot =
		!hasRequiredRootFiles || previousState === null || nextState === null || previousState.ownFilesHash !== nextState.ownFilesHash;
	const shouldSyncCommonLib =
		!hasRequiredCommonLibFiles ||
		previousState === null ||
		nextState === null ||
		previousState.commonlibf4Commit !== nextState.commonlibf4Commit;

	if (shouldSyncRoot) {
		accumulateSyncStats(
			stats,
			syncTree(sourceDir, stageDir, {
				preserveTopLevel: ["build", ".xmake", "lib"],
				progressLabel: "Staging DLL project",
			}),
		);
	}
	if (shouldSyncCommonLib) {
		accumulateSyncStats(
			stats,
			syncTree(sourceSubmoduleDir, stageSubmoduleDir, {
				progressLabel: "Staging DLL commonlibf4",
			}),
		);
	}
	if (nextState !== null) {
		fs.outputFileSync(statePath, nextState.text);
	} else {
		fs.removeSync(statePath);
	}
	return { stageDir, reused: false, ...stats };
}

function formatStep(step: BuildStep): string {
	return `${step.file} ${step.args.join(" ")}`.trim();
}

async function runStep(
	config: Config,
	execaFn: typeof execa,
	runWindowsExeFn: typeof runWindowsExe,
	cwd: string,
	step: BuildStep,
): Promise<{ exitCode?: number; stdout: string; stderr: string; failed?: boolean; signal?: string }> {
	return runWhile(`Running ${formatStep(step)}`, async () => {
		if (config.isWsl) {
			return runWindowsExeFn(step.file, step.args, {
				execaFn,
				stdio: "pipe",
				cwd,
			});
		}

		const result = await execaFn(step.file, step.args, { cwd, reject: false, stdio: "pipe" });
		return {
			exitCode: result.exitCode,
			failed: result.failed,
			signal: result.signal ?? undefined,
			stderr: result.stderr ?? "",
			stdout: result.stdout ?? "",
		};
	});
}

function printCommandOutput(result: { stdout: string; stderr: string }): void {
	if (result.stdout) {
		console.log(result.stdout);
	}
	if (result.stderr) {
		console.error(result.stderr);
	}
}

export async function buildDll(config: Config, opts?: BuildDllOpts): Promise<void> {
	const execaImpl = opts?.execaFn ?? execa;
	const mode = opts?.mode ?? DEFAULT_BUILD_MODE;
	const readSubmoduleCommitFn = opts?.readSubmoduleCommitFn;
	const runWindowsExeFn = opts?.runWindowsExeFn ?? runWindowsExe;
	const cwd = getBuildCwd(config);
	const stageStartedAt = Date.now();
	const staged = config.isWsl ? stageDllProject(config, readSubmoduleCommitFn) : null;
	const buildCwd = staged?.stageDir ?? cwd;
	const needsConfigure = !staged || !staged.reused || !fs.existsSync(path.join(buildCwd, ".xmake"));
	const steps = resolveBuildSteps(needsConfigure);

	if (staged !== null) {
		const stageElapsedMs = Date.now() - stageStartedAt;
		if (staged.reused) {
			console.log(`Reusing staged DLL project (${stageElapsedMs}ms).`);
		} else {
			console.log(
				`Updated staged DLL project (${stageElapsedMs}ms, copied=${staged.copied}, removed=${staged.removed}, skipped=${staged.skipped}).`,
			);
		}
	}
	console.log(`Building DLL (mode: ${mode})...`);
	for (const step of steps) {
		const result = await runStep(config, execaImpl, runWindowsExeFn, buildCwd, step);
		printCommandOutput(result);
		if (result.exitCode !== 0) {
			throw new Error([
				`DLL build failed (mode: ${mode}).`,
				`Command: ${formatStep(step)}`,
				`Working directory: ${cwd}`,
			].join("\n"));
		}
	}
	if (config.isWsl) {
		const stageOutputDir = resolveStagedDllBuildOutputDir(buildCwd, config.dllBuildDir, "releasedbg");
		const outputDir = resolveDllBuildOutputDir(config.projectRoot, config.dllBuildDir, "releasedbg");
		if (!fs.existsSync(stageOutputDir)) {
			throw new Error([
				"DLL build succeeded but staged output was not found.",
				`Expected: ${stageOutputDir}`,
			].join("\n"));
		}
		syncStageDir(stageOutputDir, outputDir);
	}
	console.log("DLL build complete.");
}

if (isCliEntry("build-dll")) {
	try {
		const config = createConfig();
		const { mode } = parseArgs(process.argv.slice(2));
		await buildDll(config, { mode });
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
