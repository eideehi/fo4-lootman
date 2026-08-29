import { execa, type Options as ExecaOptions } from "execa";
import fs from "fs-extra";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { readNativeHookManifest } from "../../native-hooks/scripts/native-hook-addresses.js";

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
	provenNativeHooks?: boolean;
	manifestPath?: string;
	execaFn?: typeof execa;
	stdio?: ExecaOptions["stdio"];
}

export interface GhidraProbeCommand {
	command: string;
	args: string[];
	reportPath: string;
}

export const defaultProvenHookEvidencePath = "tools/ghidra/reports/fallout4-1.11.240/proven-call-site-evidence.txt";

function getProvenHookAddresses(root: string, manifestPath?: string): string[] {
	const resolvedManifestPath = manifestPath
		? resolveWorkspacePath(root, manifestPath)
		: path.join(root, "tools", "native-hooks", "papyrus_lootman_hooks.addresses.json");
	const manifest = readNativeHookManifest(resolvedManifestPath);
	const addresses: string[] = [];
	for (const entry of manifest.entries) {
		if (entry.discoveryStrategy.status !== "proven") continue;
		const proof = entry.discoveryStrategy.proof;
		if (!proof) throw new Error(`${entry.id}: proven entry has no proof.`);
		if (proof.sites) {
			addresses.push(...proof.sites.map((site) => site.absoluteAddress));
			continue;
		}
		if (!entry.sites || entry.sites.length !== 1) {
			throw new Error(`${entry.id}: proven entry needs one manifest site or explicit proof sites.`);
		}
		addresses.push(`0x${(0x140000000 + Number.parseInt(entry.sites[0].rva.slice(2), 16)).toString(16).toUpperCase()}`);
	}
	if (addresses.length === 0) throw new Error("No proven native hook call sites were found.");
	return [...new Set(addresses)];
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

function expandEnvironmentPlaceholders(value: string, label: string): string {
	return value.replace(/\$\{([A-Za-z_][A-Za-z0-9_]*)\}/g, (_match, name: string) => {
		const envValue = process.env[name];
		if (envValue === undefined || envValue === "") {
			throw new Error(`${label} references unset environment variable ${name}.`);
		}
		return envValue;
	});
}

function requireExpandedString(value: unknown, label: string): string {
	return expandEnvironmentPlaceholders(requireString(value, label), label);
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
		analyzeHeadless: resolveCommand(root, requireExpandedString(rawConfig.analyzeHeadless, "analyzeHeadless")),
		projectLocation: resolveWorkspacePath(
			root,
			requireExpandedString(rawConfig.projectLocation, "projectLocation"),
		),
		projectName: requireString(rawConfig.projectName, "projectName"),
		programName: requireString(rawConfig.programName, "programName"),
		fallout4Exe: typeof rawConfig.fallout4Exe === "string" && rawConfig.fallout4Exe.trim() !== ""
			? expandEnvironmentPlaceholders(rawConfig.fallout4Exe, "fallout4Exe")
			: undefined,
		scriptPath: resolveWorkspacePath(root, requireExpandedString(rawConfig.scriptPath, "scriptPath")),
		probeReportPath: resolveWorkspacePath(root, requireExpandedString(rawConfig.probeReportPath, "probeReportPath")),
		probeAddress: requireString(rawConfig.probeAddress, "probeAddress"),
		probeInstructionCount: requirePositiveInteger(rawConfig.probeInstructionCount, "probeInstructionCount"),
	};
}

export function buildGhidraInstructionWindowProbeCommand(
	config: GhidraHeadlessConfig,
	options: Pick<GhidraProbeOptions, "projectRoot" | "reportPath" | "address" | "instructionCount" | "provenNativeHooks" | "manifestPath"> = {},
): GhidraProbeCommand {
	const root = options.projectRoot ?? projectRoot;
	const reportPath = options.reportPath ? resolveWorkspacePath(root, options.reportPath) : config.probeReportPath;
	const address = options.address ?? config.probeAddress;
	const instructionCount = options.instructionCount ?? config.probeInstructionCount;
	if (!Number.isInteger(instructionCount) || instructionCount < 1) {
		throw new Error("instructionCount must be a positive integer.");
	}

	const addresses = options.provenNativeHooks ? getProvenHookAddresses(root, options.manifestPath) : [address];
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
			...addresses,
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
	if (!fs.existsSync(command.reportPath)) {
		throw new Error([
			`Ghidra headless probe exited successfully without creating ${command.reportPath}.`,
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
		} else if (arg === "--proven-native-hooks") {
			options.provenNativeHooks = true;
			options.reportPath ??= defaultProvenHookEvidencePath;
		} else if (arg.startsWith("--manifest=")) {
			options.manifestPath = arg.slice("--manifest=".length);
		} else {
			throw new Error(`Unknown argument: ${arg}`);
		}
	}
	return options;
}
