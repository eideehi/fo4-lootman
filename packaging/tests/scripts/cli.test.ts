import { beforeEach, describe, expect, it, vi } from "vitest";
import { createTestConfig } from "../helpers/config-fixture.js";

const mocks = vi.hoisted(() => ({
	clean: vi.fn(),
	collectResources: vi.fn(),
	collectFomod: vi.fn(),
	collectLicense: vi.fn(),
	collectPapyrus: vi.fn().mockResolvedValue(undefined),
	buildDll: vi.fn().mockResolvedValue(undefined),
	collectDll: vi.fn(),
	compilePapyrus: vi.fn().mockResolvedValue(undefined),
	createArchives: vi.fn().mockResolvedValue(undefined),
	makeFomod: vi.fn().mockResolvedValue(undefined),
	syncDeploy: vi.fn(),
	undeploy: vi.fn(),
}));

vi.mock("../../scripts/clean.js", () => ({ clean: mocks.clean }));
vi.mock("../../scripts/collect-resources.js", () => ({ collectResources: mocks.collectResources }));
vi.mock("../../scripts/collect-fomod.js", () => ({ collectFomod: mocks.collectFomod }));
vi.mock("../../scripts/collect-license.js", () => ({ collectLicense: mocks.collectLicense }));
vi.mock("../../scripts/collect-papyrus.js", () => ({ collectPapyrus: mocks.collectPapyrus }));
vi.mock("../../scripts/build-dll.js", () => ({ buildDll: mocks.buildDll }));
vi.mock("../../scripts/collect-dll.js", () => ({ collectDll: mocks.collectDll }));
vi.mock("../../scripts/compile-papyrus.js", () => ({ compilePapyrus: mocks.compilePapyrus }));
vi.mock("../../scripts/archive2.js", () => ({ createArchives: mocks.createArchives }));
vi.mock("../../scripts/make-fomod.js", () => ({ makeFomod: mocks.makeFomod }));
vi.mock("../../scripts/sync-deploy.js", () => ({ syncDeploy: mocks.syncDeploy }));
vi.mock("../../scripts/undeploy.js", () => ({ undeploy: mocks.undeploy }));

import { runCli } from "../../scripts/cli.js";

describe("runCli", () => {
	const config = createTestConfig("C:/tmp/root");

	beforeEach(() => {
		vi.clearAllMocks();
		mocks.syncDeploy.mockReturnValue({ copied: 1, removed: 0, skipped: 0, total: 1 });
	});

	it("runs clean command with --all option", async () => {
		await runCli(config, ["clean", "--all"]);
		expect(mocks.clean).toHaveBeenCalledWith(config, { all: true, wslBuild: false });
	});

	it("runs clean command with --wsl-build option", async () => {
		await runCli(config, ["clean", "--wsl-build"]);
		expect(mocks.clean).toHaveBeenCalledWith(config, { all: false, wslBuild: true });
	});

	it("lets --all take precedence over --wsl-build", async () => {
		await runCli(config, ["clean", "--all", "--wsl-build"]);
		expect(mocks.clean).toHaveBeenCalledWith(config, { all: true, wslBuild: true });
	});

	it("runs default production build with papyrus enabled and creates fomod", async () => {
		await runCli(config, ["build"]);

		expect(mocks.clean).not.toHaveBeenCalled();
		expect(mocks.collectResources).toHaveBeenCalledWith(config);
		expect(mocks.collectFomod).toHaveBeenCalledWith(config);
		expect(mocks.collectLicense).toHaveBeenCalledWith(config);
		expect(mocks.collectPapyrus).toHaveBeenCalledTimes(1);
		expect(mocks.buildDll).toHaveBeenCalledTimes(1);
		expect(mocks.buildDll).toHaveBeenCalledWith(config, { mode: "product" });
		expect(mocks.collectDll).toHaveBeenCalledWith(config, "product");
		expect(mocks.compilePapyrus).toHaveBeenCalledWith(config, { mode: "product" });
		expect(mocks.createArchives).toHaveBeenCalledWith(config, { mode: "product" });
		expect(mocks.makeFomod).toHaveBeenCalledWith(config);
	});

	it("runs build with papyrus disabled", async () => {
		await runCli(config, ["build", "--no-papyrus"]);

		expect(mocks.collectPapyrus).not.toHaveBeenCalled();
		expect(mocks.compilePapyrus).not.toHaveBeenCalled();
		expect(mocks.createArchives).not.toHaveBeenCalled();
		expect(mocks.buildDll).toHaveBeenCalledWith(config, { mode: "product" });
		expect(mocks.collectDll).toHaveBeenCalledWith(config, "product");
		expect(mocks.makeFomod).not.toHaveBeenCalled();
	});

	it("runs deploy build flow with papyrus", async () => {
		await runCli(config, ["deploy", "--lang=ja", "--build", "--with-papyrus", "--full-sync"]);

		expect(mocks.collectResources).toHaveBeenCalledWith(config);
		expect(mocks.collectPapyrus).toHaveBeenCalledWith(config);
		expect(mocks.buildDll).toHaveBeenCalledWith(config, { mode: "product" });
		expect(mocks.collectDll).toHaveBeenCalledWith(config, "product");
		expect(mocks.compilePapyrus).toHaveBeenCalledWith(config, { mode: "product" });
		expect(mocks.syncDeploy).toHaveBeenCalledWith(config, {
			mode: "product",
			lang: "ja",
			withPapyrus: true,
			fullSync: true,
		});
	});

	it("runs deploy without build using defaults", async () => {
		await runCli(config, ["deploy"]);

		expect(mocks.collectResources).not.toHaveBeenCalled();
		expect(mocks.buildDll).not.toHaveBeenCalled();
		expect(mocks.syncDeploy).toHaveBeenCalledWith(config, {
			mode: "product",
			lang: "en",
			withPapyrus: false,
			fullSync: false,
		});
	});

	it("runs undeploy command", async () => {
		await runCli(config, ["undeploy"]);
		expect(mocks.undeploy).toHaveBeenCalledWith(config);
	});

	it("throws on unknown command", async () => {
		await expect(runCli(config, ["unknown"])).rejects.toThrow("Unknown command: unknown");
	});

	it("throws when papyrus flags conflict", async () => {
		await expect(runCli(config, ["build", "--with-papyrus", "--no-papyrus"])).rejects.toThrow(
			"Cannot use both --with-papyrus and --no-papyrus.",
		);
	});

	it("rejects unsupported build mode", async () => {
		await expect(runCli(config, ["build", "--mode=debug"])).rejects.toThrow('Invalid mode: debug. Must be "product".');
	});

	it("rejects unsupported deploy mode", async () => {
		await expect(runCli(config, ["deploy", "--mode=debug"])).rejects.toThrow('Invalid mode: debug. Must be "product".');
	});
});
