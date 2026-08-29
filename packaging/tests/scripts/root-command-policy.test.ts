import fs from "fs-extra";
import path from "node:path";
import { describe, expect, it } from "vitest";

describe("root packaging command policy", () => {
	const packageJson = fs.readJsonSync(path.resolve("package.json")) as { scripts: Record<string, string> };

	it("makes the normal deploy command build and deploy the complete product runtime", () => {
		expect(packageJson.scripts["package:deploy"]).toContain("deploy --mode=product --build --with-papyrus");
	});

	it("makes the dev deploy command build debug artifacts with Papyrus", () => {
		expect(packageJson.scripts["package:deploy:dev"]).toContain("deploy --mode=debug --build --with-papyrus");
	});
});
