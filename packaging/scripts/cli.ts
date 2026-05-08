import { createArchives } from "./archive2.js";
import { DEFAULT_BUILD_MODE, parseBuildModeValue, type BuildMode } from "./build-mode.js";
import { buildDll } from "./build-dll.js";
import { clean } from "./clean.js";
import { collectDll } from "./collect-dll.js";
import { collectFomod } from "./collect-fomod.js";
import { collectLicense } from "./collect-license.js";
import { collectPapyrus } from "./collect-papyrus.js";
import { collectResources } from "./collect-resources.js";
import { compilePapyrus } from "./compile-papyrus.js";
import { type Config, createConfig, isCliEntry } from "./config.js";
import { makeFomod } from "./make-fomod.js";
import { type DeployLang, type DeployMode, syncDeploy } from "./sync-deploy.js";
import { undeploy } from "./undeploy.js";

type Command = "clean" | "build" | "deploy" | "undeploy";

interface ParsedCommand {
	command: Command;
	args: string[];
}

interface CleanCommandArgs {
	all: boolean;
	wslBuild: boolean;
}

interface BuildCommandArgs {
	mode: BuildMode;
	withPapyrus: boolean;
}

interface DeployCommandArgs {
	mode: DeployMode;
	lang: DeployLang;
	withPapyrus: boolean;
	fullSync: boolean;
	build: boolean;
}

function readOption(argv: string[], name: string): string | null {
	const prefix = `${name}=`;
	for (const arg of argv) {
		if (arg.startsWith(prefix)) {
			return arg.slice(prefix.length);
		}
	}
	return null;
}

function hasFlag(argv: string[], flag: string): boolean {
	return argv.includes(flag);
}

function parseWithPapyrus(argv: string[], defaultValue: boolean): boolean {
	const hasWith = hasFlag(argv, "--with-papyrus");
	const hasWithout = hasFlag(argv, "--no-papyrus");
	if (hasWith && hasWithout) {
		throw new Error("Cannot use both --with-papyrus and --no-papyrus.");
	}
	if (hasWith) return true;
	if (hasWithout) return false;
	return defaultValue;
}

function parseCommand(argv: string[]): ParsedCommand {
	const command = argv[0];
	const args = argv.slice(1);

	if (command === "clean" || command === "build" || command === "deploy" || command === "undeploy") {
		return { command, args };
	}

	throw new Error(`Unknown command: ${command}`);
}

function parseCleanArgs(argv: string[]): CleanCommandArgs {
	return {
		all: hasFlag(argv, "--all"),
		wslBuild: hasFlag(argv, "--wsl-build"),
	};
}

function parseBuildArgs(argv: string[]): BuildCommandArgs {
	const modeRaw = readOption(argv, "--mode");
	const mode = parseBuildModeValue(modeRaw);

	return {
		mode,
		withPapyrus: parseWithPapyrus(argv, true),
	};
}

function parseDeployArgs(argv: string[]): DeployCommandArgs {
	const modeRaw = readOption(argv, "--mode");
	const mode: DeployMode = parseBuildModeValue(modeRaw);

	const langRaw = readOption(argv, "--lang");
	const lang: DeployLang = langRaw === null ? "en" : (langRaw as DeployLang);
	if (lang !== "en" && lang !== "ja") {
		throw new Error(`Invalid lang: ${lang}. Must be "en" or "ja".`);
	}

	return {
		mode,
		lang,
		withPapyrus: parseWithPapyrus(argv, false),
		fullSync: hasFlag(argv, "--full-sync"),
		build: hasFlag(argv, "--build"),
	};
}

async function buildModeArtifacts(config: Config, mode: DeployMode, withPapyrus: boolean): Promise<void> {
	await buildDll(config, { mode });
	collectDll(config, mode);

	if (!withPapyrus) {
		return;
	}

	await compilePapyrus(config, { mode });
	await createArchives(config, { mode });
}

async function runBuild(config: Config, argv: string[]): Promise<void> {
	const args = parseBuildArgs(argv);

	console.log(`Running build flow (mode=${args.mode}, withPapyrus=${args.withPapyrus})...`);
	collectResources(config);
	collectFomod(config);
	collectLicense(config);

	if (args.withPapyrus) {
		await collectPapyrus(config);
	}

	await buildModeArtifacts(config, args.mode, args.withPapyrus);

	if (args.withPapyrus) {
		await makeFomod(config);
	} else {
		console.log("Skipping FOMOD archive creation. Use --with-papyrus to create distributable archive.");
	}
}

async function runDeploy(config: Config, argv: string[]): Promise<void> {
	const args = parseDeployArgs(argv);

	console.log(
		`Running deploy flow (mode=${args.mode}, lang=${args.lang}, withPapyrus=${args.withPapyrus}, fullSync=${args.fullSync}, build=${args.build})...`,
	);

	if (args.build) {
		collectResources(config);
		if (args.withPapyrus) {
			await collectPapyrus(config);
		}

		await buildDll(config, { mode: args.mode });
		collectDll(config, args.mode);

		if (args.withPapyrus) {
			await compilePapyrus(config, { mode: args.mode });
		}
	}

	const result = syncDeploy(config, {
		mode: args.mode,
		lang: args.lang,
		withPapyrus: args.withPapyrus,
		fullSync: args.fullSync,
	});
	if (result.removed > 0) {
		console.log(`Removed stale files: ${result.removed}`);
	}
	console.log(`Deploy complete. copied=${result.copied} skipped=${result.skipped}`);
}

function printUsage(): void {
	console.log([
		"Usage:",
		"  pnpm run package:clean [-- --all|-- --wsl-build]",
		`  pnpm run package:build -- [--mode=${DEFAULT_BUILD_MODE}] [--with-papyrus|--no-papyrus]`,
		`  pnpm run package:deploy -- [--mode=${DEFAULT_BUILD_MODE}] [--lang=en|ja] [--with-papyrus] [--full-sync] [--build]`,
		"  pnpm run package:undeploy",
	].join("\n"));
}

export async function runCli(config: Config, argv: string[]): Promise<void> {
	const { command, args } = parseCommand(argv);

	if (command === "clean") {
		const cleanArgs = parseCleanArgs(args);
		clean(config, { all: cleanArgs.all, wslBuild: cleanArgs.wslBuild });
		return;
	}

	if (command === "build") {
		await runBuild(config, args);
		return;
	}

	if (command === "deploy") {
		await runDeploy(config, args);
		return;
	}

	undeploy(config);
}

if (isCliEntry("cli")) {
	try {
		const argv = process.argv.slice(2);
		if (argv.length === 0 || argv[0] === "help" || argv[0] === "--help" || argv[0] === "-h") {
			printUsage();
		} else {
			const config = createConfig();
			await runCli(config, argv);
		}
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
