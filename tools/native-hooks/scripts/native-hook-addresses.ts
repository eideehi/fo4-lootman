import fs from "fs-extra";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
export const nativeHooksRoot = path.resolve(__dirname, "..");
export const projectRoot = path.resolve(nativeHooksRoot, "..", "..");
export const defaultManifestPath = path.join(projectRoot, "tools", "native-hooks", "papyrus_lootman_hooks.addresses.json");

type HookAddressCategory = "call_site_rva" | "function_rva" | "global_rva" | "layout_offset" | "constant";
type DiscoveryStatus = "automated" | "manual" | "proven" | "unproven";

export interface NativeHookGhidraReferenceProofSite {
	siteId: string;
	absoluteAddress: string;
}

export interface NativeHookGhidraExcludedReference {
	absoluteAddress: string;
	reason: string;
}

export interface NativeHookGhidraReferenceProof {
	kind: "ghidra_reference_report";
	report: string;
	instructionReports?: string[];
	targetAbsoluteAddress: string;
	referenceType: "UNCONDITIONAL_CALL";
	sites?: NativeHookGhidraReferenceProofSite[];
	excludedReferences?: NativeHookGhidraExcludedReference[];
}

export type NativeHookDiscoveryProof = NativeHookGhidraReferenceProof;

export interface NativeHookDiscoveryStrategy {
	status: DiscoveryStatus;
	summary: string;
	proof?: NativeHookDiscoveryProof;
}

export interface NativeHookCallSite {
	id: string;
	rva: string;
	sourceId: string;
	label: string;
}

export interface NativeHookAddressLibraryMetadata {
	id: string;
	offset: string;
	namedId?: string;
}

export interface NativeHookAddressEntry {
	id: string;
	cppName: string;
	category: HookAddressCategory;
	expectedCount: number;
	expectedInstructionKind?: "call_rel32";
	expectedOriginalTargetGroup?: string;
	value?: string;
	addressLibrary?: NativeHookAddressLibraryMetadata;
	evidence: string[];
	discoveryStrategy: NativeHookDiscoveryStrategy;
	sites?: NativeHookCallSite[];
}

export interface NativeHookAddressManifest {
	schemaVersion: 1;
	targetRuntime: string;
	sourceFile: string;
	generatedHeader: string;
	entries: NativeHookAddressEntry[];
}

export interface ValidationOptions {
	projectRoot?: string;
	checkEvidencePaths?: boolean;
	checkGeneratedHeader?: boolean;
	checkSource?: boolean;
}

export interface ValidationResult {
	valid: boolean;
	errors: string[];
}

