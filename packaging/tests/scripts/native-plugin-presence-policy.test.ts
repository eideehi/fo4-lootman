import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

function readUtf16LeWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file)).toString("utf16le").replace(/^﻿/, "");
}

function extractPapyrusFunction(source: string, name: string): string {
	const match = new RegExp(`Function\\s+${name}\\([^\\n]*\\)([\\s\\S]*?)EndFunction`, "i").exec(source);
	expect(match, `missing Papyrus function ${name}`).not.toBeNull();
	return match![1]!;
}

function extractPapyrusEvent(source: string, name: string): string {
	const match = new RegExp(`Event\\s+${name}\\([^\\n]*\\)([\\s\\S]*?)EndEvent`, "i").exec(source);
	expect(match, `missing Papyrus event ${name}`).not.toBeNull();
	return match![1]!;
}

// Papyrus statements with comments and blank lines dropped. Order-and-content
// assertions run against this so a mutation cannot hide inside a comment, and so
// "somewhere in the body" is never good enough to pass.
function papyrusStatements(body: string): string[] {
	return body
		.split(/\r?\n/)
		.map((line) => line.trim())
		.filter((line) => line !== "" && !line.startsWith(";"));
}

function countOccurrences(source: string, needle: string): number {
	return source.split(needle).length - 1;
}

function translationValue(source: string, key: string): string {
	const line = source.split(/\r?\n/).find((entry) => entry.startsWith(`${key}\t`));
	expect(line, `missing translation key ${key}`).toBeDefined();
	return line!.slice(key.length + 1);
}

interface McmContentRow {
	type?: string;
	text?: string;
	groupControl?: number;
	groupCondition?: unknown;
	valueOptions?: { sourceType?: string; sourceForm?: string; propertyName?: string };
}

const NATIVE_PROBE_GROUP = 8;

const WARNING_KEYS = [
	"$PAGE_SYSTEM_NATIVE_PLUGIN_SECTION",
	"$PAGE_SYSTEM_NATIVE_PLUGIN_MISSING",
	"$PAGE_SYSTEM_NATIVE_PLUGIN_MISSING_TEXT",
	"$PAGE_SYSTEM_NATIVE_PLUGIN_MISSING_FIX",
] as const;

const TRANSLATION_FILES = [
	"packaging/resources/lootman/common/Interface/Translations/LootMan_cn.txt",
	"packaging/resources/lootman/common/Interface/Translations/LootMan_de.txt",
	"packaging/resources/lootman/common/Interface/Translations/LootMan_en.txt",
	"packaging/resources/lootman/common/Interface/Translations/LootMan_es.txt",
	"packaging/resources/lootman/common/Interface/Translations/LootMan_fr.txt",
	"packaging/resources/lootman/common/Interface/Translations/LootMan_it.txt",
	"packaging/resources/lootman/common/Interface/Translations/LootMan_ja.txt",
	"packaging/resources/lootman/common/Interface/Translations/LootMan_pl.txt",
	"packaging/resources/lootman/common/Interface/Translations/LootMan_ptbr.txt",
	"packaging/resources/lootman/common/Interface/Translations/LootMan_ru.txt",
	"packaging/resources/lootman/en/Interface/Translations/LootMan_en.txt",
	"packaging/resources/lootman/ja/Interface/Translations/LootMan_en.txt",
	"packaging/resources/lootman/ja/Interface/Translations/LootMan_ja.txt",
];

