import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { MESSAGE_RECORDS, applyMessageRecords, parseArgs, parseMessageTexts } from "../../scripts/apply-message-records.js";
import { walkTopLevelGroups } from "../../scripts/plugin-bytes.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

// The fixtures below are synthetic plugins: a TES4 record and the two groups MESG has to be filed
// between. No real Fallout 4 plugin is touched.

const recordHeaderSize = 24;
const groupHeaderSize = 24;
const subrecordHeaderSize = 6;
const localizedStringsFlag = 0x00000080;
const fileIndex = 0x01;
const spec = MESSAGE_RECORDS[0]!;

let tempDirs: string[] = [];

afterEach(() => {
	for (const dir of tempDirs) {
		removeTempDir(dir);
	}
	tempDirs = [];
});

function tempDir(): string {
	const dir = createTempDir("lootman-message-test-");
	tempDirs.push(dir);
	return dir;
}

function subrecord(signature: string, payload: Buffer): Buffer {
	const header = Buffer.alloc(subrecordHeaderSize);
	header.write(signature, 0, "latin1");
	header.writeUInt16LE(payload.length, 4);
	return Buffer.concat([header, payload]);
}

function zstring(value: string): Buffer {
	return Buffer.concat([Buffer.from(value, "utf8"), Buffer.from([0])]);
}

function messageBoxDnam(): Buffer {
	const buffer = Buffer.alloc(4);
	buffer.writeUInt32LE(1);
	return buffer;
}

function record(signature: string, formId: number, data: Buffer, flags = 0): Buffer {
	const header = Buffer.alloc(recordHeaderSize);
	header.write(signature, 0, "latin1");
	header.writeUInt32LE(data.length, 4);
	header.writeUInt32LE(flags, 8);
	header.writeUInt32LE(formId, 12);
	header.writeUInt16LE(131, 20);
	return Buffer.concat([header, data]);
}

function group(label: string, records: Buffer[]): Buffer {
	const body = Buffer.concat(records);
	const header = Buffer.alloc(groupHeaderSize);
	header.write("GRUP", 0, "latin1");
	header.writeUInt32LE(groupHeaderSize + body.length, 4);
	header.write(label, 8, "latin1");
	return Buffer.concat([header, body]);
}

interface PluginOptions {
	localized?: boolean;
	/** Group labels in file order, which the tool requires to be the canonical order. */
	groups?: string[];
	recordCount?: number;
}

function header(recordCount: number, localized: boolean): Buffer {
	const hedr = Buffer.alloc(12);
	hedr.writeFloatLE(1, 0);
	hedr.writeUInt32LE(recordCount, 4);
	hedr.writeUInt32LE(0x756, 8);
	return record("TES4", 0, Buffer.concat([subrecord("HEDR", hedr), subrecord("CNAM", zstring("lootman-test"))]), localized ? localizedStringsFlag : 0);
}

function buildPlugin(options: PluginOptions = {}): Buffer {
	const labels = options.groups ?? ["LCTN", "COBJ"];
	return Buffer.concat([
		header(options.recordCount ?? 2, options.localized === true),
		...labels.map((label, index) =>
			group(label, [record(label, ((fileIndex << 24) >>> 0) + 0xf00 + index, subrecord("EDID", zstring(`LTMN_${label}`)))]),
		),
	]);
}

function writePlugin(dir: string, name: string, options: PluginOptions = {}): string {
	const file = path.join(dir, name);
	fs.outputFileSync(file, buildPlugin(options));
	return file;
}

interface XmlRow {
	edid: string;
	rec: string;
	source: string;
	dest: string;
}

function defaultRows(): XmlRow[] {
	return [
		{ edid: spec.edid, rec: "MESG:FULL", source: "Not Working", dest: "動作していません" },
		{ edid: spec.edid, rec: "MESG:DESC", source: "The plugin is not loaded.", dest: "プラグインが読み込まれていません。" },
	];
}

function writeTranslation(dir: string, rows: XmlRow[] = defaultRows()): string {
	const body = rows
		.map((row) =>
			[
				"    <String List=\"0\">",
				`      <EDID>${row.edid}</EDID>`,
				`      <REC>${row.rec}</REC>`,
				`      <Source>${row.source}</Source>`,
				`      <Dest>${row.dest}</Dest>`,
				"    </String>",
			].join("\n"),
		)
		.join("\n");
	const file = path.join(dir, "Lootman_en_ja.xml");
	fs.outputFileSync(file, `﻿<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<SSTXMLRessources>\n  <Content>\n${body}\n  </Content>\n</SSTXMLRessources>\n`);
	return file;
}

