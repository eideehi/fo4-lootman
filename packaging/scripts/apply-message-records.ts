import fs from "fs-extra";
import path from "node:path";
import { isCliEntry } from "./config.js";
import {
	assertNotLocalized,
	compressedRecordFlag,
	groupHeaderSize,
	maxSubrecordSize,
	recordHeaderSize,
	subrecordHeaderSize,
	walkTopLevelGroups,
	writeVerifiedPlugin,
} from "./plugin-bytes.js";
import type { TopLevelGroup } from "./plugin-bytes.js";
import { parseTranslationStrings } from "./translation-xml.js";

// LootMan's MESG records carry the one message the mod has to be able to show while its own DLL is
// not loaded: Papyrus can call Message.Show without a single LootMan native, and the text is plugin
// data, so the en and ja plugins each carry their own wording. An xEdit script cannot author the
// Japanese side of that (its 8-bit string engine double-encodes anything outside Latin-1, see
// docs/workflows/xedit-scripting.md), so the record is written here, from the same xTranslator
// dictionary that drives the terminal labels.
//
// The whole MESG group is rebuilt from this table on every run, which makes the tool idempotent and
// makes the table - not the plugin - the description of what LootMan's messages are.

/** The Fallout 4 MESG layout, in the subrecord order every vanilla record uses. */
const messageGroupLabel = "MESG";
const messageBoxFlag = 0x00000001;
const recordFormVersion = 131;

export interface MessageRecordSpec {
	edid: string;
	/** Object ID only. Light plugins cannot hold one above 0xFFF. */
	objectId: number;
	/**
	 * DNAM "Message Box". A HUD notification scrolls past unread in the corner, which is how this
	 * failure went unreported for three Nexus threads; the interrupting box is the point.
	 */
	messageBox: boolean;
}

export const MESSAGE_RECORDS: readonly MessageRecordSpec[] = [
	{ edid: "LTMN_MSG_NativePluginMissing", objectId: 0xfbd, messageBox: true },
];

export type MessageLanguage = "en" | "ja";

export interface MessageText {
	full: string;
	desc: string;
}

export interface MessageRecordChange {
	edid: string;
	formId: number;
	before: MessageText | null;
	after: MessageText;
}

export interface ApplyMessageRecordsOptions {
	language: MessageLanguage;
	/** Compute and report the changes without writing anything. */
	dryRun?: boolean;
	/** Write the patched plugin here instead of over the input. */
	outputPath?: string;
}

export interface ApplyMessageRecordsResult {
	pluginPath: string;
	outputPath: string;
	dryRun: boolean;
	written: boolean;
	changes: MessageRecordChange[];
	unchanged: number;
}

/**
 * The canonical top-level group order, from the wbAddGroupOrder table in wbDefinitionsFO4.pas,
 * narrowed to the groups LootMan.esp holds plus MESG. A new group has to land in this order or the
 * plugin stops matching what the Creation Kit and xEdit write, and the walk below proves the
 * plugin's own groups are still a subsequence of it before anything is inserted.
 */
const groupOrder = ["CONT", "DOOR", "NPC_", "NOTE", "TERM", "CELL", "QUST", "LCTN", "MESG", "COBJ"];

interface ExistingMessageRecord {
	edid: string;
	formId: number;
	text: MessageText;
	/** DNAM, so the verify pass can prove a shipped record still interrupts. */
	flags: number;
}

