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

function extractCppStruct(source: string, name: string): string {
	const match = new RegExp(`struct\\s+${name}\\s*\\{([\\s\\S]*?)\\n\\s*\\};`).exec(source);
	expect(match, `missing C++ struct ${name}`).not.toBeNull();
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
	const lootManScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/LootMan.psc");
	const patchScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Patch.psc");
	const papyrusBinding = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman.cpp");
	const papyrusInternal = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_internal.h");
	const nativeDiagnostics = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_diagnostics.cpp");
	const mcmConfig = JSON.parse(readWorkspaceFile("packaging/resources/lootman/common/MCM/Config/LootMan/config.json")) as {
		pages: Array<{
			content?: Array<{ id?: string; text?: string; type?: string; help?: string; valueOptions?: { allowModifierKeys?: boolean; min?: number } }>;
		}>;
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

	it("wires the nearby object diagnostics hotkey to native debug logging", () => {
		const labelKey = "$PAGE_HOTKEY_DUMP_NEARBY_OBJECT_DIAGNOSTICS";
		const helpKey = "$PAGE_HOTKEY_DUMP_NEARBY_OBJECT_DIAGNOSTICS_HELP";
		const diagnosticsKeybind = keybinds.keybinds.find((keybind) => keybind.id === "DumpNearbyObjectDiagnostics");
		expect(diagnosticsKeybind?.action).toEqual({
			type: "CallFunction",
			form: "LootMan.esp|F9B",
			function: "DumpNearbyObjectDiagnostics",
			params: [],
		});

		const diagnosticsControl = mcmConfig.pages
			.flatMap((page) => page.content ?? [])
			.find((item) => item.id === "DumpNearbyObjectDiagnostics");
		expect(diagnosticsControl).toMatchObject({
			text: labelKey,
			type: "hotkey",
			help: helpKey,
			valueOptions: { allowModifierKeys: true },
		});

		const dumpNearbyObjectDiagnostics = extractPapyrusFunction(mcmScript, "DumpNearbyObjectDiagnostics");
		expect(dumpNearbyObjectDiagnostics).toContain(
			"properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled",
		);
		expect(dumpNearbyObjectDiagnostics).toContain('LogMcmEvent("nearby_object_diagnostics_started", "")');
		expect(dumpNearbyObjectDiagnostics).toContain("LTMN2:LootMan.DumpNearbyObjectDiagnostics(player, context)");
		expect(dumpNearbyObjectDiagnostics).toContain(
			'LogMcmEvent("nearby_object_diagnostics_completed", "rows_logged=" + rowsLogged)',
		);

		expect(lootManScript).toContain(
			'int Function DumpNearbyObjectDiagnostics(ObjectReference player, string context = "") global native',
		);
		expect(papyrusInternal).toMatch(
			/std::int32_t DumpNearbyObjectDiagnostics\(\s*std::monostate, RE::TESObjectREFR\* player, RE::BSFixedString context\);/,
		);
		expect(papyrusBinding).toContain('"DumpNearbyObjectDiagnostics"sv');
		expect(papyrusBinding).toContain("&DumpNearbyObjectDiagnostics");

		expect(nativeDiagnostics).toContain("component=nearby_object_diagnostics event=scan_started");
		expect(nativeDiagnostics).toContain("component=nearby_object_diagnostics event=reference");
		expect(nativeDiagnostics).toContain("component=nearby_object_diagnostics event=summary");
		expect(nativeDiagnostics).toContain("enabled_looting_form_type_mask");
		expect(nativeDiagnostics).toContain("GetFile(0)");
		expect(nativeDiagnostics).toContain("base_source=");
		expect(nativeDiagnostics).toContain("requires_include_activator");
		expect(nativeDiagnostics).toContain("requires_include_activation_block");
		expect(nativeDiagnostics).toContain("requires_include_quest_item");
		expect(nativeDiagnostics).toContain("requires_include_unique_item");
		expect(nativeDiagnostics).toContain("requires_include_featured_item");
		expect(nativeDiagnostics).toContain("excluded_by_injection_data");
		expect(nativeDiagnostics).not.toContain("REX::INFO");

		expect(translationValue(englishTranslations, labelKey)).toBe("Dump Nearby Object Diagnostics");
		expect(translationValue(englishTranslations, helpKey)).toBe(
			"Writes detailed nearby object diagnostics to the LootMan log.",
		);
		expect(translationValue(japaneseEnglishTranslations, labelKey)).toBe("周囲のオブジェクト診断を出力");
		expect(translationValue(japaneseEnglishTranslations, helpKey)).toBe(
			"周囲のオブジェクト情報をLootManのログへ詳しく書き出します。",
		);
		expect(translationValue(japaneseTranslations, labelKey)).toBe("周囲のオブジェクト診断を出力");
		expect(translationValue(japaneseTranslations, helpKey)).toBe(
			"周囲のオブジェクト情報をLootManのログへ詳しく書き出します。",
		);
	});

	it("keeps nearby object diagnostics row probes behind recoverable native boundaries", () => {
		const diagnosticObjectEntry = extractCppStruct(nativeDiagnostics, "DiagnosticObjectEntry");

		expect(nativeDiagnostics).toContain("TryGetInventoryStatusSafe");
		expect(nativeDiagnostics).toContain('"probe_failed"');
		expect(nativeDiagnostics).toContain("TryCaptureDiagnosticRowSnapshotSafe");
		expect(nativeDiagnostics).toContain("TryDetermineDiagnosticReasonSafe");
		expect(nativeDiagnostics).toContain("row_probe_failed={}");
		expect(nativeDiagnostics).toContain("cellFormID");
		expect(diagnosticObjectEntry).not.toContain("TESObjectCELL*");
		expect(nativeDiagnostics).not.toContain("entries[index].cell");
		expect(nativeDiagnostics).not.toMatch(/return\s+HasLootableItem\(/);
	});

	it("rejects invalid MCM utility item types and guards missing current locations", () => {
		const moveItemsInternal = extractPapyrusFunction(mcmScript, "MoveItemsInternal");
		expect(moveItemsInternal).toContain("MoveItemsType < MOVE_ITEM_ALL || MoveItemsType > MOVE_ITEM_KEY");

		const scrapItemsInternal = extractPapyrusFunction(mcmScript, "ScrapItemsInternal");
		expect(scrapItemsInternal).toContain("ScrapItemsType < SCRAP_ITEM_ALL || ScrapItemsType > SCRAP_ITEM_JUNK");

		const settingChange = extractPapyrusFunction(mcmScript, "ApplySettingSideEffects");
		const branchStart = settingChange.indexOf('ElseIf (id == "NotLootingFromSettlement")');
		const branchEnd = settingChange.indexOf('ElseIf (id == "WorkerInvokeInterval")');
		expect(branchStart, "missing NotLootingFromSettlement branch").toBeGreaterThanOrEqual(0);
		expect(branchEnd, "missing WorkerInvokeInterval branch").toBeGreaterThan(branchStart);

		const settlementBranch = settingChange.slice(branchStart, branchEnd);
		expect(settlementBranch).toContain("bool isSettlementLocation = false");
		expect(settlementBranch).toMatch(/If \(currentLocation\)[\s\S]*currentLocation\.HasKeyword[\s\S]*EndIf/);
		expect(settlementBranch).toContain("If (isSettlementLocation || currentWorkshop != None)");
		expect(settlementBranch).not.toContain("If (currentLocation.HasKeyword");
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
