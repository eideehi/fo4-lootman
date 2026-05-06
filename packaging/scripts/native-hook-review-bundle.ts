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
	proofReadiness: ReviewBundleProofReadiness;
}

interface ReviewBundleCandidates {
	schemaVersion: 1;
	binaryVersion: string;
	manifestPath: string;
	sourceFile: string;
	generatedHeader: string;
	entries: ReviewBundleCandidateEntry[];
}

type ProofReadinessStatus =
	| "ready_for_proof_metadata"
	| "needs_instruction_window_refresh"
	| "needs_target_allrefs_report"
	| "needs_exclusion_triage"
	| "needs_rediscovery"
	| "already_proven"
	| "not_applicable";

interface ReviewBundleProofSelectedSite {
	id: string;
	rva: string;
	sourceId: string;
	label: string;
	absoluteAddress: string;
}

interface ReviewBundleProofTargetCandidate {
	targetAbsoluteAddress: string;
	selectedSiteIds: string[];
	selectedSiteAbsoluteAddresses: string[];
	evidence: string[];
}

interface ReviewBundleProofTargetReport {
	path: string;
	targetAbsoluteAddress: string;
	referenceCount: number;
	selectedReferenceCount: number;
	missingSelectedReferences: string[];
	extraSameTargetReferences: string[];
}

interface ReviewBundleProofReadiness {
	status: ProofReadinessStatus;
	summary: string;
	selectedSites: ReviewBundleProofSelectedSite[];
	targetAbsoluteAddress?: string;
	targetCandidates: ReviewBundleProofTargetCandidate[];
	targetReport?: ReviewBundleProofTargetReport;
	directCallInstructionCount: number;
	missingDirectCallSites: string[];
	extraSameTargetReferences: string[];
	recommendedNextAction: string;
}

interface EvidenceReport {
	path: string;
	text: string;
	imageBase?: number;
}

interface CallReference {
	from: string;
	to: string;
	type: string;
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
const PROOF_READINESS_STATUS_ORDER: ProofReadinessStatus[] = [
	"ready_for_proof_metadata",
	"needs_instruction_window_refresh",
	"needs_target_allrefs_report",
	"needs_exclusion_triage",
	"needs_rediscovery",
	"already_proven",
	"not_applicable",
];
const PLAIN_HEX_PATTERN = /^[0-9A-F]+$/i;

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
	if (
		entry.category === "call_site_rva"
		&& entry.discoveryStrategy.status !== "automated"
		&& entry.discoveryStrategy.status !== "proven"
	) {
		items.push("Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.");
	}
	if (entry.category === "layout_offset") {
		items.push("Layout offset is not an executable RVA; verify object layout separately before changing it.");
	}
	return items;
}

function parseHex(value: string): number | null {
	const raw = value.startsWith("0x") || value.startsWith("0X") ? value.slice(2) : value;
	if (!PLAIN_HEX_PATTERN.test(raw)) {
		return null;
	}
	const parsed = Number.parseInt(raw, 16);
	if (!Number.isSafeInteger(parsed) || parsed < 0) {
		return null;
	}
	return parsed;
}

function formatAbsoluteAddress(value: number): string {
	return `0x${value.toString(16).toUpperCase()}`;
}

function normalizeHexLiteral(value: string): string | null {
	const parsed = parseHex(value);
	return parsed === null ? null : formatAbsoluteAddress(parsed);
}

function compareHexLiterals(a: string, b: string): number {
	const parsedA = parseHex(a) ?? 0;
	const parsedB = parseHex(b) ?? 0;
	return parsedA - parsedB;
}

function stripHexPrefix(value: string): string {
	return value.replace(/^0x/i, "").toLowerCase();
}

function parseImageBase(reportText: string): number | undefined {
	const match = reportText.match(/^Image base:\s*([0-9A-F]+)\s*$/im);
	return match ? parseHex(match[1]) ?? undefined : undefined;
}

function readEvidenceReports(root: string, entry: NativeHookAddressEntry): EvidenceReport[] {
	return entry.evidence.map((reportPath) => {
		const text = fs.readFileSync(path.join(root, reportPath), "utf8");
		return {
			path: reportPath,
			text,
			imageBase: parseImageBase(text),
		};
	});
}

