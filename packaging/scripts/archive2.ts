import fs from "fs-extra";
import path from "node:path";
import { execa } from "execa";
import { DEFAULT_BUILD_MODE, parseBuildModeArg, type BuildMode } from "./build-mode.js";
import { type Config, createConfig, isCliEntry } from "./config.js";
import { runWhile } from "./progress.js";
import { runWindowsExe } from "./windows-exec.js";
import { toWindowsPath } from "./windows-path.js";


export interface CreateArchivesOpts {
	execaFn?: typeof execa;
	mode?: BuildMode;
	runWindowsExeFn?: typeof runWindowsExe;
	toWindowsPathFn?: typeof toWindowsPath;
}

async function createBa2(
	variant: "product",
	opts: {
		config: Config;
		papyrusRoot: string;
		resourcesRoot: string;
		tmpBa2Dir: string;
		ba2Dir: string;
		ba2Path: string;
		archive2Path: string;
		execaFn: typeof execa;
		runWindowsExeFn: typeof runWindowsExe;
		toWindowsPathFn: typeof toWindowsPath;
	},
): Promise<void> {
	console.log(`Creating ${variant} BA2...`);
	fs.removeSync(opts.tmpBa2Dir);
	fs.removeSync(opts.ba2Path);
	fs.mkdirsSync(opts.tmpBa2Dir);
	// Keep packaged scripts in the build workspace for deploy and FOMOD flows.
	fs.copySync(path.join(opts.papyrusRoot, variant, "binary"), path.join(opts.tmpBa2Dir, "Scripts"));
	fs.copySync(path.join(opts.resourcesRoot, "common", "Meshes"), path.join(opts.tmpBa2Dir, "Meshes"));
	fs.removeSync(opts.ba2Dir);
	fs.mkdirsSync(opts.ba2Dir);
	const archive2Path = opts.config.isWsl ? opts.toWindowsPathFn(opts.archive2Path, { isWsl: true }) : opts.archive2Path;
	const windowsTmpBa2Dir = opts.config.isWsl ? opts.toWindowsPathFn(opts.tmpBa2Dir, { isWsl: true }) : opts.tmpBa2Dir;
	const windowsBa2Path = opts.config.isWsl ? opts.toWindowsPathFn(opts.ba2Path, { isWsl: true }) : opts.ba2Path;
	const args = [windowsTmpBa2Dir, `-r=${windowsTmpBa2Dir}`, `-c=${windowsBa2Path}`];
	const result = await runWhile(`Running Archive2 for ${variant} BA2`, () => opts.config.isWsl
		? opts.runWindowsExeFn(archive2Path, args, {
				execaFn: opts.execaFn,
				windowsCwd: opts.toWindowsPathFn(opts.tmpBa2Dir, { isWsl: true }),
				windowsShell: "powershell",
		  })
		: opts.execaFn(opts.archive2Path, args, {
				reject: false,
		  }));
	if (result.stdout) console.log(result.stdout);
	if (result.stderr) console.error(result.stderr);
	if (result.exitCode !== 0) {
		throw new Error([
			`Archive2 failed to create ${variant} BA2. Check output above.`,
			`Archive2: ${archive2Path}`,
			`Input: ${opts.tmpBa2Dir}`,
			`Output: ${opts.ba2Path}`,
			`Working directory: ${opts.tmpBa2Dir}`,
			`Args: ${args.join(" ")}`,
			`Exit code: ${result.exitCode ?? (result.failed ? 1 : 0)}`,
		].join("\n"));
	}
	console.log(`${variant.charAt(0).toUpperCase() + variant.slice(1)} BA2 created.`);
}

export async function createArchives(config: Config, opts?: CreateArchivesOpts): Promise<void> {
	const execaImpl = opts?.execaFn ?? execa;
	const mode = opts?.mode ?? DEFAULT_BUILD_MODE;
	const runWindowsExeFn = opts?.runWindowsExeFn ?? runWindowsExe;
	const toWindowsPathFn = opts?.toWindowsPathFn ?? toWindowsPath;

	const filesRoot = path.join(config.buildTempDir, "files");
	const tmpBa2Dir = path.join(filesRoot, "ba2", "tmp");
	const papyrusRoot = path.join(filesRoot, "papyrus");
	const resourcesRoot = path.join(filesRoot, "resources");

	const shared = {
		config,
		papyrusRoot,
		resourcesRoot,
		tmpBa2Dir,
		archive2Path: config.archive2Path,
		execaFn: execaImpl,
		runWindowsExeFn,
		toWindowsPathFn,
	};

	await createBa2(mode, {
		...shared,
		ba2Dir: path.join(filesRoot, "ba2", mode),
		ba2Path: path.join(filesRoot, "ba2", mode, "LootMan - Main.ba2"),
	});

	// Clean tmp
	fs.removeSync(tmpBa2Dir);
}

export function parseArgs(argv: string[]): { mode: BuildMode } {
	return { mode: parseBuildModeArg(argv) };
}

if (isCliEntry("archive2")) {
	try {
		const config = createConfig();
		const { mode } = parseArgs(process.argv.slice(2));
		await createArchives(config, { mode });
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
