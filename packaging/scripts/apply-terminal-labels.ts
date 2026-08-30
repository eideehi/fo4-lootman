import fs from "fs-extra";
import path from "node:path";
import { isCliEntry } from "./config.js";

// Terminal menu-item labels cannot be translated by an xEdit script: the script
// engine holds 8-bit strings and the save path re-encodes them from Latin-1, so
// Japanese double-encodes (see docs/workflows/xedit-scripting.md). Patching the
// plugin bytes directly is the only automatable path, and the sizes that move
// when a label changes length are rewritten here.

const recordHeaderSize = 24;
const groupHeaderSize = 24;
const subrecordHeaderSize = 6;
const compressedRecordFlag = 0x00040000;
const localizedStringsFlag = 0x00000080;
const maxSubrecordSize = 0xffff;
const terminalGroupLabel = "TERM";

export interface TerminalLabelChange {
	edid: string;
	itid: number;
	before: string;
	after: string;
}

export interface ApplyTerminalLabelsOptions {
	/** Restrict patching to these record EDIDs; every other translation row is ignored. */
	edids?: string[];
	/** Compute and report the changes without writing anything. */
	dryRun?: boolean;
	/** Write the patched plugin here instead of over the input. */
	outputPath?: string;
}

export interface ApplyTerminalLabelsResult {
	pluginPath: string;
	outputPath: string;
	dryRun: boolean;
	written: boolean;
	changes: TerminalLabelChange[];
	changed: number;
	unchanged: number;
}

export interface TerminalLabelRow {
	edid: string;
	/** 0-based menu item index carried by the REC id attribute. */
	index: number;
	/** The ITID the index is expected to address; the plugin is the authority. */
	itid: number;
	/** The label the plugin is expected to hold before patching; guards against the wrong plugin. */
	source: string;
	dest: string;
}

export interface TopLevelGroup {
	label: string;
	start: number;
	size: number;
}

interface TerminalItem {
	itid: number;
	itxtStart: number;
	payloadStart: number;
	payloadEnd: number;
	text: string;
}

interface TerminalRecord {
	edid: string;
	start: number;
	groupStart: number;
	items: TerminalItem[];
}

interface ByteEdit {
	start: number;
	end: number;
	bytes: Buffer;
}

function decodeXmlText(value: string): string {
	return value.replace(/&(#[xX][0-9a-fA-F]+|#[0-9]+|[a-zA-Z]+);/g, (match, entity: string) => {
		if (entity.startsWith("#x") || entity.startsWith("#X")) {
			return String.fromCodePoint(Number.parseInt(entity.slice(2), 16));
		}
		if (entity.startsWith("#")) {
			return String.fromCodePoint(Number.parseInt(entity.slice(1), 10));
		}
		switch (entity) {
			case "amp":
				return "&";
			case "lt":
				return "<";
			case "gt":
				return ">";
			case "quot":
				return "\"";
			case "apos":
				return "'";
			default:
				throw new Error(`Unsupported XML entity in translation source: ${match}`);
		}
	});
}

/** Reads the TERM:ITXT rows of an xTranslator XML export. The file is UTF-8 with a BOM. */
export function parseTerminalLabelRows(xml: string): TerminalLabelRow[] {
	const rows: TerminalLabelRow[] = [];
	const stringBlocks = xml.matchAll(/<String\b[^>]*>([\s\S]*?)<\/String>/g);

	for (const block of stringBlocks) {
		const body = block[1];
		const rec = /<REC\b([^>]*)>([\s\S]*?)<\/REC>/.exec(body);
		if (rec === null || decodeXmlText(rec[2]).trim() !== "TERM:ITXT") {
			continue;
		}

		const edid = /<EDID>([\s\S]*?)<\/EDID>/.exec(body);
		if (edid === null) {
			throw new Error(`Translation row for TERM:ITXT has no EDID:\n${block[0]}`);
		}
		const recordEdid = decodeXmlText(edid[1]).trim();

		const id = /\bid="(\d+)"/.exec(rec[1]);
		if (id === null) {
			throw new Error(`Translation row for TERM:ITXT has no REC id attribute: ${recordEdid}`);
		}
		const index = Number.parseInt(id[1], 10);

		const source = /<Source>([\s\S]*?)<\/Source>/.exec(body);
		if (source === null) {
			throw new Error(`Translation row for TERM:ITXT has no Source: ${recordEdid} id=${index}`);
		}

		const dest = /<Dest>([\s\S]*?)<\/Dest>/.exec(body);
		if (dest === null) {
			throw new Error(`Translation row for TERM:ITXT has no Dest: ${recordEdid} id=${index}`);
		}
		const destText = decodeXmlText(dest[1]);
		// An embedded NUL would terminate the label early in-game while the subrecord still carries
		// the tail, so the plugin would look patched and read wrong. Never write one.
		if (destText.includes("\0")) {
			throw new Error(`Translation row for TERM:ITXT has a NUL character in Dest: ${recordEdid} id=${index} (${JSON.stringify(destText)}); the game would only read the text before it`);
		}

		rows.push({ edid: recordEdid, index, itid: index + 1, source: decodeXmlText(source[1]), dest: destText });
	}

	return rows;
}