function findTargetSection(reportText: string, targetAbsoluteAddress: string): string | undefined {
	const target = stripHexPrefix(targetAbsoluteAddress);
	const lines = reportText.split(/\r?\n/);
	const start = lines.findIndex((line) => line.trim().toLowerCase() === `target ${target}`);
	if (start === -1) {
		return undefined;
	}

	let end = lines.length;
	for (let index = start + 1; index < lines.length; index++) {
		const trimmed = lines[index].trim().toLowerCase();
		if (trimmed.startsWith("target ") && PLAIN_HEX_PATTERN.test(trimmed.slice("target ".length))) {
			end = index;
			break;
		}
	}

	return lines.slice(start, end).join("\n");
}

function parseEntryReferences(sectionText: string, targetAbsoluteAddress: string): { hasReferenceBlock: boolean; references: CallReference[] } {
	const references: CallReference[] = [];
	let inReferences = false;
	let hasReferenceBlock = false;
	const target = stripHexPrefix(targetAbsoluteAddress);

	for (const line of sectionText.split(/\r?\n/)) {
		const trimmed = line.trim();
		if (trimmed === "References to entry:") {
			inReferences = true;
			hasReferenceBlock = true;
			continue;
		}
		if (inReferences && (trimmed === "" || /^[A-Za-z].+:$/.test(trimmed))) {
			break;
		}
		if (!inReferences) {
			continue;
		}

		const match = trimmed.match(/^([0-9A-F]+)\s+->\s+([0-9A-F]+)\s+type=([A-Z_]+)$/i);
		if (!match || stripHexPrefix(match[2]) !== target) {
			continue;
		}
		references.push({
			from: normalizeHexLiteral(match[1]) ?? `0x${match[1].toUpperCase()}`,
			to: normalizeHexLiteral(match[2]) ?? `0x${match[2].toUpperCase()}`,
			type: match[3],
		});
	}

	return { hasReferenceBlock, references };
}

function findFirstImageBase(reports: EvidenceReport[]): number | undefined {
	return reports.find((report) => report.imageBase !== undefined)?.imageBase;
}

function buildSelectedSites(entry: NativeHookAddressEntry, imageBase: number): ReviewBundleProofSelectedSite[] {
	return (entry.sites ?? []).map((site) => {
		const rva = parseHex(site.rva);
		return {
			id: site.id,
			rva: site.rva,
			sourceId: site.sourceId,
			label: site.label,
			absoluteAddress: rva === null ? site.rva : formatAbsoluteAddress(imageBase + rva),
		};
	});
}

function addTargetCandidate(
	targets: Map<string, {
		targetAbsoluteAddress: string;
		selectedSiteIds: Set<string>;
		selectedSiteAbsoluteAddresses: Set<string>;
		evidence: Set<string>;
	}>,
	site: ReviewBundleProofSelectedSite,
	targetAbsoluteAddress: string,
	reportPath: string,
): void {
	const normalizedTarget = normalizeHexLiteral(targetAbsoluteAddress);
	if (!normalizedTarget) {
		return;
	}
	const existing = targets.get(normalizedTarget) ?? {
		targetAbsoluteAddress: normalizedTarget,
		selectedSiteIds: new Set<string>(),
		selectedSiteAbsoluteAddresses: new Set<string>(),
		evidence: new Set<string>(),
	};
	existing.selectedSiteIds.add(site.id);
	existing.selectedSiteAbsoluteAddresses.add(site.absoluteAddress);
	existing.evidence.add(reportPath);
	targets.set(normalizedTarget, existing);
}

