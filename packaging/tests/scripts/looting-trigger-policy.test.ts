import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

function readUtf16LeWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file)).toString("utf16le").replace(/^\uFEFF/, "");
}

function extractPapyrusFunction(source: string, name: string): string {
	const match = new RegExp(`Function\\s+${name}\\([^\\n]*\\)([\\s\\S]*?)EndFunction`, "i").exec(source);
	expect(match, `missing Papyrus function ${name}`).not.toBeNull();
	return match![1]!;
}

function extractPapyrusEvent(source: string, name: string): string {
	const match = new RegExp(`Event\\s+${name}\\([^\\n]*\\)([\\s\\S]*?)EndEvent`, "i").exec(source);
	expect(match, `missing Papyrus event ${name}`).not.toBeNull();
	return match![1]!;
}

function translationValue(source: string, key: string): string {
	const line = source.split(/\r?\n/).find((entry) => entry.startsWith(`${key}\t`));
	expect(line, `missing translation key ${key}`).toBeDefined();
	return line!.slice(key.length + 1);
}

describe("looting trigger policy", () => {
	const systemScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/System.psc");
	const mcmScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/MCM.psc");
	const patchScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Patch.psc");
	const mcmConfig = JSON.parse(readWorkspaceFile("packaging/resources/lootman/common/MCM/Config/LootMan/config.json")) as {
		pages: Array<{ content?: Array<{ id?: string; valueOptions?: { min?: number } }> }>;
	};
	const keybinds = JSON.parse(readWorkspaceFile("packaging/resources/lootman/common/MCM/Config/LootMan/keybinds.json")) as {
		keybinds: Array<{ id: string; action: { type: string; form: string; function: string; params: unknown[] } }>;
	};
	const englishTranslations = readUtf16LeWorkspaceFile("packaging/resources/lootman/en/Interface/Translations/LootMan_en.txt");
	const japaneseEnglishTranslations = readUtf16LeWorkspaceFile("packaging/resources/lootman/ja/Interface/Translations/LootMan_en.txt");
	const japaneseTranslations = readUtf16LeWorkspaceFile("packaging/resources/lootman/ja/Interface/Translations/LootMan_ja.txt");

	it("lets the ExecuteLooting hotkey run one manual looting pass while LootMan is disabled", () => {
		const executeLootingKeybind = keybinds.keybinds.find((keybind) => keybind.id === "ExecuteLooting");
		expect(executeLootingKeybind?.action).toEqual({
			type: "CallFunction",
			form: "LootMan.esp|F9B",
			function: "ExecuteLooting",
			params: [],
		});

		const executeLooting = extractPapyrusFunction(mcmScript, "ExecuteLooting");
		expect(executeLooting).toContain("properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled");
		expect(executeLooting).toMatch(/system\.Looting\(true\)[\s\S]*system\.DeliverLootManInventory\(\)/);

		const looting = extractPapyrusFunction(systemScript, "Looting");
		expect(systemScript).toContain("Function Looting(bool force = false)");
		expect(looting).toContain("If (!force && !properties.EnableLootMan)");
		expect(looting).not.toContain("If (!properties.EnableLootMan)");
		expect(looting).toContain("If (properties.IsOverweight && !properties.IgnoreOverweight)");
		expect(looting).toContain("If (properties.IsInSettlement && properties.NotLootingFromSettlement)");
		expect(looting).toContain("If (!LTMN2:Utils.IsLootingSafe())");
		expect(looting).toContain("LTMN2:LootMan.LootNearbyEnabledReferences");

		const timer = extractPapyrusEvent(systemScript, "OnTimer");
		expect(timer).toContain("Looting()");
		expect(timer).not.toContain("Looting(true)");
	});

	it("delivers manually looted items through the shared delivery path", () => {
		const update = extractPapyrusFunction(systemScript, "Update");
		const deliverLootManInventory = extractPapyrusFunction(systemScript, "DeliverLootManInventory");

		expect(update).toContain("If (!properties.EnableLootMan)");
		expect(update).toContain("DeliverLootManInventory()");
		expect(deliverLootManInventory).toContain("If (properties.LootManRef.GetItemCount() > 0)");
		expect(deliverLootManInventory).toContain("properties.LootIsDeliverToPlayer");
		expect(deliverLootManInventory).toContain("LTMN2:LootMan.TransferInventoryItems(properties.LootManRef, player");
		expect(deliverLootManInventory).toContain("LTMN2:LootMan.TransferInventoryItems(properties.LootManRef, properties.LootManWorkshopRef");
		expect(deliverLootManInventory).toContain('LogSystemEvent("lootman_inventory_delivered"');
	});

	it("migrates the legacy zero interval setting into disabled recurring looting", () => {
		const v300 = extractPapyrusFunction(patchScript, "v3_0_0");

		expect(v300).toContain("If (properties.WorkerInvokeInterval == 0)");
		expect(v300).toContain("properties.EnableLootMan = false");
		expect(v300).toContain("properties.WorkerInvokeInterval = 1.0");
	});

	it("prevents interval zero in MCM and updates interval help text", () => {
		const workerInterval = mcmConfig.pages
			.flatMap((page) => page.content ?? [])
			.find((item) => item.id === "WorkerInvokeInterval");
		expect(workerInterval?.valueOptions?.min).toBe(0.1);

		const intervalHelpKey = "$PAGE_LOOTING_WORKER_WORKER_INVOKE_INTERVAL_HELP";
		expect(translationValue(englishTranslations, intervalHelpKey)).toBe(
			"Set the interval for recurring item looting. Turn off LootMan to stop recurring item looting; hotkey looting remains available. (Unit: seconds)",
		);
		expect(translationValue(japaneseEnglishTranslations, intervalHelpKey)).toBe(
			"一定時間ごとのアイテム収集間隔を設定します。一定時間ごとの収集を止めるにはLootManを無効にしてください。ショートカットキーからの収集は利用できます。（単位: 秒）",
		);
		expect(translationValue(japaneseTranslations, intervalHelpKey)).toBe(
			"一定時間ごとのアイテム収集間隔を設定します。一定時間ごとの収集を止めるにはLootManを無効にしてください。ショートカットキーからの収集は利用できます。（単位: 秒）",
		);
	});
});
