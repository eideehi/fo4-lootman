import fs from "fs-extra";
import { globSync } from "glob";
import { type Config } from "./config.js";

function normalizeRelativePath(relativePath: string): string {
	return relativePath.replace(/\\/g, "/");
}

function hasAdjacentPathSegments(segments: string[], first: string, second: string): boolean {
	for (let i = 0; i < segments.length - 1; i++) {
		if (segments[i] === first && segments[i + 1] === second) {
			return true;
		}
	}
	return false;
}

export function isForbiddenPapyrusDistributionSource(relativePath: string): boolean {
	const normalized = normalizeRelativePath(relativePath);
	const lower = normalized.toLowerCase();
	const segments = lower.split("/");

	if (lower.endsWith(".flg") || lower.endsWith(".ppj")) {
		return true;
	}

	if (hasAdjacentPathSegments(segments, "scripts", "source")) {
		return true;
	}

	if (!lower.startsWith("files/papyrus/") || !lower.endsWith(".psc")) {
		return false;
	}

	return !lower.startsWith("files/papyrus/product/source/ltmn2/");
}

export function findForbiddenPapyrusDistributionSources(config: Config): string[] {
	if (!fs.existsSync(config.buildTempDir)) {
		return [];
	}

	return globSync("**/*", { cwd: config.buildTempDir, dot: true, nodir: true })
		.map(normalizeRelativePath)
		.filter(isForbiddenPapyrusDistributionSource)
		.sort();
}

export function assertNoForbiddenPapyrusDistributionSources(config: Config): void {
	const forbidden = findForbiddenPapyrusDistributionSources(config);
	if (forbidden.length === 0) {
		return;
	}

	throw new Error([
		"Forbidden Papyrus source artifacts found in distributable archive input.",
		"These files must not be packaged:",
		...forbidden.map((file) => `- ${file}`),
		"Move compiler-only imports outside the FOMOD input tree or clean stale build output.",
	].join("\n"));
}