function collectTargetCandidates(
	reports: EvidenceReport[],
	selectedSites: ReviewBundleProofSelectedSite[],
): ReviewBundleProofTargetCandidate[] {
	const targets = new Map<string, {
		targetAbsoluteAddress: string;
		selectedSiteIds: Set<string>;
		selectedSiteAbsoluteAddresses: Set<string>;
		evidence: Set<string>;
	}>();

	for (const site of selectedSites) {
		const source = stripHexPrefix(site.absoluteAddress);
		for (const report of reports) {
			const directCall = report.text.match(new RegExp(`^\\s*${source}:\\s+CALL\\s+0x([0-9A-F]+)\\b`, "im"));
			if (directCall) {
				addTargetCandidate(targets, site, `0x${directCall[1]}`, report.path);
			}

			const reference = report.text.match(new RegExp(`^\\s*${source}\\s+->\\s+([0-9A-F]+)\\s+type=UNCONDITIONAL_CALL\\b`, "im"));
			if (reference) {
				addTargetCandidate(targets, site, `0x${reference[1]}`, report.path);
			}
		}
	}

	return [...targets.values()]
		.sort((a, b) => compareHexLiterals(a.targetAbsoluteAddress, b.targetAbsoluteAddress))
		.map((target) => ({
			targetAbsoluteAddress: target.targetAbsoluteAddress,
			selectedSiteIds: [...target.selectedSiteIds].sort(),
			selectedSiteAbsoluteAddresses: [...target.selectedSiteAbsoluteAddresses].sort(compareHexLiterals),
			evidence: [...target.evidence].sort(),
		}));
}

function hasDirectCallInstruction(reportText: string, sourceAbsoluteAddress: string, targetAbsoluteAddress: string): boolean {
	const source = stripHexPrefix(sourceAbsoluteAddress);
	const target = stripHexPrefix(targetAbsoluteAddress);
	return new RegExp(`^\\s*${source}:\\s+CALL\\s+0x${target}\\b`, "im").test(reportText);
}

function buildTargetReport(
	reports: EvidenceReport[],
	targetAbsoluteAddress: string,
	selectedSites: ReviewBundleProofSelectedSite[],
): ReviewBundleProofTargetReport | undefined {
	const selected = new Set(selectedSites.map((site) => stripHexPrefix(site.absoluteAddress)));
	for (const report of reports) {
		const section = findTargetSection(report.text, targetAbsoluteAddress);
		if (!section) {
			continue;
		}
		const parsed = parseEntryReferences(section, targetAbsoluteAddress);
		if (!parsed.hasReferenceBlock) {
			continue;
		}
		const references = parsed.references
			.filter((reference) => reference.type === "UNCONDITIONAL_CALL")
			.map((reference) => reference.from)
			.sort(compareHexLiterals);
		const referenceSet = new Set(references.map(stripHexPrefix));
		const missingSelectedReferences = selectedSites
			.map((site) => site.absoluteAddress)
			.filter((absoluteAddress) => !referenceSet.has(stripHexPrefix(absoluteAddress)))
			.sort(compareHexLiterals);
		const extraSameTargetReferences = references
			.filter((absoluteAddress) => !selected.has(stripHexPrefix(absoluteAddress)))
			.sort(compareHexLiterals);

		return {
			path: report.path,
			targetAbsoluteAddress,
			referenceCount: references.length,
			selectedReferenceCount: selectedSites.length - missingSelectedReferences.length,
			missingSelectedReferences,
			extraSameTargetReferences,
		};
	}
	return undefined;
}

function countDirectCallInstructions(
	reports: EvidenceReport[],
	targetAbsoluteAddress: string | undefined,
	selectedSites: ReviewBundleProofSelectedSite[],
): { count: number; missing: string[] } {
	if (!targetAbsoluteAddress) {
		return {
			count: 0,
			missing: selectedSites.map((site) => site.absoluteAddress).sort(compareHexLiterals),
		};
	}

	const missing = selectedSites
		.filter((site) => !reports.some((report) => hasDirectCallInstruction(report.text, site.absoluteAddress, targetAbsoluteAddress)))
		.map((site) => site.absoluteAddress)
		.sort(compareHexLiterals);
	return {
		count: selectedSites.length - missing.length,
		missing,
	};
}

