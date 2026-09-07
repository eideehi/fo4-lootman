import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";
import { MESSAGE_RECORDS, parseMessageTexts, verifyMessageRecords } from "../../scripts/apply-message-records.js";

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
	id?: string;
	text?: string;
	groupControl?: number;
	groupCondition?: unknown;
	valueOptions?: { sourceType?: string; sourceForm?: string; propertyName?: string };
}

// MCM group numbers are page-local, but the same two mean the same thing on every
// page that uses them: 9 is "the plugin is missing", 10 is "the plugin is loaded".
const NATIVE_PROBE_GROUP = 9;
const NATIVE_PRESENT_GROUP = 10;

const WARNING_KEYS = [
	"$COMMON_NATIVE_PLUGIN_SECTION",
	"$COMMON_NATIVE_PLUGIN_MISSING",
	"$COMMON_NATIVE_PLUGIN_MISSING_TEXT",
	"$COMMON_NATIVE_PLUGIN_MISSING_FIX",
] as const;

// Every page whose controls do nothing without the DLL. The System page is
// deliberately absent: Force Install and Uninstall are what a stranded player
// needs to reach, so they stay visible in exactly this failure mode.
const GATED_PAGES = [
	"$PAGE_GENERAL_SETTINGS",
	"$PAGE_LOOTING_WORKER",
	"$PAGE_UTILITY",
	"$PAGE_HOTKEY",
] as const;

