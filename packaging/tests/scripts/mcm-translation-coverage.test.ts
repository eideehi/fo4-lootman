import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

const sharedTranslationsDir = path.resolve("packaging/resources/lootman/common/Interface/Translations");
const englishSourcePath = path.resolve("packaging/resources/lootman/en/Interface/Translations/LootMan_en.txt");
const japaneseSourcePath = path.resolve("packaging/resources/lootman/ja/Interface/Translations/LootMan_ja.txt");
const japaneseEnglishOverridePath = path.resolve("packaging/resources/lootman/ja/Interface/Translations/LootMan_en.txt");

const targetLanguageCodes = ["en", "fr", "it", "de", "es", "pl", "ptbr", "ru", "cn", "ja"] as const;
const englishFallbackLanguageCodes = ["fr", "it", "es", "pl", "ptbr", "ru", "cn"] as const;

// Localized files: everything else either is English or is a byte copy of one of these.
const localizedLanguageCodes = ["de", "ja"] as const;

interface TranslationFile {
	hasUtf16LeBom: boolean;
	hasCrlfOnlyLineEndings: boolean;
	table: Map<string, string>;
}

function readTranslationFile(filePath: string): TranslationFile {
	const buffer = fs.readFileSync(filePath);
	const hasUtf16LeBom = buffer.length >= 2 && buffer[0] === 0xff && buffer[1] === 0xfe;
	const text = buffer.toString("utf16le").replace(/^﻿/, "");
	// Every line terminator must be CRLF: no bare LF may survive the CRLF split.
	const hasCrlfOnlyLineEndings = text.split("\r\n").every((segment) => !segment.includes("\n"));
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

	return { hasUtf16LeBom, hasCrlfOnlyLineEndings, table };
}

function sharedTranslationPath(lang: string): string {
	return path.join(sharedTranslationsDir, `LootMan_${lang}.txt`);
}

function placeholders(value: string): string[] {
	return [...value.matchAll(/\{[^}]+\}/g)].map((match) => match[0]!).sort();
}

// Every shipped translation file: the ten shared ones plus the three per-plugin-language
// copies that the FOMOD installs over them.
const shippedTranslationPaths: readonly string[] = [
	...targetLanguageCodes.map((lang) => sharedTranslationPath(lang)),
	englishSourcePath,
	japaneseEnglishOverridePath,
	japaneseSourcePath,
];

// A value that is byte-identical to the English one is only acceptable when it carries no
// English prose to translate. Two Latin words in a row is the signal that it does; single
// tokens ("Trace", "LootMan", "Stimpak"), hyphenated names ("Nuka-Cola"), rules ("===="),
// and placeholder templates ("{name} [{value}]") are language neutral and stay as they are.
const LATIN_WORD_PAIR = /[A-Za-z]+\s+[A-Za-z]+/;

// R4 reference scan. Keys are referenced only as quoted string literals: config/keybinds
// "text"/"help" fields, Papyrus message and label keys, and native ResolveText("$...") calls.
// Verified: nothing in the tree builds a key by concatenation, so a literal scan is exact.
const referenceFilePaths = [
	"packaging/resources/lootman/common/MCM/Config/LootMan/config.json",
	"packaging/resources/lootman/common/MCM/Config/LootMan/keybinds.json",
];
const referenceSearchRoots: ReadonlyArray<{ dir: string; pattern: RegExp }> = [
	{ dir: "papyrus/Scripts/Source/User/LTMN2", pattern: /\.psc$/i },
	{ dir: "commonlibf4-plugin/src", pattern: /\.(cpp|h)$/i },
	{ dir: "packaging/scripts", pattern: /\.ts$/i },
];

function collectFiles(dir: string, pattern: RegExp, found: string[] = []): string[] {
	for (const entry of fs.readdirSync(path.resolve(dir), { withFileTypes: true })) {
		const entryPath = path.join(dir, entry.name);
		if (entry.isDirectory()) {
			collectFiles(entryPath, pattern, found);
		} else if (pattern.test(entry.name)) {
			found.push(entryPath);
		}
	}
	return found;
}

