import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

const sharedTranslationsDir = path.resolve("packaging/resources/lootman/common/Interface/Translations");
const englishSourcePath = path.resolve("packaging/resources/lootman/en/Interface/Translations/LootMan_en.txt");
const japaneseSourcePath = path.resolve("packaging/resources/lootman/ja/Interface/Translations/LootMan_ja.txt");
const japaneseEnglishOverridePath = path.resolve("packaging/resources/lootman/ja/Interface/Translations/LootMan_en.txt");

const targetLanguageCodes = ["en", "fr", "it", "de", "es", "pl", "ptbr", "ru", "cn", "ja"] as const;
const englishFallbackLanguageCodes = ["fr", "it", "es", "pl", "ptbr", "ru", "cn"] as const;

interface TranslationFile {
	hasUtf16LeBom: boolean;
	table: Map<string, string>;
}

function readTranslationFile(filePath: string): TranslationFile {
	const buffer = fs.readFileSync(filePath);
	const hasUtf16LeBom = buffer.length >= 2 && buffer[0] === 0xff && buffer[1] === 0xfe;
	const text = buffer.toString("utf16le").replace(/^\uFEFF/, "");
	const table = new Map<string, string>();

	for (const [index, line] of text.split(/\r?\n/).entries()) {
		if (line.length === 0) {
			continue;
		}
		expect(line, `${filePath}:${index + 1} must start with a translation key`).toMatch(/^\$/);
		const parts = line.split("\t");
		expect(parts, `${filePath}:${index + 1} must contain exactly one tab delimiter`).toHaveLength(2);
		const [key, value] = parts as [string, string];
		expect(table.has(key), `${filePath}:${index + 1} duplicates ${key}`).toBe(false);
		expect(value, `${filePath}:${index + 1} must have a value`).not.toBe("");
		table.set(key, value);
	}

	return { hasUtf16LeBom, table };
}

function sharedTranslationPath(lang: string): string {
	return path.join(sharedTranslationsDir, `LootMan_${lang}.txt`);
}

function placeholders(value: string): string[] {
	return [...value.matchAll(/\{[^}]+\}/g)].map((match) => match[0]!).sort();
}

describe("MCM translation coverage", () => {
	it("ships shared translation files for every supported Fallout 4 language code", () => {
		const fileNames = fs.readdirSync(sharedTranslationsDir).sort();

		expect(fileNames).toEqual(targetLanguageCodes.map((lang) => `LootMan_${lang}.txt`).sort());
		expect(fs.existsSync(japaneseEnglishOverridePath)).toBe(true);
	});

	it("keeps every shared translation parseable with matching keys and placeholders", () => {
		const english = readTranslationFile(sharedTranslationPath("en"));
		const englishKeys = [...english.table.keys()];

		for (const lang of targetLanguageCodes) {
			const translation = readTranslationFile(sharedTranslationPath(lang));
			expect(translation.hasUtf16LeBom, `LootMan_${lang}.txt must have a UTF-16 LE BOM`).toBe(true);
			expect([...translation.table.keys()], `LootMan_${lang}.txt key order`).toEqual(englishKeys);

			const placeholderDrift = englishKeys.filter((key) =>
				placeholders(translation.table.get(key) ?? "").join("|") !== placeholders(english.table.get(key) ?? "").join("|")
			);
			expect(placeholderDrift, `LootMan_${lang}.txt placeholder drift`).toEqual([]);
		}
	});

	it("uses English text for non-German non-Japanese fallback files", () => {
		const english = fs.readFileSync(sharedTranslationPath("en"));
		expect(english.equals(fs.readFileSync(englishSourcePath))).toBe(true);

		for (const lang of englishFallbackLanguageCodes) {
			expect(fs.readFileSync(sharedTranslationPath(lang)).equals(english), `LootMan_${lang}.txt`).toBe(true);
		}
	});

	it("keeps Japanese shared text and the Japanese LootMan_en override localized", () => {
		const japaneseSource = readTranslationFile(japaneseSourcePath);
		const sharedJapanese = readTranslationFile(sharedTranslationPath("ja"));
		const japaneseEnglishOverride = readTranslationFile(japaneseEnglishOverridePath);

		expect(sharedJapanese.table).toEqual(japaneseSource.table);
		expect(japaneseEnglishOverride.table.get("$PAGE_HOTKEY_DUMP_NEARBY_OBJECT_DIAGNOSTICS")).toBe("周囲のオブジェクト診断を出力");
		expect(japaneseEnglishOverride.table.get("$PAGE_HOTKEY_DUMP_NEARBY_OBJECT_DIAGNOSTICS_HELP")).toBe(
			"周囲のオブジェクト情報をLootManのログへ詳しく書き出します。",
		);
	});

	it("keeps German localized and covers the current system-message placeholders", () => {
		const english = readTranslationFile(sharedTranslationPath("en"));
		const german = readTranslationFile(sharedTranslationPath("de"));
		let valuesMatchingEnglish = 0;

		for (const key of english.table.keys()) {
			if (english.table.get(key) === german.table.get(key)) {
				valuesMatchingEnglish++;
			}
		}

		expect(valuesMatchingEnglish).toBeLessThan(20);
		expect(german.table.get("$PAGE_HOTKEY_DUMP_NEARBY_OBJECT_DIAGNOSTICS_HELP")).toBe(
			"Schreibt detaillierte Diagnosen der Objekte in der Nähe in das LootMan-Log.",
		);
		expect(german.table.get("$LTMN_SYSTEM_MESSAGE_LINKED_TO_WORKSHOP_NAMED")).toBe(
			"[LootMan] Mit Werkstatt verbunden: {workshopName}.",
		);
		expect(german.table.get("$LTMN_SYSTEM_MESSAGE_UNLINKED_TO_WORKSHOP_NAMED")).toBe(
			"[LootMan] Von Werkstatt getrennt: {workshopName}.",
		);
	});
});