/** Reads the MESG:FULL and MESG:DESC rows of the xTranslator dictionary for one language. */
export function parseMessageTexts(xml: string, language: MessageLanguage): Map<string, MessageText> {
	const texts = new Map<string, Partial<MessageText>>();

	for (const entry of parseTranslationStrings(xml)) {
		const field = /^MESG:(FULL|DESC)$/.exec(entry.rec);
		if (field === null) {
			continue;
		}
		if (entry.edid === null) {
			throw new Error(`Translation row for ${entry.rec} has no EDID:\n${entry.block}`);
		}
		// Source is the English plugin's text and Dest the Japanese one, exactly as for the
		// terminal labels, so one dictionary describes both plugins.
		const value = language === "en" ? entry.source : entry.dest;
		if (value === null) {
			throw new Error(`Translation row for ${entry.rec} has no ${language === "en" ? "Source" : "Dest"}: ${entry.edid}`);
		}
		if (value === "") {
			throw new Error(`Translation row for ${entry.rec} has an empty ${language === "en" ? "Source" : "Dest"}: ${entry.edid}`);
		}
		if (value.includes("\0")) {
			throw new Error(`Translation row for ${entry.rec} has a NUL character in its text: ${entry.edid}; the game would only read the text before it`);
		}

		const current = texts.get(entry.edid) ?? {};
		const key = field[1] === "FULL" ? "full" : "desc";
		if (current[key] !== undefined) {
			throw new Error(`Translation names ${entry.rec} for ${entry.edid} twice`);
		}
		texts.set(entry.edid, { ...current, [key]: value });
	}

	const complete = new Map<string, MessageText>();
	for (const [edid, text] of texts) {
		// DESC is the message body and the record definition marks it required; FULL is the title
		// the message box shows above it. A record missing either one would display blank.
		if (text.full === undefined || text.desc === undefined) {
			throw new Error(`Translation for ${edid} needs both MESG:FULL and MESG:DESC rows`);
		}
		complete.set(edid, { full: text.full, desc: text.desc });
	}
	return complete;
}

function subrecord(signature: string, payload: Buffer): Buffer {
	if (payload.length > maxSubrecordSize) {
		throw new Error(`${signature} payload is ${payload.length} bytes, over the subrecord limit`);
	}
	const header = Buffer.alloc(subrecordHeaderSize);
	header.write(signature, 0, "latin1");
	header.writeUInt16LE(payload.length, 4);
	return Buffer.concat([header, payload]);
}

function zstring(text: string): Buffer {
	return Buffer.concat([Buffer.from(text, "utf8"), Buffer.from([0])]);
}

function uint32(value: number): Buffer {
	const buffer = Buffer.alloc(4);
	buffer.writeUInt32LE(value);
	return buffer;
}

function buildMessageRecord(spec: MessageRecordSpec, text: MessageText, fileIndex: number): Buffer {
	if (spec.objectId > 0xfff) {
		throw new Error(`${spec.edid} has object ID 0x${spec.objectId.toString(16)}, which a light plugin cannot hold`);
	}

	// EDID, DESC, FULL, INAM, DNAM: the order 292 vanilla message-box records use. INAM is the
	// engine's leftover icon field and is always a null FormID.
	const data = Buffer.concat([
		subrecord("EDID", zstring(spec.edid)),
		subrecord("DESC", zstring(text.desc)),
		subrecord("FULL", zstring(text.full)),
		subrecord("INAM", uint32(0)),
		subrecord("DNAM", uint32(spec.messageBox ? messageBoxFlag : 0)),
	]);

	const header = Buffer.alloc(recordHeaderSize);
	header.write(messageGroupLabel, 0, "latin1");
	header.writeUInt32LE(data.length, 4);
	header.writeUInt32LE(0, 8);
	header.writeUInt32LE(((fileIndex << 24) >>> 0) + spec.objectId, 12);
	header.writeUInt32LE(0, 16);
	header.writeUInt16LE(recordFormVersion, 20);
	header.writeUInt16LE(0, 22);
	return Buffer.concat([header, data]);
}

function buildMessageGroup(records: Buffer[]): Buffer {
	const body = Buffer.concat(records);
	const header = Buffer.alloc(groupHeaderSize);
	header.write("GRUP", 0, "latin1");
	header.writeUInt32LE(groupHeaderSize + body.length, 4);
	header.write(messageGroupLabel, 8, "latin1");
	// Group type 0 (top level), and the stamp/version fields left at zero, which is what the last
	// groups added to this plugin carry.
	return Buffer.concat([header, body]);
}

