import fs from "node:fs";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

// The native label table (`kLabelRows` in terminal_labels.cpp) decides which
// holotape item shows which setting value. Nothing at build time links it to the
// Papyrus terminal fragments that write those settings, so a renumbered item or a
// swapped mask bit would silently label the wrong row. These checks rebuild the
// expected table from the fragment scripts and compare it against the parsed
// source, so the two sides cannot drift apart unnoticed.

const TERMINAL_LABELS_SOURCE = "commonlibf4-plugin/src/terminal_labels.cpp";
const PROPERTIES_HEADER = "commonlibf4-plugin/src/properties.h";
const FRAGMENT_DIR = "papyrus/Scripts/Source/User/LTMN2/Fragments/Terminals";

// The fragment scripts of the three value-bearing pages. The trailing form-id
// suffix of the file name is the holotape page the fragments belong to.
const FRAGMENT_FILES = [
	"TERM_ConfigGeneral_FB8.psc",
	"TERM_ConfigObjectFilter_FB9.psc",
	"TERM_ConfigLogLevel_FBA.psc",
];

// Pages that hold no settings and must therefore never appear in the table.
const ROOT_PAGE_FORM_ID = "000FB7";
const UTILITY_PAGE_FORM_ID = "000FBB";

interface LabelRow {
	// LabelPage enumerator name, e.g. `kPrimary`.
	page: string;
	itemId: number;
	// LabelKind enumerator name, e.g. `kBoolProperty`.
	kind: string;
	// Rendered exactly as written in the table: a qualified constant or an integer.
	payload: string;
}

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

// Throws rather than asserting: the fragment scripts are read while the suite is
// collected, where a failed assertion would report as an unrelated load error.
function extractPapyrusFunction(source: string, name: string): string {
	const match = new RegExp(`Function\\s+${name}\\([^\\n]*\\)([\\s\\S]*?)EndFunction`, "i").exec(source);
	if (!match) {
		throw new Error(`missing Papyrus function ${name}`);
	}
	return match[1]!;
}

function stripLineComments(source: string): string {
	return source.replace(/\/\/[^\n]*/g, "");
}

function rowKey(row: LabelRow): string {
	return `${row.page}#${row.itemId}`;
}

function describeRow(row: LabelRow): string {
	return `${row.page}#${row.itemId} ${row.kind} ${row.payload}`;
}

// --- source parsing (pure over the C++ text) ---------------------------------

// The raw text between the braces of the `kLabelRows` initializer. Scanned by
// brace balance rather than by a terminator pattern, so a row that gains or loses
// a brace is reported instead of silently truncating the table.
function extractLabelRowsTable(source: string): string {
	const anchor = /constexpr\s+LabelRow\s+kLabelRows\[\]\s*=\s*\{/.exec(source);
	if (!anchor) {
		throw new Error("kLabelRows table not found");
	}
	const start = anchor.index + anchor[0].length;
	let depth = 0;
	for (let index = start; index < source.length; index += 1) {
		const char = source[index];
		if (char === "{") {
			depth += 1;
		} else if (char === "}") {
			if (depth === 0) {
				return source.slice(start, index);
			}
			depth -= 1;
		}
	}
	throw new Error("kLabelRows table is not brace balanced");
}

function parseLabelRows(source: string): LabelRow[] {
	const table = extractLabelRowsTable(source);
	const pattern = /\{\s*LabelPage::(\w+)\s*,\s*(\d+)\s*,\s*LabelKind::(\w+)\s*,\s*([A-Za-z_][\w:]*|\d+)\s*\}/g;
	const rows = [...table.matchAll(pattern)].map<LabelRow>((match) => ({
		page: match[1]!,
		itemId: Number(match[2]),
		kind: match[3]!,
		payload: match[4]!,
	}));
	// Every row mentions its page exactly once, so a row shape the pattern cannot
	// read is reported instead of being dropped from the comparison.
	const mentioned = (table.match(/LabelPage::/g) ?? []).length;
	if (mentioned !== rows.length) {
		throw new Error(`kLabelRows has ${mentioned} row entries but ${rows.length} are parsable`);
	}
	return rows;
}

// LabelPage enumerator -> holotape form id, resolved through the page form
// constant that carries the enumerator's name (`kPrimary` -> `kPrimaryPageForm`).
function parsePageFormIds(source: string): Map<string, string> {
	const enumBlock = /enum class LabelPage\s*\{([^}]*)\}/.exec(source);
	if (!enumBlock) {
		throw new Error("LabelPage enum not found");
	}
	const enumerators = [...stripLineComments(enumBlock[1]!).matchAll(/\bk\w+/g)].map((match) => match[0]);
	const formIds = new Map<string, string>();
	for (const enumerator of enumerators) {
		const constant = new RegExp(
			`constexpr\\s+const\\s+char\\*\\s+${enumerator}PageForm\\s*=\\s*"[^"|]*\\|(\\w+)"`,
		).exec(source);
		if (constant) {
			formIds.set(enumerator, constant[1]!);
		}
	}
	return formIds;
}