interface ParsedMessage {
	edid: string;
	formId: number;
	full: string;
	desc: string;
	dnam: number;
}

/** Reads the MESG group back out of a plugin the way the game's record walk would. */
function readMessages(buffer: Buffer): ParsedMessage[] {
	const groups = walkTopLevelGroups(buffer, "fixture");
	const mesg = groups.find((entry) => entry.label === "MESG");
	if (mesg === undefined) {
		return [];
	}

	const messages: ParsedMessage[] = [];
	let offset = mesg.start + groupHeaderSize;
	const end = mesg.start + mesg.size;
	while (offset < end) {
		const dataSize = buffer.readUInt32LE(offset + 4);
		const parsed: ParsedMessage = { edid: "", formId: buffer.readUInt32LE(offset + 12), full: "", desc: "", dnam: 0 };
		let cursor = offset + recordHeaderSize;
		const dataEnd = cursor + dataSize;
		while (cursor < dataEnd) {
			const signature = buffer.toString("latin1", cursor, cursor + 4);
			const size = buffer.readUInt16LE(cursor + 4);
			const payload = buffer.subarray(cursor + subrecordHeaderSize, cursor + subrecordHeaderSize + size);
			if (signature === "EDID") {
				parsed.edid = payload.toString("utf8", 0, size - 1);
			} else if (signature === "FULL") {
				parsed.full = payload.toString("utf8", 0, size - 1);
			} else if (signature === "DESC") {
				parsed.desc = payload.toString("utf8", 0, size - 1);
			} else if (signature === "DNAM") {
				parsed.dnam = payload.readUInt32LE(0);
			}
			cursor += subrecordHeaderSize + size;
		}
		messages.push(parsed);
		offset = dataEnd;
	}
	return messages;
}

function readRecordCount(buffer: Buffer): number {
	return buffer.readUInt32LE(recordHeaderSize + subrecordHeaderSize + 4);
}

