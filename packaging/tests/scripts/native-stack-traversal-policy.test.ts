import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

describe("native inventory stack traversal policy", () => {
	it("guards favorite and scrap stack-chain advancement", () => {
		const favoriteSource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_player_items.cpp");
		const scrapSource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_scrap.cpp");
		const favoriteBody = favoriteSource.slice(
			favoriteSource.indexOf("bool HasInventoryFavoriteStack("),
			favoriteSource.indexOf("std::int32_t GetPlayerProtectedStackCount(", favoriteSource.indexOf("bool HasInventoryFavoriteStack(")),
		);
		expect(favoriteBody).not.toContain("stack->nextStack.get()");
		expect(favoriteBody).toContain("TryGetNextStackSafe(stack, nextStack)");
		expect(scrapSource).toContain("TryGetNextStackSafe(stack, nextStack)");
		expect(scrapSource.match(/stack = advanceStackSafe\(stack\)/g)).toHaveLength(2);
		expect(scrapSource).not.toContain("stack = stack->nextStack.get()");
	});
});