function summarizeStatus(status: ProofReadinessStatus, entry: NativeHookAddressEntry): string {
	switch (status) {
		case "ready_for_proof_metadata":
			return "Existing evidence satisfies the resolver proof gates for this unproven call-site family.";
		case "needs_instruction_window_refresh":
			return "Target allrefs are present, but at least one selected site lacks a direct CALL instruction line.";
		case "needs_target_allrefs_report":
			return "Selected sites point to one target, but no target allrefs report section was found.";
		case "needs_exclusion_triage":
			return "Target allrefs include same-target references outside the selected hook sites.";
		case "needs_rediscovery":
			return "Current evidence does not identify exactly one usable target for the selected sites.";
		case "already_proven":
			return "The manifest already contains resolver proof metadata for this call-site family.";
		case "not_applicable":
			return `${entry.category} entries do not use direct call-site proof refresh.`;
	}
}

function recommendNextAction(
	status: ProofReadinessStatus,
	targetAbsoluteAddress: string | undefined,
	missingDirectCallSites: string[],
	extraSameTargetReferences: string[],
): string {
	switch (status) {
		case "ready_for_proof_metadata":
			return "Create a separate proof-promotion checkpoint with explicit proof.sites metadata.";
		case "needs_instruction_window_refresh":
			return `Refresh instruction-window evidence for ${missingDirectCallSites.join(", ")}.`;
		case "needs_target_allrefs_report":
			return `Generate a target allrefs report for ${targetAbsoluteAddress ?? "the selected target"}.`;
		case "needs_exclusion_triage":
			return `Triage ${extraSameTargetReferences.length} extra same-target references before adding exclusions.`;
		case "needs_rediscovery":
			return "Refresh discovery reports around the selected manifest sites.";
		case "already_proven":
			return "No proof refresh needed; resolver proof metadata is already present.";
		case "not_applicable":
			return "No call-site proof refresh needed.";
	}
}

function buildProofReadiness(entry: NativeHookAddressEntry, root: string): ReviewBundleProofReadiness {
	if (entry.category !== "call_site_rva") {
		const status: ProofReadinessStatus = "not_applicable";
		return {
			status,
			summary: summarizeStatus(status, entry),
			selectedSites: [],
			targetCandidates: [],
			directCallInstructionCount: 0,
			missingDirectCallSites: [],
			extraSameTargetReferences: [],
			recommendedNextAction: recommendNextAction(status, undefined, [], []),
		};
	}

	const reports = readEvidenceReports(root, entry);
	const imageBase = findFirstImageBase(reports);
	const selectedSites = imageBase === undefined ? [] : buildSelectedSites(entry, imageBase);
	const provenTarget = entry.discoveryStrategy.proof?.targetAbsoluteAddress
		? normalizeHexLiteral(entry.discoveryStrategy.proof.targetAbsoluteAddress) ?? entry.discoveryStrategy.proof.targetAbsoluteAddress
		: undefined;

	if (entry.discoveryStrategy.status === "proven") {
		const status: ProofReadinessStatus = "already_proven";
		const directCalls = countDirectCallInstructions(reports, provenTarget, selectedSites);
		return {
			status,
			summary: summarizeStatus(status, entry),
			selectedSites,
			targetAbsoluteAddress: provenTarget,
			targetCandidates: provenTarget
				? [{
					targetAbsoluteAddress: provenTarget,
					selectedSiteIds: selectedSites.map((site) => site.id).sort(),
					selectedSiteAbsoluteAddresses: selectedSites.map((site) => site.absoluteAddress).sort(compareHexLiterals),
					evidence: [...entry.evidence].sort(),
				}]
				: [],
			directCallInstructionCount: directCalls.count,
			missingDirectCallSites: directCalls.missing,
			extraSameTargetReferences: [],
			recommendedNextAction: recommendNextAction(status, provenTarget, [], []),
		};
	}

	if (imageBase === undefined || selectedSites.length === 0) {
		const status: ProofReadinessStatus = "needs_rediscovery";
		return {
			status,
			summary: summarizeStatus(status, entry),
			selectedSites,
			targetCandidates: [],
			directCallInstructionCount: 0,
			missingDirectCallSites: [],
			extraSameTargetReferences: [],
			recommendedNextAction: recommendNextAction(status, undefined, [], []),
		};
	}

	const targetCandidates = collectTargetCandidates(reports, selectedSites);
	if (targetCandidates.length !== 1) {
		const status: ProofReadinessStatus = "needs_rediscovery";
		return {
			status,
			summary: summarizeStatus(status, entry),
			selectedSites,
			targetCandidates,
			directCallInstructionCount: 0,
			missingDirectCallSites: selectedSites.map((site) => site.absoluteAddress).sort(compareHexLiterals),
			extraSameTargetReferences: [],
			recommendedNextAction: recommendNextAction(status, undefined, [], []),
		};
	}

	const targetAbsoluteAddress = targetCandidates[0].targetAbsoluteAddress;
	const targetReport = buildTargetReport(reports, targetAbsoluteAddress, selectedSites);
	const directCalls = countDirectCallInstructions(reports, targetAbsoluteAddress, selectedSites);

	let status: ProofReadinessStatus;
	if (!targetReport) {
		status = "needs_target_allrefs_report";
	} else if (targetReport.missingSelectedReferences.length > 0) {
		status = "needs_rediscovery";
	} else if (targetReport.extraSameTargetReferences.length > 0) {
		status = "needs_exclusion_triage";
	} else if (directCalls.missing.length > 0) {
		status = "needs_instruction_window_refresh";
	} else {
		status = "ready_for_proof_metadata";
	}

	const extraSameTargetReferences = targetReport?.extraSameTargetReferences ?? [];
	return {
		status,
		summary: summarizeStatus(status, entry),
		selectedSites,
		targetAbsoluteAddress,
		targetCandidates,
		targetReport,
		directCallInstructionCount: directCalls.count,
		missingDirectCallSites: directCalls.missing,
		extraSameTargetReferences,
		recommendedNextAction: recommendNextAction(
			status,
			targetAbsoluteAddress,
			directCalls.missing,
			extraSameTargetReferences,
		),
	};
}