describe("applyMessageRecords", () => {
	it("creates the record with the English source text and the canonical layout", () => {
		const dir = tempDir();
		const plugin = writePlugin(dir, "LootMan.esp");
		const translation = writeTranslation(dir);

		const result = applyMessageRecords(plugin, translation, { language: "en" });
		expect(result.written).toBe(true);
		expect(result.changes).toHaveLength(1);

		const patched = fs.readFileSync(plugin);
		expect(readMessages(patched)).toEqual([
			{
				edid: spec.edid,
				formId: ((fileIndex << 24) >>> 0) + spec.objectId,
				full: "Not Working",
				desc: "The plugin is not loaded.",
				// DNAM bit 0 is "Message Box". Without it the game shows a corner
				// notification that scrolls away unread.
				dnam: 1,
			},
		]);

		// MESG sorts between LCTN and COBJ in the Creation Kit's group order, and the
		// record count in the header follows the record that was added.
		expect(walkTopLevelGroups(patched, "patched").map((entry) => entry.label)).toEqual(["LCTN", "MESG", "COBJ"]);
		expect(readRecordCount(patched)).toBe(3);
	});

	it("writes the Japanese dest text as UTF-8", () => {
		const dir = tempDir();
		const plugin = writePlugin(dir, "LootMan.esp");
		const translation = writeTranslation(dir);

		applyMessageRecords(plugin, translation, { language: "ja" });

		const messages = readMessages(fs.readFileSync(plugin));
		expect(messages[0]!.full).toBe("動作していません");
		expect(messages[0]!.desc).toBe("プラグインが読み込まれていません。");
	});

	it("is idempotent and rewrites the text when the dictionary changes", () => {
		const dir = tempDir();
		const plugin = writePlugin(dir, "LootMan.esp");
		const translation = writeTranslation(dir);

		applyMessageRecords(plugin, translation, { language: "en" });
		const first = fs.readFileSync(plugin);

		const second = applyMessageRecords(plugin, translation, { language: "en" });
		expect(second.written).toBe(false);
		expect(second.unchanged).toBe(1);
		expect(fs.readFileSync(plugin).equals(first)).toBe(true);

		const rows = defaultRows();
		rows[1]!.source = "A much longer explanation than the first one.";
		applyMessageRecords(plugin, writeTranslation(dir, rows), { language: "en" });

		const rewritten = fs.readFileSync(plugin);
		expect(readMessages(rewritten)[0]!.desc).toBe("A much longer explanation than the first one.");
		// The record was replaced, not appended, so the header count stays where the
		// first run left it and the group still holds exactly one record.
		expect(readRecordCount(rewritten)).toBe(3);
		expect(readMessages(rewritten)).toHaveLength(1);
	});

	it("leaves the plugin alone on a dry run", () => {
		const dir = tempDir();
		const plugin = writePlugin(dir, "LootMan.esp");
		const before = fs.readFileSync(plugin);

		const result = applyMessageRecords(plugin, writeTranslation(dir), { language: "en", dryRun: true });
		expect(result.written).toBe(false);
		expect(result.changes).toHaveLength(1);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("refuses a localized plugin", () => {
		const dir = tempDir();
		const plugin = writePlugin(dir, "LootMan.esp", { localized: true });

		expect(() => applyMessageRecords(plugin, writeTranslation(dir), { language: "en" })).toThrow(/flagged as localized/);
	});

	it("refuses a plugin whose groups are not in the canonical order", () => {
		const dir = tempDir();
		const plugin = writePlugin(dir, "LootMan.esp", { groups: ["COBJ", "LCTN"] });

		expect(() => applyMessageRecords(plugin, writeTranslation(dir), { language: "en" })).toThrow(/canonical order/);
	});

	it("refuses to drop a MESG record it does not own", () => {
		const dir = tempDir();
		const plugin = path.join(dir, "LootMan.esp");
		const foreign = group("MESG", [
			record("MESG", ((fileIndex << 24) >>> 0) + 0xfff, Buffer.concat([
				subrecord("EDID", zstring("SomeOtherMod_MSG")),
				subrecord("DESC", zstring("body")),
				subrecord("FULL", zstring("title")),
				subrecord("DNAM", messageBoxDnam()),
			])),
		]);
		const base = buildPlugin({ groups: ["LCTN"] });
		fs.outputFileSync(plugin, Buffer.concat([base, foreign]));

		expect(() => applyMessageRecords(plugin, writeTranslation(dir), { language: "en" })).toThrow(/does not own/);
	});

	it("refuses to move a record that already has a different FormID", () => {
		const dir = tempDir();
		const plugin = path.join(dir, "LootMan.esp");
		const moved = group("MESG", [
			record("MESG", ((fileIndex << 24) >>> 0) + 0xfa0, Buffer.concat([
				subrecord("EDID", zstring(spec.edid)),
				subrecord("DESC", zstring("body")),
				subrecord("FULL", zstring("title")),
				subrecord("DNAM", messageBoxDnam()),
			])),
		]);
		fs.outputFileSync(plugin, Buffer.concat([buildPlugin({ groups: ["LCTN"] }), moved]));

		// Every save that stored the old FormID would lose the form, so the tool has to
		// stop rather than renumber a shipped record.
		expect(() => applyMessageRecords(plugin, writeTranslation(dir), { language: "en" })).toThrow(/would break every save/);
	});

	it("refuses a dictionary that is missing a field or a language", () => {
		const dir = tempDir();
		const plugin = writePlugin(dir, "LootMan.esp");

		const onlyFull = writeTranslation(dir, [defaultRows()[0]!]);
		expect(() => applyMessageRecords(plugin, onlyFull, { language: "en" })).toThrow(/needs both MESG:FULL and MESG:DESC/);

		const rows = defaultRows();
		rows[1]!.dest = "";
		expect(() => applyMessageRecords(plugin, writeTranslation(dir, rows), { language: "ja" })).toThrow(/empty Dest/);
	});
});

describe("parseMessageTexts", () => {
	it("reads Source for en and Dest for ja", () => {
		const xml = fs.readFileSync(writeTranslation(tempDir()), "utf8").replace(/^﻿/, "");

		expect(parseMessageTexts(xml, "en").get(spec.edid)).toEqual({ full: "Not Working", desc: "The plugin is not loaded." });
		expect(parseMessageTexts(xml, "ja").get(spec.edid)).toEqual({ full: "動作していません", desc: "プラグインが読み込まれていません。" });
	});
});

describe("parseArgs", () => {
	it("requires both paths and an explicit language", () => {
		expect(parseArgs(["plugin.esp", "translation.xml", "--lang=ja", "--dry-run"])).toEqual({
			pluginPath: "plugin.esp",
			translationPath: "translation.xml",
			options: { language: "ja", dryRun: true, outputPath: undefined },
		});
		expect(() => parseArgs(["plugin.esp", "translation.xml"])).toThrow(/--lang=en\|ja/);
		expect(() => parseArgs(["plugin.esp", "--lang=en"])).toThrow(/Usage/);
	});
});