function readSubrecords(buffer: Buffer, dataStart: number, dataEnd: number, label: string): Map<string, Buffer> {
	const found = new Map<string, Buffer>();
	let offset = dataStart;

	while (offset < dataEnd) {
		if (offset + subrecordHeaderSize > dataEnd) {
			throw new Error(`${label} has a truncated subrecord header at offset ${offset}`);
		}
		const signature = buffer.toString("latin1", offset, offset + 4);
		const size = buffer.readUInt16LE(offset + 4);
		const payloadEnd = offset + subrecordHeaderSize + size;
		if (payloadEnd > dataEnd) {
			throw new Error(`${label} has a subrecord ${signature} running past the record data at offset ${offset}`);
		}
		if (signature === "XXXX") {
			throw new Error(`${label} uses an XXXX size override, which is not supported`);
		}
		found.set(signature, buffer.subarray(offset + subrecordHeaderSize, payloadEnd));
		offset = payloadEnd;
	}

	return found;
}

function readZstring(payload: Buffer): string {
	if (payload.length === 0 || payload[payload.length - 1] !== 0) {
		throw new Error("expected NUL-terminated text");
	}
	return payload.toString("utf8", 0, payload.length - 1);
}

function collectMessageRecords(buffer: Buffer, group: TopLevelGroup, label: string): ExistingMessageRecord[] {
	const records: ExistingMessageRecord[] = [];
	let offset = group.start + groupHeaderSize;
	const groupEnd = group.start + group.size;

	while (offset < groupEnd) {
		if (offset + recordHeaderSize > groupEnd) {
			throw new Error(`${label} has a truncated record header at offset ${offset}`);
		}
		const signature = buffer.toString("latin1", offset, offset + 4);
		if (signature !== messageGroupLabel) {
			throw new Error(`${label} has a ${signature} record inside the MESG group at offset ${offset}`);
		}
		const dataSize = buffer.readUInt32LE(offset + 4);
		if ((buffer.readUInt32LE(offset + 8) & compressedRecordFlag) !== 0) {
			throw new Error(`${label} has a compressed MESG record at offset ${offset}; decompress it before applying messages`);
		}
		const dataStart = offset + recordHeaderSize;
		const dataEnd = dataStart + dataSize;
		if (dataEnd > groupEnd) {
			throw new Error(`${label} has a record at offset ${offset} running past its group`);
		}

		const subrecords = readSubrecords(buffer, dataStart, dataEnd, label);
		const edid = subrecords.get("EDID");
		const desc = subrecords.get("DESC");
		const full = subrecords.get("FULL");
		const dnam = subrecords.get("DNAM");
		if (edid === undefined || desc === undefined || full === undefined || dnam === undefined) {
			throw new Error(`${label} has a MESG record at offset ${offset} without EDID, DESC, FULL and DNAM`);
		}
		records.push({
			edid: readZstring(edid),
			formId: buffer.readUInt32LE(offset + 12),
			text: { full: readZstring(full), desc: readZstring(desc) },
			flags: dnam.readUInt32LE(0),
		});
		offset = dataEnd;
	}

	return records;
}

/**
 * The file index every record in this plugin carries. It follows from the master count, so reading
 * it off the plugin is the only way to build a new record that belongs to the same file - and a
 * plugin whose records disagree is not one this tool may write to.
 */
function readFileIndex(buffer: Buffer, groups: TopLevelGroup[], label: string): number {
	const indexes = new Set<number>();

	for (const group of groups) {
		if (group.label === "CELL") {
			// The CELL group nests its children in sub-groups, and the top-level record scan below
			// does not descend. Every other group is enough to answer the question.
			continue;
		}
		let offset = group.start + groupHeaderSize;
		const groupEnd = group.start + group.size;
		while (offset < groupEnd) {
			if (buffer.toString("latin1", offset, offset + 4) === "GRUP") {
				break;
			}
			indexes.add(buffer.readUInt32LE(offset + 12) >>> 24);
			offset += recordHeaderSize + buffer.readUInt32LE(offset + 4);
		}
	}

	if (indexes.size !== 1) {
		throw new Error(`${label} has records from ${indexes.size} file indexes (${[...indexes].join(", ")}); it is not a single-file plugin this tool can extend`);
	}
	return [...indexes][0]!;
}

