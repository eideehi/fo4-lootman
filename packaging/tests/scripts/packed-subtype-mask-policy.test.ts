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

function extractPapyrusEvent(source: string, name: string): string {
	const match = new RegExp(`Event\\s+${name}\\([^\\n]*\\)([\\s\\S]*?)EndEvent`, "i").exec(source);
	expect(match, `missing Papyrus event ${name}`).not.toBeNull();
	return match![1]!;
}

// Papyrus statements with comments and blank lines dropped. Order assertions run
// against this so a mutation cannot hide inside a comment, and so "mentioned
// somewhere in the body" is never good enough to pass.
function papyrusStatements(body: string): string[] {
	return body
		.split(/\r?\n/)
		.map((line) => line.trim())
		.filter((line) => line !== "" && !line.startsWith(";"));
}

function countOccurrences(source: string, needle: string): number {
	return source.split(needle).length - 1;
}

// Index of the first statement of `sequence` inside `statements`, or -1. Used
// where "appears somewhere in the body" is not the claim being made: the block
// has to be these exact statements, in this exact order, adjacent.
function indexOfSequence(statements: string[], sequence: string[]): number {
	for (let start = 0; start + sequence.length <= statements.length; start += 1) {
		if (sequence.every((line, offset) => statements[start + offset] === line)) {
			return start;
		}
	}
	return -1;
}

function readIntConstant(source: string, name: string): number {
	const match = new RegExp(`int property ${name} = (-?\\d+) autoreadonly`, "i").exec(source);
	expect(match, `missing autoreadonly int constant ${name}`).not.toBeNull();
	return Number.parseInt(match![1]!, 10);
}

function readIntPropertyDefault(source: string, name: string): number {
	const match = new RegExp(`int property ${name} = (-?\\d+) auto hidden`, "i").exec(source);
	expect(match, `missing auto int property ${name}`).not.toBeNull();
	return Number.parseInt(match![1]!, 10);
}

function readBoolPropertyDefault(source: string, name: string): boolean {
	const match = new RegExp(`bool property ${name} = (true|false) auto hidden`, "i").exec(source);
	expect(match, `missing auto bool property ${name}`).not.toBeNull();
	return match![1]!.toLowerCase() === "true";
}

// The five packed masks and every bit they carry, spelled out here so the table
// lives in the test as well as in the script. Dropping a row from
// RecomputePackedMasks means that option can no longer reach the native loot gate
// at all, which is invisible until a player toggles it - so it is pinned by name.
interface MaskSpec {
	property: string;
	local: string;
	rows: Array<[bool: string, bit: string]>;
}

const MASK_TABLE: MaskSpec[] = [
	{
		property: "LootableInventoryItemType",
		local: "inventoryMask",
		rows: [
			["EnableInventoryLootingOfALCH", "ITEM_TYPE_ALCH"],
			["EnableInventoryLootingOfAMMO", "ITEM_TYPE_AMMO"],
			["EnableInventoryLootingOfARMO", "ITEM_TYPE_ARMO"],
			["EnableInventoryLootingOfBOOK", "ITEM_TYPE_BOOK"],
			["EnableInventoryLootingOfINGR", "ITEM_TYPE_INGR"],
			["EnableInventoryLootingOfKEYM", "ITEM_TYPE_KEYM"],
			["EnableInventoryLootingOfMISC", "ITEM_TYPE_MISC"],
			["EnableInventoryLootingOfWEAP", "ITEM_TYPE_WEAP"],
		],
	},
	{
		property: "LootableALCHItemType",
		local: "alchMask",
		rows: [
			["EnableALCHItemAlcohol", "ALCH_ITEM_TYPE_ALCOHOL"],
			["EnableALCHItemChemistry", "ALCH_ITEM_TYPE_CHEMISTRY"],
			["EnableALCHItemFood", "ALCH_ITEM_TYPE_FOOD"],
			["EnableALCHItemNukaCola", "ALCH_ITEM_TYPE_NUKA_COLA"],
			["EnableALCHItemStimpak", "ALCH_ITEM_TYPE_STIMPAK"],
			["EnableALCHItemSyringerAmmo", "ALCH_ITEM_TYPE_SYRINGER_AMMO"],
			["EnableALCHItemWater", "ALCH_ITEM_TYPE_WATER"],
			["EnableALCHItemOther", "ALCH_ITEM_TYPE_OTHER"],
		],
	},
	{
		property: "LootableBOOKItemType",
		local: "bookMask",
		rows: [
			["EnableBOOKItemPerkMagazine", "BOOK_ITEM_TYPE_PERKMAGAZINE"],
			["EnableBOOKItemOther", "BOOK_ITEM_TYPE_OTHER"],
		],
	},
	{
		property: "LootableMISCItemType",
		local: "miscMask",
		rows: [
			["EnableMISCItemBobblehead", "MISC_ITEM_TYPE_BOBBLEHEAD"],
			["EnableMISCItemOther", "MISC_ITEM_TYPE_OTHER"],
		],
	},
	{
		property: "LootableWEAPItemType",
		local: "weapMask",
		rows: [
			["EnableWEAPItemGrenade", "WEAP_ITEM_TYPE_GRENADE"],
			["EnableWEAPItemMine", "WEAP_ITEM_TYPE_MINE"],
			["EnableWEAPItemOther", "WEAP_ITEM_TYPE_OTHER"],
		],
	},
];

