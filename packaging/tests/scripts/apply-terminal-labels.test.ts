import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { applyTerminalLabels, parseArgs, walkTopLevelGroups } from "../../scripts/apply-terminal-labels.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

// The fixtures below are synthetic plugins: a TES4 record, a TERM group, and one unrelated group
// so the group walk has more than one group to traverse. No real Fallout 4 plugin is touched.

const recordHeaderSize = 24;
const groupHeaderSize = 24;
const compressedRecordFlag = 0x00040000;

interface FixtureItem {
	text: string;
	itid?: number;
}

interface FixtureRecord {
	edid: string;
	items: FixtureItem[];
	compressed?: boolean;
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

function record(signature: string, formId: number, data: Buffer, flags = 0): Buffer {
	const header = Buffer.alloc(recordHeaderSize);
	header.write(signature, 0, "latin1");
	header.writeUInt32LE(data.length, 4);
	header.writeUInt32LE(flags, 8);
	header.writeUInt32LE(formId, 12);
	return Buffer.concat([header, data]);
}

function terminalRecord(fixture: FixtureRecord, formId: number): Buffer {
	const data = Buffer.concat([
		subrecord("EDID", zstring(fixture.edid)),
		subrecord("FULL", zstring(`${fixture.edid} Terminal`)),
		...fixture.items.flatMap((item, index) => [
			// A menu item is ITXT, then ANAM, then ITID, so the ITID trails the text it names.
			subrecord("ITXT", zstring(item.text)),
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

function buildPlugin(fixtures: FixtureRecord[]): Buffer {
	const header = Buffer.concat([subrecord("HEDR", Buffer.alloc(12)), subrecord("CNAM", zstring("lootman-test"))]);
	const keyword = record("KYWD", 0x0f000001, subrecord("EDID", zstring("LTMN_UnrelatedKeyword")));
	return Buffer.concat([
		record("TES4", 0, header),
		group("KYWD", [keyword]),
		group("TERM", fixtures.map((fixture, index) => terminalRecord(fixture, 0x0f000100 + index))),
		group("MISC", [record("MISC", 0x0f000200, subrecord("EDID", zstring("LTMN_UnrelatedMisc")))]),
	]);
}

function writePlugin(dir: string, name: string, fixtures: FixtureRecord[]): string {
	const file = path.join(dir, name);
	fs.outputFileSync(file, buildPlugin(fixtures));
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
