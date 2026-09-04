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

function translationValue(source: string, key: string): string {
	const line = source.split(/\r?\n/).find((entry) => entry.startsWith(`${key}\t`));
	expect(line, `missing translation key ${key}`).toBeDefined();
	return line!.slice(key.length + 1);
}

function translationKeys(source: string): string[] {
	return source
		.split(/\r?\n/)
		.filter((line) => line.includes("\t"))
		.map((line) => line.slice(0, line.indexOf("\t")));
}

const FRAGMENT_DIR = "papyrus/Scripts/Source/User/LTMN2/Fragments/Terminals";

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

// The three renamed options plus the new workshop-mode pause switch.
const NEW_PROPERTIES = [
	"EnableCarryWeightLimit",
	"DisplayPickupMessage",
	"EnableLootingInSettlement",
	"PauseLootingInWorkshopMode",
];

const OLD_PAPYRUS_NAMES = ["IgnoreOverweight", "LootingWithoutLogs", "NotLootingFromSettlement"];

// The native enum keys terminal-label-table-policy derives from the fragment ids.
const NEW_NATIVE_KEYS = ["enable_carry_weight_limit", "display_pickup_message", "enable_looting_in_settlement"];

const OLD_NATIVE_TOKENS = [
	"ignore_overweight",
	"looting_without_logs",
	"not_looting_from_settlement",
	'"IgnoreOverweight"',
	'"LootingWithoutLogs"',
	'"NotLootingFromSettlement"',
];

const NEW_TRANSLATION_KEYS = [
	"$PAGE_GENERAL_SETTINGS_ENABLE_CARRY_WEIGHT_LIMIT",
	"$PAGE_GENERAL_SETTINGS_DISPLAY_PICKUP_MESSAGE",
	"$PAGE_GENERAL_SETTINGS_ENABLE_LOOTING_IN_SETTLEMENT",
	"$PAGE_GENERAL_SETTINGS_PAUSE_LOOTING_IN_WORKSHOP_MODE",
];

const OLD_TRANSLATION_KEYS = [
	"$PAGE_GENERAL_SETTINGS_IGNORE_OVERWEIGHT",
	"$PAGE_GENERAL_SETTINGS_LOOTING_WITHOUT_LOGS",
	"$PAGE_GENERAL_SETTINGS_NOT_LOOTING_FROM_SETTLEMENT",
];

const LEGACY_COMMENT = "; Legacy property kept so existing saves can migrate their setting.";

const PAUSE_GATE = 'If (!force && properties.PauseLootingInWorkshopMode && UI.IsMenuOpen("WorkshopMenu"))';

