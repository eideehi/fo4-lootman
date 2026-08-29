export const DEFAULT_BUILD_MODE = "product" as const;

export type BuildMode = typeof DEFAULT_BUILD_MODE | "debug";

export function parseBuildModeValue(value: string | null | undefined): BuildMode {
	const mode = value ?? DEFAULT_BUILD_MODE;
	if (mode !== DEFAULT_BUILD_MODE && mode !== "debug") {
		throw new Error(`Invalid mode: ${mode}. Must be "product" or "debug".`);
	}
	return mode;
}

export function parseBuildModeArg(argv: string[]): BuildMode {
	let modeRaw: string | null = null;
	for (const arg of argv) {
		if (!arg.startsWith("--mode=")) {
			continue;
		}
		modeRaw = arg.slice("--mode=".length);
	}
	return parseBuildModeValue(modeRaw);
}
