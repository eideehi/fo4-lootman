import fs from "fs-extra";
import path from "node:path";
import {
	defaultManifestPath,
	projectRoot as defaultProjectRoot,
	readNativeHookManifest,
	validateNativeHookManifest,
	type NativeHookAddressEntry,
	type NativeHookAddressManifest,
} from "./native-hook-addresses.js";

interface ReviewBundleOptions {
	manifestPath?: string;
	projectRoot?: string;
	binaryVersion?: string;
	outputRoot?: string;
	checkEvidencePaths?: boolean;
}

interface ReviewBundlePaths {
	bundleDir: string;
	markdown: string;
	candidates: string;
	manifest: string;
	sourceSlice: string;
}

interface ReviewBundleCandidateEntry {
	id: string;
	category: NativeHookAddressEntry["category"];
	cppName: string;
	expectedCount: number;
	expectedInstructionKind?: string;
	expectedOriginalTargetGroup?: string;
	discoveryStatus: string;
	discoverySummary: string;
	candidateRvas: Array<{
		id: string;
		rva: string;
		sourceId: string;
		label: string;
		status: "current_manifest_rva";
	}>;
	value?: string;
	instructionWindowAddresses: string[];
	evidence: string[];
	unresolvedItems: string[];
}

interface ReviewBundleCandidates {
	schemaVersion: 1;
	binaryVersion: string;
	manifestPath: string;
	sourceFile: string;
	generatedHeader: string;
	entries: ReviewBundleCandidateEntry[];
}

const SOURCE_SLICE_PATTERNS = [
	"papyrus_lootman_hook_addresses.generated.h",
	"DecodeDirectCallSite",
	"ValidateDirectCallSiteFamily",
	"WriteValidatedDirectCallHook",
	"InstallDirectCallHookFamily",
	"InstallDirectCallHookSite",
	"InstallEncounterZoneResetSuppressionHooks",
	"InstallWorkbenchSharedContainerHooks",
	"InstallWorkshopMaterialProbeHooks",
];

function normalizePathForMarkdown(value: string): string {
	return value.replaceAll("\\", "/");
}

function slugifyBinaryVersion(value: string): string {
	return value.trim().toLowerCase().replace(/[^a-z0-9.]+/g, "-").replace(/^-+|-+$/g, "");
}

function makeBundlePaths(
	manifest: NativeHookAddressManifest,
	root: string,
	binaryVersion: string,
	outputRoot?: string,
): ReviewBundlePaths {
	const baseOutput = outputRoot ?? path.join(root, "tools", "native-hooks", "reports");
	const bundleDir = path.join(baseOutput, slugifyBinaryVersion(binaryVersion || manifest.targetRuntime));
	return {
		bundleDir,
		markdown: path.join(bundleDir, "llm-review-bundle.md"),
		candidates: path.join(bundleDir, "candidate-rvas.json"),
		manifest: path.join(bundleDir, "manifest.json"),
		sourceSlice: path.join(bundleDir, "papyrus_lootman_hooks.slice.cpp"),
	};
}

function formatEntryValue(entry: NativeHookAddressEntry): string | undefined {
	if (entry.category === "call_site_rva") {
		return undefined;
	}
	return entry.value;
}

function buildUnresolvedItems(entry: NativeHookAddressEntry): string[] {
	const items: string[] = [];
	if (entry.discoveryStrategy.status !== "automated" && entry.discoveryStrategy.status !== "proven") {
		items.push(`Discovery strategy is ${entry.discoveryStrategy.status}: ${entry.discoveryStrategy.summary}`);
	}
	if (entry.category === "call_site_rva") {
		items.push("Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.");
	}
	if (entry.category === "layout_offset") {
		items.push("Layout offset is not an executable RVA; verify object layout separately before changing it.");
	}
	return items;
}

function buildCandidateEntry(entry: NativeHookAddressEntry): ReviewBundleCandidateEntry {
	const candidateRvas = (entry.sites ?? []).map((site) => ({
		id: site.id,
		rva: site.rva,
		sourceId: site.sourceId,
		label: site.label,
		status: "current_manifest_rva" as const,
	}));

	return {
		id: entry.id,
		category: entry.category,
		cppName: entry.cppName,
		expectedCount: entry.expectedCount,
		expectedInstructionKind: entry.expectedInstructionKind,
		expectedOriginalTargetGroup: entry.expectedOriginalTargetGroup,
		discoveryStatus: entry.discoveryStrategy.status,
		discoverySummary: entry.discoveryStrategy.summary,
		candidateRvas,
		value: formatEntryValue(entry),
		instructionWindowAddresses: candidateRvas.map((candidate) => candidate.rva),
		evidence: [...entry.evidence],
		unresolvedItems: buildUnresolvedItems(entry),
	};
}

function buildCandidates(
	manifest: NativeHookAddressManifest,
	manifestPath: string,
	binaryVersion: string,
): ReviewBundleCandidates {
	return {
		schemaVersion: 1,
		binaryVersion,
		manifestPath: normalizePathForMarkdown(manifestPath),
		sourceFile: manifest.sourceFile,
		generatedHeader: manifest.generatedHeader,
		entries: manifest.entries.map(buildCandidateEntry),
	};
}