function parseNamedFormId(source: string, constant: string): string {
	const match = new RegExp(`constexpr\\s+const\\s+char\\*\\s+${constant}\\s*=\\s*"(?:[^"|]*\\|)?(\\w+)"`).exec(source);
	if (!match) {
		throw new Error(`missing form-id constant ${constant}`);
	}
	return match[1]!;
}

function parseEnumerators(source: string, enumName: string): string[] {
	const block = new RegExp(`enum class ${enumName}\\s*\\{([^}]*)\\}`).exec(source);
	if (!block) {
		throw new Error(`${enumName} enum not found`);
	}
	return [...stripLineComments(block[1]!).matchAll(/\bk\w+/g)].map((match) => match[0]);
}

// The whole policy check, expressed as violation lines over a source string so
// the perturbation cases below can run it against a scratch copy.
function findLabelTableViolations(source: string, expected: LabelRow[]): string[] {
	const violations: string[] = [];
	let rows: LabelRow[];
	try {
		rows = parseLabelRows(source);
	} catch (error) {
		return [`unreadable label table: ${(error as Error).message}`];
	}

	const pageFormIds = parsePageFormIds(source);
	const excludedFormIds = new Set([
		parseNamedFormId(source, "kRootPageLabel"),
		parseNamedFormId(source, "kUtilityPageLabel"),
	]);
	const kinds = new Set(parseEnumerators(source, "LabelKind"));

	const seen = new Set<string>();
	for (const row of rows) {
		const formId = pageFormIds.get(row.page);
		if (formId === undefined) {
			violations.push(`row ${describeRow(row)} targets a page with no resolvable form`);
		} else if (excludedFormIds.has(formId)) {
			violations.push(`row ${describeRow(row)} targets settings-free page ${formId}`);
		}
		if (!kinds.has(row.kind)) {
			violations.push(`row ${describeRow(row)} uses an undeclared LabelKind`);
		}
		if (seen.has(rowKey(row))) {
			violations.push(`duplicate table entry for ${rowKey(row)}`);
		}
		seen.add(rowKey(row));
	}

	const parsedByKey = new Map(rows.map((row) => [rowKey(row), row]));
	for (const row of expected) {
		const parsed = parsedByKey.get(rowKey(row));
		if (!parsed) {
			violations.push(`missing table entry for ${describeRow(row)}`);
		} else if (parsed.kind !== row.kind || parsed.payload !== row.payload) {
			violations.push(`table entry ${rowKey(row)} renders ${parsed.kind} ${parsed.payload}, expected ${row.kind} ${row.payload}`);
		}
	}

	const expectedKeys = new Set(expected.map(rowKey));
	for (const row of rows) {
		if (!expectedKeys.has(rowKey(row))) {
			violations.push(`unexpected table entry ${describeRow(row)}`);
		}
	}

	return violations;
}

// --- expectation derived from the Papyrus fragments --------------------------

// Papyrus setting id -> properties::Key enumerator. `LootMan` is one word in the
// native enum, so it is folded before the PascalCase boundaries are split.
function toPropertyKey(id: string): string {
	return id
		.replace(/LootMan/g, "Lootman")
		.replace(/([a-z0-9])([A-Z])/g, "$1_$2")
		.replace(/([A-Z]+)([A-Z][a-z])/g, "$1_$2")
		.toLowerCase();
}

