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
}

interface CallReference {
	from: number;
	to: number;
	type: string;
}

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
	const pattern = new RegExp(`^\\s*${source}:\\s+CALL\\s+0x${target}\\b`, "im");
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
	const changed = entry.sites[0].rva.toUpperCase() !== candidateRvas[0].toUpperCase();
	entry.sites[0] = {
		...entry.sites[0],
		rva: candidateRvas[0],
	};

	return {
		id: entry.id,
		targetAbsoluteAddress: formatAbsoluteAddress(targetAddress),
		candidateRvas,
		changed,
	};
}

function resolveExplicitCallSiteEntry(
	root: string,
	entry: NativeHookAddressEntry,
	proof: NativeHookDiscoveryProof,
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

	const resolvedSites: ResolvedNativeHookAddressSite[] = entry.sites.map((site) => {
		const reference = selectedReferencesBySiteId.get(site.id);
		if (!reference) {
			throw new Error(`${entry.id}: proof.sites must include manifest site ${site.id}.`);
		}
		const rva = referenceToRva(entry, reference, imageBase);
		return {
			siteId: site.id,
			rva,
			changed: site.rva.toUpperCase() !== rva.toUpperCase(),
		};
	});
	const resolvedBySiteId = new Map(resolvedSites.map((site) => [site.siteId, site]));
	entry.sites = entry.sites.map((site) => ({
		...site,
		rva: resolvedBySiteId.get(site.id)?.rva ?? site.rva,
	}));

	return {
		id: entry.id,
		targetAbsoluteAddress: formatAbsoluteAddress(targetAddress),
		candidateRvas: resolvedSites.map((site) => site.rva),
		changed: resolvedSites.some((site) => site.changed),
		sites: resolvedSites,
	};
}

function resolveCallSiteEntry(root: string, entry: NativeHookAddressEntry): ResolvedNativeHookAddressEntry {
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
		return resolveExplicitCallSiteEntry(root, entry, proof);
	}

	if (entry.expectedCount !== 1) {
		throw new Error(`${entry.id}: proof.sites is required for multi-site proven call-site entries.`);
	}
	return resolveSingleCallSiteEntry(root, entry, proof);
}

function writeManifest(manifestPath: string, manifest: NativeHookAddressManifest): void {
	fs.writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
}

export function resolveNativeHookAddresses(options: ResolveNativeHookAddressOptions = {}): ResolveNativeHookAddressResult {
	const root = options.projectRoot ?? projectRoot;
	const manifestPath = options.manifestPath ?? defaultManifestPath;
	const manifest = readNativeHookManifest(manifestPath);

	assertValidNativeHookManifest(manifest, {
		projectRoot: root,
		checkEvidencePaths: true,
		checkGeneratedHeader: true,
		checkSource: false,
	});

	const resolvedEntries: ResolvedNativeHookAddressEntry[] = [];
	const skippedEntries: string[] = [];

	for (const entry of manifest.entries) {
		if (entry.discoveryStrategy.status !== "proven") {
			skippedEntries.push(entry.id);
			continue;
		}
		resolvedEntries.push(resolveCallSiteEntry(root, entry));
	}

	if (resolvedEntries.length === 0) {
		throw new Error("No proven native hook address entries are available for automatic resolution.");
	}

	let generatedHeader: string | undefined;
	if (options.write) {
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
		} else {
			throw new Error(`Unknown argument: ${arg}`);
		}
	}
	return options;
}
