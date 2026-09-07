import fs from "node:fs";

// Structural constants of a Fallout 4 plugin. They are shared by every tool that edits plugin
// bytes directly, so that a record walk means the same thing wherever it is done.
export const recordHeaderSize = 24;
export const groupHeaderSize = 24;
export const subrecordHeaderSize = 6;
export const maxSubrecordSize = 0xffff;
export const compressedRecordFlag = 0x00040000;
export const localizedStringsFlag = 0x00000080;

export interface TopLevelGroup {
	label: string;
	start: number;
	size: number;
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
 * A localized plugin keeps a 4-byte string ID in its text subrecords instead of text, and nearly
 * every ID has a zero high byte, so a NUL-termination check accepts one and a patch would overwrite
 * the ID with literal bytes. The TES4 header flag is the only reliable signal, so it decides.
 */
export function assertNotLocalized(buffer: Buffer, label: string): void {
	if (buffer.length < recordHeaderSize) {
		throw new Error(`${label} is too small to hold a TES4 record`);
	}
	const flags = buffer.readUInt32LE(8);
	if ((flags & localizedStringsFlag) !== 0) {
		throw new Error(`${label} is flagged as localized (TES4 flags 0x${flags.toString(16).toUpperCase().padStart(8, "0")}); its text subrecords hold string IDs, not text, so its strings live in the .strings files and cannot be patched here`);
	}
}

/**
 * Writes patched plugin bytes through a staging file: the bytes are read back and verified before
 * they replace the plugin, so a failure or a crash costs the staging file and never the plugin.
 */
export function writeVerifiedPlugin(outputPath: string, patched: Buffer, verify: (bytes: Buffer, label: string) => void): void {
	const stagingPath = `${outputPath}.tmp`;
	if (fs.existsSync(stagingPath)) {
		throw new Error(`Refusing to write ${outputPath}: the staging file ${stagingPath} already exists; delete it and re-run`);
	}
	try {
		fs.writeFileSync(stagingPath, patched);
		verify(fs.readFileSync(stagingPath), `${outputPath} (staged)`);
		fs.renameSync(stagingPath, outputPath);
	} catch (error) {
		fs.rmSync(stagingPath, { force: true });
		throw error;
	}
}
