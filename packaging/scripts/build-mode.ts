export const DEFAULT_BUILD_MODE = "product" as const;

export type BuildMode = typeof DEFAULT_BUILD_MODE;

export function parseBuildModeValue(value: string | null | undefined): BuildMode {
	const mode = value ?? DEFAULT_BUILD_MODE;
	if (mode !== DEFAULT_BUILD_MODE) {
		throw new Error(`Invalid mode: ${mode}. Must be "${DEFAULT_BUILD_MODE}".`);
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
