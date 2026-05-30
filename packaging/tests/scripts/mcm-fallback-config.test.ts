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
	const configScript = readWorkspaceFile(CONFIG_SCRIPT);
	const modVersion = Number(/int\s+MOD_VERSION\s*=\s*(\d+)\s+const/.exec(systemScript)?.[1] ?? "0");

	it("bumps MOD_VERSION past the 3.0.x line so existing saves migrate", () => {
		// Existing 3.0.x saves stored CurrentModVersion 30000; the bump must exceed
		// it or Patch() never runs for the current player base.
		expect(modVersion).toBeGreaterThan(30000);
	});

	it("keeps package.json version and MOD_VERSION in lockstep", () => {
		const pkg = JSON.parse(readWorkspaceFile("package.json")) as { version: string };
		const [major, minor, patch] = pkg.version.split(".").map(Number);
		// GetVersionString encodes Major*10000 + Minor*100 + Patch (System.psc).
		expect(major * 10000 + minor * 100 + patch, "package.json version must match MOD_VERSION encoding").toBe(modVersion);
	});

	it("wires a version-gated migration grant reachable from 3.0.x saves", () => {
		const patch = extractPapyrusFunction(systemScript, "Patch");
		// Anchor to one physical gate line so the bound cannot be captured from an
		// unrelated CurrentModVersion check elsewhere in Patch().
		const gate = /If\s*\(CurrentModVersion\s*<\s*(\d+)\)\s*\r?\n\s*LTMN2:Patch\.v3_1_0\(\)/.exec(patch);
		expect(gate, "Patch() does not wire a gated LTMN2:Patch.v3_1_0 call").not.toBeNull();
		// The gate must equal MOD_VERSION (so the step is live) and exceed 30000 (so
		// saves stored at 30000 still run it).
		expect(Number(gate![1])).toBe(modVersion);
		expect(Number(gate![1]), "migration gate must be passable by saves at 30000").toBeGreaterThan(30000);

		const migration = extractPapyrusFunction(patchScript, "v3_1_0");
		expect(migration).toContain("IsInstalled");
		expect(migration).toContain("GrantConfigHolotape");
	});

	it("grants the holotape idempotently by form id from both install and migration", () => {
		const grant = extractPapyrusFunction(systemScript, "GrantConfigHolotape");
		expect(grant).toContain("0x000FB6");
		// Count-check and add must target the same actor, or idempotency breaks.
		expect(grant, "grant must be count-checked on the actor it adds to").toMatch(/target\.GetItemCount\([^)]*\)\s*==\s*0/);
		expect(grant).toContain("target.AddItem(");

		const install = extractPapyrusFunction(systemScript, "Install");
		expect(install).toContain("GrantConfigHolotape(player)");
	});

	it("routes the config facade through the shared MCM dispatcher", () => {
		for (const fn of ["SetBool", "Toggle", "ToggleBit", "AdjustFloat", "AdjustInt", "SetLogLevel", "Notify"]) {
			expect(configScript, `facade missing ${fn}`).toContain(`Function ${fn}(`);
		}

		// Single shared state: the facade must route through MCM in code (not just a
		// doc comment) and never call the native refresh directly, so the terminal
		// and the MCM cannot drift.
		const configCode = configScript
			.split(/\r?\n/)
			.filter((line) => !line.trim().startsWith(";"))
			.join("\n");
		const routeHits = configCode.split("ApplySettingSideEffects(").length - 1;
		expect(routeHits, "facade must route through ApplySettingSideEffects in code, not only a comment").toBeGreaterThanOrEqual(5);
		expect(configCode, "facade must not call the native refresh directly").not.toMatch(/LTMN2:LootMan\.OnUpdateLootManProperty/);

		// The thin wrapper must keep the modName guard before delegating.
		const onChange = extractPapyrusFunction(mcmScript, "OnMCMSettingChange");
		expect(onChange).toContain('If (modName != "LootMan")');
		expect(onChange, "wrapper must guard modName before delegating").toMatch(/modName != "LootMan"[\s\S]*Return[\s\S]*ApplySettingSideEffects\(id\)/);
		expect(mcmScript).toContain("Function ApplySettingSideEffects(string id)");
	});

	it("keeps packed-bitmask ids out of the absolute SetBool path", () => {
		// SetBool writes via WriteSettableBool, which must not handle packed-bitmask
		// ids; those are toggle-only because ApplySettingSideEffects XORs the packed
		// int, so an absolute set would desync the bool and the int.
		const settable = extractPapyrusFunction(configScript, "WriteSettableBool");
		const packedWriter = extractPapyrusFunction(configScript, "WritePackedBool");
		for (const packed of ["EnableInventoryLootingOfALCH", "EnableALCHItemFood", "EnableBOOKItemPerkMagazine", "EnableMISCItemBobblehead", "EnableWEAPItemGrenade"]) {
			expect(settable, `WriteSettableBool must not handle packed id ${packed}`).not.toContain(packed);
			expect(packedWriter, `WritePackedBool must handle packed id ${packed}`).toContain(packed);
		}

		// SetBool must bail when the id is not absolute-settable.
		const setBool = extractPapyrusFunction(configScript, "SetBool");
		expect(setBool).toContain("WriteSettableBool");
		expect(setBool).toContain("Return");
	});
});
