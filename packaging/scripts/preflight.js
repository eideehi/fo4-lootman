import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
export const packagingRoot = path.resolve(__dirname, "..");

const COMMAND_TOOLS = {
	clean: ["tsx"],
	build: ["tsx"],
	generate: ["tsx"],
	resolve: ["tsx"],
	verify: ["tsx"],
	deploy: ["tsx"],
	undeploy: ["tsx"],
	test: ["vitest"],
};

function normalizePath(dir) {
	return path.resolve(dir).replaceAll("\\", "/").toLowerCase();
}

export function normalizeCommand(command) {
	return command.split(":", 1)[0];
}

export function describeUserCommand(command) {
	const normalized = normalizeCommand(command);
	return normalized === "test" ? "pnpm test" : `pnpm run ${command}`;
}

export function assertPackagingExecutionContext(command, cwd = process.cwd(), root = packagingRoot) {
	if (normalizePath(cwd) === normalizePath(root)) {
		return;
	}

	throw new Error(
		[
			`Packaging commands must be run from ${root}.`,
			`Current directory: ${cwd}`,
			"Run these commands instead:",
			"  Set-Location packaging",
			`  ${describeUserCommand(command)}`,
		].join("\n"),
	);
}

function getShimCandidates(tool, platform = process.platform) {
	return platform === "win32" ? [`${tool}.CMD`, `${tool}.cmd`, tool] : [tool];
}

export function findToolArtifacts(tool, root = packagingRoot, platform = process.platform) {
	const packageJson = path.join(root, "node_modules", tool, "package.json");
	const shimPaths = getShimCandidates(tool, platform).map((entry) => path.join(root, "node_modules", ".bin", entry));

	return {
		packageJson,
		shimPaths,
		hasPackage: fs.existsSync(packageJson),
		hasShim: shimPaths.some((entry) => fs.existsSync(entry)),
	};
}

export function assertRequiredTools(command, root = packagingRoot, platform = process.platform) {
	const normalized = normalizeCommand(command);
	const tools = COMMAND_TOOLS[normalized];
	if (!tools) {
		throw new Error(`Unknown preflight command: ${command}`);
	}

	const missing = tools
		.map((tool) => ({ tool, ...findToolArtifacts(tool, root, platform) }))
		.filter((entry) => !entry.hasPackage || !entry.hasShim);

	if (missing.length === 0) {
		return;
	}

	const details = missing.map((entry) => {
		const missingPaths = [];
		if (!entry.hasPackage) {
			missingPaths.push(path.relative(root, entry.packageJson).replaceAll("\\", "/"));
		}
		if (!entry.hasShim) {
			missingPaths.push(entry.shimPaths.map((shim) => path.relative(root, shim).replaceAll("\\", "/")).join(" or "));
		}
		return `- ${entry.tool}: missing ${missingPaths.join(" and ")}`;
	});

	throw new Error(
		[
			`Packaging dependencies are incomplete for '${command}'.`,
			...details,
			"Repair them manually from packaging/:",
			"  Set-Location packaging",
			"  pnpm install",
			"The packaging scripts do not repair dependencies automatically.",
		].join("\n"),
	);
}

export function runPreflight(command, options = {}) {
	const root = options.packagingRoot ?? packagingRoot;
	const cwd = options.cwd ?? process.cwd();
	const platform = options.platform ?? process.platform;

	assertPackagingExecutionContext(command, cwd, root);
	assertRequiredTools(command, root, platform);
}

function isCliEntry() {
	const arg = process.argv[1] ?? "";
	return path.basename(arg) === "preflight.js";
}

function printUsage() {
	console.log([
		"Usage:",
		"  node scripts/preflight.js <clean|clean:all|clean:wsl-build|build|generate:native-hooks|generate:native-hook-bundle|resolve:native-hooks|verify:native-hooks|deploy|undeploy|test|test:watch>",
	].join("\n"));
}

if (isCliEntry()) {
	try {
		const command = process.argv[2];
		if (!command || command === "help" || command === "--help" || command === "-h") {
			printUsage();
			process.exit(command ? 0 : 1);
		}

		runPreflight(command);
		console.log(`Preflight OK for ${command}.`);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