const ALL_PACKED_IDS = MASK_TABLE.flatMap((mask) => mask.rows.map(([boolName]) => boolName));

const USER_SCRIPTS = [
	"papyrus/Scripts/Source/User/LTMN2/Config.psc",
	"papyrus/Scripts/Source/User/LTMN2/MCM.psc",
	"papyrus/Scripts/Source/User/LTMN2/Patch.psc",
	"papyrus/Scripts/Source/User/LTMN2/Properties.psc",
	"papyrus/Scripts/Source/User/LTMN2/System.psc",
	"papyrus/Scripts/Source/User/LTMN2/Utils.psc",
];

describe("packed subtype mask policy", () => {
	const propertiesScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/Properties.psc");
	const mcmScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/MCM.psc");
	const systemScript = readWorkspaceFile("papyrus/Scripts/Source/User/LTMN2/System.psc");
	const recompute = extractPapyrusFunction(propertiesScript, "RecomputePackedMasks");
	const applySideEffects = extractPapyrusFunction(mcmScript, "ApplySettingSideEffects");

	it("derives every packed mask from its backing bool, one full table per call", () => {
		// Each mask is assembled from zero and assigned once. That is what makes the
		// function total and idempotent: every bit is set or cleared from its bool on
		// every call, so a single call repairs any divergence however it arose. A
		// mask that were only partially rewritten would keep stale bits forever.
		const statements = papyrusStatements(recompute);
		const expected: string[] = [];
		for (const mask of MASK_TABLE) {
			expected.push(`int ${mask.local} = 0`);
			for (const [boolName, bit] of mask.rows) {
				expected.push(`${mask.local} = Math.LogicalOr(${mask.local}, MaskBit(${boolName}, ${bit}))`);
			}
			expected.push(`${mask.property} = ${mask.local}`);
		}
		expect(statements, "RecomputePackedMasks must rebuild all five masks from the full bit table").toEqual(expected);

		// 23 bits, no more and no less. The count is asserted separately from the
		// table above so a row that was dropped and a row that was duplicated cannot
		// cancel each other out in the diff.
		expect(ALL_PACKED_IDS.length).toBe(23);
		expect(countOccurrences(recompute, "MaskBit(")).toBe(23);

		// MaskBit is the only conditional in the table: bit when on, nothing when off.
		expect(papyrusStatements(extractPapyrusFunction(propertiesScript, "MaskBit"))).toEqual([
			"If (enabled)",
			"Return bit",
			"EndIf",
			"Return 0",
		]);
	});

	it("never flips a packed bit and never writes a mask from anywhere else", () => {
		// XOR is the defect this policy exists to keep out. Flipping is only correct
		// while the bool and the bit already agree and the callback runs exactly once
		// per change; the callback can return early or abort on an unbound native
		// after MCM has already written the bool, and one missed flip inverts the
		// option permanently.
		expect(applySideEffects, "ApplySettingSideEffects must not flip packed bits").not.toContain("Math.LogicalXor");
		expect(recompute, "RecomputePackedMasks must derive, not flip").not.toContain("Math.LogicalXor");

		for (const file of USER_SCRIPTS) {
			const source = readWorkspaceFile(file);
			for (const mask of MASK_TABLE) {
				// Reads are fine; assignments are not. RecomputePackedMasks owns every
				// write, so a second writer anywhere is a second source of truth.
				const writes = [...source.matchAll(new RegExp(`^[ \\t]*(?:properties\\.)?${mask.property} =`, "gm"))].length;
				const allowed = file.endsWith("Properties.psc") ? 1 : 0;
				expect(writes, `${file} must not write ${mask.property} outside RecomputePackedMasks`).toBe(allowed);
			}
		}
	});

	it("keeps the packed id list and the bit table naming the same 23 settings", () => {
		// Two lists that must agree: the ids ApplySettingSideEffects routes into the
		// rebuild, and the bools the rebuild actually reads. An id missing from the
		// first never triggers a rebuild; a bool missing from the second is never
		// published to the native gate.
		const predicate = extractPapyrusFunction(propertiesScript, "IsPackedSubtypeSetting");
		const predicateStatements = papyrusStatements(predicate);

		// Naming an id is not the same as answering true for it. Each branch is read
		// together with the statement it guards, so flipping one arm to Return false
		// - which drops that option out of the rebuild while every id is still
		// spelled out in the function - fails here instead of passing silently.
		const predicateIds: string[] = [];
		const answersTrue: string[] = [];
		predicateStatements.forEach((statement, index) => {
			const match = /^(?:If|ElseIf) \(id == "([A-Za-z_]+)"\)$/.exec(statement);
			if (!match) {
				return;
			}
			predicateIds.push(match[1]!);
			if (predicateStatements[index + 1] === "Return true") {
				answersTrue.push(match[1]!);
			}
		});

		expect(predicateIds.slice().sort(), "IsPackedSubtypeSetting must name exactly the bit-table bools").toEqual(
			ALL_PACKED_IDS.slice().sort(),
		);
		expect(answersTrue.slice().sort(), "every bit-table bool must branch to Return true").toEqual(
			ALL_PACKED_IDS.slice().sort(),
		);
		expect(predicateIds.length, "IsPackedSubtypeSetting must not list an id twice").toBe(ALL_PACKED_IDS.length);
		expect(predicateStatements.at(-1), "an unknown id must not be treated as packed").toBe("Return false");
	});

	it("agrees with the declared defaults so a fresh install needs no repair", () => {
		// The compile-time defaults are the state of a brand-new save. If the bools
		// and the mask they derive to ever disagreed, the very first load would
		// silently rewrite a mask under the player.
		for (const mask of MASK_TABLE) {
			const seen = new Set<number>();
			let derived = 0;
			for (const [boolName, bit] of mask.rows) {
				const value = readIntConstant(propertiesScript, bit);
				expect(value > 0 && (value & (value - 1)) === 0, `${bit} must be a single bit`).toBe(true);
				expect(seen.has(value), `${bit} reuses a bit already taken in ${mask.property}`).toBe(false);
				seen.add(value);
				if (readBoolPropertyDefault(propertiesScript, boolName)) {
					derived |= value;
				}
			}
			expect(derived, `${mask.property}'s default must equal the mask its bool defaults derive to`).toBe(
				readIntPropertyDefault(propertiesScript, mask.property),
			);
		}

		// The one option whose bit is off by default. It is the only member of the 23
		// where a desync reads as "switch on, nothing looted" instead of the reverse,
		// which is why it is the one users report.
		expect(readBoolPropertyDefault(propertiesScript, "EnableMISCItemBobblehead")).toBe(false);
	});

	it("rebuilds the masks on the settings-change path, before the guard and the native log", () => {
		const sideEffectStatements = papyrusStatements(applySideEffects);
		const rebuildBlock = indexOfSequence(sideEffectStatements, [
			"If (properties.IsPackedSubtypeSetting(id))",
			"properties.RecomputePackedMasks()",
			"EndIf",
		]);
		expect(rebuildBlock, "packed ids must route into the rebuild, in a branch of their own").toBeGreaterThanOrEqual(0);
		expect(
			countOccurrences(applySideEffects, "properties.RecomputePackedMasks()"),
			"the settings-change path must rebuild the masks exactly once",
		).toBe(1);

		// Above the install-state guard, not inside the branch chain below it.
		// Rebuilding a mask from its own backing bool is pure state repair and is
		// correct in every install state; the caller has already written the bool by
		// the time this runs. Leaving it under the guard is what let a toggle made
		// while not installed strand the mask for the rest of the session, since
		// nothing else recomputes.
		const installGuard = sideEffectStatements.indexOf("If (properties.IsNotInstalled || properties.IsUninstalled)");
		expect(installGuard, "the install-state guard must be kept").toBeGreaterThanOrEqual(0);
		expect(rebuildBlock, "the packed rebuild must run before the install-state guard can return").toBeLessThan(
			installGuard,
		);

		// It still has to come after the lazy member resolution, or it None-derefs
		// on the holotape entry path.
		expect(
			sideEffectStatements.indexOf("properties = LTMN2:Properties.GetInstance()"),
			"the rebuild must not run before properties is resolved",
		).toBeLessThan(rebuildBlock);

		// Ordering, not presence. LogMcmEvent is an LTMN2:LootMan native, so on an
		// install where lootman.dll never loaded it aborts this frame. Logging first
		// is what used to leave the bool written and the mask untouched.
		const rebuild = applySideEffects.indexOf("properties.RecomputePackedMasks()");
		const log = applySideEffects.indexOf('LogMcmEvent("setting_changed"');
		expect(rebuild, "ApplySettingSideEffects never rebuilds the masks").toBeGreaterThanOrEqual(0);
		expect(log, "the setting_changed log must be kept, not dropped").toBeGreaterThan(rebuild);
		expect(
			countOccurrences(applySideEffects, 'LogMcmEvent("setting_changed"'),
			"setting_changed must be logged exactly once",
		).toBe(1);

		// The tail is pinned as a sequence so the log cannot drift back above the
		// branch chain while still technically following some other mutation.
		expect(papyrusStatements(applySideEffects).slice(-3), "log then refresh, both after the branch chain").toEqual([
			"EndIf",
			'LogMcmEvent("setting_changed", "id=" + id)',
			"LTMN2:LootMan.OnUpdateLootManProperty(id)",
		]);

		// The early return keeps its own log. It no longer sits above every mutation
		// - the packed rebuild is deliberately hoisted past it - but it still marks
		// the point where the install-gated side effects stop, and losing it would
		// make a skipped setting change invisible.
		const guard = applySideEffects.indexOf('LogMcmEvent("setting_change_skipped"');
		expect(guard, "the not-installed guard must keep its log").toBeGreaterThanOrEqual(0);
		expect(guard, "the skip log belongs to the guard, below the packed rebuild").toBeGreaterThan(rebuild);

		// Everything else this function owns stays where it was.
		expect(applySideEffects).toContain('If (id == "AutomaticallyLinkAndUnlinkToWorkshop")');
		expect(applySideEffects).toContain('ElseIf (id == "EnableLootingInSettlement")');
		expect(applySideEffects).toContain('ElseIf (id == "WorkerInvokeInterval")');
		expect(applySideEffects).toContain("LTMN2:LootMan.SetLogLevel(LogLevel)");
		expect(applySideEffects).toContain("system.LinkWorkshop(workshop, prefix)");
	});

	it("reconciles once per load, before the first LootMan native in the frame", () => {
		// Toggling repairs one option. Saves already carrying a stranded mask need a
		// standing pass, and it has to survive the frame that produced the divergence
		// in the first place - the one without lootman.dll. That is a claim about
		// LTMN2:LootMan natives specifically, not about natives in general: the
		// rebuild does call Math.LogicalOr, an F4SE native, which is bound because
		// F4SE is a hard prerequisite and only lootman.dll is missing in that mode.
		expect(recompute, "RecomputePackedMasks must not call a LootMan native").not.toContain("LTMN2:LootMan");

		// OnInit and OnPlayerLoadGame are mutually exclusive: a new game runs the
		// first, a loaded save the second. Together they are exactly one reconcile
		// per load. Initialize() is not a candidate - it is timer driven, gated on
		// install state, and opens with a native log call.
		for (const eventName of ["OnInit", "Actor.OnPlayerLoadGame"]) {
			const body = extractPapyrusEvent(systemScript, eventName);
			expect(
				countOccurrences(body, "properties.RecomputePackedMasks()"),
				`${eventName} must reconcile the packed masks exactly once`,
			).toBe(1);

			const reconcile = body.indexOf("properties.RecomputePackedMasks()");
			const firstLog = body.indexOf("LogSystemEvent(");
			const firstNative = body.indexOf("LTMN2:LootMan.");
			expect(firstLog, `${eventName} must still log`).toBeGreaterThanOrEqual(0);
			expect(reconcile, `${eventName} must reconcile before its first native log`).toBeLessThan(firstLog);
			if (firstNative >= 0) {
				expect(reconcile, `${eventName} must reconcile before any LootMan native`).toBeLessThan(firstNative);
			}

			// Nothing between the top of the event and the reconcile may reach a
			// LootMan native either, or the frame is already gone by then.
			const before = papyrusStatements(body.slice(0, reconcile)).join("\n");
			expect(before, `nothing may call a LootMan native before ${eventName} reconciles`).not.toContain("LTMN2:LootMan.");
			expect(before, `nothing may log before ${eventName} reconciles`).not.toContain("LogSystemEvent(");
		}

		// Initialize() must not become the reconcile site: it is skipped entirely for
		// a not-yet-installed save and re-entered on every re-init.
		expect(
			extractPapyrusFunction(systemScript, "Initialize"),
			"Initialize is not once-per-load and is not native-safe",
		).not.toContain("RecomputePackedMasks");
	});

	it("lets a pending migration read the stored masks before reconciling them", () => {
		// Direction is the whole point. RecomputePackedMasks derives masks from
		// bools; Patch.v2_0_1 derives bools from masks - the opposite way. On a
		// pre-2.0.1 save, reconciling at the top of the load would hand v2_0_1 a mask
		// this same frame had just rebuilt from the compile-time bool defaults, and
		// the player's stored subtype choices would be read back as those defaults.
		// Bobblehead is the concrete loss (mask bit on, bool default off), and
		// perk magazines the mirror image (mask bit off, bool default on).
		const load = papyrusStatements(extractPapyrusEvent(systemScript, "Actor.OnPlayerLoadGame"));
		expect(
			indexOfSequence(load, [
				"If (CurrentModVersion == MOD_VERSION)",
				"properties.RecomputePackedMasks()",
				"EndIf",
			]),
			"the early load reconcile must be skipped while a migration is still pending",
		).toBeGreaterThanOrEqual(0);
		expect(load.indexOf("Patch()"), "the load event must still run migrations").toBeGreaterThanOrEqual(0);

		// ...and the migration path has to end in a reconcile of its own, or a
		// migrating save would never get the repair the load reconcile just skipped,
		// and the two representations would stay out of step until the next load.
		const patch = papyrusStatements(extractPapyrusFunction(systemScript, "Patch"));
		const reconcile = patch.indexOf("properties.RecomputePackedMasks()");
		expect(
			countOccurrences(patch.join("\n"), "properties.RecomputePackedMasks()"),
			"Patch must re-establish the bool/mask invariant exactly once",
		).toBe(1);

		// After every migration step, so each one has already read the stored masks,
		// and after the version stamp, so the state it leaves matches what the next
		// load's guard will assume.
		for (const step of ["LTMN2:Patch.v2_0_1()", "LTMN2:Patch.v3_0_0()", "LTMN2:Patch.v3_1_0()", "LTMN2:Patch.v3_3_0()"]) {
			const index = patch.indexOf(step);
			expect(index, `Patch must still run ${step}`).toBeGreaterThanOrEqual(0);
			expect(reconcile, `the reconcile must run after ${step}`).toBeGreaterThan(index);
		}
		const versionStamp = patch.indexOf("CurrentModVersion = MOD_VERSION");
		expect(versionStamp, "Patch must stamp the save version").toBeGreaterThanOrEqual(0);
		expect(reconcile, "the reconcile must run after the version stamp").toBeGreaterThan(versionStamp);

		// Before the patch_completed log, for the same reason the load reconcile sits
		// above its own log: that log is an LTMN2:LootMan native and aborts the frame
		// when lootman.dll never loaded.
		const patchLog = patch.findIndex((statement) => statement.startsWith('LogSystemEvent("patch_completed"'));
		expect(patchLog, "patch_completed must still be logged").toBeGreaterThanOrEqual(0);
		expect(patchLog, "the reconcile must precede the native patch log").toBeGreaterThan(reconcile);

		// OnInit keeps an unguarded reconcile on purpose. It is the fresh-quest path:
		// CurrentModVersion is still 0, Patch never runs from it, and every packed
		// property still holds its compile-time default, so there is nothing of the
		// player's for it to overwrite. Guarding it there would only switch it off.
		const init = papyrusStatements(extractPapyrusEvent(systemScript, "OnInit"));
		const initReconcile = init.indexOf("properties.RecomputePackedMasks()");
		expect(initReconcile, "OnInit must still reconcile").toBeGreaterThan(0);
		expect(init[initReconcile - 1], "OnInit's reconcile is the fresh-quest path and must not be version gated").not.toBe(
			"If (CurrentModVersion == MOD_VERSION)",
		);
	});
});
