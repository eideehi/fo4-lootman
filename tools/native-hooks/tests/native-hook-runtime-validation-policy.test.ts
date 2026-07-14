import fs from "fs-extra";
import path from "node:path";
import { describe, expect, it } from "vitest";

const sourcePath = path.resolve("commonlibf4-plugin/src/papyrus_lootman_hooks.cpp");

describe("native hook runtime validation policy", () => {
	it("checks reviewed target and context evidence before a family can write", () => {
		const source = fs.readFileSync(sourcePath, "utf8");
		const validationStart = source.indexOf("bool ValidateDirectCallSiteFamily(");
		const writeStart = source.indexOf("OriginalFn WriteValidatedDirectCallHook(");
		const installStart = source.indexOf("bool InstallDirectCallHookFamily(");
		const nextTemplate = source.indexOf("template <class OriginalFn, class HookFn>", installStart);
		const install = source.slice(installStart, nextTemplate);

		expect(source.slice(0, writeStart)).toContain("targetRva != site.expectedTargetRva");
		expect(source.slice(0, writeStart)).toContain("site.contextBytes.begin()");
		expect(install.indexOf("ValidateDirectCallSiteFamily(")).toBeLessThan(install.indexOf("WriteValidatedDirectCallHook<OriginalFn>("));
	});
});