export function applyMessageRecords(pluginPath: string, translationPath: string, options: ApplyMessageRecordsOptions): ApplyMessageRecordsResult {
	const outputPath = options.outputPath ?? pluginPath;
	const dryRun = options.dryRun === true;
	const original = fs.readFileSync(pluginPath);
	const texts = parseMessageTexts(fs.readFileSync(translationPath, "utf8").replace(/^﻿/, ""), options.language);

	console.log(`Applying ${options.language} message records to ${path.basename(pluginPath)}...`);

	const groups = walkTopLevelGroups(original, pluginPath);
	assertNotLocalized(original, pluginPath);

	const order = groups.map((group) => groupOrder.indexOf(group.label));
	const unknown = groups.filter((group) => !groupOrder.includes(group.label));
	if (unknown.length > 0) {
		throw new Error(`${pluginPath} holds ${unknown.map((group) => group.label).join(", ")}, which the canonical group order in this tool does not cover`);
	}
	if (order.some((position, index) => index > 0 && position <= order[index - 1]!)) {
		throw new Error(`${pluginPath} does not hold its groups in the canonical order (${groups.map((group) => group.label).join(", ")})`);
	}

	const fileIndex = readFileIndex(original, groups, pluginPath);
	const existingGroup = groups.find((group) => group.label === messageGroupLabel);
	const existing = existingGroup === undefined ? [] : collectMessageRecords(original, existingGroup, pluginPath);

	// The group is rebuilt from MESSAGE_RECORDS, so a record this table does not describe would be
	// dropped silently. Refuse instead: it is either a record that belongs in the table or a sign
	// that this plugin is not the one the table was written for.
	for (const record of existing) {
		if (!MESSAGE_RECORDS.some((spec) => spec.edid === record.edid)) {
			throw new Error(`${pluginPath} holds MESG record ${record.edid}, which this tool does not own`);
		}
	}

	const changes: MessageRecordChange[] = [];
	let unchanged = 0;
	const records = MESSAGE_RECORDS.map((spec) => {
		const text = texts.get(spec.edid);
		if (text === undefined) {
			throw new Error(`${path.basename(translationPath)} has no MESG:FULL and MESG:DESC rows for ${spec.edid}`);
		}
		const formId = ((fileIndex << 24) >>> 0) + spec.objectId;
		const before = existing.find((record) => record.edid === spec.edid);
		if (before !== undefined && before.formId !== formId) {
			throw new Error(`${pluginPath} holds ${spec.edid} as ${before.formId.toString(16).toUpperCase()}, not the ${formId.toString(16).toUpperCase()} this tool assigns; a moved FormID would break every save that stored the old one`);
		}

		if (before !== undefined && before.text.full === text.full && before.text.desc === text.desc) {
			unchanged += 1;
		} else {
			changes.push({ edid: spec.edid, formId, before: before?.text ?? null, after: text });
			console.log(`  ${spec.edid} ${before === undefined ? "created" : "updated"}: ${text.full}`);
		}
		return buildMessageRecord(spec, text, fileIndex);
	});

	const group = buildMessageGroup(records);
	let patched: Buffer;
	if (existingGroup === undefined) {
		const after = groups.find((entry) => groupOrder.indexOf(entry.label) > groupOrder.indexOf(messageGroupLabel));
		const at = after === undefined ? original.length : after.start;
		patched = Buffer.concat([original.subarray(0, at), group, original.subarray(at)]);
	} else {
		patched = Buffer.concat([
			original.subarray(0, existingGroup.start),
			group,
			original.subarray(existingGroup.start + existingGroup.size),
		]);
	}

	// HEDR's record count is informational, but xEdit reports a stale one and the plugin should not
	// need a round trip through the Creation Kit to look correct.
	const added = records.length - existing.length;
	if (added !== 0) {
		const hedr = readSubrecordOffset(patched, "HEDR");
		patched.writeUInt32LE(patched.readUInt32LE(hedr + 4) + added, hedr + 4);
	}

	verifyMessageRecords(patched, `${pluginPath} (patched)`, texts);

	if (dryRun) {
		console.log(`Message record dry run complete. changed=${changes.length} unchanged=${unchanged}`);
		return { pluginPath, outputPath, dryRun, written: false, changes, unchanged };
	}
	if (changes.length === 0 && outputPath === pluginPath) {
		console.log(`Message records already up to date. unchanged=${unchanged}`);
		return { pluginPath, outputPath, dryRun, written: false, changes, unchanged };
	}

	writeVerifiedPlugin(outputPath, patched, (bytes, label) => verifyMessageRecords(bytes, label, texts));
	console.log(`Message records applied. changed=${changes.length} unchanged=${unchanged} output=${outputPath}`);
	return { pluginPath, outputPath, dryRun, written: true, changes, unchanged };
}

