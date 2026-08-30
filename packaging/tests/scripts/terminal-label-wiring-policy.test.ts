import fs from "node:fs";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

// The config holotape label feature renders setting values into terminal item
// text. Its label table is covered elsewhere; what the table alone cannot show is
// whether anything still calls the feature. Deleting the refresh call in the
// property callback, or any one of the three lifecycle calls in the F4SE message
// handler, leaves the module compiling and every table check green while the
// labels never change in game. That failure mode has already cost an in-game test
// session, so the wiring is asserted here: which branch makes which call, in which
// order, which page each enumerator resolves to, and which translation key each
// composition path uses.

const MAIN_SOURCE = "commonlibf4-plugin/src/main.cpp";
const FORMS_SOURCE = "commonlibf4-plugin/src/papyrus_lootman_forms.cpp";
const TERMINAL_LABELS_SOURCE = "commonlibf4-plugin/src/terminal_labels.cpp";
const EN_TRANSLATION = "packaging/resources/lootman/en/Interface/Translations/LootMan_en.txt";

// The two composition keys and the C++ symbols that must use them.
const VALUE_KEY = "$LTMN_CFG_ITEM_LABEL";
const SELECTED_KEY = "$LTMN_CFG_ITEM_SELECTED";
// Row kinds that render a value; the log-level rows mark a selection instead.
const VALUE_KINDS = ["kBoolProperty", "kFloatProperty", "kIntProperty", "kMaskBit"];

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

function readTranslationEntries(file: string): Map<string, string> {
	const text = fs.readFileSync(path.resolve(file)).toString("utf16le").replace(/^\uFEFF/, "");
	const entries = new Map<string, string>();
	for (const line of text.split(/\r?\n/)) {
		const tab = line.indexOf("\t");
		if (tab > 0) {
			entries.set(line.slice(0, tab), line.slice(tab + 1));
		}
	}
	return entries;
}

// --- source parsing (pure over the C++ text) ---------------------------------

// Comments are removed before anything is matched, so a call named only in an
// explanatory comment can never stand in for the call itself.
function stripComments(source: string): string {
	return source.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/[^\n]*/g, "");
}

// The text between the braces of the block opening at the first `{` at or after
// `from`. Scanned by brace balance, so a body that gains or loses a brace is
// reported instead of silently truncating at the wrong place.
function readBracedBlock(source: string, from: number, what: string): string {
	const open = source.indexOf("{", from);
	if (open < 0) {
		throw new Error(`no block opens after ${what}`);
	}
	let depth = 0;
	for (let index = open; index < source.length; index += 1) {
		const char = source[index];
		if (char === "{") {
			depth += 1;
		} else if (char === "}") {
			depth -= 1;
			if (depth === 0) {
				return source.slice(open + 1, index);
			}
		}
	}
	throw new Error(`${what} is not brace balanced`);
}

// The body of the function the anchor introduces. Bodies are extracted rather
// than searched for in the whole file, so a call that moved into a neighbouring
// function is reported as missing here.
function readFunctionBody(source: string, anchor: RegExp, what: string): string {
	const match = anchor.exec(source);
	if (!match) {
		throw new Error(`missing ${what}`);
	}
	return readBracedBlock(source, match.index + match[0].length, what);
}

