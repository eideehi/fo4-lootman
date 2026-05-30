import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

function extractPapyrusFunction(source: string, name: string): string {
	const match = new RegExp(`Function\\s+${name}\\([^\\n]*\\)([\\s\\S]*?)EndFunction`, "i").exec(source);
	expect(match, `missing Papyrus function ${name}`).not.toBeNull();
	return match![1]!;
}

const SYSTEM_SCRIPT = "papyrus/Scripts/Source/User/LTMN2/System.psc";
const PATCH_SCRIPT = "papyrus/Scripts/Source/User/LTMN2/Patch.psc";
const MCM_SCRIPT = "papyrus/Scripts/Source/User/LTMN2/MCM.psc";
const CONFIG_SCRIPT = "papyrus/Scripts/Source/User/LTMN2/Config.psc";

describe("mcm fallback config delivery", () => {
	const systemScript = readWorkspaceFile(SYSTEM_SCRIPT);
	const patchScript = readWorkspaceFile(PATCH_SCRIPT);
	const mcmScript = readWorkspaceFile(MCM_SCRIPT);

	it("bumps MOD_VERSION past the 3.0.x line so existing saves migrate", () => {
		const match = /int\s+MOD_VERSION\s*=\s*(\d+)\s+const/.exec(systemScript);
		expect(match, "missing MOD_VERSION").not.toBeNull();
		// Existing 3.0.x saves stored CurrentModVersion 30000; the bump must exceed
		// it or Patch() never runs for the current player base.
		expect(Number(match![1])).toBeGreaterThan(30000);
	});

	it("wires a version-gated migration grant reachable from 3.0.x saves", () => {
		const patch = extractPapyrusFunction(systemScript, "Patch");
		const gate = /If\s*\(CurrentModVersion\s*<\s*(\d+)\)\s*LTMN2:Patch\.v3_1_0\(\)/s.exec(patch);
		expect(gate, "Patch() does not wire a gated LTMN2:Patch.v3_1_0 call").not.toBeNull();
		expect(Number(gate![1]), "migration gate must be passable by saves at 30000").toBeGreaterThan(30000);

		const migration = extractPapyrusFunction(patchScript, "v3_1_0");
		expect(migration).toContain("IsInstalled");
		expect(migration).toContain("GrantConfigHolotape");
	});

	it("grants the holotape idempotently by form id from both install and migration", () => {
		const grant = extractPapyrusFunction(systemScript, "GrantConfigHolotape");
		expect(grant).toContain("0x000FB6");
		expect(grant, "grant must be count-checked").toMatch(/GetItemCount\([^)]*\)\s*==\s*0/);

		const install = extractPapyrusFunction(systemScript, "Install");
		expect(install).toContain("GrantConfigHolotape(player)");
	});

	it("routes the config facade through the shared MCM dispatcher", () => {
		expect(fs.existsSync(path.resolve(CONFIG_SCRIPT)), "Config.psc facade is missing").toBe(true);
		const configScript = readWorkspaceFile(CONFIG_SCRIPT);

		for (const fn of ["SetBool", "Toggle", "ToggleBit", "AdjustFloat", "AdjustInt", "SetLogLevel", "Notify"]) {
			expect(configScript, `facade missing ${fn}`).toContain(`Function ${fn}(`);
		}

		// Single shared state: the facade applies through MCM and never calls the
		// native refresh directly, so the terminal and the MCM cannot drift.
		expect(configScript).toContain("ApplySettingSideEffects");
		expect(configScript, "facade must route through ApplySettingSideEffects, not call native refresh").not.toMatch(
			/LTMN2:LootMan\.OnUpdateLootManProperty/,
		);

		const onChange = extractPapyrusFunction(mcmScript, "OnMCMSettingChange");
		expect(onChange).toContain("ApplySettingSideEffects(id)");
		expect(mcmScript).toContain("Function ApplySettingSideEffects(string id)");
	});
});
