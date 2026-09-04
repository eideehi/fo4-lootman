import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

function readUtf16LeWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file)).toString("utf16le").replace(/^﻿/, "");
}

function translationValue(source: string, key: string): string {
	const line = source.split(/\r?\n/).find((entry) => entry.startsWith(`${key}\t`));
	expect(line, `missing translation key ${key}`).toBeDefined();
	return line!.slice(key.length + 1);
}

const LABEL_KEY = "$PAGE_LOOTING_WORKER_ADVANCED_FILTER_EQUIPMENT_ALWAYS_LOOTING_CLOTHING";
const HELP_KEY = `${LABEL_KEY}_HELP`;

describe("always loot clothing option policy", () => {
	const config = JSON.parse(
		readWorkspaceFile("packaging/resources/lootman/common/MCM/Config/LootMan/config.json"),
	) as {
		pages: Array<{ content?: Array<{ id?: string; text?: string; type?: string; help?: string; valueOptions?: Record<string, unknown> }> }>;
	};
	const controls = config.pages.flatMap((page) => page.content ?? []);
	const propertiesScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Properties.psc");
	const configScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Config.psc");
	const patchScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Patch.psc");
	const propertiesHeader = readWorkspaceFile("commonlibf4-plugin/src/properties.h");
	const propertiesSource = readWorkspaceFile("commonlibf4-plugin/src/properties.cpp");
	const internalHeader = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_internal.h");
	const nearbyLooting = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_nearby_looting.cpp");
	const validationSource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_validation.cpp");
	const englishTranslations = readUtf16LeWorkspaceFile("packaging/resources/lootman/en/Interface/Translations/LootMan_en.txt");
	const japaneseEnglishTranslations = readUtf16LeWorkspaceFile("packaging/resources/lootman/ja/Interface/Translations/LootMan_en.txt");
	const japaneseTranslations = readUtf16LeWorkspaceFile("packaging/resources/lootman/ja/Interface/Translations/LootMan_ja.txt");

	it("adds an Equipment MCM switcher bound to the AlwaysLootingClothing property, after explosives", () => {
		const clothing = controls.find((item) => item.id === "AlwaysLootingClothing");
		expect(clothing).toMatchObject({
			text: LABEL_KEY,
			type: "switcher",
			help: HELP_KEY,
			valueOptions: {
				sourceType: "PropertyValueBool",
				sourceForm: "LootMan.esp|F9A",
				propertyName: "AlwaysLootingClothing",
			},
		});
		const explosivesIndex = controls.findIndex((item) => item.id === "AlwaysLootingExplosives");
		const clothingIndex = controls.findIndex((item) => item.id === "AlwaysLootingClothing");
		expect(explosivesIndex).toBeGreaterThanOrEqual(0);
		expect(clothingIndex).toBe(explosivesIndex + 1);
	});

	it("localizes the option label and help in all three translation files", () => {
		for (const key of [LABEL_KEY, HELP_KEY]) {
			const english = translationValue(englishTranslations, key);
			const japanese = translationValue(japaneseTranslations, key);

			expect(english, `${key} has no English text`).not.toBe("");
			expect(japanese, `${key} is not localized in ja/LootMan_ja.txt`).not.toBe(english);
			// This test owns the ja LootMan_en override: the Japanese ESP is played with the game
			// language on English, so the override must carry the Japanese text, not the source text.
			expect(
				translationValue(japaneseEnglishTranslations, key),
				`${key} in ja/LootMan_en.txt must match ja/LootMan_ja.txt`,
			).toBe(japanese);
		}
	});

	it("declares the persisted Papyrus property defaulting off and wires Config get/set", () => {
		expect(propertiesScript).toContain("bool property AlwaysLootingClothing = false auto hidden");
		expect(configScript).toMatch(/ElseIf \(id == "AlwaysLootingClothing"\)\s*\n\s*Return properties\.AlwaysLootingClothing/);
		expect(configScript).toMatch(/ElseIf \(id == "AlwaysLootingClothing"\)\s*\n\s*properties\.AlwaysLootingClothing = value/);
	});

	it("does not force-enable the option via a Patch migration (default off reaches existing saves)", () => {
		expect(patchScript).not.toContain("AlwaysLootingClothing");
	});

	it("exposes the option to native and captures it in the per-pass snapshot", () => {
		expect(propertiesHeader).toContain("always_looting_clothing");
		expect(propertiesSource).toContain('propertyName = "AlwaysLootingClothing";');
		expect(propertiesSource).toContain("updates[always_looting_clothing] = GetBoolProperty(propertyName);");
		expect(internalHeader).toContain("bool alwaysLootingClothing = false;");
		expect(nearbyLooting).toContain(
			"s.alwaysLootingClothing = properties::GetBool(properties::always_looting_clothing);",
		);
	});

	it("gates the ARMO clothing exception on the option flag", () => {
		const helperStart = validationSource.indexOf("bool IsLegendaryOnlyExceptionArmor(");
		const helperEnd = validationSource.indexOf("bool IsLootableInventoryItem(", helperStart);
		expect(helperStart).toBeGreaterThanOrEqual(0);
		expect(helperEnd).toBeGreaterThan(helperStart);
		const helper = validationSource.slice(helperStart, helperEnd);
		// exception only when the option is on, and still ARMO-only against the include key.
		expect(helper).toContain("alwaysLootingClothing");
		expect(helper).toContain("properties::always_looting_clothing");
		expect(helper).toContain("ENUM_FORM_ID::kARMO");
		expect(helper).toContain("injection_data::include_legendary_only_exception");
	});
});
