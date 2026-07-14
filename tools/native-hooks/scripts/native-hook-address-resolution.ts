import fs from "fs-extra";
import path from "node:path";
import {
	assertValidNativeHookManifest,
	defaultManifestPath,
	generateNativeHookHeader,
	type NativeHookAddressEntry,
	type NativeHookAddressManifest,
	type NativeHookDiscoveryProof,
	projectRoot,
	readNativeHookManifest,
	writeGeneratedNativeHookHeader,
} from "./native-hook-addresses.js";

export interface ResolveNativeHookAddressOptions {
	projectRoot?: string;
	manifestPath?: string;
	write?: boolean;
	evidenceReportPath?: string;
}

export interface ResolvedNativeHookAddressEntry {
	id: string;
	targetAbsoluteAddress: string;
	candidateRvas: string[];
	changed: boolean;
	sites?: ResolvedNativeHookAddressSite[];
}

export interface ResolveNativeHookAddressResult {
	manifestPath: string;
	generatedHeader?: string;
	resolvedEntries: ResolvedNativeHookAddressEntry[];
	skippedEntries: string[];
	wroteManifest: boolean;
}

export interface ResolvedNativeHookAddressSite {
	siteId: string;
	rva: string;
	changed: boolean;
	expectedTargetRva: string;
	contextBytes: string;
	contextInstructionCount: number;
}

interface CallReference {
	from: number;
	to: number;
	type: string;
}

interface EvidenceInstruction {
	address: number;
	bytes: number[];
	disassembly: string;
}

const DEFAULT_EVIDENCE_REPORT = "tools/ghidra/reports/fallout4-1.11.221/proven-call-site-evidence.txt";

const HEX_LITERAL_PATTERN = /^0x[0-9A-F]+$/i;
const PLAIN_HEX_PATTERN = /^[0-9A-F]+$/i;

function parseHex(value: string, label: string): number {
	const raw = value.startsWith("0x") || value.startsWith("0X") ? value.slice(2) : value;
	if (!PLAIN_HEX_PATTERN.test(raw)) {
		throw new Error(`${label} must be a hex literal: ${value}`);
	}
	const parsed = Number.parseInt(raw, 16);
	if (!Number.isSafeInteger(parsed) || parsed < 0) {
		throw new Error(`${label} must be a non-negative safe integer: ${value}`);
	}
	return parsed;
}

function formatRva(value: number): string {
	return `0x${value.toString(16).toUpperCase()}`;
}

function formatAbsoluteAddress(value: number): string {
	return `0x${value.toString(16).toUpperCase()}`;
}

function parseEvidenceReport(root: string, reportPath: string): Map<number, EvidenceInstruction[]> {
	const absolutePath = path.isAbsolute(reportPath) ? reportPath : path.join(root, reportPath);
	const reportText = fs.readFileSync(absolutePath, "utf8");
	const windows = new Map<number, EvidenceInstruction[]>();
	let currentAddress: number | undefined;
	for (const [lineIndex, line] of reportText.split(/\r?\n/).entries()) {
		const targetMatch = line.trim().match(/^Target\s+([0-9A-F]+)$/i);
		if (targetMatch) {
			currentAddress = parseHex(targetMatch[1], `${reportPath}:${lineIndex + 1} target`);
			if (windows.has(currentAddress)) throw new Error(`${reportPath}: duplicate Target ${formatAbsoluteAddress(currentAddress)}.`);
			windows.set(currentAddress, []);
			continue;
		}
		if (currentAddress === undefined || line.trim() === "") continue;
		const instructionMatch = line.match(/^\s*([0-9A-F]+):\s*\[([^\]]+)\]\s+(.+)$/i);
		if (!instructionMatch) continue;
		const rawBytes = instructionMatch[2].trim().split(/\s+/);
		if (rawBytes.length === 0 || rawBytes.some((byte) => !/^[0-9A-F]{2}$/i.test(byte))) {
			throw new Error(`${reportPath}:${lineIndex + 1}: malformed raw instruction bytes.`);
		}
		windows.get(currentAddress)?.push({
			address: parseHex(instructionMatch[1], `${reportPath}:${lineIndex + 1} instruction address`),
			bytes: rawBytes.map((byte) => Number.parseInt(byte, 16)),
			disassembly: instructionMatch[3],
		});
	}
	return windows;
}