// F4SE message name -> the body of the branch that handles it.
function extractMessageBranches(source: string): Map<string, string> {
	const handler = readFunctionBody(source, /void\s+OnMessage\s*\(/, "OnMessage");
	const branches = new Map<string, string>();
	for (const match of handler.matchAll(/a_msg->type\s*==\s*F4SE::MessagingInterface::(k\w+)/g)) {
		const message = match[1]!;
		branches.set(message, readBracedBlock(handler, match.index + match[0].length, `${message} branch`));
	}
	return branches;
}

function findLifecycleWiringViolations(source: string): string[] {
	const violations: string[] = [];
	let branches: Map<string, string>;
	try {
		branches = extractMessageBranches(stripComments(source));
	} catch (error) {
		return [`unreadable message handler: ${(error as Error).message}`];
	}

	const gameLoaded = branches.get("kGameLoaded");
	if (gameLoaded === undefined) {
		violations.push("the message handler has no kGameLoaded branch");
	} else {
		const propertiesAt = gameLoaded.indexOf("properties::Initialize()");
		const labelsAt = gameLoaded.indexOf("terminal_labels::Initialize()");
		if (propertiesAt < 0) {
			violations.push("the kGameLoaded branch does not call properties::Initialize()");
		}
		if (labelsAt < 0) {
			violations.push("the kGameLoaded branch does not call terminal_labels::Initialize()");
		}
		if (propertiesAt >= 0 && labelsAt >= 0 && labelsAt < propertiesAt) {
			// The label module captures page text and reads the property cache the
			// other initializer sets up, so it can only run second.
			violations.push("terminal_labels::Initialize() runs before properties::Initialize() in the kGameLoaded branch");
		}
	}

	for (const [message, call] of [
		["kPreLoadGame", "terminal_labels::OnPreLoadGame()"],
		["kPostLoadGame", "terminal_labels::OnPostLoadGame()"],
	] as const) {
		const body = branches.get(message);
		if (body === undefined) {
			violations.push(`the message handler has no ${message} branch`);
		} else if (!body.includes(call)) {
			violations.push(`the ${message} branch does not call ${call}`);
		}
	}

	return violations;
}

function findPropertyCallbackWiringViolations(source: string): string[] {
	const violations: string[] = [];
	let body: string;
	try {
		body = readFunctionBody(stripComments(source), /void\s+OnUpdateLootManProperty\s*\(/, "OnUpdateLootManProperty");
	} catch (error) {
		return [`unreadable property callback: ${(error as Error).message}`];
	}

	const updateAt = body.indexOf("properties::Update(");
	const refreshAt = body.indexOf("terminal_labels::RefreshAll(");
	if (updateAt < 0) {
		violations.push("OnUpdateLootManProperty does not call properties::Update(...)");
	}
	if (refreshAt < 0) {
		violations.push("OnUpdateLootManProperty does not call terminal_labels::RefreshAll(...)");
	}
	if (updateAt >= 0 && refreshAt >= 0 && refreshAt < updateAt) {
		// A refresh that runs first renders the values the update is about to replace.
		violations.push("terminal_labels::RefreshAll(...) runs before properties::Update(...) in OnUpdateLootManProperty");
	}

	if (!body.includes("terminal_labels::MarkPropertyCacheReady(")) {
		violations.push("OnUpdateLootManProperty does not call terminal_labels::MarkPropertyCacheReady(...)");
	} else {
		// Readiness may only be published by the full refresh, which is the empty
		// property name, so the call has to sit inside that guard.
		const guard = /if\s*\([^)]*'\\0'[^)]*\)/.exec(body);
		if (!guard) {
			violations.push("OnUpdateLootManProperty has no empty-name full-refresh guard");
		} else {
			let guarded: string;
			try {
				guarded = readBracedBlock(body, guard.index + guard[0].length, "full-refresh guard");
			} catch (error) {
				return [...violations, `unreadable full-refresh guard: ${(error as Error).message}`];
			}
			if (!guarded.includes("terminal_labels::MarkPropertyCacheReady(")) {
				violations.push("terminal_labels::MarkPropertyCacheReady(...) is not called on the full-refresh path");
			}
		}
	}

	return violations;
}

function parseEnumerators(source: string, enumName: string): string[] {
	const block = new RegExp(`enum class ${enumName}\\s*\\{([^}]*)\\}`).exec(source);
	if (!block) {
		throw new Error(`${enumName} enum not found`);
	}
	return [...stripComments(block[1]!).matchAll(/\bk\w+/g)].map((match) => match[0]);
}