/**
 * Walks the TES4 record and every top-level group, requiring the walk to land exactly on the end
 * of the buffer. Any stale size field makes the walk overshoot or stop short, so this is the check
 * that a patched plugin is still structurally intact.
 */
export function walkTopLevelGroups(buffer: Buffer, label: string): TopLevelGroup[] {
	if (buffer.length < recordHeaderSize || buffer.toString("latin1", 0, 4) !== "TES4") {
		throw new Error(`${label} does not start with a TES4 record`);
	}

	const groups: TopLevelGroup[] = [];
	let offset = recordHeaderSize + buffer.readUInt32LE(4);

	while (offset < buffer.length) {
		if (offset + groupHeaderSize > buffer.length) {
			throw new Error(`${label} has a truncated group header at offset ${offset}`);
		}
		const signature = buffer.toString("latin1", offset, offset + 4);
		if (signature !== "GRUP") {
			throw new Error(`${label} expected GRUP at offset ${offset} but found ${signature}`);
		}
		const size = buffer.readUInt32LE(offset + 4);
		if (size < groupHeaderSize || offset + size > buffer.length) {
			throw new Error(`${label} has an invalid group size ${size} at offset ${offset}`);
		}
		groups.push({ label: buffer.toString("latin1", offset + 8, offset + 12), start: offset, size });
		offset += size;
	}

	if (offset !== buffer.length) {
		throw new Error(`${label} group walk ended at offset ${offset} but the file is ${buffer.length} bytes`);
	}
	return groups;
}

/**
 * A localized plugin keeps a 4-byte string ID in ITXT instead of text, and nearly every ID has a
 * zero high byte, so the NUL-termination check below accepts one and the patch would overwrite the
 * ID with literal bytes. The TES4 header flag is the only reliable signal, so it decides.
 */
function assertNotLocalized(buffer: Buffer, label: string): void {
	if (buffer.length < recordHeaderSize) {
		throw new Error(`${label} is too small to hold a TES4 record`);
	}
	const flags = buffer.readUInt32LE(8);
	if ((flags & localizedStringsFlag) !== 0) {
		throw new Error(`${label} is flagged as localized (TES4 flags 0x${flags.toString(16).toUpperCase().padStart(8, "0")}); its ITXT subrecords hold string IDs, not text, so its terminal labels live in the .strings files and cannot be patched here`);
	}
}