function collectSourceSlice(sourceText: string): string {
	const lines = sourceText.split(/\r?\n/);
	const selected = new Set<number>();
	for (const pattern of SOURCE_SLICE_PATTERNS) {
		for (const [index, line] of lines.entries()) {
			if (!line.includes(pattern)) {
				continue;
			}
			const start = Math.max(0, index - 8);
			const end = Math.min(lines.length - 1, index + 36);
			for (let current = start; current <= end; current++) {
				selected.add(current);
			}
		}
	}

	const sorted = [...selected].sort((a, b) => a - b);
	const output: string[] = [
		"// Source slice generated for native hook address review.",
		"// It keeps the address catalog include and direct-call install helpers near their call sites.",
		"",
	];
	let previous = -2;
	for (const index of sorted) {
		if (previous !== -2 && index > previous + 1) {
			output.push("");
			output.push("// ...");
			output.push("");
		}
		const sourceLine = lines[index].replace(/[ \t]+$/u, "");
		output.push(sourceLine.length > 0
			? `// ${String(index + 1).padStart(4, " ")} | ${sourceLine}`
			: `// ${String(index + 1).padStart(4, " ")} |`);
		previous = index;
	}
	output.push("");
	return output.join("\n");
}

function formatCandidateList(entry: ReviewBundleCandidateEntry): string {
	if (entry.candidateRvas.length === 0) {
		return entry.value ? `value=${entry.value}` : "no RVA candidates";
	}
	return entry.candidateRvas
		.map((candidate) => `${candidate.id}=${candidate.rva}`)
		.join(", ");
}

function generateMarkdown(
	manifest: NativeHookAddressManifest,
	candidates: ReviewBundleCandidates,
	paths: ReviewBundlePaths,
	root: string,
): string {
	const relative = (file: string) => normalizePathForMarkdown(path.relative(root, file));
	const lines: string[] = [
		"# LootMan Native Hook Address Review Bundle",
		"",
		`Binary version: ${candidates.binaryVersion}`,
		`Manifest target runtime: ${manifest.targetRuntime}`,
		"",
		"## Bundle Files",
		`- Manifest copy: ${relative(paths.manifest)}`,
		`- Candidate RVAs: ${relative(paths.candidates)}`,
		`- Current hook source slice: ${relative(paths.sourceSlice)}`,
		"",
		"## Candidate RVAs",
	];

	for (const entry of candidates.entries) {
		lines.push(`- ${entry.id} (${entry.category}, ${entry.discoveryStatus}): ${formatCandidateList(entry)}`);
	}

	lines.push("", "## Instruction Windows");
	for (const entry of candidates.entries.filter((candidate) => candidate.instructionWindowAddresses.length > 0)) {
		lines.push(`- ${entry.id}: ${entry.instructionWindowAddresses.join(", ")}`);
	}

	lines.push("", "## Referenced Ghidra Reports");
	for (const reportPath of [...new Set(candidates.entries.flatMap((entry) => entry.evidence))].sort()) {
		lines.push(`- ${reportPath}`);
	}

	lines.push("", "## Unresolved Items Checklist");
	for (const entry of candidates.entries) {
		for (const item of entry.unresolvedItems) {
			lines.push(`- [ ] ${entry.id}: ${item}`);
		}
	}

	lines.push("");
	return lines.join("\n");
}

export function generateNativeHookReviewBundle(options: ReviewBundleOptions = {}): ReviewBundlePaths {
	const root = options.projectRoot ?? defaultProjectRoot;
	const manifestPath = options.manifestPath ?? defaultManifestPath;
	const manifest = readNativeHookManifest(manifestPath);
	const validation = validateNativeHookManifest(manifest, {
		projectRoot: root,
		checkEvidencePaths: options.checkEvidencePaths ?? true,
		checkGeneratedHeader: true,
		checkSource: true,
	});
	if (!validation.valid) {
		throw new Error(validation.errors.join("\n"));
	}

	const binaryVersion = options.binaryVersion ?? manifest.targetRuntime;
	const paths = makeBundlePaths(manifest, root, binaryVersion, options.outputRoot);
	const candidates = buildCandidates(manifest, path.relative(root, manifestPath), binaryVersion);
	const sourcePath = path.join(root, manifest.sourceFile);

	fs.ensureDirSync(paths.bundleDir);
	fs.writeJsonSync(paths.manifest, manifest, { spaces: 2 });
	fs.writeJsonSync(paths.candidates, candidates, { spaces: 2 });
	fs.writeFileSync(paths.sourceSlice, collectSourceSlice(fs.readFileSync(sourcePath, "utf8")), "utf8");
	fs.writeFileSync(paths.markdown, generateMarkdown(manifest, candidates, paths, root), "utf8");

	return paths;
}

export function parseReviewBundleArgs(argv: string[]): ReviewBundleOptions {
	const options: ReviewBundleOptions = {};
	for (const arg of argv) {
		if (arg.startsWith("--binary-version=")) {
			options.binaryVersion = arg.slice("--binary-version=".length);
		} else if (arg.startsWith("--output-root=")) {
			options.outputRoot = path.resolve(arg.slice("--output-root=".length));
		} else if (arg.startsWith("--manifest=")) {
			options.manifestPath = path.resolve(arg.slice("--manifest=".length));
		} else {
			throw new Error(`Unknown option: ${arg}`);
		}
	}
	return options;
}