/** Offset of a TES4 header subrecord, which is where the record count lives. */
function readSubrecordOffset(buffer: Buffer, signature: string): number {
	const headerEnd = recordHeaderSize + buffer.readUInt32LE(4);
	let offset = recordHeaderSize;

	while (offset < headerEnd) {
		if (buffer.toString("latin1", offset, offset + 4) === signature) {
			return offset + subrecordHeaderSize;
		}
		offset += subrecordHeaderSize + buffer.readUInt16LE(offset + 4);
	}
	throw new Error(`The plugin header has no ${signature} subrecord`);
}

/** Re-walks a patched plugin and proves every message is there as NUL-terminated UTF-8. */
export function verifyMessageRecords(buffer: Buffer, label: string, texts: Map<string, MessageText>): void {
	const groups = walkTopLevelGroups(buffer, label);
	const group = groups.find((entry) => entry.label === messageGroupLabel);
	if (group === undefined) {
		throw new Error(`${label} has no MESG group after the patch`);
	}

	const records = collectMessageRecords(buffer, group, label);
	for (const spec of MESSAGE_RECORDS) {
		const record = records.find((entry) => entry.edid === spec.edid);
		const text = texts.get(spec.edid);
		if (record === undefined || text === undefined) {
			throw new Error(`${label} does not hold ${spec.edid} after the patch`);
		}
		if (record.text.full !== text.full || record.text.desc !== text.desc) {
			throw new Error(`${label} holds ${spec.edid} with ${JSON.stringify(record.text)}, not ${JSON.stringify(text)}`);
		}
		if ((record.formId & 0xffffff) !== spec.objectId) {
			throw new Error(`${label} holds ${spec.edid} as object ID ${(record.formId & 0xffffff).toString(16)}, not ${spec.objectId.toString(16)}`);
		}
		const flags = spec.messageBox ? messageBoxFlag : 0;
		if (record.flags !== flags) {
			throw new Error(`${label} holds ${spec.edid} with DNAM 0x${record.flags.toString(16)}, not the 0x${flags.toString(16)} this tool assigns`);
		}
	}
	if (records.length !== MESSAGE_RECORDS.length) {
		throw new Error(`${label} holds ${records.length} MESG records, not the ${MESSAGE_RECORDS.length} this tool owns`);
	}
}

export function parseArgs(argv: string[]): { pluginPath: string; translationPath: string; options: ApplyMessageRecordsOptions } {
	const positional = argv.filter((argument) => !argument.startsWith("--"));
	const language = argv.find((argument) => argument.startsWith("--lang="))?.slice("--lang=".length);

	if (positional.length !== 2 || (language !== "en" && language !== "ja")) {
		throw new Error("Usage: apply-message-records <plugin.esp> <translation.xml> --lang=en|ja [--dry-run] [--out=PATH]");
	}

	return {
		pluginPath: positional[0]!,
		translationPath: positional[1]!,
		options: {
			language,
			dryRun: argv.includes("--dry-run"),
			outputPath: argv.find((argument) => argument.startsWith("--out="))?.slice("--out=".length),
		},
	};
}

if (isCliEntry("apply-message-records")) {
	try {
		const args = parseArgs(process.argv.slice(2));
		applyMessageRecords(args.pluginPath, args.translationPath, args.options);
	} catch (error) {
		console.error(error instanceof Error ? error.message : error);
		process.exitCode = 1;
	}
}