function deriveRowFromFragmentBody(body: string, page: string, itemId: number): LabelRow {
	const toggle = /\.Toggle\("([^"]+)"\)/.exec(body);
	if (toggle) {
		const id = toggle[1]!;
		const formType = /^EnableObjectLootingOf(\w+)$/.exec(id);
		return formType
			? { page, itemId, kind: "kMaskBit", payload: `properties::kEnableFormType${formType[1]!}` }
			: { page, itemId, kind: "kBoolProperty", payload: `properties::${toPropertyKey(id)}` };
	}
	const adjustFloat = /\.AdjustFloat\("([^"]+)"/.exec(body);
	if (adjustFloat) {
		return { page, itemId, kind: "kFloatProperty", payload: `properties::${toPropertyKey(adjustFloat[1]!)}` };
	}
	const adjustInt = /\.AdjustInt\("([^"]+)"/.exec(body);
	if (adjustInt) {
		return { page, itemId, kind: "kIntProperty", payload: `properties::${toPropertyKey(adjustInt[1]!)}` };
	}
	const logLevel = /\.SetLogLevel\((\d+)\)/.exec(body);
	if (logLevel) {
		return { page, itemId, kind: "kLogLevel", payload: logLevel[1]! };
	}
	// `ToggleEnableLootMan()` goes through the MCM for its localized HUD message,
	// but still labels the plain bool the toggle flips.
	const mcmToggle = /\.Toggle(\w+)\(\)/.exec(body);
	if (mcmToggle) {
		return { page, itemId, kind: "kBoolProperty", payload: `properties::${toPropertyKey(mcmToggle[1]!)}` };
	}
	throw new Error(`fragment ${page}#${itemId} makes no recognizable config call`);
}

