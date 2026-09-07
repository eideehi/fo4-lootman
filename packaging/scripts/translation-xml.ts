// Reader for the xTranslator dictionary in translation/. It is the one source of the plugin's
// localized text, so every tool that writes plugin strings reads its rows through here rather than
// re-deriving what a <String> block means.

export interface TranslationString {
	/** The record signature and subrecord, e.g. "TERM:ITXT" or "MESG:DESC". */
	rec: string;
	/** The raw attribute text of the REC tag, which carries the array index as id="N". */
	recAttributes: string;
	edid: string | null;
	source: string | null;
	dest: string | null;
	/** The whole block, for error messages that have to show which row is wrong. */
	block: string;
}

export function decodeXmlText(value: string): string {
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

/** Every <String> row of an xTranslator XML export. The file is UTF-8 with a BOM. */
export function parseTranslationStrings(xml: string): TranslationString[] {
	const strings: TranslationString[] = [];

	for (const block of xml.matchAll(/<String\b[^>]*>([\s\S]*?)<\/String>/g)) {
		const body = block[1];
		const rec = /<REC\b([^>]*)>([\s\S]*?)<\/REC>/.exec(body);
		if (rec === null) {
			continue;
		}
		const read = (tag: string): string | null => {
			const match = new RegExp(`<${tag}>([\\s\\S]*?)</${tag}>`).exec(body);
			return match === null ? null : decodeXmlText(match[1]);
		};

		strings.push({
			rec: decodeXmlText(rec[2]).trim(),
			recAttributes: rec[1],
			edid: read("EDID")?.trim() ?? null,
			source: read("Source"),
			dest: read("Dest"),
			block: block[0],
		});
	}

	return strings;
}
