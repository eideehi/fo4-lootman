import fs from "fs-extra";
import xxHash from "xxhashjs";

export const XXHASH32_SEED = 0x123;

export function hashContent(content: Buffer | string): string {
	const buffer = typeof content === "string" ? Buffer.from(content, "utf8") : content;
	return xxHash.h32(buffer, XXHASH32_SEED).toString(16);
}

export function hashFile(filePath: string): string {
	return hashContent(fs.readFileSync(filePath));
}
