import { execa, type Options as ExecaOptions } from "execa";
import fs from "fs-extra";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
export const ghidraRoot = path.resolve(__dirname, "..");
export const projectRoot = path.resolve(ghidraRoot, "..", "..");
export const defaultGhidraHeadlessLocalConfigPath = path.join(projectRoot, "tools", "ghidra", "headless.local.json");
export const defaultGhidraHeadlessExampleConfigPath = path.join(projectRoot, "tools", "ghidra", "headless.example.json");

export interface GhidraHeadlessConfig {
	analyzeHeadless: string;
	projectLocation: string;
	projectName: string;
	programName: string;
	fallout4Exe?: string;
	scriptPath: string;
	probeReportPath: string;
	probeAddress: string;
	probeInstructionCount: number;
}

export interface ReadGhidraHeadlessConfigOptions {
	projectRoot?: string;
	configPath?: string;
}

export interface GhidraProbeOptions extends ReadGhidraHeadlessConfigOptions {
	reportPath?: string;
	address?: string;
	instructionCount?: number;
	execaFn?: typeof execa;
	stdio?: ExecaOptions["stdio"];
}

export interface GhidraProbeCommand {
	command: string;
	args: string[];
	reportPath: string;
}

export interface GhidraProbeResult extends GhidraProbeCommand {
	exitCode?: number;
	stdout: string;
	stderr: string;
}

interface ProcessResult {
	exitCode?: number;
	stdout?: string;
	stderr?: string;
	failed?: boolean;
}

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function requireString(value: unknown, label: string): string {
	if (typeof value !== "string" || value.trim() === "") {
		throw new Error(`${label} must be a non-empty string.`);
	}
	return value;
}

function requirePositiveInteger(value: unknown, label: string): number {
	if (!Number.isInteger(value) || typeof value !== "number" || value < 1) {
		throw new Error(`${label} must be a positive integer.`);
	}
	return value;
}

function resolveWorkspacePath(root: string, value: string): string {
	return path.isAbsolute(value) ? value : path.resolve(root, value);
}

function resolveCommand(root: string, value: string): string {
	if (path.isAbsolute(value) || value.includes("/") || value.includes("\\") || value.startsWith(".")) {
		return resolveWorkspacePath(root, value);
	}
	return value;
}

function getConfigPath(root: string, configPath?: string): string {
	if (configPath) {
		return path.isAbsolute(configPath) ? configPath : path.resolve(root, configPath);
	}
	const localConfig = path.join(root, "tools", "ghidra", "headless.local.json");
	return fs.existsSync(localConfig) ? localConfig : path.join(root, "tools", "ghidra", "headless.example.json");
}

export function readGhidraHeadlessConfig(options: ReadGhidraHeadlessConfigOptions = {}): GhidraHeadlessConfig {
	const root = options.projectRoot ?? projectRoot;
	const configPath = getConfigPath(root, options.configPath);
	const rawConfig = fs.readJsonSync(configPath) as unknown;
	if (!isRecord(rawConfig)) {
		throw new Error(`Ghidra headless config must be an object: ${configPath}`);
	}

	return {
		analyzeHeadless: resolveCommand(root, requireString(rawConfig.analyzeHeadless, "analyzeHeadless")),
		projectLocation: resolveWorkspacePath(root, requireString(rawConfig.projectLocation, "projectLocation")),
		projectName: requireString(rawConfig.projectName, "projectName"),
		programName: requireString(rawConfig.programName, "programName"),
		fallout4Exe: typeof rawConfig.fallout4Exe === "string" && rawConfig.fallout4Exe.trim() !== ""
			? rawConfig.fallout4Exe
			: undefined,
		scriptPath: resolveWorkspacePath(root, requireString(rawConfig.scriptPath, "scriptPath")),
		probeReportPath: resolveWorkspacePath(root, requireString(rawConfig.probeReportPath, "probeReportPath")),
		probeAddress: requireString(rawConfig.probeAddress, "probeAddress"),
		probeInstructionCount: requirePositiveInteger(rawConfig.probeInstructionCount, "probeInstructionCount"),
	};
}

export function buildGhidraInstructionWindowProbeCommand(
	config: GhidraHeadlessConfig,
	options: Pick<GhidraProbeOptions, "projectRoot" | "reportPath" | "address" | "instructionCount"> = {},
): GhidraProbeCommand {
	const root = options.projectRoot ?? projectRoot;
	const reportPath = options.reportPath ? resolveWorkspacePath(root, options.reportPath) : config.probeReportPath;
	const address = options.address ?? config.probeAddress;
	const instructionCount = options.instructionCount ?? config.probeInstructionCount;
	if (!Number.isInteger(instructionCount) || instructionCount < 1) {
		throw new Error("instructionCount must be a positive integer.");
	}

	return {
		command: config.analyzeHeadless,
		args: [
			config.projectLocation,
			config.projectName,
			"-process",
			config.programName,
			"-readOnly",
			"-noanalysis",
			"-scriptPath",
			config.scriptPath,
			"-postScript",
			"DumpFo4InstructionWindow",
			reportPath,
			String(instructionCount),
			address,
		],
		reportPath,
	};
}

export async function runGhidraHeadlessProbe(options: GhidraProbeOptions = {}): Promise<GhidraProbeResult> {
	const config = readGhidraHeadlessConfig(options);
	const command = buildGhidraInstructionWindowProbeCommand(config, options);
	const execaFn = options.execaFn ?? execa;
	const result = await execaFn(command.command, command.args, {
		cwd: options.projectRoot ?? projectRoot,
		reject: false,
		stdio: options.stdio ?? "pipe",
	}) as ProcessResult;
	const probeResult = {
		...command,
		exitCode: result.exitCode,
		stdout: result.stdout ?? "",
		stderr: result.stderr ?? "",
	};
	if (result.failed || (result.exitCode ?? 0) !== 0) {
		throw new Error([
			`Ghidra headless probe failed with exit code ${result.exitCode ?? "unknown"}.`,
			probeResult.stdout,
			probeResult.stderr,
		].filter((line) => line !== "").join("\n"));
	}
	return probeResult;
}

export function parseGhidraHeadlessProbeArgs(args: string[]): GhidraProbeOptions {
	const options: GhidraProbeOptions = {};
	for (const arg of args) {
		if (arg === "--") {
			continue;
		}
		if (arg.startsWith("--config=")) {
			options.configPath = arg.slice("--config=".length);
		} else if (arg.startsWith("--report=")) {
			options.reportPath = arg.slice("--report=".length);
		} else if (arg.startsWith("--address=")) {
			options.address = arg.slice("--address=".length);
		} else if (arg.startsWith("--instruction-count=")) {
			options.instructionCount = Number.parseInt(arg.slice("--instruction-count=".length), 10);
		} else {
			throw new Error(`Unknown argument: ${arg}`);
		}
	}
	return options;
}
