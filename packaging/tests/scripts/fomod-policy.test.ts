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

describe("FOMOD release policy", () => {
	it("states the 3.0.0 update compatibility policy", () => {
		const notice = getCompatibilityNotice(readModuleConfig());

		expect(notice).toContain('group name="About Compatibility"');
		expect(notice).toContain('plugin name="LootMan 3.0.0 update compatibility"');
		expect(notice).toContain("LootMan 3.0.0 supports overwrite updates from LootMan 2.x.");
		expect(notice).toContain("LootMan 3.0.0 does not support overwrite updates from LootMan 1.x.");
		expect(notice).toContain("If you are upgrading from 1.x, uninstall 1.x and make a clean save before installing 3.0.0.");
		expect(notice).toContain('<type name="Required"/>');
		expect(notice).not.toContain("2.0.0");
		expect(notice).not.toContain("2.x.x");
	});
});
