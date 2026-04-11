import fs from "fs-extra";
import path from "node:path";
import { execa } from "execa";
import { type Config, createConfig, isCliEntry } from "./config.js";
import { runWhile } from "./progress.js";
import { runWindowsExe } from "./windows-exec.js";
import { toWindowsPath } from "./windows-path.js";


export interface MakeFomodOpts {
	execaFn?: typeof execa;
	runWindowsExeFn?: typeof runWindowsExe;
	toWindowsPathFn?: typeof toWindowsPath;
}

export function appendWindowsWildcard(dir: string): string {
	return dir.endsWith("\\") ? `${dir}*` : `${dir}\\*`;
}

export async function makeFomod(config: Config, opts?: MakeFomodOpts): Promise<void> {
	const execaImpl = opts?.execaFn ?? execa;
	const runWindowsExeFn = opts?.runWindowsExeFn ?? runWindowsExe;
	const toWindowsPathFn = opts?.toWindowsPathFn ?? toWindowsPath;

	const outDir = path.join(config.packagingRoot, "dist");
	const outFile = path.join(outDir, `${config.archiveName}.7z`);

	fs.mkdirsSync(outDir);
	fs.removeSync(outFile);

	console.log("Creating FOMOD archive...");
	const sevenzipPath = config.isWsl ? toWindowsPathFn(config.sevenzipPath, { isWsl: true }) : config.sevenzipPath;
	const windowsOutFile = config.isWsl ? toWindowsPathFn(outFile, { isWsl: true }) : outFile;
	const windowsBuildTempDir = config.isWsl ? toWindowsPathFn(config.buildTempDir, { isWsl: true }) : config.buildTempDir;
	const args = config.isWsl
		? ["a", windowsOutFile, appendWindowsWildcard(windowsBuildTempDir)]
		: ["a", outFile, `${config.buildTempDir}/*`];
	const result = await runWhile("Running 7-Zip for FOMOD archive", () => config.isWsl
		? runWindowsExeFn(sevenzipPath, args, {
				execaFn: execaImpl,
				windowsCwd: toWindowsPathFn(config.buildTempDir, { isWsl: true }),
				windowsShell: "powershell",
		  })
		: execaImpl(config.sevenzipPath, args, { reject: false }));
	if (result.stdout) console.log(result.stdout);
	if (result.stderr) console.error(result.stderr);
	if (result.exitCode !== 0) {
		throw new Error([
			"7-Zip failed to create FOMOD archive. Check output above.",
			`7-Zip: ${config.sevenzipPath}`,
			`Input: ${config.buildTempDir}`,
			`Output: ${outFile}`,
			`Working directory: ${config.buildTempDir}`,
			`Args: ${args.join(" ")}`,
			`Exit code: ${result.exitCode ?? (result.failed ? 1 : 0)}`,
		].join("\n"));
	}
	console.log(`FOMOD archive created: ${outFile}`);
}

if (isCliEntry("make-fomod")) {
	try {
		const config = createConfig();
		await makeFomod(config);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
