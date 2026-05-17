import fs from "fs-extra";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.resolve(__dirname, "..", "..", "..");

function readModuleConfig(): string {
	return fs.readFileSync(path.join(projectRoot, "packaging", "resources", "fomod", "ModuleConfig.xml"), "utf8");
}

function getCompatibilityNotice(moduleConfig: string): string {
	return moduleConfig.match(/<installStep name="Notice">[\s\S]*?<\/installStep>/)?.[0] ?? "";
}

function getFolderSources(moduleConfig: string): string[] {
	return [...moduleConfig.matchAll(/<folder\s+source="([^"]+)"/g)].map((match) => match[1]!);
}

function getPluginBlocks(moduleConfig: string): string[] {
	return [...moduleConfig.matchAll(/<plugin\b[\s\S]*?<\/plugin>/g)].map((match) => match[0]!);
}

describe("FOMOD release policy", () => {
	it("states the 3.0.0 update compatibility policy", () => {
		const notice = getCompatibilityNotice(readModuleConfig());

		expect(notice).toContain('group name="About Compatibility"');
		expect(notice).toContain('plugin name="LootMan 3.0.0 update compatibility"');
		expect(notice).toContain('<flag name="compatibility_notice_read">selected</flag>');
		expect(notice).toContain("LootMan 3.0.0 supports overwrite updates from LootMan 2.x.");
		expect(notice).toContain("LootMan 3.0.0 does not support overwrite updates from LootMan 1.x.");
		expect(notice).toContain("If you are upgrading from 1.x, uninstall 1.x and make a clean save before installing 3.0.0.");
		expect(notice).toContain('<type name="Required"/>');
		expect(notice).not.toContain("2.0.0");
		expect(notice).not.toContain("2.x.x");
	});

	it("references only generated distributable folders", () => {
		const moduleConfig = readModuleConfig();

		expect(getFolderSources(moduleConfig)).toEqual([
			"files/resources/common",
			"files/dll/product",
			"files/ba2/product",
			"files/resources/en",
			"files/resources/ja",
			"files/papyrus/product/source",
		]);
		expect(moduleConfig).not.toContain("files/dll/product/1_10_163");
		expect(moduleConfig).not.toContain("files/dll/debug");
		expect(moduleConfig).not.toContain("files/ba2/debug");
		expect(moduleConfig).not.toContain("files/papyrus/debug/source");
		expect(moduleConfig).not.toContain("install_debug");
	});

	it("does not use conditional flag dependencies that MO2 logs as non-matches", () => {
		const moduleConfig = readModuleConfig();

		expect(moduleConfig).not.toContain("<conditionalFileInstalls>");
		expect(moduleConfig).not.toContain("<flagDependency");
	});

	it("does not use exactly-one groups for single required plugins", () => {
		const moduleConfig = readModuleConfig();
		const notice = getCompatibilityNotice(moduleConfig);
		const runtimeGroup = moduleConfig.match(/<group name="Select your game version\."[\s\S]*?<\/group>/)?.[0] ?? "";

		expect(notice).toContain('group name="About Compatibility" type="SelectAny"');
		expect(runtimeGroup).toContain('group name="Select your game version." type="SelectAny"');
		expect(notice).not.toContain('type="SelectExactlyOne"');
		expect(runtimeGroup).not.toContain('type="SelectExactlyOne"');
	});

	it("keeps every installer plugin actionable for FOMOD parsers", () => {
		const plugins = getPluginBlocks(readModuleConfig());

		expect(plugins.length).toBeGreaterThan(0);
		for (const plugin of plugins) {
			expect(plugin).toContain("<typeDescriptor>");
			expect(plugin.includes("<files>") || plugin.includes("<conditionFlags>")).toBe(true);
		}
	});

	it("targets the supported Fallout 4 runtime", () => {
		const moduleConfig = readModuleConfig();

		expect(moduleConfig).toContain('<plugin name="1.11.191">');
		expect(moduleConfig).toContain('<flag name="runtime_1_11_191">selected</flag>');
		expect(moduleConfig).not.toContain("1.10.163");
		expect(moduleConfig).not.toContain("runtime_1_10_163");
	});
});