function deriveRowsFromFragment(fragmentSource: string, page: string): LabelRow[] {
	const indices = [...fragmentSource.matchAll(/Function\s+Fragment_Terminal_(\d{2})\(/gi)].map((match) =>
		Number(match[1]),
	);
	return indices.map((itemId) => {
		const body = extractPapyrusFunction(fragmentSource, `Fragment_Terminal_${String(itemId).padStart(2, "0")}`);
		return deriveRowFromFragmentBody(body, page, itemId);
	});
}

describe("config holotape label table", () => {
	const labelSource = readWorkspaceFile(TERMINAL_LABELS_SOURCE);
	const propertiesHeader = readWorkspaceFile(PROPERTIES_HEADER);
	const pageFormIds = parsePageFormIds(labelSource);
	// Form id -> LabelPage enumerator, so the fragment files address pages by the
	// holotape form they belong to instead of by a hardcoded enumerator name.
	const pageByFormId = new Map([...pageFormIds].map(([page, formId]) => [formId, page]));

	const derivedRows = FRAGMENT_FILES.flatMap((file) => {
		const formId = file.replace(/\.psc$/, "").split("_").pop()!.padStart(6, "0");
		const page = pageByFormId.get(formId);
		if (!page) {
			throw new Error(`no LabelPage enumerator resolves holotape page ${formId}`);
		}
		return deriveRowsFromFragment(readWorkspaceFile(`${FRAGMENT_DIR}/${file}`), page);
	});
	const parsedRows = parseLabelRows(labelSource);

	it("labels every terminal item the config fragments write, and nothing else", () => {
		expect(derivedRows).toHaveLength(33);
		expect(findLabelTableViolations(labelSource, derivedRows)).toEqual([]);
		// Same membership stated once more as a whole-set comparison, so a defect the
		// per-row rules do not name still shows up as a diff.
		const serialize = (rows: LabelRow[]): string[] => rows.map(describeRow).sort();
		expect(serialize(parsedRows)).toEqual(serialize(derivedRows));
	});

	it("carries no duplicate page and item-id pair", () => {
		const keys = parsedRows.map(rowKey);
		expect(keys).toHaveLength(new Set(keys).size);
	});

	it("holds the frozen per-page row counts", () => {
		const counts = new Map<string, number>();
		for (const row of parsedRows) {
			counts.set(row.page, (counts.get(row.page) ?? 0) + 1);
		}
		expect(counts.get(pageByFormId.get("000FB8")!)).toBe(14);
		expect(counts.get(pageByFormId.get("000FB9")!)).toBe(12);
		expect(counts.get(pageByFormId.get("000FBA")!)).toBe(7);
		expect(parsedRows).toHaveLength(33);
	});

	it("never labels the settings-free root or utility page", () => {
		// The root page and the utility page carry no settings, so a row aimed at
		// either would rewrite an item that has no value to show.
		expect(parseNamedFormId(labelSource, "kRootPageLabel")).toBe(ROOT_PAGE_FORM_ID);
		expect(parseNamedFormId(labelSource, "kUtilityPageLabel")).toBe(UTILITY_PAGE_FORM_ID);
		for (const row of parsedRows) {
			const formId = pageFormIds.get(row.page);
			expect(formId, `row ${describeRow(row)} targets an unresolvable page`).toBeDefined();
			expect([ROOT_PAGE_FORM_ID, UTILITY_PAGE_FORM_ID]).not.toContain(formId);
		}
		// Neither form id may reach the table by a literal either.
		const table = extractLabelRowsTable(labelSource);
		expect(table).not.toContain(ROOT_PAGE_FORM_ID);
		expect(table).not.toContain(UTILITY_PAGE_FORM_ID);
	});

	it("resolves every bool, float and int row against the native property keys", () => {
		const keys = new Set(
			[...(/enum Key\s*\{([^}]*)\}/.exec(propertiesHeader)![1]!.matchAll(/\b([a-z][a-z0-9_]*)\s*,/g))].map(
				(match) => match[1]!,
			),
		);
		expect(keys.size, "properties::Key parsed no enumerators").toBeGreaterThan(20);
		for (const row of parsedRows) {
			if (!["kBoolProperty", "kFloatProperty", "kIntProperty"].includes(row.kind)) {
				continue;
			}
			const key = row.payload.replace(/^properties::/, "");
			expect(keys.has(key), `row ${describeRow(row)} names an undeclared properties::Key`).toBe(true);
		}
	});

	it("selects one distinct form-type mask bit per object-filter row", () => {
		const declared = new Map(
			[...propertiesHeader.matchAll(/inline\s+constexpr\s+int\s+(kEnableFormType\w+)\s*=\s*(\d+)\s*;/g)].map(
				(match) => [match[1]!, Number(match[2])] as const,
			),
		);
		expect(declared.size, "properties.h declares no form-type mask bits").toBe(12);

		const used = parsedRows
			.filter((row) => row.kind === "kMaskBit")
			.map((row) => row.payload.replace(/^properties::/, ""));
		expect(used).toHaveLength(12);
		// Exactly the declared bits, each used once: no filter item shares another
		// item's bit, and none of the declared filters is left unlabeled.
		expect([...used].sort()).toEqual([...declared.keys()].sort());

		const values = used.map((name) => declared.get(name)!);
		expect(new Set(values).size, "two rows resolve to the same mask bit").toBe(values.length);
		for (const value of values) {
			expect(value > 0 && (value & (value - 1)) === 0, `mask bit ${value} is not a single bit`).toBe(true);
		}
	});

	it("maps each log-level item to the level one below its item id", () => {
		const logLevelPage = pageByFormId.get("000FBA")!;
		const levels = parsedRows
			.filter((row) => row.page === logLevelPage)
			.map((row) => {
				expect(row.kind, `log-level row ${rowKey(row)} must render a log level`).toBe("kLogLevel");
				expect(Number(row.payload), `log-level row ${rowKey(row)} must select level ${row.itemId - 1}`).toBe(
					row.itemId - 1,
				);
				return Number(row.payload);
			});
		expect([...levels].sort((a, b) => a - b)).toEqual([0, 1, 2, 3, 4, 5, 6]);
	});

	it("reads the label table from the refresh routine", () => {
		// A table nothing reads would pass every check above while labeling nothing.
		const refresh = /void RunRefreshAll\([^)]*\)\s*\{([\s\S]*?)\n\t\}/.exec(labelSource);
		expect(refresh, "RunRefreshAll not found in terminal_labels.cpp").not.toBeNull();
		expect(refresh![1]).toContain("kLabelRows");
	});

	// The checks above only mean something if they reject a wrong table. Each case
	// perturbs a scratch copy of the source in a temp directory -- never the source
	// file itself -- and requires the violation the perturbation should raise.
	describe("rejects a mislabeled table", () => {
		const tempDirs: string[] = [];

		afterEach(() => {
			for (const dir of tempDirs.splice(0)) {
				removeTempDir(dir);
			}
		});

		function violationsForPerturbedCopy(perturb: (source: string) => string): string[] {
			const perturbed = perturb(labelSource);
			expect(perturbed, "perturbation did not change the source").not.toBe(labelSource);
			const dir = createTempDir("lootman-label-table-");
			tempDirs.push(dir);
			const scratch = path.join(dir, "terminal_labels.cpp");
			fs.writeFileSync(scratch, perturbed, "utf8");
			return findLabelTableViolations(fs.readFileSync(scratch, "utf8"), derivedRows);
		}

		it("catches an item id that no longer matches its fragment", () => {
			const violations = violationsForPerturbedCopy((source) =>
				source.replace("{ LabelPage::kPrimary, 8,", "{ LabelPage::kPrimary, 88,"),
			);
			expect(violations).toContain(
				"missing table entry for kPrimary#8 kBoolProperty properties::enable_looting_in_settlement",
			);
			expect(violations).toContain(
				"unexpected table entry kPrimary#88 kBoolProperty properties::enable_looting_in_settlement",
			);
		});

		it("catches two object-filter rows with their mask bits swapped", () => {
			const violations = violationsForPerturbedCopy((source) =>
				source
					.replace("properties::kEnableFormTypeALCH", "properties::kEnableFormTypeSWAPPED")
					.replace("properties::kEnableFormTypeAMMO", "properties::kEnableFormTypeALCH")
					.replace("properties::kEnableFormTypeSWAPPED", "properties::kEnableFormTypeAMMO"),
			);
			expect(violations).toContain(
				"table entry kSecondary#2 renders kMaskBit properties::kEnableFormTypeAMMO, expected kMaskBit properties::kEnableFormTypeALCH",
			);
			expect(violations).toContain(
				"table entry kSecondary#3 renders kMaskBit properties::kEnableFormTypeALCH, expected kMaskBit properties::kEnableFormTypeAMMO",
			);
		});

		it("catches a row repeated for the same item", () => {
			const row = "{ LabelPage::kTertiary, 7, LabelKind::kLogLevel, 6 },";
			const violations = violationsForPerturbedCopy((source) => source.replace(row, `${row}\n\t\t${row}`));
			expect(violations).toContain("duplicate table entry for kTertiary#7");
		});

		it("catches a row added for the settings-free utility page", () => {
			const violations = violationsForPerturbedCopy((source) =>
				source
					.replace("\t\tkTertiary    // LootMan.esp|000FBA", "\t\tkTertiary,   // LootMan.esp|000FBA\n\t\tkUtility     // LootMan.esp|000FBB")
					.replace(
						'constexpr const char* kTertiaryPageForm = "LootMan.esp|000FBA";',
						'constexpr const char* kTertiaryPageForm = "LootMan.esp|000FBA";\n\tconstexpr const char* kUtilityPageForm = "LootMan.esp|000FBB";',
					)
					.replace(
						"{ LabelPage::kTertiary, 7, LabelKind::kLogLevel, 6 },",
						"{ LabelPage::kTertiary, 7, LabelKind::kLogLevel, 6 },\n\t\t{ LabelPage::kUtility, 1, LabelKind::kLogLevel, 0 },",
					),
			);
			expect(violations).toContain("row kUtility#1 kLogLevel 0 targets settings-free page 000FBB");
			expect(violations).toContain("unexpected table entry kUtility#1 kLogLevel 0");
		});
	});
});