describe("native plugin presence policy", () => {
	const systemScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/System.psc");
	const propertiesScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Properties.psc");
	const mcmConfig = JSON.parse(
		readWorkspaceFile("packaging/resources/lootman/common/MCM/Config/LootMan/config.json"),
	) as { pages: Array<{ pageDisplayName?: string; content?: McmContentRow[] }> };
	const systemPage = mcmConfig.pages.find((page) => page.pageDisplayName === "$PAGE_SYSTEM");
	const systemRows = systemPage?.content ?? [];

	it("probes F4SE for the lootman plugin without touching a LootMan native", () => {
		const probe = extractPapyrusFunction(systemScript, "ProbeNativePlugin");

		// F4SE's own native is the only safe probe: in this failure mode F4SE is
		// loaded and lootman.dll is not, so nothing in this frame may reach
		// LTMN2:LootMan - logging included.
		expect(probe, "ProbeNativePlugin must not call a LootMan native").not.toContain("LTMN2:LootMan");
		expect(probe, "ProbeNativePlugin must not log through the native logger").not.toContain("LogSystemEvent");
		expect(probe).toContain('F4SE.GetPluginVersion("lootman")');

		// The return value is a packed REL::Version, never a 30300-style integer:
		// only the sign carries meaning.
		expect(probe).toContain('If (F4SE.GetPluginVersion("lootman") >= 0)');
		const comparisons = [...probe.matchAll(/GetPluginVersion\("lootman"\)\s*([=!<>]=?)\s*(-?\d+)/g)].map(
			(match) => `${match[1]} ${match[2]}`,
		);
		expect(comparisons, "the probe may only test the sign of the packed REL::Version").toEqual([">= 0"]);

		// Fail-safe ordering: assume missing, and let only a successful probe clear
		// it. Reversing these two lines would report "present" whenever the probe
		// itself fails to run to completion.
		const assumeMissing = probe.indexOf("properties.IsNativePluginMissing = true");
		const clearOnSuccess = probe.indexOf("properties.IsNativePluginMissing = false");
		const probeCall = probe.indexOf('F4SE.GetPluginVersion("lootman")');
		expect(assumeMissing, "ProbeNativePlugin never assumes the plugin is missing").toBeGreaterThanOrEqual(0);
		expect(clearOnSuccess, "ProbeNativePlugin never clears the flag").toBeGreaterThan(assumeMissing);
		expect(probeCall, "the probe must run after the flag is set to true").toBeGreaterThan(assumeMissing);
		expect(probe).toMatch(
			/properties\.IsNativePluginMissing = true[\s\S]*If \(F4SE\.GetPluginVersion\("lootman"\) >= 0\)[\s\S]*properties\.IsNativePluginMissing = false[\s\S]*EndIf/,
		);

		const probeStatements = papyrusStatements(probe);

		// Nothing may short-circuit the probe before its own guards, and nothing
		// may run after the success branch closes: an early Return would freeze the
		// flag at whatever the last session wrote, and a trailing unconditional
		// clear would report "present" on an install that has no plugin at all.
		expect(probeStatements[0], "no statement may precede the properties guard").toBe("If (!properties)");
		expect(
			probeStatements[probeStatements.length - 1],
			"no statement may follow the success branch",
		).toBe("EndIf");
		expect(
			countOccurrences(probe, "properties.IsNativePluginMissing = false"),
			"the flag may only be cleared once, inside the success branch",
		).toBe(1);
		expect(
			countOccurrences(probe, "properties.IsNativePluginMissing = true"),
			"the flag may only be set once, before the probe",
		).toBe(1);

		// A probe event can already be queued when Uninstall clears the flag, so the
		// probe refuses to write anything once the mod is uninstalled.
		const uninstalledGuard = probeStatements.indexOf("If (properties.IsUninstalled)");
		expect(uninstalledGuard, "the probe never checks the uninstalled state").toBeGreaterThanOrEqual(0);
		expect(
			uninstalledGuard,
			"the uninstalled guard must run before the flag is written",
		).toBeLessThan(probeStatements.indexOf("properties.IsNativePluginMissing = true"));

		// The whole body is the contract. This function is short, safety-critical
		// and has no legitimate variation: pin it statement for statement so any
		// inserted Return, extra write or dropped resolution has to be argued for
		// here rather than slipping past a substring match.
		expect(probeStatements, "ProbeNativePlugin's exact body is the policy").toEqual([
			"If (!properties)",
			"properties = LTMN2:Properties.GetInstance()",
			"EndIf",
			"If (!properties)",
			"Return",
			"EndIf",
			"If (properties.IsUninstalled)",
			"Return",
			"EndIf",
			"properties.IsNativePluginMissing = true",
			'If (F4SE.GetPluginVersion("lootman") >= 0)',
			"properties.IsNativePluginMissing = false",
			"EndIf",
		]);
	});

	it("runs the probe on its own timer stack and re-arms it on every load", () => {
		expect(systemScript).toContain("int TIMER_NATIVE_PROBE = 5 const");

		// A dedicated OnTimer branch keeps the probe off any frame that calls a
		// LootMan native, because an unbound native aborts its whole call stack.
		const onTimer = extractPapyrusEvent(systemScript, "OnTimer");
		expect(onTimer).toContain("aiTimerId == TIMER_NATIVE_PROBE");
		expect(onTimer).toContain("ProbeNativePlugin()");

		// The probe branch has to be the very first thing OnTimer does. A logging
		// line - or any other LootMan native - placed above it would abort the
		// dispatcher itself in exactly the failure the probe exists to report.
		const onTimerStatements = papyrusStatements(onTimer);
		expect(onTimerStatements[0], "OnTimer must test the probe timer first").toBe(
			"If (aiTimerId == TIMER_NATIVE_PROBE)",
		);
		expect(onTimerStatements[1], "the probe branch must call the probe and nothing else first").toBe(
			"ProbeNativePlugin()",
		);
		const beforeProbeDispatch = onTimerStatements.slice(0, onTimerStatements.indexOf("ProbeNativePlugin()"));
		expect(
			beforeProbeDispatch.join("\n"),
			"nothing may call a LootMan native before OnTimer dispatches the probe",
		).not.toContain("LTMN2:LootMan");
		expect(
			beforeProbeDispatch.join("\n"),
			"nothing may log before OnTimer dispatches the probe",
		).not.toContain("LogSystemEvent");

		// Both entry points arm it, and both do so before the first LogSystemEvent
		// call, which is itself a LootMan native.
		for (const body of [extractPapyrusEvent(systemScript, "OnInit"), extractPapyrusEvent(systemScript, "Actor.OnPlayerLoadGame")]) {
			const start = body.indexOf("StartTimer(5, TIMER_NATIVE_PROBE)");
			const cancel = body.indexOf("CancelTimer(TIMER_NATIVE_PROBE)");
			const firstNativeCall = body.indexOf("LogSystemEvent(");
			expect(start, "the native probe timer is never started").toBeGreaterThanOrEqual(0);
			expect(cancel, "a stale native probe timer is never cancelled").toBeGreaterThanOrEqual(0);
			expect(cancel, "cancel must precede the restart or it kills the new timer").toBeLessThan(start);
			expect(start, "the probe must be armed before the first LootMan native call").toBeLessThan(firstNativeCall);

			// Exactly one of each. A second CancelTimer after the arming line reads
			// as harmless cleanup and silently disarms the probe that was just set.
			expect(
				countOccurrences(body, "CancelTimer(TIMER_NATIVE_PROBE)"),
				"the probe timer must be cancelled exactly once, before it is armed",
			).toBe(1);
			expect(
				countOccurrences(body, "StartTimer(5, TIMER_NATIVE_PROBE)"),
				"the probe timer must be armed exactly once",
			).toBe(1);
		}

		// OnInit's head order is load-bearing. The probe timer is one-shot and dies
		// with the session, so OnPlayerLoadGame is the only thing that can re-probe
		// a repaired install. Registering it after LogSystemEvent means a first run
		// without lootman.dll aborts the frame before the subscription exists, and
		// the save is then stuck showing the warning forever.
		expect(
			papyrusStatements(extractPapyrusEvent(systemScript, "OnInit")).slice(0, 6),
			"OnInit must resolve properties, arm the probe and register the load event before its first native call",
		).toEqual([
			"properties = LTMN2:Properties.GetInstance()",
			"CancelTimer(TIMER_NATIVE_PROBE)",
			"StartTimer(5, TIMER_NATIVE_PROBE)",
			"player = Game.GetPlayer()",
			'RegisterForRemoteEvent(player, "OnPlayerLoadGame")',
			'LogSystemEvent("first_run", "version=" + GetVersionString(MOD_VERSION))',
		]);

		expect(
			papyrusStatements(extractPapyrusEvent(systemScript, "Actor.OnPlayerLoadGame")).slice(0, 2),
			"the load path must re-arm the probe before its first native call",
		).toEqual(["CancelTimer(TIMER_NATIVE_PROBE)", "StartTimer(5, TIMER_NATIVE_PROBE)"]);

		// Uninstall stops the probe and clears the flag before it touches a single
		// LootMan native. Uninstalling is exactly what a player does when the DLL
		// is missing, and that frame aborts at the first native - so cleanup placed
		// after it never runs, and MCM ends up showing "Uninstalled" and the
		// missing-plugin warning at the same time.
		const uninstall = extractPapyrusFunction(systemScript, "Uninstall");
		expect(uninstall).toContain("CancelTimer(TIMER_NATIVE_PROBE)");
		expect(uninstall, "Uninstall leaves IsNativePluginMissing frozen at its last reading").toContain(
			"properties.IsNativePluginMissing = false",
		);

		const firstLootManNative = uninstall.indexOf("LTMN2:LootMan.");
		expect(firstLootManNative, "Uninstall no longer calls a LootMan native").toBeGreaterThanOrEqual(0);
		expect(
			uninstall.indexOf("CancelTimer(TIMER_NATIVE_PROBE)"),
			"the probe timer must be cancelled before the first LootMan native call",
		).toBeLessThan(firstLootManNative);
		expect(
			uninstall.indexOf("properties.IsNativePluginMissing = false"),
			"the flag must be cleared before the first LootMan native call",
		).toBeLessThan(firstLootManNative);

		// Only the two skip guards may precede the cleanup, and they return without
		// uninstalling anything. Anything else in front of it - an early Return
		// most of all - puts the cleanup back behind an abortable frame.
		expect(
			papyrusStatements(uninstall).slice(0, 10),
			"Uninstall's diagnostic cleanup must sit directly after the skip guards",
		).toEqual([
			"If (properties.IsNotInstalled)",
			'LogSystemEvent("uninstall_skipped", "reason=not_installed", LOG_LEVEL_DEBUG)',
			"Return",
			"EndIf",
			"If (properties.IsUninstalled)",
			'LogSystemEvent("uninstall_skipped", "reason=already_uninstalled", LOG_LEVEL_DEBUG)',
			"Return",
			"EndIf",
			"CancelTimer(TIMER_NATIVE_PROBE)",
			"properties.IsNativePluginMissing = false",
		]);
	});

	it("stores the result in a save-safe hidden property that defaults to present", () => {
		expect(propertiesScript).toContain("bool property IsNativePluginMissing = false auto hidden");
	});

	it("binds the MCM warning rows to IsNativePluginMissing", () => {
		expect(systemPage, "config.json has no $PAGE_SYSTEM page").toBeDefined();

		const switcher = systemRows.find((row) => row.groupControl === NATIVE_PROBE_GROUP);
		expect(switcher, `no hiddenSwitcher claims groupControl ${NATIVE_PROBE_GROUP}`).toMatchObject({
			type: "hiddenSwitcher",
			groupControl: NATIVE_PROBE_GROUP,
			valueOptions: {
				sourceType: "PropertyValueBool",
				sourceForm: "LootMan.esp|F9A",
				propertyName: "IsNativePluginMissing",
			},
		});

		// Every warning row is gated on that one group, so the block is invisible
		// while the plugin is loaded.
		for (const key of WARNING_KEYS) {
			const row = systemRows.find((entry) => entry.text === key);
			expect(row, `config.json does not render ${key}`).toBeDefined();
			expect(row!.groupCondition, `${key} is not gated on group ${NATIVE_PROBE_GROUP}`).toBe(NATIVE_PROBE_GROUP);
		}

		// The three-state install status row must stay untouched by the new gate.
		for (const key of ["$PAGE_SYSTEM_STATUS_NOT_INSTALLED", "$PAGE_SYSTEM_STATUS_INSTALLED", "$PAGE_SYSTEM_STATUS_UNINSTALLED"]) {
			const row = systemRows.find((entry) => entry.text === key);
			expect(row, `config.json lost ${key}`).toBeDefined();
			expect(row!.groupCondition).not.toBe(NATIVE_PROBE_GROUP);
		}
	});

	it("ships the warning in all thirteen translation files with a real de and ja wording", () => {
		for (const file of TRANSLATION_FILES) {
			const raw = fs.readFileSync(path.resolve(file));
			expect(raw[0], `${file} is missing a UTF-16LE BOM`).toBe(0xff);
			expect(raw[1], `${file} is missing a UTF-16LE BOM`).toBe(0xfe);

			const text = raw.toString("utf16le").replace(/^﻿/, "");
			for (const segment of text.split("\r\n")) {
				expect(segment.includes("\n"), `${file} has a bare LF line ending`).toBe(false);
			}
			for (const key of WARNING_KEYS) {
				expect(translationValue(text, key), `${file} has no value for ${key}`).not.toBe("");
			}
		}

		const english = readUtf16LeWorkspaceFile("packaging/resources/lootman/common/Interface/Translations/LootMan_en.txt");
		const german = readUtf16LeWorkspaceFile("packaging/resources/lootman/common/Interface/Translations/LootMan_de.txt");
		const japanese = readUtf16LeWorkspaceFile("packaging/resources/lootman/common/Interface/Translations/LootMan_ja.txt");
		const japaneseOverride = readUtf16LeWorkspaceFile("packaging/resources/lootman/ja/Interface/Translations/LootMan_en.txt");

		for (const key of WARNING_KEYS) {
			const englishValue = translationValue(english, key);
			const japaneseValue = translationValue(japanese, key);
			expect(translationValue(german, key), `${key} is not localized in LootMan_de.txt`).not.toBe(englishValue);
			expect(japaneseValue, `${key} is not localized in LootMan_ja.txt`).not.toBe(englishValue);
			expect(translationValue(japaneseOverride, key), `${key} in ja/LootMan_en.txt must track LootMan_ja.txt`).toBe(japaneseValue);
		}

		// The warning has to name the actual cause; a generic "something is wrong"
		// row would send users back into another reinstall loop.
		const missingText = [
			translationValue(english, "$PAGE_SYSTEM_NATIVE_PLUGIN_MISSING"),
			translationValue(english, "$PAGE_SYSTEM_NATIVE_PLUGIN_MISSING_TEXT"),
			translationValue(english, "$PAGE_SYSTEM_NATIVE_PLUGIN_MISSING_FIX"),
		].join(" ");
		expect(missingText).toContain("1.11.240");
		expect(missingText).toContain("lootman.dll");
		expect(missingText).toContain("Address Library");
		expect(missingText).toContain("LootMan.log");
	});

	it("preflights F4SE.GetPluginVersion through the one required-symbol table", () => {
		const compilePapyrus = readWorkspaceFile("packaging/scripts/compile-papyrus.ts");

		// The symbol has to live in REQUIRED_PAPYRUS_SYMBOLS itself, not merely
		// somewhere in the file: that table is what the preflight reads.
		const tableStart = compilePapyrus.indexOf("const REQUIRED_PAPYRUS_SYMBOLS");
		expect(tableStart, "REQUIRED_PAPYRUS_SYMBOLS is gone").toBeGreaterThanOrEqual(0);
		const table = compilePapyrus.slice(tableStart, compilePapyrus.indexOf("\n];", tableStart));
		expect(table).toContain(
			'{ scriptFile: "F4SE.psc", needle: "GetPluginVersion", label: "F4SE.GetPluginVersion" },',
		);

		// F4SE.psc ships only in Data/Scripts/Source/F4SE, which reaches the
		// compiler as the staged overlay. The single preflight covers it because
		// the overlay dir is one of the search dirs it is handed.
		expect(compilePapyrus).toContain(
			"const importSearchDirs = buildPapyrusImportSearchDirs(sourceDir, overlayDir, config.papyrusImportDirs);",
		);
		expect(compilePapyrus).toContain("verifyPapyrusImportSymbols(importSearchDirs);");
		expect(compilePapyrus).toContain("return [\n\t\tsourceDir,\n\t\t...buildPapyrusPpjImportDirs(overlayDir, importDirs)");

		// One preflight only: a second parallel checker drifts out of sync with it.
		expect(compilePapyrus, "the duplicate overlay preflight is back").not.toContain("verifyPapyrusF4SEOverlaySymbols");
	});

	it("documents the warning and its fix in the user guide", () => {
		const guide = readWorkspaceFile("docs/user-guide.md");
		const systemSection = guide.slice(guide.indexOf("### System"), guide.indexOf("### Hotkeys"));
		const troubleshooting = guide.slice(guide.indexOf("## Troubleshooting"), guide.indexOf("## Uninstalling"));

		expect(systemSection).toContain("Native Plugin");
		expect(troubleshooting).toContain("Native Plugin");
		expect(troubleshooting).toContain("1.11.240");
		expect(troubleshooting).toContain("Data/F4SE/Plugins/LootMan.log");
	});
});