const VALID_CATEGORIES = new Set<HookAddressCategory>([
	"call_site_rva",
	"function_rva",
	"global_rva",
	"layout_offset",
	"constant",
]);
const VALID_DISCOVERY_STATUSES = new Set<DiscoveryStatus>(["automated", "manual", "proven", "unproven"]);
const HEX_VALUE_PATTERN = /^0x[0-9A-F]+$/i;
const DECIMAL_VALUE_PATTERN = /^[0-9]+$/;
const CPP_NAME_PATTERN = /^k[A-Za-z0-9]+$/;
const CPP_QUALIFIED_NAME_PATTERN = /^(?:RE|REL)::[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*$/;
const RAW_LAYOUT_OFFSET_VALUES = new Set(["0xE0", "0xE8", "0xF8", "0x2F8"]);
const ADDRESS_CODED_HOOK_NAME_PATTERN = /\bHooked[A-Za-z0-9_]*[0-9A-F]{5,}[A-Za-z0-9_]*\b/g;
const ADDRESS_CODED_LOG_LABEL_PATTERN = /"[^"]*0x14[0-9A-F]{5,}[^"]*"/gi;

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isStringArray(value: unknown): value is string[] {
	return Array.isArray(value) && value.every((entry) => typeof entry === "string");
}

function isRecordArray(value: unknown): value is Record<string, unknown>[] {
	return Array.isArray(value) && value.every(isRecord);
}

function parseNumericLiteral(value: string): number | null {
	if (HEX_VALUE_PATTERN.test(value)) {
		return Number.parseInt(value.slice(2), 16);
	}
	if (DECIMAL_VALUE_PATTERN.test(value)) {
		return Number.parseInt(value, 10);
	}
	return null;
}

function normalizeHex(value: string): string {
	const parsed = parseNumericLiteral(value);
	if (parsed === null) {
		return value;
	}
	return `0x${parsed.toString(16).toUpperCase()}`;
}

function formatHex(value: string): string {
	return normalizeHex(value);
}

function formatDecimal(value: string): string {
	const parsed = parseNumericLiteral(value);
	if (parsed === null) {
		return value;
	}
	return parsed.toString(10);
}

function formatSiteLabel(site: NativeHookCallSite): string {
	return `${site.label}@${formatHex(site.rva)}`;
}

function quoteCppString(value: string): string {
	return JSON.stringify(value);
}

function validateNumericLiteral(value: unknown, label: string, errors: string[], allowDecimal = false): void {
	if (typeof value !== "string") {
		errors.push(`${label} must be a string numeric literal.`);
		return;
	}

	const valid = allowDecimal ? HEX_VALUE_PATTERN.test(value) || DECIMAL_VALUE_PATTERN.test(value) : HEX_VALUE_PATTERN.test(value);
	if (!valid) {
		errors.push(`${label} must be ${allowDecimal ? "a decimal or " : ""}a hex literal.`);
		return;
	}

	const parsed = parseNumericLiteral(value);
	if (parsed === null || !Number.isSafeInteger(parsed) || parsed < 0) {
		errors.push(`${label} must be a non-negative safe integer literal.`);
	}
}

function validateRelativePath(
	value: unknown,
	label: string,
	errors: string[],
	root: string,
	checkExists: boolean,
): void {
	if (typeof value !== "string" || value.trim() === "") {
		errors.push(`${label} must be a non-empty string.`);
		return;
	}
	if (path.isAbsolute(value)) {
		errors.push(`${label} must use a workspace-relative path: ${value}`);
		return;
	}
	if (checkExists && !fs.existsSync(path.join(root, value))) {
		errors.push(`${label} does not exist: ${value}`);
	}
}

function validateDiscoveryProof(
	value: unknown,
	label: string,
	errors: string[],
	root: string,
	checkEvidencePaths: boolean,
): void {
	if (!isRecord(value)) {
		errors.push(`${label} must be an object.`);
		return;
	}

	if (value.kind !== "ghidra_reference_report") {
		errors.push(`${label}.kind must be ghidra_reference_report.`);
		return;
	}

	validateRelativePath(value.report, `${label}.report`, errors, root, checkEvidencePaths);
	if (value.instructionReports !== undefined) {
		if (!isStringArray(value.instructionReports)) {
			errors.push(`${label}.instructionReports must be an array of strings.`);
		} else {
			for (const [index, report] of value.instructionReports.entries()) {
				validateRelativePath(report, `${label}.instructionReports[${index}]`, errors, root, checkEvidencePaths);
			}
		}
	}
	validateNumericLiteral(value.targetAbsoluteAddress, `${label}.targetAbsoluteAddress`, errors);
	if (value.referenceType !== "UNCONDITIONAL_CALL") {
		errors.push(`${label}.referenceType must be UNCONDITIONAL_CALL.`);
	}

	if (value.sites !== undefined) {
		if (!isRecordArray(value.sites)) {
			errors.push(`${label}.sites must be an array of objects.`);
		} else if (value.sites.length === 0) {
			errors.push(`${label}.sites must contain at least one proof site.`);
		} else {
			for (const [index, site] of value.sites.entries()) {
				const siteLabel = `${label}.sites[${index}]`;
				if (typeof site.siteId !== "string" || site.siteId.trim() === "") {
					errors.push(`${siteLabel}.siteId must be a non-empty string.`);
				}
				validateNumericLiteral(site.absoluteAddress, `${siteLabel}.absoluteAddress`, errors);
			}
		}
	}

	if (value.excludedReferences !== undefined) {
		if (!isRecordArray(value.excludedReferences)) {
			errors.push(`${label}.excludedReferences must be an array of objects.`);
		} else {
			for (const [index, reference] of value.excludedReferences.entries()) {
				const referenceLabel = `${label}.excludedReferences[${index}]`;
				validateNumericLiteral(reference.absoluteAddress, `${referenceLabel}.absoluteAddress`, errors);
				if (typeof reference.reason !== "string" || reference.reason.trim() === "") {
					errors.push(`${referenceLabel}.reason must be a non-empty string.`);
				}
			}
		}
	}
}

function validateDiscoveryStrategy(
	value: unknown,
	label: string,
	errors: string[],
	root: string,
	checkEvidencePaths: boolean,
): void {
	if (!isRecord(value)) {
		errors.push(`${label}.discoveryStrategy must be an object.`);
		return;
	}

	if (typeof value.status !== "string" || !VALID_DISCOVERY_STATUSES.has(value.status as DiscoveryStatus)) {
		errors.push(`${label}.discoveryStrategy.status must be one of: ${[...VALID_DISCOVERY_STATUSES].join(", ")}.`);
	}
	if (typeof value.summary !== "string" || value.summary.trim() === "") {
		errors.push(`${label}.discoveryStrategy.summary must be a non-empty string.`);
	}
	if (value.status === "proven" && value.proof === undefined) {
		errors.push(`${label}.discoveryStrategy.proof is required when status is proven.`);
	}
	if (value.proof !== undefined) {
		validateDiscoveryProof(value.proof, `${label}.discoveryStrategy.proof`, errors, root, checkEvidencePaths);
	}
}

function validateAddressLibraryMetadata(
	value: unknown,
	label: string,
	errors: string[],
	entryValue: unknown,
): void {
	if (!isRecord(value)) {
		errors.push(`${label}.addressLibrary must be an object.`);
		return;
	}

	validateNumericLiteral(value.id, `${label}.addressLibrary.id`, errors, true);
	validateNumericLiteral(value.offset, `${label}.addressLibrary.offset`, errors);
	if (
		typeof entryValue === "string" &&
		typeof value.offset === "string" &&
		HEX_VALUE_PATTERN.test(entryValue) &&
		HEX_VALUE_PATTERN.test(value.offset) &&
		normalizeHex(value.offset) !== normalizeHex(entryValue)
	) {
		errors.push(`${label}.addressLibrary.offset must match ${label}.value for exact Address Library mappings.`);
	}

	if (value.namedId !== undefined) {
		if (typeof value.namedId !== "string" || value.namedId.trim() === "") {
			errors.push(`${label}.addressLibrary.namedId must be a non-empty string.`);
		} else if (!CPP_QUALIFIED_NAME_PATTERN.test(value.namedId)) {
			errors.push(`${label}.addressLibrary.namedId must be a C++ qualified RE:: or REL:: name.`);
		}
	}
}

function validateCallSiteProofMapping(
	rawEntry: Record<string, unknown>,
	label: string,
	errors: string[],
	expectedCountValue: number | null,
): void {
	if (!isRecord(rawEntry.discoveryStrategy)) {
		return;
	}
	const proof = rawEntry.discoveryStrategy.proof;
	if (!isRecord(proof)) {
		return;
	}

	const manifestSites = isRecordArray(rawEntry.sites) ? rawEntry.sites : null;
	const proofSites = isRecordArray(proof.sites) ? proof.sites : null;
	if (
		rawEntry.discoveryStrategy.status === "proven" &&
		expectedCountValue !== null &&
		expectedCountValue > 1 &&
		proof.sites === undefined
	) {
		errors.push(`${label}.discoveryStrategy.proof.sites is required for multi-site proven call-site entries.`);
	}

	if (proofSites) {
		if (expectedCountValue !== null && proofSites.length !== expectedCountValue) {
			errors.push(`${label}.discoveryStrategy.proof.sites length must match ${label}.expectedCount.`);
		}
		if (manifestSites && proofSites.length !== manifestSites.length) {
			errors.push(`${label}.discoveryStrategy.proof.sites length must match manifest sites length.`);
		}

		const seenSiteIds = new Set<string>();
		for (const proofSite of proofSites) {
			if (typeof proofSite.siteId !== "string" || proofSite.siteId.trim() === "") {
				continue;
			}
			if (seenSiteIds.has(proofSite.siteId)) {
				errors.push(`${label}.discoveryStrategy.proof.sites contains duplicate siteId: ${proofSite.siteId}`);
			}
			seenSiteIds.add(proofSite.siteId);
		}

		if (manifestSites) {
			const manifestSiteIds = new Set(
				manifestSites
					.map((site) => site.id)
					.filter((id): id is string => typeof id === "string" && id.trim() !== ""),
			);
			if (manifestSiteIds.size === manifestSites.length) {
				const proofSiteIds = new Set(
					proofSites
						.map((site) => site.siteId)
						.filter((siteId): siteId is string => typeof siteId === "string" && siteId.trim() !== ""),
				);
				const idsMatch = proofSiteIds.size === manifestSiteIds.size &&
					[...proofSiteIds].every((siteId) => manifestSiteIds.has(siteId));
				if (!idsMatch) {
					errors.push(`${label}.discoveryStrategy.proof.sites must match manifest site ids.`);
				}
			}
		}
	}

	const selectedAddresses = new Set(
		(proofSites ?? [])
			.map((site) => site.absoluteAddress)
			.filter((absoluteAddress): absoluteAddress is string => typeof absoluteAddress === "string" && HEX_VALUE_PATTERN.test(absoluteAddress))
			.map(normalizeHex),
	);
	if (isRecordArray(proof.excludedReferences)) {
		const seenExcludedAddresses = new Set<string>();
		for (const reference of proof.excludedReferences) {
			if (typeof reference.absoluteAddress !== "string" || !HEX_VALUE_PATTERN.test(reference.absoluteAddress)) {
				continue;
			}
			const normalized = normalizeHex(reference.absoluteAddress);
			if (seenExcludedAddresses.has(normalized)) {
				errors.push(`${label}.discoveryStrategy.proof.excludedReferences contains duplicate absoluteAddress: ${normalized}`);
			}
			seenExcludedAddresses.add(normalized);
			if (selectedAddresses.has(normalized)) {
				errors.push(`${label}.discoveryStrategy.proof.excludedReferences must not overlap proof.sites.`);
			}
		}
	}
}

function validateEvidencePaths(
	evidence: string[],
	label: string,
	errors: string[],
	root: string,
	checkEvidencePaths: boolean,
): void {
	if (evidence.length === 0) {
		errors.push(`${label}.evidence must include at least one report path.`);
		return;
	}

	for (const evidencePath of evidence) {
		if (path.isAbsolute(evidencePath)) {
			errors.push(`${label}.evidence must use workspace-relative paths: ${evidencePath}`);
			continue;
		}
		if (checkEvidencePaths && !fs.existsSync(path.join(root, evidencePath))) {
			errors.push(`${label}.evidence path does not exist: ${evidencePath}`);
		}
	}
}

function validateHookSource(sourceText: string, errors: string[]): void {
	for (const match of sourceText.matchAll(ADDRESS_CODED_HOOK_NAME_PATTERN)) {
		errors.push(`Address-coded hook wrapper name is not allowed in source: ${match[0]}`);
	}
	for (const match of sourceText.matchAll(ADDRESS_CODED_LOG_LABEL_PATTERN)) {
		errors.push(`Address-coded hook log label is not allowed outside generated metadata: ${match[0]}`);
	}
}

export function readNativeHookManifest(manifestPath = defaultManifestPath): NativeHookAddressManifest {
	return fs.readJsonSync(manifestPath) as NativeHookAddressManifest;
}

export function getGeneratedHeaderPath(manifest: NativeHookAddressManifest, root = projectRoot): string {
	return path.join(root, manifest.generatedHeader);
}

export function validateNativeHookManifest(
	manifest: unknown,
	options: ValidationOptions = {},
): ValidationResult {
	const root = options.projectRoot ?? projectRoot;
	const checkEvidencePaths = options.checkEvidencePaths ?? true;
	const errors: string[] = [];

	if (!isRecord(manifest)) {
		return { valid: false, errors: ["Manifest root must be an object."] };
	}

	if (manifest.schemaVersion !== 1) {
		errors.push("schemaVersion must be 1.");
	}
	if (typeof manifest.targetRuntime !== "string" || manifest.targetRuntime.trim() === "") {
		errors.push("targetRuntime must be a non-empty string.");
	}
	if (typeof manifest.sourceFile !== "string" || manifest.sourceFile.trim() === "") {
		errors.push("sourceFile must be a non-empty string.");
	}
	if (typeof manifest.generatedHeader !== "string" || manifest.generatedHeader.trim() === "") {
		errors.push("generatedHeader must be a non-empty string.");
	}
	if (!Array.isArray(manifest.entries)) {
		errors.push("entries must be an array.");
		return { valid: false, errors };
	}

	const entryIds = new Set<string>();
	const cppNames = new Set<string>();
	const siteIds = new Set<string>();

	for (const [index, rawEntry] of manifest.entries.entries()) {
		const label = `entries[${index}]`;
		if (!isRecord(rawEntry)) {
			errors.push(`${label} must be an object.`);
			continue;
		}

		const id = rawEntry.id;
		const cppName = rawEntry.cppName;
		const category = rawEntry.category;
		const expectedCount = rawEntry.expectedCount;
		const expectedCountValue = typeof expectedCount === "number" && Number.isInteger(expectedCount) ? expectedCount : null;

		if (typeof id !== "string" || id.trim() === "") {
			errors.push(`${label}.id must be a non-empty string.`);
		} else if (entryIds.has(id)) {
			errors.push(`${label}.id is duplicated: ${id}`);
		} else {
			entryIds.add(id);
		}

		if (typeof cppName !== "string" || !CPP_NAME_PATTERN.test(cppName)) {
			errors.push(`${label}.cppName must be a generated C++ constant name starting with k.`);
		} else if (cppNames.has(cppName)) {
			errors.push(`${label}.cppName is duplicated: ${cppName}`);
		} else {
			cppNames.add(cppName);
		}

		if (typeof category !== "string" || !VALID_CATEGORIES.has(category as HookAddressCategory)) {
			errors.push(`${label}.category is invalid: ${String(category)}`);
			continue;
		}

		if (expectedCountValue === null || expectedCountValue < 1) {
			errors.push(`${label}.expectedCount must be a positive integer.`);
		}

		const evidence = rawEntry.evidence;
		if (!isStringArray(evidence)) {
			errors.push(`${label}.evidence must be an array of strings.`);
		} else {
			validateEvidencePaths(evidence, label, errors, root, checkEvidencePaths);
		}

		validateDiscoveryStrategy(rawEntry.discoveryStrategy, label, errors, root, checkEvidencePaths);

		if (category === "call_site_rva") {
			if (rawEntry.expectedInstructionKind !== "call_rel32") {
				errors.push(`${label}.expectedInstructionKind must be call_rel32 for direct call-site entries.`);
			}
			if (typeof rawEntry.expectedOriginalTargetGroup !== "string" || rawEntry.expectedOriginalTargetGroup.trim() === "") {
				errors.push(`${label}.expectedOriginalTargetGroup is required for direct call-site entries.`);
			}
			if (!Array.isArray(rawEntry.sites) || rawEntry.sites.length === 0) {
				errors.push(`${label}.sites must contain at least one call site.`);
				continue;
			}
			if (expectedCountValue !== null && rawEntry.sites.length !== expectedCountValue) {
				errors.push(`${label}.expectedCount=${expectedCountValue} but sites length is ${rawEntry.sites.length}.`);
			}
			if (rawEntry.value !== undefined) {
				errors.push(`${label}.value must not be set for call-site entries.`);
			}
			if (rawEntry.addressLibrary !== undefined) {
				errors.push(`${label}.addressLibrary must not be set for call-site entries.`);
			}

			for (const [siteIndex, rawSite] of rawEntry.sites.entries()) {
				const siteLabel = `${label}.sites[${siteIndex}]`;
				if (!isRecord(rawSite)) {
					errors.push(`${siteLabel} must be an object.`);
					continue;
				}
				if (typeof rawSite.id !== "string" || rawSite.id.trim() === "") {
					errors.push(`${siteLabel}.id must be a non-empty string.`);
				} else if (siteIds.has(rawSite.id)) {
					errors.push(`${siteLabel}.id is duplicated: ${rawSite.id}`);
				} else {
					siteIds.add(rawSite.id);
				}
				validateNumericLiteral(rawSite.rva, `${siteLabel}.rva`, errors);
				validateNumericLiteral(rawSite.sourceId, `${siteLabel}.sourceId`, errors, true);
				if (typeof rawSite.label !== "string" || rawSite.label.trim() === "") {
					errors.push(`${siteLabel}.label must be a non-empty string.`);
				} else if (/0x[0-9A-F]+/i.test(rawSite.label)) {
					errors.push(`${siteLabel}.label must be semantic and must not contain an address literal.`);
				}
			}
			validateCallSiteProofMapping(rawEntry, label, errors, expectedCountValue);
		} else {
			validateNumericLiteral(rawEntry.value, `${label}.value`, errors, category === "constant");
			if (rawEntry.sites !== undefined) {
				errors.push(`${label}.sites must not be set for ${category} entries.`);
			}
			if (expectedCountValue !== null && expectedCountValue !== 1) {
				errors.push(`${label}.expectedCount must be 1 for ${category} entries.`);
			}
			if (rawEntry.addressLibrary !== undefined) {
				if (category === "function_rva" || category === "global_rva") {
					validateAddressLibraryMetadata(rawEntry.addressLibrary, label, errors, rawEntry.value);
				} else {
					errors.push(`${label}.addressLibrary is only allowed for function_rva and global_rva entries.`);
				}
			}
		}

		const value = typeof rawEntry.value === "string" ? normalizeHex(rawEntry.value) : null;
		if (value && RAW_LAYOUT_OFFSET_VALUES.has(value) && category !== "layout_offset") {
			errors.push(`${label}.value ${value} is a raw layout offset and must use category layout_offset.`);
		}
		if (typeof id === "string" && id.startsWith("workshop_supply_owner.") && category !== "layout_offset") {
			errors.push(`${label}.category must be layout_offset for raw workshop supply owner fields.`);
		}
	}

	if (options.checkSource && typeof manifest.sourceFile === "string") {
		const sourcePath = path.join(root, manifest.sourceFile);
		if (!fs.existsSync(sourcePath)) {
			errors.push(`sourceFile does not exist: ${manifest.sourceFile}`);
		} else {
			validateHookSource(fs.readFileSync(sourcePath, "utf8"), errors);
		}
	}

	if (options.checkGeneratedHeader && manifest.schemaVersion === 1 && typeof manifest.generatedHeader === "string") {
		const typedManifest = manifest as unknown as NativeHookAddressManifest;
		const expected = generateNativeHookHeader(typedManifest);
		const headerPath = getGeneratedHeaderPath(typedManifest, root);
		if (!fs.existsSync(headerPath)) {
			errors.push(`Generated header is missing: ${manifest.generatedHeader}`);
		} else {
			const actual = fs.readFileSync(headerPath, "utf8");
			if (actual !== expected) {
				errors.push(`Generated header is stale: ${manifest.generatedHeader}`);
			}
		}
	}

	return { valid: errors.length === 0, errors };
}

export function assertValidNativeHookManifest(
	manifest: NativeHookAddressManifest,
	options: ValidationOptions = {},
): void {
	const result = validateNativeHookManifest(manifest, options);
	if (!result.valid) {
		throw new Error(result.errors.join("\n"));
	}
}

export function generateNativeHookHeader(manifest: NativeHookAddressManifest): string {
	assertValidNativeHookManifest(manifest, {
		checkEvidencePaths: false,
		checkGeneratedHeader: false,
		checkSource: false,
	});

	const lines: string[] = [
		"#pragma once",
		"",
		"// <auto-generated>",
		`// Source: tools/native-hooks/papyrus_lootman_hooks.addresses.json`,
		`// Target runtime: ${manifest.targetRuntime}`,
		"// Regenerate from repository root: pnpm run native-hooks:generate",
		"// </auto-generated>",
		"",
		"#include <array>",
		"#include <cstdint>",
		"#include <REL/ID.h>",
		"",
		"namespace papyrus_lootman",
		"{",
		"\tstruct NativeHookCallSite",
		"\t{",
		"\t\tconst char* id;",
		"\t\tstd::uintptr_t rva;",
		"\t\tstd::uint32_t sourceId;",
		"\t\tconst char* label;",
		"\t};",
		"",
	];

	for (const entry of manifest.entries) {
		if (entry.category === "call_site_rva") {
			const sites = entry.sites ?? [];
			if (sites.length === 1) {
				const site = sites[0];
				lines.push(
					`\tinline constexpr NativeHookCallSite ${entry.cppName}{`,
					`\t\t${quoteCppString(site.id)},`,
					`\t\t${formatHex(site.rva)},`,
					`\t\t${formatHex(site.sourceId)},`,
					`\t\t${quoteCppString(formatSiteLabel(site))},`,
					"\t};",
					"",
				);
			} else {
				lines.push(`\tinline constexpr std::array<NativeHookCallSite, ${sites.length}> ${entry.cppName}{{`);
				for (const site of sites) {
					lines.push(
						"\t\t{",
						`\t\t\t${quoteCppString(site.id)},`,
						`\t\t\t${formatHex(site.rva)},`,
						`\t\t\t${formatHex(site.sourceId)},`,
						`\t\t\t${quoteCppString(formatSiteLabel(site))},`,
						"\t\t},",
					);
				}
				lines.push("\t}};", "");
			}
			continue;
		}

		const type = entry.category === "constant" ? "std::uint32_t" : "std::uintptr_t";
		const value = entry.value ?? "0";
		if (entry.addressLibrary) {
			if (entry.addressLibrary.namedId) {
				lines.push(`\t// Address Library: ${entry.addressLibrary.namedId} @ ${formatHex(entry.addressLibrary.offset)}`);
			} else {
				lines.push(`\t// Address Library offset: ${formatHex(entry.addressLibrary.offset)}`);
			}
			lines.push(`\tinline constexpr REL::ID ${entry.cppName}{ ${formatDecimal(entry.addressLibrary.id)} };`, "");
			continue;
		}
		lines.push(`\tinline constexpr ${type} ${entry.cppName} = ${formatHex(value)};`, "");
	}

	lines.push("}", "");
	return lines.join("\n");
}

export function writeGeneratedNativeHookHeader(
	manifest: NativeHookAddressManifest,
	root = projectRoot,
): string {
	const headerPath = getGeneratedHeaderPath(manifest, root);
	fs.outputFileSync(headerPath, generateNativeHookHeader(manifest), "utf8");
	return headerPath;
}