function readReferenceSources(): string {
	const files = [
		...referenceFilePaths,
		...referenceSearchRoots.flatMap((root) => collectFiles(root.dir, root.pattern)),
	];
	return files.map((file) => fs.readFileSync(path.resolve(file), "utf8")).join("\n");
}

function isReferenced(sources: string, key: string): boolean {
	const escaped = key.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	return new RegExp(`["'\`]${escaped}["'\`]`).test(sources);
}

describe("MCM translation coverage", () => {
	it("ships shared translation files for every supported Fallout 4 language code", () => {
		const fileNames = fs.readdirSync(sharedTranslationsDir).sort();

		expect(fileNames).toEqual(targetLanguageCodes.map((lang) => `LootMan_${lang}.txt`).sort());
		expect(fs.existsSync(japaneseEnglishOverridePath)).toBe(true);
	});

	it("keeps every shipped translation file on one key order, CRLF, and matching placeholders", () => {
		const english = readTranslationFile(sharedTranslationPath("en"));
		const englishKeys = [...english.table.keys()];

		for (const filePath of shippedTranslationPaths) {
			const translation = readTranslationFile(filePath);
			expect(translation.hasUtf16LeBom, `${filePath} must have a UTF-16 LE BOM`).toBe(true);
			expect(translation.hasCrlfOnlyLineEndings, `${filePath} has a bare LF line ending`).toBe(true);
			expect([...translation.table.keys()], `${filePath} key order`).toEqual(englishKeys);

			const placeholderDrift = englishKeys.filter((key) =>
				placeholders(translation.table.get(key) ?? "").join("|") !== placeholders(english.table.get(key) ?? "").join("|")
			);
			expect(placeholderDrift, `${filePath} placeholder drift`).toEqual([]);
		}
	});

	it("uses English text for non-German non-Japanese fallback files", () => {
		const english = fs.readFileSync(sharedTranslationPath("en"));
		expect(english.equals(fs.readFileSync(englishSourcePath)), englishSourcePath).toBe(true);

		for (const lang of englishFallbackLanguageCodes) {
			expect(fs.readFileSync(sharedTranslationPath(lang)).equals(english), `LootMan_${lang}.txt`).toBe(true);
		}
	});

	it("serves the Japanese plugin the same Japanese text under both file names", () => {
		// The ja plugin ships LootMan_en.txt as well, because the Japanese ESP is still loaded
		// with the game language set to English; both names must resolve to the ja source.
		const japaneseSource = readTranslationFile(japaneseSourcePath);
		const sharedJapanese = readTranslationFile(sharedTranslationPath("ja"));
		const japaneseEnglishOverride = readTranslationFile(japaneseEnglishOverridePath);

		expect(sharedJapanese.table, sharedTranslationPath("ja")).toEqual(japaneseSource.table);
		expect(japaneseEnglishOverride.table, japaneseEnglishOverridePath).toEqual(japaneseSource.table);
		expect(
			fs.readFileSync(japaneseEnglishOverridePath).equals(fs.readFileSync(japaneseSourcePath)),
			`${japaneseEnglishOverridePath} must be byte-identical to ${japaneseSourcePath}`,
		).toBe(true);
		expect(
			fs.readFileSync(sharedTranslationPath("ja")).equals(fs.readFileSync(japaneseSourcePath)),
			`${sharedTranslationPath("ja")} must be byte-identical to ${japaneseSourcePath}`,
		).toBe(true);
	});

	it("leaves no English prose sitting in a localized file", () => {
		const english = readTranslationFile(sharedTranslationPath("en"));

		for (const lang of localizedLanguageCodes) {
			const localized = readTranslationFile(sharedTranslationPath(lang));
			const untranslated = [...english.table.keys()].filter((key) => {
				const value = localized.table.get(key) ?? "";
				return value === english.table.get(key) && LATIN_WORD_PAIR.test(value);
			});

			expect(untranslated, `LootMan_${lang}.txt still holds the English wording for these keys`).toEqual([]);
		}
	});

	it("references every shipped translation key from the mod's own sources", () => {
		const english = readTranslationFile(sharedTranslationPath("en"));
		const sources = readReferenceSources();

		const orphans = [...english.table.keys()].filter((key) => !isReferenced(sources, key));

		expect(orphans, "translation keys no config, script, or native source refers to").toEqual([]);
	});
});
