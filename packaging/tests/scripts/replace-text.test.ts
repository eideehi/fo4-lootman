import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { replace } from "../../scripts/replace-text.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("replace-text", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("replaces tokens in-place", () => {
		const root = createTempDir();
		dirs.push(root);
		const file = path.join(root, "input.txt");

		fs.writeFileSync(file, "Version: __VER__");
		replace(file, [{ key: "__VER__", value: "3.0.0" }]);

		expect(fs.readFileSync(file, "utf8")).toBe("Version: 3.0.0");
	});

	it("supports regexp replacement", () => {
		const root = createTempDir();
		dirs.push(root);
		const file = path.join(root, "input.txt");

		fs.writeFileSync(file, "a a a");
		replace(file, [{ key: /a/g, value: "b" }]);

		expect(fs.readFileSync(file, "utf8")).toBe("b b b");
	});

	it("writes to outputFile when provided", () => {
		const root = createTempDir();
		dirs.push(root);
		const input = path.join(root, "in.txt");
		const output = path.join(root, "nested", "out.txt");

		fs.writeFileSync(input, "__A__");
		replace(input, [{ key: "__A__", value: "ok" }], output);

		expect(fs.readFileSync(input, "utf8")).toBe("__A__");
		expect(fs.readFileSync(output, "utf8")).toBe("ok");
	});
});
