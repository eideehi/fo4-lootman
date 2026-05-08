import fs from "fs-extra";
import path from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import {
	buildGhidraInstructionWindowProbeCommand,
	parseGhidraHeadlessProbeArgs,
	readGhidraHeadlessConfig,
	runGhidraHeadlessProbe,
} from "../scripts/ghidra-headless.js";
import { createTempDir, removeTempDir } from "../../tests/helpers/temp-dir.js";

function writeConfig(root: string, name: "headless.example.json" | "headless.local.json", overrides: Record<string, unknown> = {}): string {
	const configPath = path.join(root, "tools", "ghidra", name);
	fs.outputJsonSync(configPath, {
		analyzeHeadless: "analyzeHeadless",
		projectLocation: "tools/ghidra/projects",
		projectName: "fo4-fallout4",
		programName: "Fallout4.exe",
		fallout4Exe: "G:/steam/steamapps/common/Fallout 4/Fallout4.exe",
		scriptPath: "tools/ghidra/scripts",
		probeReportPath: "tools/ghidra/reports/headless-probe.txt",
		probeAddress: "0x14059D378",
		probeInstructionCount: 1,
		...overrides,
	}, { spaces: 2 });
	return configPath;
}

describe("ghidra headless", () => {
	const dirs: string[] = [];

	afterEach(() => {
		for (const dir of dirs.splice(0)) {
			removeTempDir(dir);
		}
	});

	it("reads the example config and resolves workspace paths", () => {
		const root = createTempDir();
		dirs.push(root);
		writeConfig(root, "headless.example.json");

		const config = readGhidraHeadlessConfig({ projectRoot: root });

		expect(config.analyzeHeadless).toBe("analyzeHeadless");
		expect(config.projectLocation).toBe(path.join(root, "tools", "ghidra", "projects"));
		expect(config.scriptPath).toBe(path.join(root, "tools", "ghidra", "scripts"));
		expect(config.probeReportPath).toBe(path.join(root, "tools", "ghidra", "reports", "headless-probe.txt"));
		expect(config.programName).toBe("Fallout4.exe");
	});

	it("prefers a local config when present", () => {
		const root = createTempDir();
		dirs.push(root);
		writeConfig(root, "headless.example.json", { projectName: "example" });
		writeConfig(root, "headless.local.json", { projectName: "local" });

		expect(readGhidraHeadlessConfig({ projectRoot: root }).projectName).toBe("local");
	});

	it("builds a read-only no-analysis instruction-window command", () => {
		const root = createTempDir();
		dirs.push(root);
		writeConfig(root, "headless.example.json");
		const config = readGhidraHeadlessConfig({ projectRoot: root });

		const command = buildGhidraInstructionWindowProbeCommand(config);

		expect(command.command).toBe("analyzeHeadless");
		expect(command.args).toEqual([
			path.join(root, "tools", "ghidra", "projects"),
			"fo4-fallout4",
			"-process",
			"Fallout4.exe",
			"-readOnly",
			"-noanalysis",
			"-scriptPath",
			path.join(root, "tools", "ghidra", "scripts"),
			"-postScript",
			"DumpFo4InstructionWindow",
			path.join(root, "tools", "ghidra", "reports", "headless-probe.txt"),
			"1",
			"0x14059D378",
		]);
	});

	it("resolves relative report overrides from the selected project root", () => {
		const root = createTempDir();
		dirs.push(root);
		writeConfig(root, "headless.example.json");
		const config = readGhidraHeadlessConfig({ projectRoot: root });

		const command = buildGhidraInstructionWindowProbeCommand(config, {
			projectRoot: root,
			reportPath: "tools/ghidra/reports/custom-probe.txt",
		});

		expect(command.reportPath).toBe(path.join(root, "tools", "ghidra", "reports", "custom-probe.txt"));
	});

	it("runs the probe command with injected process execution", async () => {
		const root = createTempDir();
		dirs.push(root);
		writeConfig(root, "headless.example.json");
		const execaFn = vi.fn().mockResolvedValue({ exitCode: 0, stdout: "ok", stderr: "" });

		const result = await runGhidraHeadlessProbe({ projectRoot: root, execaFn });

		expect(result.exitCode).toBe(0);
		expect(result.reportPath).toBe(path.join(root, "tools", "ghidra", "reports", "headless-probe.txt"));
		expect(execaFn).toHaveBeenCalledWith("analyzeHeadless", expect.arrayContaining(["-readOnly", "-noanalysis"]), {
			cwd: root,
			reject: false,
			stdio: "pipe",
		});
	});

	it("parses CLI overrides", () => {
		expect(parseGhidraHeadlessProbeArgs([
			"--",
			"--config=tools/ghidra/headless.local.json",
			"--report=/tmp/probe.txt",
			"--address=0x140000000",
			"--instruction-count=3",
		])).toEqual({
			configPath: "tools/ghidra/headless.local.json",
			reportPath: "/tmp/probe.txt",
			address: "0x140000000",
			instructionCount: 3,
		});
	});
});