describe("option polarity policy", () => {
	const propertiesScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Properties.psc");
	const patchScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Patch.psc");
	const systemScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/System.psc");
	const mcmScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/MCM.psc");
	const configScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Config.psc");
	const lootManScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/LootMan.psc");
	const utilsScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Utils.psc");
	const generalFragment = readWorkspaceFile(`${FRAGMENT_DIR}/TERM_ConfigGeneral_FB8.psc`);

	const propertiesHeader = readWorkspaceFile("commonlibf4-plugin/src/properties.h");
	const propertiesSource = readWorkspaceFile("commonlibf4-plugin/src/properties.cpp");
	const nearbyLooting = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_nearby_looting.cpp");
	const inventoryTransfer = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_inventory_transfer.cpp");
	const validationSource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_validation.cpp");
	const notificationsSource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_notifications.cpp");
	const terminalLabels = readWorkspaceFile("commonlibf4-plugin/src/terminal_labels.cpp");

	const mcmConfig = JSON.parse(
		readWorkspaceFile("packaging/resources/lootman/common/MCM/Config/LootMan/config.json"),
	) as {
		pages: Array<{
			content?: Array<{
				id?: string;
				text?: string;
				type?: string;
				help?: string;
				valueOptions?: Record<string, unknown>;
			}>;
		}>;
	};

	it("declares the four positively named options and keeps the legacy declarations", () => {
		for (const name of NEW_PROPERTIES) {
			expect(propertiesScript, `missing declaration for ${name}`).toContain(
				`bool property ${name} = false auto hidden`,
			);
		}

		// The legacy booleans stay declared (and stay true-by-default) so a migrated
		// save can be read back after a downgrade; each carries the precedent comment.
		for (const legacy of OLD_PAPYRUS_NAMES) {
			expect(propertiesScript, `missing legacy declaration for ${legacy}`).toContain(
				`bool property ${legacy} = true auto hidden`,
			);
			expect(propertiesScript, `missing legacy comment above ${legacy}`).toMatch(
				new RegExp(
					`; Legacy property kept so existing saves can migrate their setting\\.\\s*\\r?\\n\\s*bool property ${legacy} = true auto hidden`,
				),
			);
		}
	});

	it("migrates stored values into the new properties behind a 3.3.0 version gate", () => {
		expect(systemScript).toContain("int MOD_VERSION = 30300 const");

		const patch = extractPapyrusFunction(systemScript, "Patch");
		expect(patch).toMatch(/If \(CurrentModVersion < 30300\)\s*\r?\n\s*LTMN2:Patch\.v3_3_0\(\)/);

		// Cumulative order: the new gate runs after the historical 3.1.0 gate.
		const gate3100 = patch.indexOf("If (CurrentModVersion < 30100)");
		const gate3300 = patch.indexOf("If (CurrentModVersion < 30300)");
		expect(gate3100, "missing the historical < 30100 gate").toBeGreaterThanOrEqual(0);
		expect(gate3300).toBeGreaterThan(gate3100);

		const migration = extractPapyrusFunction(patchScript, "v3_3_0");
		expect(migration).toContain("properties.EnableCarryWeightLimit = !properties.IgnoreOverweight");
		expect(migration).toContain("properties.DisplayPickupMessage = !properties.LootingWithoutLogs");
		expect(migration).toContain("properties.EnableLootingInSettlement = !properties.NotLootingFromSettlement");

		// The legacy values are read, never reset, so the step is idempotent and a
		// downgrade still sees the player's pre-3.3.0 choice.
		expect(migration).not.toContain("properties.IgnoreOverweight =");
		expect(migration).not.toContain("properties.LootingWithoutLogs =");
		expect(migration).not.toContain("properties.NotLootingFromSettlement =");
	});

	it("pauses only the unforced looting pass while the workshop menu is open", () => {
		const looting = extractPapyrusFunction(systemScript, "Looting");

		const gateIndex = looting.indexOf(PAUSE_GATE);
		expect(gateIndex, "missing the workshop-mode pause gate").toBeGreaterThanOrEqual(0);

		const blockEnd = looting.indexOf("EndIf", gateIndex);
		expect(blockEnd, "pause gate has no EndIf").toBeGreaterThan(gateIndex);
		expect(looting.slice(gateIndex, blockEnd)).toContain("Return");

		const enableIndex = looting.indexOf("If (!force && !properties.EnableLootMan)");
		const nativeIndex = looting.indexOf("LTMN2:LootMan.LootNearbyEnabledReferences");
		expect(enableIndex, "missing the EnableLootMan gate").toBeGreaterThanOrEqual(0);
		expect(nativeIndex, "missing the native loot pass call").toBeGreaterThanOrEqual(0);
		expect(gateIndex, "pause gate must follow the EnableLootMan gate").toBeGreaterThan(enableIndex);
		expect(gateIndex, "pause gate must precede the native loot pass").toBeLessThan(nativeIndex);
		const overweightIndex = looting.indexOf("If (properties.IsOverweight && properties.EnableCarryWeightLimit)");
		expect(overweightIndex, "missing the overweight gate").toBeGreaterThanOrEqual(0);
		expect(gateIndex, "pause gate must sit directly after the EnableLootMan gate, before the overweight gate").toBeLessThan(overweightIndex);

		// The timer pass stays unforced; the hotkey path (Looting(true)) skips the gate.
		const timer = extractPapyrusEvent(systemScript, "OnTimer");
		expect(timer).toContain("Looting()");
		expect(timer).not.toContain("Looting(true)");
	});

	it("reads the positively named options at every Papyrus gate", () => {
		const looting = extractPapyrusFunction(systemScript, "Looting");
		expect(looting).toContain("If (properties.IsOverweight && properties.EnableCarryWeightLimit)");
		expect(looting).toContain("If (properties.IsInSettlement && !properties.EnableLootingInSettlement)");

		const update = extractPapyrusFunction(systemScript, "Update");
		expect(update).toContain("If (properties.EnableCarryWeightLimit)");
		// The clearing branch must survive the flip or IsOverweight can stick true.
		expect(update).toContain("ElseIf (properties.IsOverweight)");

		const locationChange = extractPapyrusEvent(systemScript, "Actor\\.OnLocationChange");
		expect(locationChange).toContain("If (!properties.EnableLootingInSettlement)");

		const deliver = extractPapyrusFunction(systemScript, "DeliverLootManInventory");
		expect(deliver).toContain("!properties.DisplayPickupMessage");
	});

	it("leaves no old Papyrus option name outside Properties.psc and Patch.psc", () => {
		const surfaces: Array<[string, string]> = [
			["System.psc", systemScript],
			["MCM.psc", mcmScript],
			["Config.psc", configScript],
			["LootMan.psc", lootManScript],
			["Utils.psc", utilsScript],
			["TERM_ConfigGeneral_FB8.psc", generalFragment],
			["TERM_ConfigObjectFilter_FB9.psc", readWorkspaceFile(`${FRAGMENT_DIR}/TERM_ConfigObjectFilter_FB9.psc`)],
			["TERM_ConfigLogLevel_FBA.psc", readWorkspaceFile(`${FRAGMENT_DIR}/TERM_ConfigLogLevel_FBA.psc`)],
			["TERM_ConfigUtility_FBB.psc", readWorkspaceFile(`${FRAGMENT_DIR}/TERM_ConfigUtility_FBB.psc`)],
		];

		for (const [name, source] of surfaces) {
			for (const old of OLD_PAPYRUS_NAMES) {
				expect(source, `${name} still references ${old}`).not.toContain(old);
			}
		}

		// Positive controls so the negative assertions cannot pass on an empty read.
		expect(systemScript).toContain("EnableCarryWeightLimit");
		expect(configScript).toContain("DisplayPickupMessage");
	});

	it("flips the native readers onto the positively named keys", () => {
		for (const key of NEW_NATIVE_KEYS) {
			expect(propertiesHeader, `properties.h is missing ${key}`).toContain(key);
		}

		expect(propertiesSource).toContain('propertyName = "EnableCarryWeightLimit";');
		expect(propertiesSource).toContain("updates[enable_carry_weight_limit] = GetBoolProperty(propertyName);");
		expect(propertiesSource).toContain('propertyName = "DisplayPickupMessage";');
		expect(propertiesSource).toContain("updates[display_pickup_message] = GetBoolProperty(propertyName);");
		expect(propertiesSource).toContain('propertyName = "EnableLootingInSettlement";');
		expect(propertiesSource).toContain("updates[enable_looting_in_settlement] = GetBoolProperty(propertyName);");

		// Unresolved-cache defaults keep today's behavior: loot proceeds, no capacity
		// tracking, pickup messages stay silent.
		expect(nearbyLooting).toContain(
			"s.notLootingFromSettlement = !properties::GetBool(properties::enable_looting_in_settlement, true);",
		);
		const trackCapacity = "const bool trackCapacity = properties::GetBool(properties::enable_carry_weight_limit, false);";
		expect(nearbyLooting.split(trackCapacity).length - 1, "nearby looting should read trackCapacity twice").toBe(2);
		expect(inventoryTransfer).toContain(trackCapacity);
		expect(validationSource).toContain(": !properties::GetBool(properties::enable_looting_in_settlement, true);");
		expect(notificationsSource).toContain("if (!properties::GetBool(properties::display_pickup_message, false))");

		expect(terminalLabels).toContain(
			"{ LabelPage::kPrimary, 5, LabelKind::kBoolProperty, properties::enable_carry_weight_limit },",
		);
		expect(terminalLabels).toContain(
			"{ LabelPage::kPrimary, 7, LabelKind::kBoolProperty, properties::display_pickup_message },",
		);
		expect(terminalLabels).toContain(
			"{ LabelPage::kPrimary, 8, LabelKind::kBoolProperty, properties::enable_looting_in_settlement },",
		);
	});

	it("leaves no old option token anywhere under commonlibf4-plugin/src, comments included", () => {
		const dir = path.resolve("commonlibf4-plugin/src");
		const files = fs.readdirSync(dir).filter((name) => name.endsWith(".h") || name.endsWith(".cpp"));
		expect(files.length, "no native sources found").toBeGreaterThan(5);

		let newKeyOccurrences = 0;
		for (const name of files) {
			const source = fs.readFileSync(path.join(dir, name), "utf8");
			for (const token of OLD_NATIVE_TOKENS) {
				expect(source, `${name} still references ${token}`).not.toContain(token);
			}
			for (const key of NEW_NATIVE_KEYS) {
				newKeyOccurrences += source.split(key).length - 1;
			}
		}

		// Positive control: the renamed keys really are wired across the native tree.
		expect(newKeyOccurrences).toBeGreaterThanOrEqual(10);
	});

	it("binds the four options across the MCM config, the holotape facade, and the fragments", () => {
		const controls = mcmConfig.pages.flatMap((page) => page.content ?? []);
		for (const name of NEW_PROPERTIES) {
			const control = controls.find((item) => item.id === name);
			expect(control, `MCM config has no switcher for ${name}`).toBeDefined();
			expect(control).toMatchObject({
				type: "switcher",
				valueOptions: {
					sourceType: "PropertyValueBool",
					sourceForm: "LootMan.esp|F9A",
					propertyName: name,
				},
			});
		}

		// The pause switch sits directly after the auto-link switch in the same page.
		const generalPage = mcmConfig.pages.find((page) =>
			(page.content ?? []).some((item) => item.id === "AutomaticallyLinkAndUnlinkToWorkshop"),
		);
		expect(generalPage, "no page declares the auto-link switcher").toBeDefined();
		const generalContent = generalPage!.content ?? [];
		const autoLinkIndex = generalContent.findIndex((item) => item.id === "AutomaticallyLinkAndUnlinkToWorkshop");
		const pauseIndex = generalContent.findIndex((item) => item.id === "PauseLootingInWorkshopMode");
		expect(pauseIndex).toBe(autoLinkIndex + 1);

		// Every bound property name must exist as a declared Papyrus property.
		const bound = controls
			.map((item) => item.valueOptions as Record<string, unknown> | undefined)
			.filter(
				(opts): opts is Record<string, unknown> =>
					!!opts && opts.sourceType === "PropertyValueBool" && opts.sourceForm === "LootMan.esp|F9A",
			)
			.map((opts) => String(opts.propertyName));
		expect(bound.length).toBeGreaterThan(20);
		for (const name of bound) {
			expect(propertiesScript, `config.json binds undeclared property ${name}`).toMatch(
				new RegExp(`property ${name}\\b`),
			);
		}

		expect(extractPapyrusFunction(generalFragment, "Fragment_Terminal_05")).toContain(
			'Toggle("EnableCarryWeightLimit")',
		);
		expect(extractPapyrusFunction(generalFragment, "Fragment_Terminal_07")).toContain(
			'Toggle("DisplayPickupMessage")',
		);
		expect(extractPapyrusFunction(generalFragment, "Fragment_Terminal_08")).toContain(
			'Toggle("EnableLootingInSettlement")',
		);

		// GetLabelKey tolerates only comment lines between the ElseIf and the Return.
		const getLabelKey = extractPapyrusFunction(configScript, "GetLabelKey");
		const labelPairs: Array<[string, string]> = [
			["EnableCarryWeightLimit", "$PAGE_GENERAL_SETTINGS_ENABLE_CARRY_WEIGHT_LIMIT"],
			["DisplayPickupMessage", "$PAGE_GENERAL_SETTINGS_DISPLAY_PICKUP_MESSAGE"],
			["EnableLootingInSettlement", "$PAGE_GENERAL_SETTINGS_ENABLE_LOOTING_IN_SETTLEMENT"],
		];
		for (const [id, key] of labelPairs) {
			expect(getLabelKey, `GetLabelKey does not map ${id}`).toMatch(
				new RegExp(`\\(id == "${id}"\\)\\s*\\r?\\n(?:\\s*;[^\\n]*\\r?\\n)*\\s*Return "\\${key}"`),
			);
		}

		const readBool = extractPapyrusFunction(configScript, "ReadBool");
		const writeSettableBool = extractPapyrusFunction(configScript, "WriteSettableBool");
		for (const name of NEW_PROPERTIES) {
			expect(readBool, `ReadBool has no ${name} branch`).toMatch(
				new RegExp(`\\(id == "${name}"\\)\\s*\\r?\\n\\s*Return properties\\.${name}`),
			);
			expect(writeSettableBool, `WriteSettableBool has no ${name} branch`).toMatch(
				new RegExp(`\\(id == "${name}"\\)\\s*\\r?\\n\\s*properties\\.${name} = value`),
			);
		}

		expect(mcmScript).toContain('ElseIf (id == "EnableLootingInSettlement")');
	});

	it("renames the option keys in all thirteen translation files without breaking their shape", () => {
		const referenceKeys = translationKeys(
			readUtf16LeWorkspaceFile("packaging/resources/lootman/common/Interface/Translations/LootMan_en.txt"),
		);
		expect(referenceKeys.length).toBeGreaterThan(200);

		for (const file of TRANSLATION_FILES) {
			const raw = fs.readFileSync(path.resolve(file));
			expect(raw[0], `${file} is missing a UTF-16LE BOM`).toBe(0xff);
			expect(raw[1], `${file} is missing a UTF-16LE BOM`).toBe(0xfe);

			const text = raw.toString("utf16le").replace(/^﻿/, "");
			// Every line terminator must be CRLF: no LF survives the CRLF split.
			for (const segment of text.split("\r\n")) {
				expect(segment.includes("\n"), `${file} has a bare LF line ending`).toBe(false);
			}

			expect(translationKeys(text), `${file} key order drifted`).toEqual(referenceKeys);

			for (const key of NEW_TRANSLATION_KEYS) {
				expect(text, `${file} is missing ${key}`).toContain(`${key}\t`);
				expect(text, `${file} is missing ${key}_HELP`).toContain(`${key}_HELP\t`);
			}
			for (const key of OLD_TRANSLATION_KEYS) {
				expect(text, `${file} still ships ${key}`).not.toContain(key);
			}
		}

		const english = readUtf16LeWorkspaceFile("packaging/resources/lootman/common/Interface/Translations/LootMan_en.txt");
		const japanese = readUtf16LeWorkspaceFile("packaging/resources/lootman/common/Interface/Translations/LootMan_ja.txt");
		const german = readUtf16LeWorkspaceFile("packaging/resources/lootman/common/Interface/Translations/LootMan_de.txt");
		const japaneseOverride = readUtf16LeWorkspaceFile("packaging/resources/lootman/ja/Interface/Translations/LootMan_en.txt");

		// Each renamed option must carry real text in English and be genuinely translated in the
		// two localized files; the ja override has to track the ja source rather than drift.
		for (const key of NEW_TRANSLATION_KEYS.flatMap((base) => [base, `${base}_HELP`])) {
			const englishValue = translationValue(english, key);
			const japaneseValue = translationValue(japanese, key);

			expect(englishValue, `${key} has no English text`).not.toBe("");
			expect(japaneseValue, `${key} is not localized in LootMan_ja.txt`).not.toBe(englishValue);
			expect(translationValue(german, key), `${key} is not localized in LootMan_de.txt`).not.toBe(englishValue);
			expect(
				translationValue(japaneseOverride, key),
				`${key} in ja/LootMan_en.txt must match LootMan_ja.txt`,
			).toBe(japaneseValue);
		}
	});

	it("keeps the release version literals and the F4SE symbol guard in lockstep", () => {
		const pkg = JSON.parse(readWorkspaceFile("package.json")) as { version: string };
		const xmake = readWorkspaceFile("commonlibf4-plugin/xmake.lua");
		expect(xmake, "xmake.lua version must match package.json").toContain(`set_version("${pkg.version}")`);

		// The pause gate calls UI.IsMenuOpen, so a missing F4SE overlay must fail the
		// Papyrus preflight by name instead of as an opaque compile error.
		const compilePapyrus = readWorkspaceFile("packaging/scripts/compile-papyrus.ts");
		expect(compilePapyrus).toContain(
			'{ scriptFile: "UI.psc", needle: "IsMenuOpen", label: "UI.IsMenuOpen" },',
		);
	});

	it("documents the renamed options in the user guide and the README update policy", () => {
		const userGuide = readWorkspaceFile("docs/user-guide.md");
		const readme = readWorkspaceFile("README.md");

		for (const label of [
			"Enable Carry Weight Limit",
			"Display Pickup Messages",
			"Enable Looting In Settlements",
			"Pause Looting In Workshop Mode",
		]) {
			expect(userGuide, `user guide does not document ${label}`).toContain(label);
		}
		for (const label of [
			"Ignore Overweight",
			"Suppress Looting Pickup Messages",
			"Not Looting From Settlement",
			"Toggle ignore overweight",
			"Toggle silent looting",
			"Toggle no looting in settlement",
		]) {
			expect(userGuide, `user guide still names ${label}`).not.toContain(label);
		}

		expect(readme).toContain("3.3.0 Update Policy");
	});
});
