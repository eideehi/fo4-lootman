import path from "node:path";
import { execa, type Options as ExecaOptions } from "execa";

export interface RunWindowsExeOptions {
	cwd?: string;
	execaFn?: typeof execa;
	stdio?: ExecaOptions["stdio"];
	windowsCwd?: string;
	windowsShell?: "cmd" | "powershell";
}

export interface RunWindowsExeResult {
	exitCode?: number;
	stdout: string;
	stderr: string;
	failed?: boolean;
	signal?: string;
}

interface WindowsProcessResult {
	exitCode?: number;
	stdout?: string;
	stderr?: string;
	failed?: boolean;
	signal?: string | null;
}

function resolveWindowsExecutable(file: string): string {
	if (path.extname(file).length > 0 || file.includes("/") || file.includes("\\")) {
		return file;
	}
	return `${file}.exe`;
}

function quoteCmdArg(arg: string): string {
	return `"${arg.replaceAll("\"", "\"\"")}"`;
}

function quotePowerShellArg(arg: string): string {
	return `'${arg.replaceAll("'", "''")}'`;
}

function getWindowsLauncherCwd(): string {
	return "/mnt/c/Windows/System32";
}

function buildWindowsCommand(file: string, args: string[], windowsCwd: string): string {
	const changeDirCommand = windowsCwd.startsWith("\\\\") ? "pushd" : "cd /d";
	return `${changeDirCommand} ${quoteCmdArg(windowsCwd)} && ${[file, ...args].map(quoteCmdArg).join(" ")}`;
}

function buildPowerShellCommand(file: string, args: string[], windowsCwd: string): string {
	return [
		`Set-Location -LiteralPath ${quotePowerShellArg(windowsCwd)}`,
		`& ${quotePowerShellArg(file)} ${args.map(quotePowerShellArg).join(" ")}`,
		"exit $LASTEXITCODE",
	].join("; ");
}

export async function runWindowsExe(
	file: string,
	args: string[],
	opts?: RunWindowsExeOptions,
): Promise<RunWindowsExeResult> {
	const execaFn = opts?.execaFn ?? execa;
	const resolvedFile = resolveWindowsExecutable(file);
	const stdio = opts?.stdio ?? "pipe";
	let result: WindowsProcessResult;
	if (opts?.windowsCwd) {
		const launcherArgs = opts.windowsShell === "powershell"
			? ["-NoProfile", "-Command", buildPowerShellCommand(resolvedFile, args, opts.windowsCwd)]
			: ["/d", "/c", buildWindowsCommand(resolvedFile, args, opts.windowsCwd)];
		const launcher = opts.windowsShell === "powershell" ? "powershell.exe" : "cmd.exe";
		result = await execaFn(launcher, launcherArgs, {
			cwd: getWindowsLauncherCwd(),
			reject: false,
			stdio,
		}) as WindowsProcessResult;
	} else {
		result = await execaFn(resolvedFile, args, {
			cwd: opts?.cwd,
			reject: false,
			stdio,
		}) as WindowsProcessResult;
	}
	return {
		exitCode: result.exitCode,
		stdout: result.stdout ?? "",
		stderr: result.stderr ?? "",
		failed: "failed" in result ? result.failed : undefined,
		signal: "signal" in result ? (result.signal ?? undefined) : undefined,
	};
}
