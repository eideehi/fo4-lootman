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

describe("system message policy", () => {
	const systemScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/System.psc");
	const mcmScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/MCM.psc");
	const lootManScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/LootMan.psc");
	const papyrusBinding = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman.cpp");
	const nativeNotifications = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_notifications.cpp");
	const messageQueue = readWorkspaceFile("commonlibf4-plugin/src/message_queue.cpp");
	const messageQueueHeader = readWorkspaceFile("commonlibf4-plugin/src/message_queue.h");
	const englishTranslations = readUtf16LeWorkspaceFile("packaging/resources/lootman/en/Interface/Translations/LootMan_en.txt");
	const japaneseEnglishTranslations = readUtf16LeWorkspaceFile("packaging/resources/lootman/ja/Interface/Translations/LootMan_en.txt");
	const japaneseTranslations = readUtf16LeWorkspaceFile("packaging/resources/lootman/ja/Interface/Translations/LootMan_ja.txt");
	const keybinds = JSON.parse(readWorkspaceFile("packaging/resources/lootman/common/MCM/Config/LootMan/keybinds.json")) as {
		keybinds: Array<{ id: string }>;
	};

	it("keeps duplicate suppression per system message id for repeated status reports", () => {
		const showMessage = extractPapyrusFunction(systemScript, "ShowMessage");
		const update = extractPapyrusFunction(systemScript, "Update");
		const firstOverweightBranch = /If \(!wasOverweight && overweight\)([\s\S]*?)ElseIf \(wasOverweight && overweight\)/.exec(update);

		expect(systemScript).toContain("Function ShowMessage(int messageId, float interval = 30.0, int maxDisplayCount = 0)");
		expect(showMessage).toContain("If (!properties.EnableLootMan)");
		expect(showMessage).toContain("int arrayIndex = messageId - 1");
		expect(showMessage).toContain("messageDisplayCount[arrayIndex]");
		expect(showMessage).toContain("lastMessageDisplayTime[arrayIndex]");
		expect(showMessage).toContain("If (maxDisplayCount > 0 && count >= maxDisplayCount)");
		expect(showMessage).toContain("If (count > 0 && (time - lastMessageDisplayTime[arrayIndex]) < interval)");
		expect(showMessage).toContain("messageDisplayCount[arrayIndex] = count + 1");
		expect(showMessage).toContain("lastMessageDisplayTime[arrayIndex] = time");

		expect(systemScript).toContain("ShowMessage(MESSAGE_REMIND_NOT_LOOTING_IN_SETTLEMENT)");
		expect(systemScript).toContain("ShowMessage(MESSAGE_HAS_OVERWEIGHT)");
		expect(systemScript).not.toContain("ShowMessage(MESSAGE_LINKED_TO_WORKSHOP)");
		expect(systemScript).not.toContain("ShowMessage(MESSAGE_UNLINKED_TO_WORKSHOP)");
		expect(firstOverweightBranch, "missing initial overweight branch").not.toBeNull();
		expect(firstOverweightBranch![1]!).toContain("ShowMessage(MESSAGE_HAS_OVERWEIGHT)");
	});

	it("routes state-change feedback through the immediate path", () => {
		expect(keybinds.keybinds.map((keybind) => keybind.id)).toEqual([
			"ToggleEnableLootMan",
			"OpenLootManInventory",
			"ToggleLinkToWorkshop",
			"ExecuteLooting",
			"DumpNearbyObjectDiagnostics",
		]);

		const toggleEnable = extractPapyrusFunction(mcmScript, "ToggleEnableLootMan");
		expect(toggleEnable).toContain("system.ShowMessageImmediate(system.MESSAGE_ENABLED)");
		expect(toggleEnable).toContain("system.ShowMessageImmediate(system.MESSAGE_DISABLED)");
		expect(toggleEnable).not.toContain("system.ShowMessage(");

		const toggleWorkshop = extractPapyrusFunction(mcmScript, "ToggleLinkToWorkshop");
		expect(toggleWorkshop).toContain("system.ShowWorkshopMessageImmediate(system.MESSAGE_LINKED_TO_WORKSHOP, workshop.myLocation)");
		expect(toggleWorkshop).toContain("system.ShowWorkshopMessageImmediate(system.MESSAGE_UNLINKED_TO_WORKSHOP, workshopLocation)");
		expect(toggleWorkshop).toContain("system.ShowMessageImmediate(system.MESSAGE_WORKSHOP_NOT_FOUND)");

		const timer = extractPapyrusEvent(mcmScript, "OnTimer");
		expect(timer).toContain("system.ShowMessageImmediate(system.MESSAGE_UTILITY_PROCESS_COMPLETE)");
		expect(timer).not.toContain("system.ShowMessage(");

		const settingChange = extractPapyrusFunction(mcmScript, "ApplySettingSideEffects");
		expect(settingChange).toContain("system.ShowWorkshopMessageImmediate(system.MESSAGE_LINKED_TO_WORKSHOP, workshop.myLocation)");
		expect(settingChange).toContain("system.ShowWorkshopMessageImmediate(system.MESSAGE_UNLINKED_TO_WORKSHOP, removedWorkshopLocation)");

		expect(systemScript).toContain("ShowWorkshopMessageImmediate(MESSAGE_LINKED_TO_WORKSHOP, currentWorkshop.myLocation)");
		expect(systemScript).toContain("ShowWorkshopMessageImmediate(MESSAGE_UNLINKED_TO_WORKSHOP, autoLinkedWorkshopLocation)");
		expect(systemScript).toContain("ShowWorkshopMessageImmediate(MESSAGE_UNLINKED_TO_WORKSHOP, oldWorkshop.myLocation)");
	});

	it("keeps immediate system messages out of duplicate throttle state", () => {
		const immediate = extractPapyrusFunction(systemScript, "ShowMessageImmediate");

		expect(immediate).toContain("CanShowSystemMessage(messageId)");
		expect(immediate).toContain("LTMN2:LootMan.ShowSystemMessage(messageId)");
		expect(immediate).not.toContain("properties.EnableLootMan");
		expect(immediate).not.toContain("messageDisplayCount");
		expect(immediate).not.toContain("lastMessageDisplayTime");
	});

	it("lets localized system messages bypass pickup pacing in native queue", () => {
		const localizedTake = /bool\s+TakeNextLocalizedTextMessage\([^)]*\)\s*\{([\s\S]*?)\n\t\t\}/.exec(messageQueue);
		const pickupTake = /bool\s+TakeNextPickupMessage\([^)]*\)\s*\{([\s\S]*?)\n\t\t\}/.exec(messageQueue);

		expect(localizedTake, "missing localized system message dequeue path").not.toBeNull();
		expect(pickupTake, "missing pickup message dequeue path").not.toBeNull();
		expect(localizedTake![1]!).toContain("PendingMessageType::localizedText");
		expect(localizedTake![1]!).not.toContain("lastPickupDisplayTime");
		expect(pickupTake![1]!).toContain("lastPickupDisplayTime");
		expect(pickupTake![1]!).toContain("kDisplayInterval");
		expect(messageQueue).toContain("it->type == PendingMessageType::pickup");
	});

	it("routes workshop link feedback through named immediate messages", () => {
		const workshopImmediate = extractPapyrusFunction(systemScript, "ShowWorkshopMessageImmediate");

		expect(lootManScript).toContain("Function ShowSystemMessageWithName(int messageId, Form nameSource) global native");
		expect(papyrusBinding).toContain("\"ShowSystemMessageWithName\"sv");
		expect(papyrusBinding).toContain("&ShowSystemMessageWithName");

		expect(nativeNotifications).toContain("void ShowSystemMessageWithName(std::monostate, std::int32_t messageId, TESForm* nameSource)");
		expect(nativeNotifications).toContain("GetFormName(nameSource)");
		expect(nativeNotifications).toContain("$LTMN_SYSTEM_MESSAGE_LINKED_TO_WORKSHOP_NAMED");
		expect(nativeNotifications).toContain("$LTMN_SYSTEM_MESSAGE_UNLINKED_TO_WORKSHOP_NAMED");
		expect(nativeNotifications).toContain("{workshopName}");
		expect(nativeNotifications).toContain("ShowSystemMessage(std::monostate{}, messageId)");

		expect(workshopImmediate).toContain("CanShowSystemMessage(messageId)");
		expect(workshopImmediate).toContain("LTMN2:LootMan.ShowSystemMessageWithName(messageId, workshopLocation)");
		expect(workshopImmediate).not.toContain("messageDisplayCount");
		expect(workshopImmediate).not.toContain("lastMessageDisplayTime");

		expect(systemScript).toContain("Location Function GetAutoLinkedWorkshopLocation()");
		expect(systemScript).toContain("ShowWorkshopMessageImmediate(MESSAGE_UNLINKED_TO_WORKSHOP, autoLinkedWorkshopLocation)");
		expect(systemScript).toContain("ShowWorkshopMessageImmediate(MESSAGE_UNLINKED_TO_WORKSHOP, oldWorkshop.myLocation)");
		expect(systemScript).toContain("ShowWorkshopMessageImmediate(MESSAGE_LINKED_TO_WORKSHOP, currentWorkshop.myLocation)");

		expect(mcmScript).toContain("removedWorkshopLocation = system.GetAutoLinkedWorkshopLocation()");
		expect(mcmScript).toContain("system.ShowWorkshopMessageImmediate(system.MESSAGE_UNLINKED_TO_WORKSHOP, removedWorkshopLocation)");
		expect(mcmScript).toContain("system.ShowWorkshopMessageImmediate(system.MESSAGE_UNLINKED_TO_WORKSHOP, workshopLocation)");
		expect(mcmScript).toContain("system.ShowWorkshopMessageImmediate(system.MESSAGE_LINKED_TO_WORKSHOP, workshop.myLocation)");
		expect(mcmScript).not.toContain("system.ShowMessageImmediate(system.MESSAGE_LINKED_TO_WORKSHOP)");
		expect(mcmScript).not.toContain("system.ShowMessageImmediate(system.MESSAGE_UNLINKED_TO_WORKSHOP)");

		expect(messageQueueHeader).toContain("struct TextReplacement");
		expect(messageQueue).toContain("std::vector<TextReplacement> replacements");
		expect(messageQueue).toContain("for (const auto& replacement : msg.replacements)");
		expect(messageQueue).toContain("text = ReplaceAll(std::move(text), replacement.token, replacement.value);");

		expect(englishTranslations).toContain("$LTMN_SYSTEM_MESSAGE_LINKED_TO_WORKSHOP_NAMED\t[LootMan] Workshop linked: {workshopName}.");
		expect(englishTranslations).toContain("$LTMN_SYSTEM_MESSAGE_UNLINKED_TO_WORKSHOP_NAMED\t[LootMan] Workshop unlinked: {workshopName}.");
		expect(japaneseEnglishTranslations).toContain("$LTMN_SYSTEM_MESSAGE_LINKED_TO_WORKSHOP_NAMED\t[LootMan] ワークショップ接続：{workshopName}");
		expect(japaneseEnglishTranslations).toContain("$LTMN_SYSTEM_MESSAGE_UNLINKED_TO_WORKSHOP_NAMED\t[LootMan] ワークショップ接続解除：{workshopName}");
		expect(japaneseTranslations).toContain("$LTMN_SYSTEM_MESSAGE_LINKED_TO_WORKSHOP_NAMED\t[LootMan] ワークショップ接続：{workshopName}");
		expect(japaneseTranslations).toContain("$LTMN_SYSTEM_MESSAGE_UNLINKED_TO_WORKSHOP_NAMED\t[LootMan] ワークショップ接続解除：{workshopName}");
	});
});
