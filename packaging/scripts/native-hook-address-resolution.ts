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
}

export interface ResolveNativeHookAddressResult {
	manifestPath: string;
	generatedHeader?: string;
	resolvedEntries: ResolvedNativeHookAddressEntry[];
	skippedEntries: string[];
	wroteManifest: boolean;
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
	if (entry.expectedCount !== 1) {
		throw new Error(`${entry.id}: proof-gated update currently supports single-site hook families only.`);
	}
	if (!entry.sites || entry.sites.length !== 1) {
		throw new Error(`${entry.id}: expected exactly one manifest site.`);
	}

	const targetAddress = parseHex(proof.targetAbsoluteAddress, `${entry.id} proof target`);
	const proofReportPath = path.join(root, proof.report);
	const proofReportText = fs.readFileSync(proofReportPath, "utf8");
	const imageBase = parseImageBase(proofReportText, proof.report);
	const section = findTargetSection(proofReportText, proof.targetAbsoluteAddress, proof.report);
	const references = parseEntryReferences(section, targetAddress)
		.filter((reference) => reference.type === proof.referenceType);

	if (references.length !== entry.expectedCount) {
		throw new Error(
			`${entry.id}: expected ${entry.expectedCount} ${proof.referenceType} candidate, found ${references.length}.`,
		);
	}

	const instructionReports = getProofReports(root, proof);
	for (const reference of references) {
		const hasInstruction = instructionReports.some((report) => hasDirectCallInstruction(report.text, reference.from, reference.to));
		if (!hasInstruction) {
			throw new Error(
				`${entry.id}: ${formatAbsoluteAddress(reference.from)} is referenced but no CALL ${formatAbsoluteAddress(reference.to)} instruction line was found.`,
			);
		}
	}

	const candidateRvas = references
		.map((reference) => {
			const rva = reference.from - imageBase;
			if (rva < 0) {
				throw new Error(`${entry.id}: ${formatAbsoluteAddress(reference.from)} is below image base ${formatAbsoluteAddress(imageBase)}.`);
			}
			return rva;
		})
		.sort((a, b) => a - b)
		.map(formatRva);
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