function signedInt32(bytes: number[]): number {
	const value = (bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
	return value;
}

function deriveSiteEvidence(
	entry: NativeHookAddressEntry,
	siteId: string,
	callAddress: number,
	expectedTarget: number,
	imageBase: number,
	windows: Map<number, EvidenceInstruction[]>,
): { expectedTargetRva: string; candidates: { bytes: string; instructionCount: number }[] } {
	const instructions = windows.get(callAddress);
	if (!instructions || instructions.length < 2) {
		throw new Error(`${entry.id}: missing raw-byte instruction window for ${siteId} at ${formatAbsoluteAddress(callAddress)}.`);
	}
	const call = instructions[0];
	if (call.address !== callAddress || call.bytes.length !== 5 || call.bytes[0] !== 0xE8) {
		throw new Error(`${entry.id}: ${siteId} must begin with a five-byte E8 CALL rel32 instruction.`);
	}
	const decodedTarget = callAddress + 5 + signedInt32(call.bytes.slice(1));
	if (decodedTarget !== expectedTarget) {
		throw new Error(`${entry.id}: ${siteId} raw CALL target ${formatAbsoluteAddress(decodedTarget)} does not match ${formatAbsoluteAddress(expectedTarget)}.`);
	}
	let bytes: number[] = [];
	const candidates = instructions.slice(1).map((instruction, index) => {
		bytes = bytes.concat(instruction.bytes);
		return {
			bytes: bytes.map((byte) => byte.toString(16).padStart(2, "0").toUpperCase()).join(" "),
			instructionCount: index + 1,
		};
	});
	return { expectedTargetRva: formatRva(expectedTarget - imageBase), candidates };
}

function selectUniqueSiteContexts(
	entry: NativeHookAddressEntry,
	siteEvidence: Map<string, { expectedTargetRva: string; candidates: { bytes: string; instructionCount: number }[] }>,
): Map<string, { expectedTargetRva: string; contextBytes: string; contextInstructionCount: number }> {
	const selected = new Map<string, { expectedTargetRva: string; contextBytes: string; contextInstructionCount: number }>();
	for (const [siteId, evidence] of siteEvidence) {
		const candidate = evidence.candidates.find((value) => {
			for (const [otherSiteId, otherEvidence] of siteEvidence) {
				if (otherSiteId === siteId) continue;
				if (otherEvidence.candidates.some((other) => other.bytes === value.bytes)) return false;
			}
			return true;
		});
		if (!candidate) throw new Error(`${entry.id}: no non-empty whole-instruction context uniquely identifies ${siteId} within its family.`);
		selected.set(siteId, {
			expectedTargetRva: evidence.expectedTargetRva,
			contextBytes: candidate.bytes,
			contextInstructionCount: candidate.instructionCount,
		});
	}
	return selected;
}

function stripHexPrefix(value: string): string {
	return value.replace(/^0x/i, "").toLowerCase();
}

function parseImageBase(reportText: string, reportPath: string): number {
	const match = reportText.match(/^Image base:\s*([0-9A-F]+)\s*$/im);
	if (!match) {
		throw new Error(`${reportPath}: missing Image base line.`);
	}
	return parseHex(match[1], `${reportPath} image base`);
}

function findTargetSection(reportText: string, targetAbsoluteAddress: string, reportPath: string): string {
	const target = stripHexPrefix(targetAbsoluteAddress);
	const lines = reportText.split(/\r?\n/);
	const start = lines.findIndex((line) => line.trim().toLowerCase() === `target ${target}`);
	if (start === -1) {
		throw new Error(`${reportPath}: missing Target ${targetAbsoluteAddress}.`);
	}

	let end = lines.length;
	for (let index = start + 1; index < lines.length; index++) {
		const trimmed = lines[index].trim().toLowerCase();
		if (trimmed.startsWith("target ") && HEX_LITERAL_PATTERN.test(`0x${trimmed.slice("target ".length)}`)) {
			end = index;
			break;
		}
	}

	return lines.slice(start, end).join("\n");
}

function parseEntryReferences(sectionText: string, targetAddress: number): CallReference[] {
	const references: CallReference[] = [];
	let inReferences = false;
	for (const line of sectionText.split(/\r?\n/)) {
		const trimmed = line.trim();
		if (trimmed === "References to entry:") {
			inReferences = true;
			continue;
		}
		if (inReferences && (trimmed === "" || /^[A-Za-z].+:$/.test(trimmed))) {
			break;
		}
		if (!inReferences) {
			continue;
		}

		const match = trimmed.match(/^([0-9A-F]+)\s+->\s+([0-9A-F]+)\s+type=([A-Z_]+)$/i);
		if (!match) {
			continue;
		}
		const from = parseHex(match[1], "reference source");
		const to = parseHex(match[2], "reference target");
		if (to !== targetAddress) {
			continue;
		}
		references.push({ from, to, type: match[3] });
	}
	return references;
}

function hasDirectCallInstruction(reportText: string, from: number, to: number): boolean {
	const source = from.toString(16);
	const target = to.toString(16);
	const pattern = new RegExp(`^\\s*${source}:\\s+(?:\\[[^\\]]+\\]\\s+)?CALL\\s+0x${target}\\b`, "im");
	return pattern.test(reportText);
}

function getProofReports(root: string, proof: NativeHookDiscoveryProof): { path: string; text: string }[] {
	if (proof.kind !== "ghidra_reference_report") {
		throw new Error(`Unsupported native hook proof kind: ${proof.kind}`);
	}

	const reports = [proof.report, ...(proof.instructionReports ?? [])];
	const uniqueReports = [...new Set(reports)];
	return uniqueReports.map((reportPath) => {
		const absolutePath = path.join(root, reportPath);
		return {
			path: reportPath,
			text: fs.readFileSync(absolutePath, "utf8"),
		};
	});
}

function readProofContext(
	root: string,
	entry: NativeHookAddressEntry,
	proof: NativeHookDiscoveryProof,
): {
	targetAddress: number;
	imageBase: number;
	references: CallReference[];
	instructionReports: { path: string; text: string }[];
} {
	const targetAddress = parseHex(proof.targetAbsoluteAddress, `${entry.id} proof target`);
	const proofReportPath = path.join(root, proof.report);
	const proofReportText = fs.readFileSync(proofReportPath, "utf8");
	const imageBase = parseImageBase(proofReportText, proof.report);
	const section = findTargetSection(proofReportText, proof.targetAbsoluteAddress, proof.report);
	const references = parseEntryReferences(section, targetAddress)
		.filter((reference) => reference.type === proof.referenceType);
	const instructionReports = getProofReports(root, proof);

	return {
		targetAddress,
		imageBase,
		references,
		instructionReports,
	};
}

function assertDirectCallInstruction(
	entry: NativeHookAddressEntry,
	instructionReports: { path: string; text: string }[],
	reference: CallReference,
): void {
	const hasInstruction = instructionReports.some((report) => hasDirectCallInstruction(report.text, reference.from, reference.to));
	if (!hasInstruction) {
		throw new Error(
			`${entry.id}: ${formatAbsoluteAddress(reference.from)} is referenced but no CALL ${formatAbsoluteAddress(reference.to)} instruction line was found.`,
		);
	}
}

function referenceToRva(entry: NativeHookAddressEntry, reference: CallReference, imageBase: number): string {
	const rva = reference.from - imageBase;
	if (rva < 0) {
		throw new Error(`${entry.id}: ${formatAbsoluteAddress(reference.from)} is below image base ${formatAbsoluteAddress(imageBase)}.`);
	}
	return formatRva(rva);
}

function resolveSingleCallSiteEntry(
	root: string,
	entry: NativeHookAddressEntry,
	proof: NativeHookDiscoveryProof,
	evidenceWindows: Map<number, EvidenceInstruction[]>,
): ResolvedNativeHookAddressEntry {
	if (!entry.sites || entry.sites.length !== 1) {
		throw new Error(`${entry.id}: expected exactly one manifest site.`);
	}

	const { targetAddress, imageBase, references, instructionReports } = readProofContext(root, entry, proof);

	if (references.length !== entry.expectedCount) {
		throw new Error(
			`${entry.id}: expected ${entry.expectedCount} ${proof.referenceType} candidate, found ${references.length}.`,
		);
	}

	for (const reference of references) {
		assertDirectCallInstruction(entry, instructionReports, reference);
	}

	const candidateRvas = references
		.map((reference) => referenceToRva(entry, reference, imageBase))
		.sort((a, b) => parseHex(a, "candidate RVA") - parseHex(b, "candidate RVA"));
	let changed = entry.sites[0].rva.toUpperCase() !== candidateRvas[0].toUpperCase();
	const siteId = entry.sites[0].id;
	const siteEvidence = new Map([[siteId, deriveSiteEvidence(
		entry, siteId, references[0].from, targetAddress, imageBase, evidenceWindows,
	)]]);
	const selected = selectUniqueSiteContexts(entry, siteEvidence).get(siteId);
	if (!selected) throw new Error(`${entry.id}: failed to select context for ${siteId}.`);
	changed ||= entry.sites[0].expectedTargetRva?.toUpperCase() !== selected.expectedTargetRva.toUpperCase() ||
		entry.sites[0].contextSignatureVersion !== 1 || entry.sites[0].contextBytes !== selected.contextBytes;
	entry.sites[0] = {
		...entry.sites[0],
		rva: candidateRvas[0],
		expectedTargetRva: selected.expectedTargetRva,
		contextSignatureVersion: 1,
		contextBytes: selected.contextBytes,
	};

	return {
		id: entry.id,
		targetAbsoluteAddress: formatAbsoluteAddress(targetAddress),
		candidateRvas,
		changed,
		sites: [{ siteId, rva: candidateRvas[0], changed, ...selected }],
	};
}

function resolveExplicitCallSiteEntry(
	root: string,
	entry: NativeHookAddressEntry,
	proof: NativeHookDiscoveryProof,
	evidenceWindows: Map<number, EvidenceInstruction[]>,
): ResolvedNativeHookAddressEntry {
	if (!entry.sites || entry.sites.length !== entry.expectedCount) {
		throw new Error(`${entry.id}: expected exactly ${entry.expectedCount} manifest sites.`);
	}
	if (!proof.sites || proof.sites.length === 0) {
		throw new Error(`${entry.id}: multi-site proof requires explicit proof sites.`);
	}

	const { targetAddress, imageBase, references, instructionReports } = readProofContext(root, entry, proof);
	const referencesBySource = new Map(references.map((reference) => [reference.from, reference]));
	const selectedSources = new Set<number>();
	const selectedReferencesBySiteId = new Map<string, CallReference>();

	for (const proofSite of proof.sites) {
		const sourceAddress = parseHex(proofSite.absoluteAddress, `${entry.id} proof site ${proofSite.siteId}`);
		const reference = referencesBySource.get(sourceAddress);
		if (!reference) {
			throw new Error(
				`${entry.id}: proof site ${proofSite.siteId} ${formatAbsoluteAddress(sourceAddress)} was not found as ${proof.referenceType} reference to ${formatAbsoluteAddress(targetAddress)}.`,
			);
		}
		assertDirectCallInstruction(entry, instructionReports, reference);
		selectedSources.add(sourceAddress);
		selectedReferencesBySiteId.set(proofSite.siteId, reference);
	}

	const excludedSources = new Set<number>();
	for (const excludedReference of proof.excludedReferences ?? []) {
		const sourceAddress = parseHex(excludedReference.absoluteAddress, `${entry.id} excluded reference`);
		if (selectedSources.has(sourceAddress)) {
			throw new Error(`${entry.id}: excluded reference ${formatAbsoluteAddress(sourceAddress)} overlaps a selected proof site.`);
		}
		if (!referencesBySource.has(sourceAddress)) {
			throw new Error(
				`${entry.id}: excluded reference ${formatAbsoluteAddress(sourceAddress)} was not found as ${proof.referenceType} reference to ${formatAbsoluteAddress(targetAddress)}.`,
			);
		}
		excludedSources.add(sourceAddress);
	}

	for (const reference of references) {
		if (selectedSources.has(reference.from) || excludedSources.has(reference.from)) {
			continue;
		}
		throw new Error(
			`${entry.id}: ${formatAbsoluteAddress(reference.from)} is an extra ${proof.referenceType} reference to ${formatAbsoluteAddress(targetAddress)} and must be listed in excludedReferences.`,
		);
	}

	const derivedEvidence = new Map(entry.sites.map((site) => {
		const reference = selectedReferencesBySiteId.get(site.id);
		if (!reference) throw new Error(`${entry.id}: proof.sites must include manifest site ${site.id}.`);
		return [site.id, deriveSiteEvidence(entry, site.id, reference.from, targetAddress, imageBase, evidenceWindows)] as const;
	}));
	const selectedContexts = selectUniqueSiteContexts(entry, derivedEvidence);
	const resolvedSites: ResolvedNativeHookAddressSite[] = entry.sites.map((site) => {
		const reference = selectedReferencesBySiteId.get(site.id);
		if (!reference) {
			throw new Error(`${entry.id}: proof.sites must include manifest site ${site.id}.`);
		}
		const rva = referenceToRva(entry, reference, imageBase);
		const context = selectedContexts.get(site.id);
		if (!context) throw new Error(`${entry.id}: failed to select context for ${site.id}.`);
		const changed = site.rva.toUpperCase() !== rva.toUpperCase() ||
			site.expectedTargetRva?.toUpperCase() !== context.expectedTargetRva.toUpperCase() ||
			site.contextSignatureVersion !== 1 || site.contextBytes !== context.contextBytes;
		return {
			siteId: site.id,
			rva,
			changed,
			...context,
		};
	});
	const resolvedBySiteId = new Map(resolvedSites.map((site) => [site.siteId, site]));
	entry.sites = entry.sites.map((site) => ({
		...site,
		rva: resolvedBySiteId.get(site.id)?.rva ?? site.rva,
		expectedTargetRva: resolvedBySiteId.get(site.id)?.expectedTargetRva,
		contextSignatureVersion: 1,
		contextBytes: resolvedBySiteId.get(site.id)?.contextBytes,
	}));

	return {
		id: entry.id,
		targetAbsoluteAddress: formatAbsoluteAddress(targetAddress),
		candidateRvas: resolvedSites.map((site) => site.rva),
		changed: resolvedSites.some((site) => site.changed),
		sites: resolvedSites,
	};
}

function resolveCallSiteEntry(
	root: string,
	entry: NativeHookAddressEntry,
	evidenceWindows: Map<number, EvidenceInstruction[]>,
): ResolvedNativeHookAddressEntry {
	const proof = entry.discoveryStrategy.proof;
	if (!proof) {
		throw new Error(`${entry.id}: proven entries require discoveryStrategy.proof.`);
	}
	if (proof.kind !== "ghidra_reference_report") {
		throw new Error(`${entry.id}: unsupported proof kind ${proof.kind}.`);
	}
	if (entry.category !== "call_site_rva") {
		throw new Error(`${entry.id}: proof-gated update currently supports call_site_rva entries only.`);
	}
	if (entry.expectedInstructionKind !== "call_rel32") {
		throw new Error(`${entry.id}: proof-gated update requires expectedInstructionKind=call_rel32.`);
	}

	if (proof.sites !== undefined) {
		return resolveExplicitCallSiteEntry(root, entry, proof, evidenceWindows);
	}

	if (entry.expectedCount !== 1) {
		throw new Error(`${entry.id}: proof.sites is required for multi-site proven call-site entries.`);
	}
	return resolveSingleCallSiteEntry(root, entry, proof, evidenceWindows);
}

function writeManifest(manifestPath: string, manifest: NativeHookAddressManifest): void {
	fs.writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
}

export function resolveNativeHookAddresses(options: ResolveNativeHookAddressOptions = {}): ResolveNativeHookAddressResult {
	const root = options.projectRoot ?? projectRoot;
	const manifestPath = options.manifestPath ?? defaultManifestPath;
	const manifest = readNativeHookManifest(manifestPath);
	const evidenceReportPath = options.evidenceReportPath ?? DEFAULT_EVIDENCE_REPORT;
	const evidenceWindows = parseEvidenceReport(root, evidenceReportPath);

	assertValidNativeHookManifest(manifest, {
		projectRoot: root,
		checkEvidencePaths: true,
		checkGeneratedHeader: manifest.schemaVersion === 2,
		checkSource: false,
	});

	const resolvedEntries: ResolvedNativeHookAddressEntry[] = [];
	const skippedEntries: string[] = [];

	for (const entry of manifest.entries) {
		if (entry.discoveryStrategy.status !== "proven") {
			skippedEntries.push(entry.id);
			continue;
		}
		resolvedEntries.push(resolveCallSiteEntry(root, entry, evidenceWindows));
	}

	if (resolvedEntries.length === 0) {
		throw new Error("No proven native hook address entries are available for automatic resolution.");
	}

	let generatedHeader: string | undefined;
	if (options.write) {
		manifest.schemaVersion = 2;
		assertValidNativeHookManifest(manifest, {
			projectRoot: root,
			checkEvidencePaths: true,
			checkGeneratedHeader: false,
			checkSource: false,
		});
		writeManifest(manifestPath, manifest);
		generatedHeader = writeGeneratedNativeHookHeader(manifest, root);
	} else {
		generateNativeHookHeader(manifest);
	}

	return {
		manifestPath,
		generatedHeader,
		resolvedEntries,
		skippedEntries,
		wroteManifest: options.write ?? false,
	};
}

export function parseResolveNativeHookAddressArgs(args: string[]): ResolveNativeHookAddressOptions {
	const options: ResolveNativeHookAddressOptions = {};
	for (const arg of args) {
		if (arg === "--") {
			continue;
		}
		if (arg === "--write") {
			options.write = true;
		} else if (arg.startsWith("--manifest=")) {
			options.manifestPath = path.resolve(arg.slice("--manifest=".length));
		} else if (arg.startsWith("--evidence-report=")) {
			options.evidenceReportPath = path.resolve(arg.slice("--evidence-report=".length));
		} else {
			throw new Error(`Unknown argument: ${arg}`);
		}
	}
	return options;
}