function buildCandidateEntry(entry: NativeHookAddressEntry, root: string): ReviewBundleCandidateEntry {
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
		proofReadiness: buildProofReadiness(entry, root),
	};
}

function buildCandidates(
	manifest: NativeHookAddressManifest,
	manifestPath: string,
	binaryVersion: string,
	root: string,
): ReviewBundleCandidates {
	return {
		schemaVersion: 1,
		binaryVersion,
		manifestPath: normalizePathForMarkdown(manifestPath),
		sourceFile: manifest.sourceFile,
		generatedHeader: manifest.generatedHeader,
		entries: manifest.entries.map((entry) => buildCandidateEntry(entry, root)),
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

function formatProofReadinessList(entry: ReviewBundleCandidateEntry): string {
	const readiness = entry.proofReadiness;
	const target = readiness.targetAbsoluteAddress ?? (
		readiness.targetCandidates.length > 0
			? readiness.targetCandidates.map((candidate) => candidate.targetAbsoluteAddress).join(", ")
			: "none"
	);
	const selectedCount = readiness.selectedSites.length;
	const selectedReferences = readiness.targetReport
		? `${readiness.targetReport.selectedReferenceCount}/${selectedCount}`
		: "n/a";
	const directCalls = selectedCount > 0
		? `${readiness.directCallInstructionCount}/${selectedCount}`
		: "n/a";
	const extras = readiness.extraSameTargetReferences.length;
	return [
		`target=${target}`,
		`selectedRefs=${selectedReferences}`,
		`directCalls=${directCalls}`,
		`extras=${extras}`,
		readiness.recommendedNextAction,
	].join("; ");
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

	lines.push("", "## Proof Readiness");
	for (const status of PROOF_READINESS_STATUS_ORDER) {
		const entries = candidates.entries.filter((entry) => entry.proofReadiness.status === status);
		if (entries.length === 0) {
			continue;
		}
		lines.push(`### ${status}`);
		for (const entry of entries) {
			lines.push(`- ${entry.id}: ${formatProofReadinessList(entry)}`);
		}
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
	const candidates = buildCandidates(manifest, path.relative(root, manifestPath), binaryVersion, root);
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
