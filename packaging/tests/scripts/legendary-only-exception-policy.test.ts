import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

function readWorkspaceFile(file: string): string {
	return fs.readFileSync(path.resolve(file), "utf8");
}

/** Slice a C++ free function body from its signature up to the start of the next known function. */
function sliceBetween(source: string, startNeedle: string, endNeedle: string): string {
	const start = source.indexOf(startNeedle);
	expect(start, `missing ${startNeedle}`).toBeGreaterThanOrEqual(0);
	const end = source.indexOf(endNeedle, start + startNeedle.length);
	expect(end, `missing ${endNeedle} after ${startNeedle}`).toBeGreaterThan(start);
	return source.slice(start, end);
}

function countOccurrences(haystack: string, needle: string): number {
	let count = 0;
	let index = haystack.indexOf(needle);
	while (index !== -1) {
		count += 1;
		index = haystack.indexOf(needle, index + needle.length);
	}
	return count;
}

describe("legendary-only clothing exception policy", () => {
	const injectionHeader = readWorkspaceFile("commonlibf4-plugin/src/injection_data.h");
	const injectionSource = readWorkspaceFile("commonlibf4-plugin/src/injection_data.cpp");
	const validationSource = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_validation.cpp");
	const internalHeader = readWorkspaceFile("commonlibf4-plugin/src/papyrus_lootman_internal.h");
	const userGuide = readWorkspaceFile("docs/user-guide.md");
	const defaultInjection = JSON.parse(
		readWorkspaceFile("packaging/resources/lootman/common/LootMan/injection-data-default.json"),
	) as {
		include: Record<string, unknown>;
		lists: Record<string, string[]>;
	};

	it("exposes the new include key and JSON pointer path", () => {
		expect(injectionHeader).toContain("include_legendary_only_exception");
		expect(injectionSource).toContain(
			'{"/include/legendary-only-exception", include_legendary_only_exception, Type::kForm | Type::kKeyword},',
		);
	});

	it("keeps the existing supported include/exclude/weap paths (regression)", () => {
		for (const path of [
			"/include/activator",
			"/include/quest-item",
			"/include/unique-item",
			"/exclude/form",
			"/exclude/keyword",
			"/weap-type/grenade",
			"/weap-type/mine",
		]) {
			expect(injectionSource, `missing existing path ${path}`).toContain(`"${path}"`);
		}
	});

	it("parses named lists and expands $list references before resolution", () => {
		expect(injectionSource).toContain('constexpr std::string_view kListsKey = "lists"sv;');
		expect(injectionSource).toContain('constexpr std::string_view kListRefPrefix = "$list:"sv;');
		// list definitions are merged, and expansion runs from Initialize before LoadInjectionData resolves.
		expect(injectionSource).toContain("void LoadNamedLists(");
		expect(injectionSource).toContain("LoadNamedLists(src[std::string(kListsKey)], file);");
		expect(injectionSource).toContain("void ExpandListReferences()");
		expect(injectionSource).toContain("ExpandListReferences();");
		// expansion runs after the file loop and before form resolution (Initialize, not LoadInjectionData).
		const initialize = sliceBetween(injectionSource, "bool Initialize()", "void LoadInjectionData()");
		expect(initialize).toContain("ExpandListReferences();");
		// invalid references degrade + skip instead of crashing/looping.
		expect(injectionSource).toContain("reason=cycle");
		expect(injectionSource).toContain("reason=not_found");
		expect(injectionSource).toContain("reason=malformed_name");
		expect(injectionSource).toContain("kMaxListExpansionDepth");
		expect(injectionSource).toContain("kMaxListExpansionVisits");
		expect(injectionSource).toContain("reason=expansion_limit");
		// a tripped expansion limit degrades only the offending "$list:" references; concrete
		// sibling identifiers on the same path must survive the rebuild instead of being
		// dropped by breaking out of the rebuild loop.
		const expand = sliceBetween(injectionSource, "void ExpandListReferences()", "void LoadNamedLists(");
		expect(expand).toContain("if (limitTripped)");
		expect(expand).toContain("limitTripped = true;");
	});

	it("routes both inventory and world legendary-only branches through one ARMO exception helper", () => {
		// helper: ARMO only, matched against the new include key.
		expect(validationSource).toContain(
			"bool IsLegendaryOnlyExceptionArmor(const TESForm* form, const PropertiesSnapshot* props, MatchCache* matchCache)",
		);
		const helper = sliceBetween(
			validationSource,
			"bool IsLegendaryOnlyExceptionArmor(",
			"bool IsLootableInventoryItem(",
		);
		expect(helper).toContain("ENUM_FORM_ID::kARMO");
		expect(helper).toContain("injection_data::include_legendary_only_exception");

		// inventory-stack branch uses the helper before the explosives fallback.
		const inventory = sliceBetween(validationSource, "bool IsLootableInventoryItem(", "bool IsLootableForm(");
		expect(inventory).toContain("MatchCache* matchCache)");
		expect(inventory).toContain("IsLegendaryOnlyExceptionArmor(form, props, matchCache)");
		expect(inventory.indexOf("IsLegendaryOnlyExceptionArmor(form, props, matchCache)")).toBeLessThan(
			inventory.indexOf("alwaysExplosives"),
		);
		// weapon explosives exception preserved.
		expect(inventory).toContain("type == WEAP::grenade || type == WEAP::mine");

		// world-reference branch uses the helper in both the normal and equipment-data-failure paths.
		const world = sliceBetween(validationSource, "bool IsLootableObject(", "bool TryIsValidObjectSafe(");
		expect(countOccurrences(world, "IsLegendaryOnlyExceptionArmor(form, props, matchCache)")).toBe(2);
		expect(world).toContain("type == WEAP::grenade || type == WEAP::mine");

		// the helper is only ever called from those three sites.
		expect(countOccurrences(validationSource, "IsLegendaryOnlyExceptionArmor(form, props, matchCache)")).toBe(3);

		// inventory lootability declaration threads a MatchCache so matching stays consistent.
		expect(internalHeader).toMatch(
			/IsLootableInventoryItem\([\s\S]*?const PropertiesSnapshot\* props,\s*MatchCache\* matchCache = nullptr\);/,
		);
	});

	it("ships default lists wired into the legendary-only exception", () => {
		expect(defaultInjection.include["legendary-only-exception"]).toEqual([
			"$list:vanilla-clothing",
			"$list:vanilla-dog-apparel",
		]);
		expect(Array.isArray(defaultInjection.lists["vanilla-clothing"])).toBe(true);
		expect(Array.isArray(defaultInjection.lists["vanilla-dog-apparel"])).toBe(true);
		expect(defaultInjection.lists["vanilla-clothing"]!.length).toBeGreaterThan(0);
		expect(defaultInjection.lists["vanilla-dog-apparel"]!.length).toBeGreaterThan(0);
		for (const [name, entries] of Object.entries(defaultInjection.lists)) {
			for (const entry of entries) {
				expect(entry, `bad identifier in list ${name}`).toMatch(/^[^|]+\.es[mplMPL]\|[0-9A-Fa-f]{6}$/);
			}
		}
	});

	it("documents the lists grammar and the legendary-only exception", () => {
		expect(userGuide).toContain("/include/legendary-only-exception");
		expect(userGuide).toContain('"$list:');
		expect(userGuide).toContain('"lists"');
		expect(userGuide).toContain("vanilla-clothing");
		expect(userGuide).toContain("vanilla-dog-apparel");
		expect(userGuide).toContain("Legendary Only exception only");
		expect(userGuide).toContain("ApparelTypeClothing");
		// existing merge/replace semantics stay documented (regression).
		expect(userGuide).toContain("List paths add array values across sorted files");
	});
});
