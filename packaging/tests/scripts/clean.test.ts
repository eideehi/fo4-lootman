import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { clean } from "../../scripts/clean.js";
import { createTestConfig } from "../helpers/config-fixture.js";
import { createTempDir, removeTempDir } from "../helpers/temp-dir.js";

describe("clean", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("removes only buildTempDir by default", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);

		fs.mkdirsSync(config.buildTempDir);
		fs.writeFileSync(path.join(config.buildTempDir, "artifact.txt"), "x");
		fs.mkdirsSync(config.buildDirRoot);
		fs.writeFileSync(path.join(config.buildDirRoot, "cache.txt"), "y");

		clean(config);

		expect(fs.existsSync(config.buildTempDir)).toBe(false);
		expect(fs.existsSync(path.join(config.buildDirRoot, "cache.txt"))).toBe(true);
	});

	it("preserves WSL build stage during default clean", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		const buildStageDir = path.join(config.wslStageDir, "build");

		fs.mkdirsSync(config.buildTempDir);
		fs.writeFileSync(path.join(config.buildTempDir, "artifact.txt"), "x");
		fs.mkdirsSync(buildStageDir);
		fs.writeFileSync(path.join(buildStageDir, "cached.txt"), "build");

		clean(config);

		expect(fs.existsSync(config.buildTempDir)).toBe(false);
		expect(fs.readFileSync(path.join(buildStageDir, "cached.txt"), "utf8")).toBe("build");
	});

	it("removes buildDirRoot when all option is true", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root);

		fs.mkdirsSync(config.buildDirRoot);
		fs.writeFileSync(path.join(config.buildDirRoot, "cache.txt"), "y");

		clean(config, { all: true });

		expect(fs.existsSync(config.buildDirRoot)).toBe(false);
	});

	it("removes WSL stage directory during clean:all", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });

		fs.mkdirsSync(config.wslStageDir);
		fs.writeFileSync(path.join(config.wslStageDir, "staged.txt"), "z");

		clean(config, { all: true });

		expect(fs.existsSync(config.wslStageDir)).toBe(false);
	});

	it("removes only WSL build stage for --wsl-build", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });
		const packageStageDir = path.join(config.wslStageDir, "package");
		const buildStageDir = path.join(config.wslStageDir, "build");

		fs.mkdirsSync(config.buildTempDir);
		fs.writeFileSync(path.join(config.buildTempDir, "artifact.txt"), "x");
		fs.mkdirsSync(packageStageDir);
		fs.writeFileSync(path.join(packageStageDir, "staged.txt"), "package");
		fs.mkdirsSync(buildStageDir);
		fs.writeFileSync(path.join(buildStageDir, "cached.txt"), "build");

		clean(config, { wslBuild: true });

		expect(fs.readFileSync(path.join(config.buildTempDir, "artifact.txt"), "utf8")).toBe("x");
		expect(fs.readFileSync(path.join(packageStageDir, "staged.txt"), "utf8")).toBe("package");
		expect(fs.existsSync(buildStageDir)).toBe(false);
	});

	it("treats --wsl-build as a no-op outside WSL", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: false });

		fs.mkdirsSync(config.buildTempDir);
		fs.writeFileSync(path.join(config.buildTempDir, "artifact.txt"), "x");

		clean(config, { wslBuild: true });

		expect(fs.readFileSync(path.join(config.buildTempDir, "artifact.txt"), "utf8")).toBe("x");
	});

	it("lets --all win when combined with --wsl-build", () => {
		const root = createTempDir();
		dirs.push(root);
		const config = createTestConfig(root, { isWsl: true });

		fs.mkdirsSync(config.buildDirRoot);
		fs.writeFileSync(path.join(config.buildDirRoot, "cache.txt"), "y");
		fs.mkdirsSync(config.wslStageDir);
		fs.writeFileSync(path.join(config.wslStageDir, "staged.txt"), "z");

		clean(config, { all: true, wslBuild: true });

		expect(fs.existsSync(config.buildDirRoot)).toBe(false);
		expect(fs.existsSync(config.wslStageDir)).toBe(false);
	});
});
