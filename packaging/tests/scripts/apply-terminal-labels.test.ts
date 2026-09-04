import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { applyTerminalLabels, parseArgs, walkTopLevelGroups, parseTerminalLabelRows } from "../../scripts/apply-terminal-labels.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

// The fixtures below are synthetic plugins: a TES4 record, a TERM group, and one unrelated group
// so the group walk has more than one group to traverse. No real Fallout 4 plugin is touched.

const recordHeaderSize = 24;
const groupHeaderSize = 24;
const compressedRecordFlag = 0x00040000;
const localizedStringsFlag = 0x00000080;

interface FixtureItem {
	text: string;
	itid?: number;
}

interface FixtureRecord {
	edid: string;
	items: FixtureItem[];
	compressed?: boolean;
}

interface FixtureOptions {
	/** Sets the TES4 localized flag and stores string IDs in ITXT, the way a localized plugin does. */
	localized?: boolean;
}

interface XmlRow {
	edid: string;
	id: number;
	source: string;
	dest: string;
	rec?: string;
}

function subrecord(signature: string, payload: Buffer): Buffer {
	const header = Buffer.alloc(6);
	header.write(signature, 0, "latin1");
	header.writeUInt16LE(payload.length, 4);
	return Buffer.concat([header, payload]);
}

function zstring(value: string): Buffer {
	return Buffer.concat([Buffer.from(value, "utf8"), Buffer.from([0])]);
}

function uint16(value: number): Buffer {
	const buffer = Buffer.alloc(2);
	buffer.writeUInt16LE(value);
	return buffer;
}

/** ITXT payload of a localized plugin: a 4-byte string ID into the .strings files, not text. */
function stringId(value: number): Buffer {
	const buffer = Buffer.alloc(4);
	buffer.writeUInt32LE(value);
	return buffer;
}

function record(signature: string, formId: number, data: Buffer, flags = 0): Buffer {
	const header = Buffer.alloc(recordHeaderSize);
	header.write(signature, 0, "latin1");
	header.writeUInt32LE(data.length, 4);
	header.writeUInt32LE(flags, 8);
	header.writeUInt32LE(formId, 12);
	return Buffer.concat([header, data]);
}

function terminalRecord(fixture: FixtureRecord, formId: number, options: FixtureOptions = {}): Buffer {
	const data = Buffer.concat([
		subrecord("EDID", zstring(fixture.edid)),
		subrecord("FULL", zstring(`${fixture.edid} Terminal`)),
		...fixture.items.flatMap((item, index) => [
			// A menu item is ITXT, then ANAM, then ITID, so the ITID trails the text it names.
			// Every string ID here is below 0x01000000, so its high byte is 0 and the payload still
			// looks NUL-terminated: exactly the case the TES4 flag has to catch.
			subrecord("ITXT", options.localized === true ? stringId(0x00000100 + index) : zstring(item.text)),
			subrecord("ANAM", Buffer.alloc(4)),
			subrecord("ITID", uint16(item.itid ?? index + 1)),
		]),
	]);
	return record("TERM", formId, data, fixture.compressed === true ? compressedRecordFlag : 0);
}

function group(label: string, records: Buffer[]): Buffer {
	const body = Buffer.concat(records);
	const header = Buffer.alloc(groupHeaderSize);
	header.write("GRUP", 0, "latin1");
	header.writeUInt32LE(groupHeaderSize + body.length, 4);
	header.write(label, 8, "latin1");
	return Buffer.concat([header, body]);
}

function buildPlugin(fixtures: FixtureRecord[], options: FixtureOptions = {}): Buffer {
	const header = Buffer.concat([subrecord("HEDR", Buffer.alloc(12)), subrecord("CNAM", zstring("lootman-test"))]);
	const keyword = record("KYWD", 0x0f000001, subrecord("EDID", zstring("LTMN_UnrelatedKeyword")));
	return Buffer.concat([
		record("TES4", 0, header, options.localized === true ? localizedStringsFlag : 0),
		group("KYWD", [keyword]),
		group("TERM", fixtures.map((fixture, index) => terminalRecord(fixture, 0x0f000100 + index, options))),
		group("MISC", [record("MISC", 0x0f000200, subrecord("EDID", zstring("LTMN_UnrelatedMisc")))]),
	]);
}

