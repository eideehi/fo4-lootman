import fs from "fs-extra";

export interface Replacement {
	key: string | RegExp;
	value: string;
}

export function replaceText(input: string, replacements: Replacement[]): string {
	let data = input;
	for (const replacement of replacements) {
		data = data.replace(replacement.key, replacement.value);
	}
	return data;
}

export function replace(inputFile: string, replacements: Replacement[], outputFile: string = inputFile): void {
	const data = replaceText(fs.readFileSync(inputFile, "utf8"), replacements);
	fs.outputFileSync(outputFile, data);
}
