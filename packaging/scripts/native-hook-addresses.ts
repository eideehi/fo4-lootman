import fs from "fs-extra";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
export const packagingRoot = path.resolve(__dirname, "..");
export const projectRoot = path.resolve(packagingRoot, "..");
export const defaultManifestPath = path.join(projectRoot, "tools", "native-hooks", "papyrus_lootman_hooks.addresses.json");

type HookAddressCategory = "call_site_rva" | "function_rva" | "global_rva" | "layout_offset" | "constant";
type DiscoveryStatus = "automated" | "manual" | "proven" | "unproven";

export interface NativeHookDiscoveryStrategy {
	status: DiscoveryStatus;
	summary: string;
}

export interface NativeHookCallSite {
	id: string;
	rva: string;
	sourceId: string;
	label: string;
}

export interface NativeHookAddressEntry {
	id: string;
	cppName: string;
	category: HookAddressCategory;
	expectedCount: number;
	expectedInstructionKind?: "call_rel32";
	expectedOriginalTargetGroup?: string;
	value?: string;
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
const RAW_LAYOUT_OFFSET_VALUES = new Set(["0xE0", "0xE8", "0xF8", "0x2F8"]);
const ADDRESS_CODED_HOOK_NAME_PATTERN = /\bHooked[A-Za-z0-9_]*[0-9A-F]{5,}[A-Za-z0-9_]*\b/g;
const ADDRESS_CODED_LOG_LABEL_PATTERN = /"[^"]*0x14[0-9A-F]{5,}[^"]*"/gi;

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isStringArray(value: unknown): value is string[] {
	return Array.isArray(value) && value.every((entry) => typeof entry === "string");
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

function validateDiscoveryStrategy(value: unknown, label: string, errors: string[]): void {
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

		if (!Number.isInteger(expectedCount) || expectedCount < 1) {
			errors.push(`${label}.expectedCount must be a positive integer.`);
		}

		const evidence = rawEntry.evidence;
		if (!isStringArray(evidence)) {
			errors.push(`${label}.evidence must be an array of strings.`);
		} else {
			validateEvidencePaths(evidence, label, errors, root, checkEvidencePaths);
		}

		validateDiscoveryStrategy(rawEntry.discoveryStrategy, label, errors);

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
			if (Number.isInteger(expectedCount) && rawEntry.sites.length !== expectedCount) {
				errors.push(`${label}.expectedCount=${expectedCount} but sites length is ${rawEntry.sites.length}.`);
			}
			if (rawEntry.value !== undefined) {
				errors.push(`${label}.value must not be set for call-site entries.`);
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
		} else {
			validateNumericLiteral(rawEntry.value, `${label}.value`, errors, category === "constant");
			if (rawEntry.sites !== undefined) {
				errors.push(`${label}.sites must not be set for ${category} entries.`);
			}
			if (Number.isInteger(expectedCount) && expectedCount !== 1) {
				errors.push(`${label}.expectedCount must be 1 for ${category} entries.`);
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
		const expected = generateNativeHookHeader(manifest as NativeHookAddressManifest);
		const headerPath = getGeneratedHeaderPath(manifest as NativeHookAddressManifest, root);
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
		"// Regenerate from packaging/: pnpm run generate:native-hooks",
		"// </auto-generated>",
		"",
		"#include <array>",
		"#include <cstdint>",
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