function writePlugin(dir: string, name: string, fixtures: FixtureRecord[], options: FixtureOptions = {}): string {
	const file = path.join(dir, name);
	fs.outputFileSync(file, buildPlugin(fixtures, options));
	return file;
}

function writeTranslation(dir: string, name: string, rows: XmlRow[]): string {
	const body = rows
		.map((row) => [
			"    <String List=\"0\">",
			`      <EDID>${row.edid}</EDID>`,
			`      <REC id="${row.id}">${row.rec ?? "TERM:ITXT"}</REC>`,
			`      <Source>${row.source}</Source>`,
			`      <Dest>${row.dest}</Dest>`,
			"    </String>",
		].join("\n"))
		.join("\n");
	// xTranslator writes UTF-8 with a BOM.
	const xml = `\uFEFF<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<SSTXMLRessources>
  <Params>
    <Addon>LootMan.esp</Addon>
  </Params>
  <Content>
${body}
  </Content>
</SSTXMLRessources>
`;
	const file = path.join(dir, name);
	fs.outputFileSync(file, xml, "utf8");
	return file;
}

interface ParsedItem {
	itid: number;
	text: string;
	payload: Buffer;
}

interface ParsedRecord {
	edid: string;
	bytes: Buffer;
	dataSize: number;
	items: ParsedItem[];
}

/** Independent reader used only by the tests, so assertions do not lean on the tool's own parser. */
function readTerminalRecords(buffer: Buffer): ParsedRecord[] {
	const records: ParsedRecord[] = [];
	let groupOffset = recordHeaderSize + buffer.readUInt32LE(4);

	while (groupOffset < buffer.length) {
		const groupSize = buffer.readUInt32LE(groupOffset + 4);
		const groupEnd = groupOffset + groupSize;
		if (buffer.toString("latin1", groupOffset + 8, groupOffset + 12) === "TERM") {
			let recordOffset = groupOffset + groupHeaderSize;
			while (recordOffset < groupEnd) {
				const dataSize = buffer.readUInt32LE(recordOffset + 4);
				const dataStart = recordOffset + recordHeaderSize;
				const dataEnd = dataStart + dataSize;
				let edid = "";
				let pending: Buffer = Buffer.alloc(0);
				const items: ParsedItem[] = [];
				let offset = dataStart;

				while (offset < dataEnd) {
					const signature = buffer.toString("latin1", offset, offset + 4);
					const payloadStart = offset + 6;
					const payloadEnd = payloadStart + buffer.readUInt16LE(offset + 4);
					if (signature === "EDID") {
						edid = buffer.toString("utf8", payloadStart, payloadEnd - 1);
					}
					if (signature === "ITXT") {
						pending = Buffer.from(buffer.subarray(payloadStart, payloadEnd));
					}
					if (signature === "ITID") {
						items.push({
							itid: buffer.readUInt16LE(payloadStart),
							text: pending.toString("utf8", 0, pending.length - 1),
							payload: pending,
						});
					}
					offset = payloadEnd;
				}

				records.push({ edid, bytes: Buffer.from(buffer.subarray(recordOffset, dataEnd)), dataSize, items });
				recordOffset = dataEnd;
			}
		}
		groupOffset = groupEnd;
	}

	return records;
}

function findRecord(buffer: Buffer, edid: string): ParsedRecord {
	const found = readTerminalRecords(buffer).find((candidate) => candidate.edid === edid);
	if (found === undefined) {
		throw new Error(`Fixture record not found: ${edid}`);
	}
	return found;
}