/** The group numbers a groupCondition reads, whatever shape it was written in. */
function conditionGroups(condition: unknown): number[] {
	if (typeof condition === "number") {
		return [condition];
	}
	if (condition === null || typeof condition !== "object") {
		return [];
	}
	return Object.values(condition as Record<string, number[]>).flat();
}

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
	) as { content?: McmContentRow[]; pages: Array<{ pageDisplayName?: string; content?: McmContentRow[] }> };
	const mainRows = mcmConfig.content ?? [];
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
			// The pair moves together, pessimistic side first. MCM cannot negate a
			// group, so the pages read IsNativePluginPresent to decide whether a
			// setting may be drawn at all; a window where neither flag is set would
			// show a page with no warning and no settings on it.
			"properties.IsNativePluginMissing = true",
			"properties.IsNativePluginPresent = false",
			'If (F4SE.GetPluginVersion("lootman") >= 0)',
			"properties.IsNativePluginMissing = false",
			"properties.IsNativePluginPresent = true",
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
			papyrusStatements(extractPapyrusEvent(systemScript, "OnInit")).slice(0, 7),
			"OnInit must resolve properties, arm the probe and register the load event before its first native call",
		).toEqual([
			"properties = LTMN2:Properties.GetInstance()",
			"CancelTimer(TIMER_NATIVE_PROBE)",
			"StartTimer(5, TIMER_NATIVE_PROBE)",
			"player = Game.GetPlayer()",
			'RegisterForRemoteEvent(player, "OnPlayerLoadGame")',
			// Load-bearing here for the same reason as the two lines above it: the
			// reconcile calls no LTMN2:LootMan native, so it still runs on the install
			// where lootman.dll never loaded. It is not native-free - Math.LogicalOr
			// is an F4SE native - but F4SE is a hard prerequisite and is loaded in
			// exactly that failure mode. See packed-subtype-mask-policy.
			"properties.RecomputePackedMasks()",
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
		expect(uninstall, "Uninstall leaves IsNativePluginPresent frozen at its last reading").toContain(
			"properties.IsNativePluginPresent = true",
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
			papyrusStatements(uninstall).slice(0, 11),
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
			"properties.IsNativePluginPresent = true",
		]);
	});

	it("stores the result in a save-safe hidden property that defaults to present", () => {
		expect(propertiesScript).toContain("bool property IsNativePluginMissing = false auto hidden");

		// The inverse defaults to the optimistic side for the same save-safety
		// reason, and because the gated pages read it: a false default would blank
		// every settings page for the few seconds before the first probe, and would
		// blank them permanently on any save whose probe never ran.
		expect(propertiesScript).toContain("bool property IsNativePluginPresent = true auto hidden");
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

		// Nothing on the System page may require the plugin to be loaded. This is
		// the page a stranded player has to reach: Force Install, Uninstall and the
		// status rows are its whole point, and gating any of them on the missing
		// DLL would lock the only recovery UI behind the failure it reports.
		for (const row of systemRows) {
			expect(
				conditionGroups(row.groupCondition),
				`the System page row ${row.text ?? row.type} must not require the plugin to be loaded`,
			).not.toContain(NATIVE_PRESENT_GROUP);
		}
	});

	it("shows the warning on the main page above the About section", () => {
		// The System page was the original home of the warning and almost nobody
		// opened it. The main page is what MCM draws the moment LootMan is picked
		// from the mod list, so the warning has to be the first thing on it.
		const switcher = mainRows.find((row) => row.groupControl === NATIVE_PROBE_GROUP);
		expect(switcher, "the main page never reads IsNativePluginMissing").toMatchObject({
			type: "hiddenSwitcher",
			valueOptions: { sourceType: "PropertyValueBool", sourceForm: "LootMan.esp|F9A", propertyName: "IsNativePluginMissing" },
		});

		const about = mainRows.findIndex((row) => row.text === "$PAGE_MAIN_ABOUT_SECTION");
		expect(about, "the main page lost its About section").toBeGreaterThanOrEqual(0);

		for (const key of WARNING_KEYS) {
			const index = mainRows.findIndex((row) => row.text === key);
			expect(index, `the main page does not render ${key}`).toBeGreaterThanOrEqual(0);
			expect(mainRows[index]!.groupCondition, `${key} is not gated on group ${NATIVE_PROBE_GROUP}`).toBe(NATIVE_PROBE_GROUP);
			expect(index, `${key} must be drawn above the About section`).toBeLessThan(about);
		}
	});

	it("replaces every settings page with the warning while the plugin is missing", () => {
		for (const name of GATED_PAGES) {
			const page = mcmConfig.pages.find((entry) => entry.pageDisplayName === name);
			expect(page, `config.json has no ${name} page`).toBeDefined();
			const rows = page!.content ?? [];

			for (const [group, property] of [
				[NATIVE_PROBE_GROUP, "IsNativePluginMissing"],
				[NATIVE_PRESENT_GROUP, "IsNativePluginPresent"],
			] as const) {
				expect(
					rows.find((row) => row.groupControl === group),
					`${name} has no hiddenSwitcher for ${property}`,
				).toMatchObject({
					type: "hiddenSwitcher",
					valueOptions: { sourceType: "PropertyValueBool", sourceForm: "LootMan.esp|F9A", propertyName: property },
				});
			}

			// MCM resolves a page's group flags from the controls it has already
			// walked, so a hiddenSwitcher declared below the rows that read it would
			// leave them reading a group that is still false.
			const lastSwitcher = rows.map((row) => row.type).lastIndexOf("hiddenSwitcher");
			const firstConditional = rows.findIndex((row) => row.groupCondition !== undefined);
			expect(lastSwitcher, `${name} declares a group after the first row that reads one`).toBeLessThan(firstConditional);

			for (const key of WARNING_KEYS) {
				const row = rows.find((entry) => entry.text === key);
				expect(row, `${name} does not render ${key}`).toBeDefined();
				expect(row!.groupCondition, `${key} on ${name} is not gated on group ${NATIVE_PROBE_GROUP}`).toBe(NATIVE_PROBE_GROUP);
			}

			// The gate itself: everything this page offers while LootMan is usable
			// also requires the plugin to be loaded. Without this the page keeps
			// answering to every click while nothing it sets can ever take effect,
			// which is the exact failure three Nexus reports came from.
			for (const row of rows) {
				const groups = conditionGroups(row.groupCondition);
				if (!groups.includes(1)) {
					continue;
				}
				expect(
					groups,
					`${name} draws ${row.text ?? row.id ?? row.type} without requiring the plugin to be loaded`,
				).toContain(NATIVE_PRESENT_GROUP);
			}

			// A row with no condition at all would survive the gate, so only the
			// invisible group carriers may be unconditional.
			for (const row of rows) {
				if (row.groupCondition === undefined) {
					expect(row.type, `${name} draws ${row.text ?? row.type} unconditionally`).toBe("hiddenSwitcher");
				}
			}
		}
	});

	it("interrupts with a plugin message that a missing DLL cannot silence", () => {
		const warn = extractPapyrusFunction(systemScript, "WarnNativePluginMissing");

		// Same rule as the probe: this runs in the session where lootman.dll never
		// loaded, so a single LootMan native would abort the frame that is trying to
		// report exactly that. Game.GetFormFromFile and Message.Show are vanilla.
		expect(warn, "the warning must not call a LootMan native").not.toContain("LTMN2:LootMan");
		expect(warn, "the warning must not log through the native logger").not.toContain("LogSystemEvent");
		expect(warn).toContain('Game.GetFormFromFile(0x000FBD, "LootMan.esp") As Message');
		expect(warn).toContain(".Show()");

		const statements = papyrusStatements(warn);
		expect(statements[0], "the message must be gated on the probe's own answer").toBe(
			"If (!properties || !properties.IsNativePluginMissing)",
		);
		// A missing record casts to none rather than throwing, so an older plugin
		// paired with a newer script stays quiet instead of erroring every load.
		expect(statements).toContain("If (warning)");

		// The probe timer is one-shot and re-armed once per load, so dispatching the
		// message from that branch is what makes it once per session. Moving it into
		// Looting() or OnTimer's default path would pop it repeatedly.
		expect(
			papyrusStatements(extractPapyrusEvent(systemScript, "OnTimer")).slice(0, 3),
			"the message must be dispatched by the probe timer, right after the probe",
		).toEqual(["If (aiTimerId == TIMER_NATIVE_PROBE)", "ProbeNativePlugin()", "WarnNativePluginMissing()"]);
	});

	it("ships that message record in both plugins, localized, at one fixed FormID", () => {
		const dictionary = readWorkspaceFile("translation/Lootman_en_ja.xml").replace(/^﻿/, "");
		const spec = MESSAGE_RECORDS.find((entry) => entry.edid === "LTMN_MSG_NativePluginMissing");
		expect(spec, "the message record is no longer in the tool's table").toBeDefined();

		// 0xFBD is the FormID the Papyrus lookup above hardcodes, and a shipped record
		// cannot move: every save that stored the old ID would lose the form.
		expect(spec!.objectId).toBe(0xfbd);
		expect(spec!.messageBox, "a corner notification would scroll away unread").toBe(true);

		// verifyMessageRecords is the same check the writer runs before it saves, so
		// the plugins in git are held to the record layout the tool guarantees.
		for (const [language, plugin] of [
			["en", "packaging/resources/lootman/en/LootMan.esp"],
			["ja", "packaging/resources/lootman/ja/LootMan.esp"],
		] as const) {
			const texts = parseMessageTexts(dictionary, language);
			verifyMessageRecords(fs.readFileSync(path.resolve(plugin)), plugin, texts);
		}

		const english = parseMessageTexts(dictionary, "en").get("LTMN_MSG_NativePluginMissing")!;
		const japanese = parseMessageTexts(dictionary, "ja").get("LTMN_MSG_NativePluginMissing")!;
		expect(japanese.desc, "the ja plugin ships the English wording").not.toBe(english.desc);
		expect(japanese.full, "the ja plugin ships the English title").not.toBe(english.full);

		// The popup has to name the same three things the MCM rows do, or it sends the
		// player back into the reinstall loop this feature exists to end.
		expect(english.desc).toContain("1.11.240");
		expect(english.desc).toContain("lootman.dll");
		expect(english.desc).toContain("Address Library");
		expect(japanese.desc).toContain("1.11.240");
		expect(japanese.desc).toContain("lootman.dll");
		expect(japanese.desc).toContain("Address Library");
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
			translationValue(english, "$COMMON_NATIVE_PLUGIN_MISSING"),
			translationValue(english, "$COMMON_NATIVE_PLUGIN_MISSING_TEXT"),
			translationValue(english, "$COMMON_NATIVE_PLUGIN_MISSING_FIX"),
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

		// Both surfaces are documented, so a player who saw only one of them can still
		// recognize what they are looking at.
		const configuration = guide.slice(guide.indexOf("## Configuration"), guide.indexOf("### General Settings"));
		expect(configuration).toContain("LootMan Is Not Working");
		expect(configuration).toContain("System");
		expect(troubleshooting).toContain("LootMan Is Not Working");
	});
});