function readTerminalItems(buffer: Buffer, dataStart: number, dataEnd: number, label: string): { edid: string; items: TerminalItem[] } {
	let edid = "";
	const items: TerminalItem[] = [];
	let pendingItxt: { itxtStart: number; payloadStart: number; payloadEnd: number } | null = null;
	let offset = dataStart;

	while (offset < dataEnd) {
		if (offset + subrecordHeaderSize > dataEnd) {
			throw new Error(`${label} has a truncated subrecord header at offset ${offset}`);
		}
		const signature = buffer.toString("latin1", offset, offset + 4);
		const size = buffer.readUInt16LE(offset + 4);
		const payloadStart = offset + subrecordHeaderSize;
		const payloadEnd = payloadStart + size;
		if (payloadEnd > dataEnd) {
			throw new Error(`${label} has a subrecord ${signature} running past the record data at offset ${offset}`);
		}

		if (signature === "XXXX") {
			// XXXX overrides the following subrecord size with a uint32; no terminal record uses it,
			// and guessing at the layout would risk writing a corrupt plugin.
			throw new Error(`${label} uses an XXXX size override, which is not supported`);
		}
		if (signature === "EDID") {
			edid = buffer.toString("utf8", payloadStart, Math.max(payloadStart, payloadEnd - 1));
		}
		if (signature === "ITXT") {
			pendingItxt = { itxtStart: offset, payloadStart, payloadEnd };
		}
		if (signature === "ITID") {
			// A menu item is emitted as ITXT, ANAM, ITID, so the ITID closes the item opened by the
			// most recent ITXT.
			if (size !== 2) {
				throw new Error(`${label} has an ITID of ${size} bytes at offset ${offset}`);
			}
			if (pendingItxt === null) {
				throw new Error(`${label} has an ITID at offset ${offset} with no preceding ITXT`);
			}
			if (pendingItxt.payloadEnd <= pendingItxt.payloadStart || buffer[pendingItxt.payloadEnd - 1] !== 0) {
				throw new Error(`${label} has an ITXT at offset ${pendingItxt.itxtStart} that is not NUL-terminated text`);
			}
			items.push({
				itid: buffer.readUInt16LE(payloadStart),
				itxtStart: pendingItxt.itxtStart,
				payloadStart: pendingItxt.payloadStart,
				payloadEnd: pendingItxt.payloadEnd,
				text: buffer.toString("utf8", pendingItxt.payloadStart, pendingItxt.payloadEnd - 1),
			});
			pendingItxt = null;
		}

		offset = payloadEnd;
	}

	return { edid, items };
}

function collectTerminalRecords(buffer: Buffer, groups: TopLevelGroup[], label: string): TerminalRecord[] {
	const records: TerminalRecord[] = [];

	for (const group of groups) {
		if (group.label !== terminalGroupLabel) {
			continue;
		}
		let offset = group.start + groupHeaderSize;
		const groupEnd = group.start + group.size;

		while (offset < groupEnd) {
			if (offset + recordHeaderSize > groupEnd) {
				throw new Error(`${label} has a truncated record header at offset ${offset}`);
			}
			const signature = buffer.toString("latin1", offset, offset + 4);
			if (signature === "GRUP") {
				throw new Error(`${label} has an unexpected nested group inside the TERM group at offset ${offset}`);
			}
			const dataSize = buffer.readUInt32LE(offset + 4);
			const flags = buffer.readUInt32LE(offset + 8);
			const dataStart = offset + recordHeaderSize;
			const dataEnd = dataStart + dataSize;
			if (dataEnd > groupEnd) {
				throw new Error(`${label} has a record at offset ${offset} running past its group`);
			}
			if ((flags & compressedRecordFlag) !== 0) {
				throw new Error(`${label} has a compressed record at offset ${offset} (formID ${buffer.readUInt32LE(offset + 12).toString(16).toUpperCase()}); decompress it before applying labels`);
			}

			const parsed = readTerminalItems(buffer, dataStart, dataEnd, label);
			records.push({ edid: parsed.edid, start: offset, groupStart: group.start, items: parsed.items });
			offset = dataEnd;
		}
	}

	return records;
}

/**
 * The one lookup both the patch path and the verify path use, so they can never disagree about
 * which record an EDID names. A duplicate EDID is rejected here, before anything is written: the
 * translation cannot say which record it means, and silently picking one is how a plugin gets
 * patched in the wrong place.
 */
function indexTerminalRecordsByEdid(records: TerminalRecord[], label: string): Map<string, TerminalRecord> {
	const byEdid = new Map<string, TerminalRecord>();

	for (const record of records) {
		if (record.edid === "") {
			// No EDID means no translation row can name it, so it is not a lookup candidate at all.
			continue;
		}
		const existing = byEdid.get(record.edid);
		if (existing !== undefined) {
			throw new Error(`${label} has more than one TERM record with EDID ${record.edid} (offsets ${existing.start} and ${record.start}); resolve the duplicate before applying labels`);
		}
		byEdid.set(record.edid, record);
	}

	return byEdid;
}

function applyByteEdits(buffer: Buffer, edits: ByteEdit[]): Buffer {
	const parts: Buffer[] = [];
	let cursor = 0;

	for (const edit of edits) {
		if (edit.start < cursor) {
			throw new Error(`Overlapping plugin edits at offset ${edit.start}`);
		}
		parts.push(buffer.subarray(cursor, edit.start));
		parts.push(edit.bytes);
		cursor = edit.end;
	}
	parts.push(buffer.subarray(cursor));
	return Buffer.concat(parts);
}