function termGroupSize(buffer: Buffer): number {
	let offset = recordHeaderSize + buffer.readUInt32LE(4);
	while (offset < buffer.length) {
		const size = buffer.readUInt32LE(offset + 4);
		if (buffer.toString("latin1", offset + 8, offset + 12) === "TERM") {
			return size;
		}
		offset += size;
	}
	throw new Error("Fixture has no TERM group");
}

const configRoot: FixtureRecord = {
	edid: "LTMN_TERM_ConfigRoot",
	items: [{ text: "General Settings" }, { text: "Object Looting Filters" }, { text: "Log Level" }],
};

const configGeneral: FixtureRecord = {
	edid: "LTMN_TERM_ConfigGeneral",
	items: [{ text: "Toggle deliver to player" }, { text: "Back" }],
};

describe("apply-terminal-labels", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("applies every translated label and rewrites the sizes that moved", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot, configGeneral]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "Basic Settings" },
			{ edid: "LTMN_TERM_ConfigRoot", id: 2, source: "Log Level", dest: "Logging Verbosity Level" },
			{ edid: "LTMN_TERM_ConfigGeneral", id: 1, source: "Back", dest: "Return" },
			{ edid: "LTMN_LootManWorkshop", id: 0, source: "LootMan", dest: "LootMan", rec: "CONT:FULL" },
		]);
		const before = fs.readFileSync(plugin);

		const result = applyTerminalLabels(plugin, xml);

		expect(result.changes).toEqual([
			{ edid: "LTMN_TERM_ConfigRoot", itid: 1, before: "General Settings", after: "Basic Settings" },
			{ edid: "LTMN_TERM_ConfigRoot", itid: 3, before: "Log Level", after: "Logging Verbosity Level" },
			{ edid: "LTMN_TERM_ConfigGeneral", itid: 2, before: "Back", after: "Return" },
		]);
		expect(result.changed).toBe(3);
		expect(result.unchanged).toBe(0);
		expect(result.written).toBe(true);
		expect(result.outputPath).toBe(plugin);

		const after = fs.readFileSync(plugin);
		expect(findRecord(after, "LTMN_TERM_ConfigRoot").items.map((item) => item.text)).toEqual([
			"Basic Settings",
			"Object Looting Filters",
			"Logging Verbosity Level",
		]);
		expect(findRecord(after, "LTMN_TERM_ConfigGeneral").items.map((item) => item.text)).toEqual([
			"Toggle deliver to player",
			"Return",
		]);

		// Every size field that covers a resized label has to move by the same total delta.
		const rootDelta = "Basic Settings".length - "General Settings".length + "Logging Verbosity Level".length - "Log Level".length;
		const generalDelta = "Return".length - "Back".length;
		expect(findRecord(after, "LTMN_TERM_ConfigRoot").dataSize).toBe(findRecord(before, "LTMN_TERM_ConfigRoot").dataSize + rootDelta);
		expect(findRecord(after, "LTMN_TERM_ConfigGeneral").dataSize).toBe(findRecord(before, "LTMN_TERM_ConfigGeneral").dataSize + generalDelta);
		expect(termGroupSize(after)).toBe(termGroupSize(before) + rootDelta + generalDelta);
		expect(after.length).toBe(before.length + rootDelta + generalDelta);
	});

	it("keeps the group walk landing exactly on the end of the patched file", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot, configGeneral]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 1, source: "Object Looting Filters", dest: "Filters" },
			{ edid: "LTMN_TERM_ConfigGeneral", id: 0, source: "Toggle deliver to player", dest: "Toggle deliver to the player character" },
		]);

		applyTerminalLabels(plugin, xml);

		const after = fs.readFileSync(plugin);
		const groups = walkTopLevelGroups(after, "patched fixture");
		expect(groups.map((entry) => entry.label)).toEqual(["KYWD", "TERM", "MISC"]);
		const walkedEnd = groups.reduce((end, entry) => end + entry.size, recordHeaderSize + after.readUInt32LE(4));
		expect(walkedEnd).toBe(after.length);
	});

	it("grows and shrinks labels in the same run", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "A much longer general settings label" },
			{ edid: "LTMN_TERM_ConfigRoot", id: 1, source: "Object Looting Filters", dest: "Filters" },
		]);
		const before = fs.readFileSync(plugin);

		const result = applyTerminalLabels(plugin, xml);
		expect(result.changed).toBe(2);

		const after = fs.readFileSync(plugin);
		const patched = findRecord(after, "LTMN_TERM_ConfigRoot");
		expect(patched.items.map((item) => item.text)).toEqual([
			"A much longer general settings label",
			"Filters",
			"Log Level",
		]);
		expect(patched.items.map((item) => item.payload.length)).toEqual([37, 8, 10]);
		const delta = "A much longer general settings label".length - "General Settings".length + "Filters".length - "Object Looting Filters".length;
		expect(after.length).toBe(before.length + delta);
		expect(() => walkTopLevelGroups(after, "patched fixture")).not.toThrow();
	});

	it("round-trips multi-byte UTF-8 labels byte-exactly", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
			{ edid: "LTMN_TERM_ConfigRoot", id: 1, source: "Object Looting Filters", dest: "オブジェクト収集フィルター" },
		]);

		const result = applyTerminalLabels(plugin, xml);
		expect(result.changes.map((change) => change.after)).toEqual(["基本設定", "オブジェクト収集フィルター"]);

		const after = fs.readFileSync(plugin);
		const items = findRecord(after, "LTMN_TERM_ConfigRoot").items;
		expect(items[0].payload.equals(Buffer.concat([Buffer.from("基本設定", "utf8"), Buffer.from([0])]))).toBe(true);
		expect(items[1].payload.equals(Buffer.concat([Buffer.from("オブジェクト収集フィルター", "utf8"), Buffer.from([0])]))).toBe(true);
		expect(items[0].payload.length).toBe(13);
		expect(items.map((item) => item.text)).toEqual(["基本設定", "オブジェクト収集フィルター", "Log Level"]);
	});

	it("leaves a record the translation does not name byte-identical", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot, configGeneral]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
		]);
		const before = findRecord(fs.readFileSync(plugin), "LTMN_TERM_ConfigGeneral");

		applyTerminalLabels(plugin, xml);

		const after = findRecord(fs.readFileSync(plugin), "LTMN_TERM_ConfigGeneral");
		expect(after.bytes.equals(before.bytes)).toBe(true);
	});

	it("restricts patching to the requested EDIDs", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot, configGeneral]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "Basic Settings" },
			{ edid: "LTMN_TERM_ConfigGeneral", id: 1, source: "Back", dest: "Return" },
		]);
		const before = findRecord(fs.readFileSync(plugin), "LTMN_TERM_ConfigGeneral");

		const result = applyTerminalLabels(plugin, xml, { edids: ["LTMN_TERM_ConfigRoot"] });

		expect(result.changes).toEqual([
			{ edid: "LTMN_TERM_ConfigRoot", itid: 1, before: "General Settings", after: "Basic Settings" },
		]);
		expect(findRecord(fs.readFileSync(plugin), "LTMN_TERM_ConfigGeneral").bytes.equals(before.bytes)).toBe(true);
	});

	it("writes to a separate output path and leaves the input untouched", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const output = path.join(root, "out", "Patched.esp");
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 2, source: "Log Level", dest: "ログレベル" },
		]);
		const before = fs.readFileSync(plugin);

		const result = applyTerminalLabels(plugin, xml, { outputPath: output });

		expect(result.outputPath).toBe(output);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
		expect(findRecord(fs.readFileSync(output), "LTMN_TERM_ConfigRoot").items[2].text).toBe("ログレベル");
	});

	it("reports changes without writing anything in dry-run mode", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const output = path.join(root, "out", "Patched.esp");
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
		]);
		const before = fs.readFileSync(plugin);

		const result = applyTerminalLabels(plugin, xml, { dryRun: true, outputPath: output });

		expect(result.changes).toEqual([
			{ edid: "LTMN_TERM_ConfigRoot", itid: 1, before: "General Settings", after: "基本設定" },
		]);
		expect(result.written).toBe(false);
		expect(fs.existsSync(output)).toBe(false);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("reports rows whose Dest already matches as no change", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "General Settings" },
			{ edid: "LTMN_TERM_ConfigRoot", id: 1, source: "Object Looting Filters", dest: "Object Looting Filters" },
		]);
		const before = fs.readFileSync(plugin);

		const result = applyTerminalLabels(plugin, xml);

		expect(result.changes).toEqual([]);
		expect(result.changed).toBe(0);
		expect(result.unchanged).toBe(2);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("throws when the translation names an ITID the record does not contain", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 9, source: "Missing", dest: "存在しない" },
		]);
		const before = fs.readFileSync(plugin);

		expect(() => applyTerminalLabels(plugin, xml)).toThrow("Terminal record LTMN_TERM_ConfigRoot does not contain ITID 10");
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("throws when a row index does not line up with the ITID it claims", () => {
		const root = createTempDir();
		dirs.push(root);
		// ITIDs 1, 2, 4 break the id-plus-one premise for the third item.
		const plugin = writePlugin(root, "Fixture.esp", [{
			edid: "LTMN_TERM_ConfigRoot",
			items: [{ text: "General Settings" }, { text: "Object Looting Filters" }, { text: "Log Level", itid: 4 }],
		}]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 3, source: "Log Level", dest: "ログレベル" },
		]);

		expect(() => applyTerminalLabels(plugin, xml)).toThrow(/item 3 is ITID missing, not 4/);
	});

	it("throws when the translation names a record the plugin does not have", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_Absent", id: 0, source: "Missing", dest: "存在しない" },
		]);

		expect(() => applyTerminalLabels(plugin, xml)).toThrow(/names terminal record LTMN_TERM_Absent/);
	});

	it("refuses a compressed record instead of guessing at its layout", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [{ ...configRoot, compressed: true }]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
		]);
		const before = fs.readFileSync(plugin);

		expect(() => applyTerminalLabels(plugin, xml)).toThrow(/compressed record/);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("throws when a size field stops the group walk landing on the end of the file", () => {
		const root = createTempDir();
		dirs.push(root);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
		]);

		// A TERM group size two bytes short leaves the walk pointing into the middle of the file.
		const shortGroup = path.join(root, "ShortGroup.esp");
		const shortBuffer = buildPlugin([configRoot]);
		const termGroupStart = shortBuffer.indexOf(Buffer.from("TERM", "latin1")) - 8;
		shortBuffer.writeUInt32LE(shortBuffer.readUInt32LE(termGroupStart + 4) - 2, termGroupStart + 4);
		fs.outputFileSync(shortGroup, shortBuffer);
		expect(() => applyTerminalLabels(shortGroup, xml)).toThrow(/expected GRUP at offset/);

		// A TES4 size past the last group skips every group and overshoots the end of the file.
		const longHeader = path.join(root, "LongHeader.esp");
		const longBuffer = buildPlugin([configRoot]);
		longBuffer.writeUInt32LE(longBuffer.length, 4);
		fs.outputFileSync(longHeader, longBuffer);
		expect(() => applyTerminalLabels(longHeader, xml)).toThrow(/group walk ended at offset/);
	});

	it("refuses a localized plugin whose ITXT holds string IDs instead of text", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot], { localized: true });
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
		]);
		const before = fs.readFileSync(plugin);

		// The string IDs end in a zero byte, so a NUL-termination heuristic alone would accept them
		// and overwrite the reference to the .strings entry with literal text.
		for (const item of findRecord(before, "LTMN_TERM_ConfigRoot").items) {
			expect(item.payload.length).toBe(4);
			expect(item.payload[item.payload.length - 1]).toBe(0);
		}

		expect(() => applyTerminalLabels(plugin, xml)).toThrow(/flagged as localized/);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("refuses a plugin with duplicate terminal EDIDs instead of guessing which record wins", () => {
		const root = createTempDir();
		dirs.push(root);
		// Two TERM records share an EDID: the patch path would take one and the verify path the
		// other, so the run has to stop before anything is written.
		const plugin = writePlugin(root, "Fixture.esp", [
			configRoot,
			{ edid: "LTMN_TERM_ConfigRoot", items: [{ text: "General Settings" }, { text: "Object Looting Filters" }, { text: "Log Level" }] },
		]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
		]);
		const before = fs.readFileSync(plugin);

		expect(() => applyTerminalLabels(plugin, xml)).toThrow(/more than one TERM record with EDID LTMN_TERM_ConfigRoot/);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("refuses a plugin whose labels match neither the Source nor the Dest of the translation", () => {
		const root = createTempDir();
		dirs.push(root);
		// This plugin was patched by an older export, so its labels are neither the English the rows
		// expect to replace nor the Japanese they would write.
		const plugin = writePlugin(root, "Fixture.esp", [{
			edid: "LTMN_TERM_ConfigRoot",
			items: [{ text: "基本設定" }, { text: "オブジェクト収集フィルター" }, { text: "ログレベル" }],
		}]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "全体設定" },
		]);
		const before = fs.readFileSync(plugin);

		expect(() => applyTerminalLabels(plugin, xml)).toThrow(/holds "基本設定", which is neither the translation Source "General Settings" nor its Dest "全体設定"/);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("accepts a row whose plugin text still matches the Source and patches it to the Dest", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
			{ edid: "LTMN_TERM_ConfigRoot", id: 1, source: "Object Looting Filters", dest: "Object Looting Filters" },
		]);

		const result = applyTerminalLabels(plugin, xml);

		expect(result.changed).toBe(1);
		expect(result.unchanged).toBe(1);
		expect(findRecord(fs.readFileSync(plugin), "LTMN_TERM_ConfigRoot").items[0].text).toBe("基本設定");
	});

	it("rejects a Dest carrying an embedded NUL that the game would read as a shorter label", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "Bad&#0;Tail" },
		]);
		const before = fs.readFileSync(plugin);

		expect(() => applyTerminalLabels(plugin, xml)).toThrow(/NUL character in Dest: LTMN_TERM_ConfigRoot id=0/);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("leaves an existing output file untouched when a run fails validation", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const output = path.join(root, "out", "Patched.esp");
		fs.outputFileSync(output, "previous output");
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
			{ edid: "LTMN_TERM_ConfigRoot", id: 1, source: "Wrong Source", dest: "フィルター" },
		]);

		expect(() => applyTerminalLabels(plugin, xml, { outputPath: output })).toThrow(/neither the translation Source/);

		expect(fs.readFileSync(output, "utf8")).toBe("previous output");
		expect(fs.existsSync(`${output}.tmp`)).toBe(false);
	});

	it("rolls the staging file back when the patched bytes cannot be moved into place", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		// A directory at the output path lets the staged write and its verification succeed, then
		// fails the move, which is the only window in which a direct write would have destroyed it.
		const output = path.join(root, "Patched.esp");
		fs.outputFileSync(path.join(output, "sentinel.txt"), "keep me");
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
		]);
		const before = fs.readFileSync(plugin);

		// The failing syscall has to be the rename of the staging file: a run that failed while
		// opening the output path itself would mean the patched bytes were still going straight
		// there.
		expect(() => applyTerminalLabels(plugin, xml, { outputPath: output })).toThrow(/rename/);
		expect(() => applyTerminalLabels(plugin, xml, { outputPath: output })).toThrow(/Patched\.esp\.tmp/);

		expect(fs.existsSync(`${output}.tmp`)).toBe(false);
		expect(fs.readFileSync(path.join(output, "sentinel.txt"), "utf8")).toBe("keep me");
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("refuses to write when a stale staging file is already in the way", () => {
		const root = createTempDir();
		dirs.push(root);
		const plugin = writePlugin(root, "Fixture.esp", [configRoot]);
		const stale = `${plugin}.tmp`;
		fs.outputFileSync(stale, "left over by a crashed run");
		const xml = writeTranslation(root, "Fixture.xml", [
			{ edid: "LTMN_TERM_ConfigRoot", id: 0, source: "General Settings", dest: "基本設定" },
		]);
		const before = fs.readFileSync(plugin);

		expect(() => applyTerminalLabels(plugin, xml)).toThrow(/staging file .* already exists/);

		// The staging file is never assumed to be ours, so a refused run must not delete it either.
		expect(fs.readFileSync(stale, "utf8")).toBe("left over by a crashed run");
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("reports the committed Japanese plugin as fully patched already", () => {
		// Real-data regression guard: the shipped ja plugin was patched from this export, so every
		// row has to line up and nothing may be rewritten.
		const plugin = path.resolve("packaging/resources/lootman/ja/LootMan.esp");
		const xml = path.resolve("translation/Lootman_en_ja.xml");
		const before = fs.readFileSync(plugin);

		const result = applyTerminalLabels(plugin, xml, { dryRun: true });

		expect(result.changed).toBe(0);
		expect(result.unchanged).toBe(39);
		expect(result.written).toBe(false);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);
	});

	it("reports the committed English plugin as the untranslated source of every row", () => {
		// Real-data regression guard for the en plugin: its labels are the Source column of the
		// export, so a dry run must accept every row as a pending translation and refuse none.
		const plugin = path.resolve("packaging/resources/lootman/en/LootMan.esp");
		const xml = path.resolve("translation/Lootman_en_ja.xml");
		const before = fs.readFileSync(plugin);

		// Rows whose Dest equals their Source (the Log Level names stay English) read as already
		// applied on the en plugin; every other row must be accepted as a pending translation.
		const rows = parseTerminalLabelRows(fs.readFileSync(xml, "utf8").replace(/^\uFEFF/, ""));
		const pending = rows.filter((row) => row.source !== row.dest).length;
		expect(rows.length).toBeGreaterThan(0);
		expect(pending).toBeGreaterThan(0);

		const result = applyTerminalLabels(plugin, xml, { dryRun: true });

		expect(result.changed).toBe(pending);
		expect(result.unchanged).toBe(rows.length - pending);
		expect(result.written).toBe(false);
		expect(fs.readFileSync(plugin).equals(before)).toBe(true);

		// The General Settings menu is the terminal the renamed options live on: every pending row
		// of that terminal must be reported against the plugin's own current label, so expectations
		// come from the export rather than from a copy of its text pinned here.
		const general = new Map(result.changes.filter((c) => c.edid === "LTMN_TERM_ConfigGeneral").map((c) => [c.itid, c]));
		const pendingGeneralRows = rows.filter((row) => row.edid === "LTMN_TERM_ConfigGeneral" && row.source !== row.dest);
		expect(pendingGeneralRows.length, "the General Settings terminal has no pending rows").toBeGreaterThan(0);
		for (const row of pendingGeneralRows) {
			expect(general.get(row.itid), `LTMN_TERM_ConfigGeneral row ${row.itid} was not reported as a change`).toMatchObject({
				before: row.source,
				after: row.dest,
			});
		}
	});

	it("parses CLI arguments", () => {
		expect(parseArgs(["Fixture.esp", "Fixture.xml"])).toEqual({
			pluginPath: "Fixture.esp",
			translationPath: "Fixture.xml",
			options: { edids: undefined, dryRun: false, outputPath: undefined },
		});
		expect(parseArgs(["Fixture.esp", "Fixture.xml", "--dry-run", "--edid=A", "--edid=B", "--out=Patched.esp"])).toEqual({
			pluginPath: "Fixture.esp",
			translationPath: "Fixture.xml",
			options: { edids: ["A", "B"], dryRun: true, outputPath: "Patched.esp" },
		});
		expect(() => parseArgs(["Fixture.esp"])).toThrow(/Usage: apply-terminal-labels/);
		expect(() => parseArgs(["Fixture.esp", "Fixture.xml", "--nope"])).toThrow("Invalid option: --nope");
	});
});