// LabelPage enumerator -> the symbol the switch returns for it. Enumerators with
// no explicit case are answered by the `default:` arm.
function parseSwitchReturns(body: string, enumerators: string[]): Map<string, string> {
	const returns = new Map<string, string>();
	for (const match of body.matchAll(/case\s+LabelPage::(k\w+)\s*:\s*return\s+([A-Za-z_]\w*)\s*;/g)) {
		returns.set(match[1]!, match[2]!);
	}
	const fallback = /default\s*:\s*return\s+([A-Za-z_]\w*)\s*;/.exec(body);
	if (fallback) {
		for (const enumerator of enumerators) {
			if (!returns.has(enumerator)) {
				returns.set(enumerator, fallback[1]!);
			}
		}
	}
	return returns;
}

// A LabelPage enumerator and the symbol a switch returns for it must name the
// same page. The correspondence is read from the names -- `kPrimary` may only be
// answered by a symbol that says "primary" and by no other enumerator's word --
// so no table in this file has to be kept in step with the source.
function findPageBindingViolations(source: string): string[] {
	const stripped = stripComments(source);
	const violations: string[] = [];
	let enumerators: string[];
	try {
		enumerators = parseEnumerators(stripped, "LabelPage");
	} catch (error) {
		return [`unreadable LabelPage enum: ${(error as Error).message}`];
	}
	if (enumerators.length === 0) {
		return ["LabelPage declares no enumerators"];
	}

	const functions: Array<[string, RegExp]> = [
		["PageForLocked", /RE::BGSTerminal\*\s+PageForLocked\s*\(/],
		["PageLabel", /const\s+char\*\s+PageLabel\s*\(/],
	];

	for (const [name, anchor] of functions) {
		let body: string;
		try {
			body = readFunctionBody(stripped, anchor, name);
		} catch (error) {
			violations.push(`unreadable ${name}: ${(error as Error).message}`);
			continue;
		}

		const returns = parseSwitchReturns(body, enumerators);
		// One enumerator may lean on `default:`; two or more would make a mis-wired
		// page indistinguishable from a correct one, so that is reported as well.
		const explicit = new Set([...body.matchAll(/case\s+LabelPage::(k\w+)\s*:/g)].map((match) => match[1]!));
		const implicit = enumerators.filter((enumerator) => !explicit.has(enumerator));
		if (implicit.length > 1) {
			violations.push(`${name} answers ${implicit.join(" and ")} from the same default arm`);
		}

		for (const enumerator of enumerators) {
			const returned = returns.get(enumerator);
			if (returned === undefined) {
				violations.push(`${name} returns nothing for LabelPage::${enumerator}`);
				continue;
			}
			const word = enumerator.replace(/^k/, "").toLowerCase();
			const lowered = returned.toLowerCase();
			const foreign = enumerators
				.filter((other) => other !== enumerator)
				.find((other) => lowered.includes(other.replace(/^k/, "").toLowerCase()));
			if (foreign) {
				violations.push(`${name} binds LabelPage::${enumerator} to ${returned}, which belongs to LabelPage::${foreign}`);
			} else if (!lowered.includes(word)) {
				violations.push(`${name} binds LabelPage::${enumerator} to ${returned}, which does not name the ${word} page`);
			}
		}
	}

	return violations;
}

// Translation key -> the built-in fallback the composition passes with it.
function parseLocalizedFormats(source: string): Map<string, string> {
	const formats = new Map<string, string>();
	for (const match of stripComments(source).matchAll(/FormatLocalizedText\(\s*"(\$[A-Z0-9_]+)"\s*,\s*"([^"]*)"/g)) {
		formats.set(match[1]!, match[2]!);
	}
	return formats;
}

// The fallback renders identically to the shipped translation while the key
// resolves, so a typo in either literal is invisible in game. Requiring the
// fallback to equal the shipped value ties the two together.
function findFallbackFormatViolations(source: string, entries: Map<string, string>): string[] {
	const violations: string[] = [];
	const formats = parseLocalizedFormats(source);
	for (const key of [VALUE_KEY, SELECTED_KEY]) {
		const fallback = formats.get(key);
		if (fallback === undefined) {
			violations.push(`no localized format call passes the translation key ${key}`);
			continue;
		}
		const shipped = entries.get(key);
		if (shipped === undefined) {
			violations.push(`translation key ${key} is missing from the shipped English translation`);
			continue;
		}
		if (fallback !== shipped) {
			violations.push(`the built-in fallback for ${key} renders "${fallback}" but the shipped English translation renders "${shipped}"`);
		}
	}
	return violations;
}

// LabelKind enumerator -> the text of its arm in a switch.
function extractSwitchArms(body: string, enumName: string): Map<string, string> {
	const arms = new Map<string, string>();
	const markers = [...body.matchAll(new RegExp(`case\\s+${enumName}::(k\\w+)\\s*:`, "g"))];
	markers.forEach((marker, position) => {
		const start = marker.index + marker[0].length;
		const next = markers[position + 1];
		let end = next ? next.index : body.length;
		if (!next) {
			const fallback = body.indexOf("default:", start);
			if (fallback >= 0) {
				end = fallback;
			}
		}
		arms.set(marker[1]!, body.slice(start, end));
	});
	return arms;
}

function findCompositionKeyViolations(source: string): string[] {
	const stripped = stripComments(source);
	const violations: string[] = [];

	let valueLabel: string;
	let compose: string;
	try {
		valueLabel = readFunctionBody(stripped, /std::string\s+FormatValueLabel\s*\(/, "FormatValueLabel");
		compose = readFunctionBody(stripped, /std::string\s+ComposeRowText\s*\(/, "ComposeRowText");
	} catch (error) {
		return [`unreadable label composition: ${(error as Error).message}`];
	}

	if (!valueLabel.includes(VALUE_KEY)) {
		violations.push(`the value label format does not use ${VALUE_KEY}`);
	}
	if (valueLabel.includes(SELECTED_KEY)) {
		violations.push(`the value label format uses the selection marker key ${SELECTED_KEY}`);
	}

	const arms = extractSwitchArms(compose, "LabelKind");
	for (const kind of VALUE_KINDS) {
		const arm = arms.get(kind);
		if (arm === undefined) {
			violations.push(`ComposeRowText has no ${kind} arm`);
			continue;
		}
		if (!arm.includes("FormatValueLabel(")) {
			violations.push(`the ${kind} arm does not compose through the value label format`);
		}
		if (arm.includes(SELECTED_KEY)) {
			violations.push(`the ${kind} arm renders its value with the selection marker key ${SELECTED_KEY}`);
		}
	}

	const logLevel = arms.get("kLogLevel");
	if (logLevel === undefined) {
		violations.push("ComposeRowText has no kLogLevel arm");
	} else {
		if (!logLevel.includes(SELECTED_KEY)) {
			violations.push(`the kLogLevel arm does not mark the selected level with ${SELECTED_KEY}`);
		}
		if (logLevel.includes(VALUE_KEY) || logLevel.includes("FormatValueLabel(")) {
			violations.push(`the kLogLevel arm marks the selected level with the value key ${VALUE_KEY}`);
		}
	}

	return violations;
}

// A refresh that re-read the item text instead of the captured original would
// compose on top of its own previous output, stacking one value suffix per pass.
function findCapturedOriginalViolations(source: string): string[] {
	const stripped = stripComments(source);
	const violations: string[] = [];

	let refresh: string;
	let capture: string;
	try {
		refresh = readFunctionBody(stripped, /void\s+RunRefreshAll\s*\(/, "RunRefreshAll");
		capture = readFunctionBody(stripped, /void\s+CaptureOriginalLabelsLocked\s*\(/, "CaptureOriginalLabelsLocked");
	} catch (error) {
		return [`unreadable refresh routine: ${(error as Error).message}`];
	}

	const composeCall = /ComposeRowText\(\s*([^,)]+)\s*,\s*([^,)]+)\s*[,)]/.exec(refresh);
	if (!composeCall) {
		violations.push("RunRefreshAll never composes a row text");
	} else if (composeCall[2]!.trim() !== "captured.original") {
		violations.push(`RunRefreshAll composes from ${composeCall[2]!.trim()} instead of the captured original text`);
	}

	const guard = /if\s*\(\s*!captured\.captured\s*\)/.exec(refresh);
	if (!guard) {
		violations.push("RunRefreshAll captures the original text without checking the already-captured flag");
	} else {
		let guarded: string;
		try {
			guarded = readBracedBlock(refresh, guard.index + guard[0].length, "capture guard");
		} catch (error) {
			return [...violations, `unreadable capture guard: ${(error as Error).message}`];
		}
		if (!guarded.includes("captured.captured = true")) {
			violations.push("the capture guard in RunRefreshAll never marks the row captured");
		}
		const assignments = [...refresh.matchAll(/captured\.original\s*=[^=]/g)].length;
		const inGuard = [...guarded.matchAll(/captured\.original\s*=[^=]/g)].length;
		if (assignments !== inGuard) {
			violations.push(
				`RunRefreshAll assigns the captured original ${assignments} times but only ${inGuard} inside the already-captured guard`,
			);
		}
	}

	const skip = /if\s*\(\s*captured\.captured\s*\)/.exec(capture);
	if (!skip) {
		violations.push("CaptureOriginalLabelsLocked captures rows without checking the already-captured flag");
	} else {
		let skipped: string;
		try {
			skipped = readBracedBlock(capture, skip.index + skip[0].length, "recapture guard");
		} catch (error) {
			return [...violations, `unreadable recapture guard: ${(error as Error).message}`];
		}
		if (!skipped.includes("continue")) {
			violations.push("CaptureOriginalLabelsLocked does not skip a row it already captured");
		}
	}

	return violations;
}

describe("config holotape label wiring", () => {
	const mainSource = readWorkspaceFile(MAIN_SOURCE);
	const formsSource = readWorkspaceFile(FORMS_SOURCE);
	const labelSource = readWorkspaceFile(TERMINAL_LABELS_SOURCE);
	const translation = readTranslationEntries(EN_TRANSLATION);

	it("initializes and reloads the label module from the game lifecycle messages", () => {
		// Each branch is extracted before it is searched, so a call that drifted into
		// another branch reads as missing from the branch that must make it.
		expect(findLifecycleWiringViolations(mainSource)).toEqual([]);
	});

	it("refreshes the labels after every property update the config writes", () => {
		expect(findPropertyCallbackWiringViolations(formsSource)).toEqual([]);
	});

	it("refreshes the labels when the terminal menu opens", () => {
		// Without this sink a page opened after a setting change would still show the
		// values that were current when the last refresh ran.
		const sink = readFunctionBody(
			stripComments(labelSource),
			/RE::BSEventNotifyControl\s+ProcessEvent\s*\(/,
			"ProcessEvent",
		);
		expect(sink, "the menu sink must match the terminal menu by name").toContain("RE::TerminalMenu::MENU_NAME");
		expect(sink, "the menu sink must act on the opening event, not the closing one").toContain("a_event.opening");
		expect(sink, "the menu sink must queue a refresh").toContain("QueueRefreshAll(");
	});

	it("resolves every label page enumerator to its own page and log label", () => {
		expect(findPageBindingViolations(labelSource)).toEqual([]);
	});

	it("ships built-in label fallbacks identical to the shipped English translation", () => {
		expect(findFallbackFormatViolations(labelSource, translation)).toEqual([]);
	});

	it("renders values with the value template and log levels with the marker template", () => {
		expect(findCompositionKeyViolations(labelSource)).toEqual([]);
	});

	it("composes every refreshed label from the once-captured original text", () => {
		expect(findCapturedOriginalViolations(labelSource)).toEqual([]);
	});

	// The checks above only mean something if they reject broken wiring. Each case
	// perturbs a scratch copy of the source in a temp directory -- never the source
	// file itself -- and requires the violation the perturbation should raise.
	describe("rejects broken wiring", () => {
		// Matched by call rather than by surrounding text, so the perturbations keep
		// working while these call sites gain or lose arguments.
		const REFRESH_CALL = /\n(\t*)(terminal_labels::RefreshAll\([^)]*\);)/;
		const LABELS_INITIALIZE = /\n\t*terminal_labels::Initialize\(\);/;
		const tempDirs: string[] = [];

		afterEach(() => {
			for (const dir of tempDirs.splice(0)) {
				removeTempDir(dir);
			}
		});

		function perturbedCopy(source: string, name: string, perturb: (source: string) => string): string {
			const perturbed = perturb(source);
			expect(perturbed, "perturbation did not change the source").not.toBe(source);
			const dir = createTempDir("lootman-label-wiring-");
			tempDirs.push(dir);
			const scratch = path.join(dir, name);
			fs.writeFileSync(scratch, perturbed, "utf8");
			return fs.readFileSync(scratch, "utf8");
		}

		it("catches the refresh call removed from the property callback", () => {
			const violations = findPropertyCallbackWiringViolations(
				perturbedCopy(formsSource, "papyrus_lootman_forms.cpp", (source) =>
					source.replace(REFRESH_CALL, ""),
				),
			);
			expect(violations).toContain("OnUpdateLootManProperty does not call terminal_labels::RefreshAll(...)");
		});

		it("catches the cache readiness call removed from the full-refresh path", () => {
			const violations = findPropertyCallbackWiringViolations(
				perturbedCopy(formsSource, "papyrus_lootman_forms.cpp", (source) =>
					source.replace(/\n\t*terminal_labels::MarkPropertyCacheReady\([^)]*\);/, ""),
				),
			);
			expect(violations).toContain("OnUpdateLootManProperty does not call terminal_labels::MarkPropertyCacheReady(...)");
		});

		it("catches a refresh queued before the property cache is updated", () => {
			const violations = findPropertyCallbackWiringViolations(
				perturbedCopy(formsSource, "papyrus_lootman_forms.cpp", (source) => {
					const refresh = REFRESH_CALL.exec(source)!;
					return source
						.replace(REFRESH_CALL, "")
						.replace(/(\n\t*)(properties::Update\([^;]*\);)/, `$1${refresh[2]!}$1$2`);
				}),
			);
			expect(violations).toContain(
				"terminal_labels::RefreshAll(...) runs before properties::Update(...) in OnUpdateLootManProperty",
			);
		});

		it("catches the module initializer removed from the game loaded branch", () => {
			const violations = findLifecycleWiringViolations(
				perturbedCopy(mainSource, "main.cpp", (source) => source.replace(LABELS_INITIALIZE, "")),
			);
			expect(violations).toContain("the kGameLoaded branch does not call terminal_labels::Initialize()");
		});

		it("catches the pre-load teardown removed from the pre load game branch", () => {
			const violations = findLifecycleWiringViolations(
				perturbedCopy(mainSource, "main.cpp", (source) => source.replace(/\n\t*terminal_labels::OnPreLoadGame\(\);/, "")),
			);
			expect(violations).toContain("the kPreLoadGame branch does not call terminal_labels::OnPreLoadGame()");
		});

		it("catches the post-load re-resolve removed from the post load game branch", () => {
			const violations = findLifecycleWiringViolations(
				perturbedCopy(mainSource, "main.cpp", (source) => source.replace(/\n\t*terminal_labels::OnPostLoadGame\(\);/, "")),
			);
			expect(violations).toContain("the kPostLoadGame branch does not call terminal_labels::OnPostLoadGame()");
		});

		it("catches a lifecycle call that moved into another message branch", () => {
			// Present in the handler, but no longer in the branch that must make it.
			const violations = findLifecycleWiringViolations(
				perturbedCopy(mainSource, "main.cpp", (source) =>
					source
						.replace(/\n(\t*)terminal_labels::OnPostLoadGame\(\);/, "")
						.replace(
							/\n(\t*)terminal_labels::OnPreLoadGame\(\);/,
							"\n$1terminal_labels::OnPreLoadGame();\n$1terminal_labels::OnPostLoadGame();",
						),
				),
			);
			expect(violations).toContain("the kPostLoadGame branch does not call terminal_labels::OnPostLoadGame()");
		});

		it("catches the label module initialized before the property cache", () => {
			const violations = findLifecycleWiringViolations(
				perturbedCopy(mainSource, "main.cpp", (source) =>
					source
						.replace(LABELS_INITIALIZE, "")
						.replace(
							/\n(\t*)properties::Initialize\(\);/,
							"\n$1terminal_labels::Initialize();\n$1properties::Initialize();",
						),
				),
			);
			expect(violations).toContain(
				"terminal_labels::Initialize() runs before properties::Initialize() in the kGameLoaded branch",
			);
		});

		it("catches a page enumerator wired to another page's pointer", () => {
			const violations = findPageBindingViolations(
				perturbedCopy(labelSource, "terminal_labels.cpp", (source) =>
					source.replace(
						"case LabelPage::kPrimary:\n\t\t\treturn primaryPage;",
						"case LabelPage::kPrimary:\n\t\t\treturn secondaryPage;",
					),
				),
			);
			expect(violations).toContain("PageForLocked binds LabelPage::kPrimary to secondaryPage, which belongs to LabelPage::kSecondary");
		});

		it("catches a page enumerator wired to another page's log label", () => {
			const violations = findPageBindingViolations(
				perturbedCopy(labelSource, "terminal_labels.cpp", (source) =>
					source.replace(
						"case LabelPage::kSecondary:\n\t\t\treturn kSecondaryPageLabel;",
						"case LabelPage::kSecondary:\n\t\t\treturn kTertiaryPageLabel;",
					),
				),
			);
			expect(violations).toContain("PageLabel binds LabelPage::kSecondary to kTertiaryPageLabel, which belongs to LabelPage::kTertiary");
		});

		it("catches a one-character drift in a built-in fallback format", () => {
			const violations = findFallbackFormatViolations(
				perturbedCopy(labelSource, "terminal_labels.cpp", (source) =>
					source.replace('"{name} [{value}]"', '"{name}[{value}]"'),
				),
				translation,
			);
			expect(violations).toContain(
				`the built-in fallback for ${VALUE_KEY} renders "{name}[{value}]" but the shipped English translation renders "{name} [{value}]"`,
			);
		});

		it("catches the log level marker composed with the value template", () => {
			const violations = findCompositionKeyViolations(
				perturbedCopy(labelSource, "terminal_labels.cpp", (source) =>
					source.replace(`"${SELECTED_KEY}", "{name} [*]"`, `"${VALUE_KEY}", "{name} [*]"`),
				),
			);
			expect(violations).toContain(`the kLogLevel arm marks the selected level with the value key ${VALUE_KEY}`);
		});

		it("catches a refresh that recaptures the original text on every pass", () => {
			const violations = findCapturedOriginalViolations(
				perturbedCopy(labelSource, "terminal_labels.cpp", (source) =>
					// Drops the already-captured guard around the capture, leaving the
					// composed text of the previous pass to be captured as the original.
					source.replace(/if\s*\(!captured\.captured\)\s*\n(\s*)\{(\s*captured\.original)/, "$1{$2"),
				),
			);
			expect(violations).toContain("RunRefreshAll captures the original text without checking the already-captured flag");
		});

		it("catches a refresh that composes from the live item text", () => {
			const violations = findCapturedOriginalViolations(
				perturbedCopy(labelSource, "terminal_labels.cpp", (source) =>
					source.replace("ComposeRowText(row, captured.original,", "ComposeRowText(row, current,"),
				),
			);
			expect(violations).toContain("RunRefreshAll composes from current instead of the captured original text");
		});

		it("catches a typo in a translation key literal", () => {
			const violations = findFallbackFormatViolations(
				perturbedCopy(labelSource, "terminal_labels.cpp", (source) =>
					source.replace(`"${SELECTED_KEY}"`, '"$LTMN_CFG_ITEM_SELECTD"'),
				),
				translation,
			);
			expect(violations).toContain(`no localized format call passes the translation key ${SELECTED_KEY}`);
		});
	});
});