/** Maps an original offset onto the patched buffer. Every size field precedes the edits it covers. */
function shiftOffset(offset: number, edits: ByteEdit[]): number {
	let delta = 0;
	for (const edit of edits) {
		if (edit.end <= offset) {
			delta += edit.bytes.length - (edit.end - edit.start);
		}
	}
	return offset + delta;
}

export function applyTerminalLabels(pluginPath: string, translationPath: string, options: ApplyTerminalLabelsOptions = {}): ApplyTerminalLabelsResult {
	const outputPath = options.outputPath ?? pluginPath;
	const dryRun = options.dryRun === true;
	const original = fs.readFileSync(pluginPath);
	const rows = parseTerminalLabelRows(fs.readFileSync(translationPath, "utf8").replace(/^\uFEFF/, ""));
	const wanted = options.edids === undefined ? null : new Set(options.edids);

	console.log(`Applying terminal labels to ${path.basename(pluginPath)}...`);

	const groups = walkTopLevelGroups(original, pluginPath);
	assertNotLocalized(original, pluginPath);
	const records = collectTerminalRecords(original, groups, pluginPath);
	const byEdid = indexTerminalRecordsByEdid(records, pluginPath);

	const changes: TerminalLabelChange[] = [];
	const edits: ByteEdit[] = [];
	const recordDeltas = new Map<number, number>();
	const groupDeltas = new Map<number, number>();
	let unchanged = 0;

	for (const row of rows) {
		if (wanted !== null && !wanted.has(row.edid)) {
			continue;
		}
		const record = byEdid.get(row.edid);
		if (record === undefined) {
			throw new Error(`Translation names terminal record ${row.edid}, which is not in ${path.basename(pluginPath)}`);
		}

		const item = record.items.find((candidate) => candidate.itid === row.itid);
		if (item === undefined) {
			throw new Error(`Terminal record ${row.edid} does not contain ITID ${row.itid} (translation row id=${row.index})`);
		}
		// The id-plus-one premise is only ever assumed for one export; re-check it against the
		// record so a reordered menu cannot silently retarget a label.
		const positional = record.items[row.index];
		if (positional === undefined || positional.itid !== row.itid) {
			throw new Error(`Terminal record ${row.edid} item ${row.index} is ITID ${positional === undefined ? "missing" : positional.itid}, not ${row.itid}`);
		}

		// EDID plus ITID is the same key in every language of this plugin, so the key alone cannot
		// tell the right plugin from the wrong one. The label the plugin holds now has to be either
		// the row's pre-patch Source or its already-applied Dest; anything else means this
		// translation does not belong to this file.
		if (item.text !== row.source && item.text !== row.dest) {
			throw new Error(`Terminal record ${row.edid} ITID ${row.itid} holds ${JSON.stringify(item.text)}, which is neither the translation Source ${JSON.stringify(row.source)} nor its Dest ${JSON.stringify(row.dest)}; ${path.basename(translationPath)} does not describe ${path.basename(pluginPath)}`);
		}

		if (item.text === row.dest) {
			unchanged += 1;
			continue;
		}

		if (row.dest === "") {
			// xTranslator writes an empty Dest for a row nobody has translated yet; writing it would
			// blank the menu item rather than leave the source label in place.
			console.warn(`  WARNING: ${row.edid} ITID ${row.itid} has an empty Dest, so its label is being blanked`);
		}

		const payload = Buffer.concat([Buffer.from(row.dest, "utf8"), Buffer.from([0])]);
		if (payload.length > maxSubrecordSize) {
			throw new Error(`Terminal record ${row.edid} ITID ${row.itid} label is ${payload.length} bytes, over the subrecord limit`);
		}
		const size = Buffer.alloc(2);
		size.writeUInt16LE(payload.length);

		// The edit covers the ITXT size field and payload together, so the subrecord size travels
		// with the new bytes and only the record and group sizes are left to fix.
		edits.push({
			start: item.itxtStart + 4,
			end: item.payloadEnd,
			bytes: Buffer.concat([size, payload]),
		});
		const delta = payload.length - (item.payloadEnd - item.payloadStart);
		recordDeltas.set(record.start, (recordDeltas.get(record.start) ?? 0) + delta);
		groupDeltas.set(record.groupStart, (groupDeltas.get(record.groupStart) ?? 0) + delta);

		changes.push({ edid: row.edid, itid: row.itid, before: item.text, after: row.dest });
		console.log(`  ${row.edid} ITID ${row.itid}: ${item.text} -> ${row.dest}`);
	}

	edits.sort((a, b) => a.start - b.start);
	const patched = applyByteEdits(original, edits);
	for (const [recordStart, delta] of recordDeltas) {
		const at = shiftOffset(recordStart + 4, edits);
		patched.writeUInt32LE(patched.readUInt32LE(at) + delta, at);
	}
	for (const [groupStart, delta] of groupDeltas) {
		const at = shiftOffset(groupStart + 4, edits);
		patched.writeUInt32LE(patched.readUInt32LE(at) + delta, at);
	}
	// The full record and subrecord walk runs on the patched bytes before they can reach the disk,
	// so a dry run proves exactly what a real run would write.
	verifyTerminalLabels(patched, `${pluginPath} (patched)`, changes);

	if (dryRun) {
		console.log(`Terminal label dry run complete. changed=${changes.length} unchanged=${unchanged}`);
		return { pluginPath, outputPath, dryRun, written: false, changes, changed: changes.length, unchanged };
	}

	// The output path is usually the input path, so the patched bytes are staged beside it and only
	// swapped in once they have been read back and verified. A failure or a crash then costs the
	// staging file, never the plugin.
	const stagingPath = `${outputPath}.tmp`;
	if (fs.existsSync(stagingPath)) {
		throw new Error(`Refusing to write ${outputPath}: the staging file ${stagingPath} already exists; delete it and re-run`);
	}
	try {
		fs.outputFileSync(stagingPath, patched);
		verifyTerminalLabels(fs.readFileSync(stagingPath), `${outputPath} (staged)`, changes);
		fs.renameSync(stagingPath, outputPath);
	} catch (e) {
		fs.removeSync(stagingPath);
		throw e;
	}

	console.log(`Terminal labels applied. changed=${changes.length} unchanged=${unchanged} output=${outputPath}`);
	return { pluginPath, outputPath, dryRun, written: true, changes, changed: changes.length, unchanged };
}

/** Re-walks a patched plugin and proves every new label is there as NUL-terminated UTF-8. */
function verifyTerminalLabels(buffer: Buffer, label: string, changes: TerminalLabelChange[]): void {
	const groups = walkTopLevelGroups(buffer, label);
	const records = collectTerminalRecords(buffer, groups, label);
	// Same lookup the patch path used, so a duplicate EDID cannot make the two disagree.
	const byEdid = indexTerminalRecordsByEdid(records, label);

	for (const change of changes) {
		const item = byEdid.get(change.edid)?.items.find((candidate) => candidate.itid === change.itid);
		if (item === undefined) {
			throw new Error(`${label} lost terminal record ${change.edid} ITID ${change.itid}`);
		}
		const expected = Buffer.concat([Buffer.from(change.after, "utf8"), Buffer.from([0])]);
		if (!buffer.subarray(item.payloadStart, item.payloadEnd).equals(expected)) {
			throw new Error(`${label} does not hold the expected label for ${change.edid} ITID ${change.itid}`);
		}
	}
}

export interface TerminalLabelCliArgs {
	pluginPath: string;
	translationPath: string;
	options: ApplyTerminalLabelsOptions;
}

export function parseArgs(argv: string[]): TerminalLabelCliArgs {
	const positional: string[] = [];
	const edids: string[] = [];
	let dryRun = false;
	let outputPath: string | undefined;

	for (const arg of argv) {
		if (arg === "--dry-run") {
			dryRun = true;
		} else if (arg.startsWith("--edid=")) {
			edids.push(arg.slice("--edid=".length));
		} else if (arg.startsWith("--out=")) {
			outputPath = arg.slice("--out=".length);
		} else if (arg.startsWith("--")) {
			throw new Error(`Invalid option: ${arg}`);
		} else {
			positional.push(arg);
		}
	}

	if (positional.length !== 2) {
		throw new Error("Usage: apply-terminal-labels <plugin.esp> <translation.xml> [--edid=EDID] [--dry-run] [--out=PATH]");
	}
	return {
		pluginPath: positional[0],
		translationPath: positional[1],
		options: { edids: edids.length > 0 ? edids : undefined, dryRun, outputPath },
	};
}

if (isCliEntry("apply-terminal-labels")) {
	try {
		const args = parseArgs(process.argv.slice(2));
		applyTerminalLabels(args.pluginPath, args.translationPath, args.options);
	} catch (e) {
		console.error(e instanceof Error ? e.message : e);
		process.exit(1);
	}
}
