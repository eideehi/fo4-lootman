# Workshop Menu Availability Extra Classification

Date: 2026-05-06

## Summary

`workshop_menu.availability` previously hooked five direct callers of
`0x1403997A0`. Current allrefs show nineteen `UNCONDITIONAL_CALL` references.
The fourteen extras all call the same out-count availability helper with an
out pointer plus `row`/`menuResult` inputs, and the callers consume the helper's
out value as an availability/count flag.

The existing hook preserves the helper's boolean return value and only changes
`*outValue` from `0` to `1` when the original helper succeeded, the selected
recipe was evaluated, all requirements are satisfied, and at least one required
item is LootMan-backed. That semantics is valid for all fourteen extras because
the caller fallback paths still run when the original helper returns false.

## Evidence

- `tools/native-hooks/reports/fallout4-1.11.191/candidate-rvas.json`
- `tools/ghidra/reports/fo4-selected-menu-helper-functions.txt`
- `tools/ghidra/reports/fo4-availability-extra-call-windows.txt`
- `tools/ghidra/reports/fo4-availability-extra-precall-windows.txt`
- `tools/ghidra/reports/fo4-availability-extra-caller-functions.txt`
- `tools/ghidra/reports/fo4-placement-resource-status-functions.txt`
- `tools/ghidra/reports/fo4-selected-build-functions.txt`
- `tools/ghidra/reports/fo4-workshopmenu-placement-writes.txt`

## Classification

| Site | Address | Caller | Decision | Reason |
| --- | --- | --- | --- | --- |
| `source-96` | `0x140396E55` | `FUN_140396DB0` | Hook | Selection helper uses `outValue > 0` to decide availability and then preserves original failure fallback. |
| `source-97` | `0x1403B7FA2` | `FUN_1403B7E50` | Hook | Placement item path tests the helper result and out count before refreshing item data. |
| `source-98` | `0x1403C189F` | `FUN_1403C1690` | Hook | Companion placement item path has the same helper result and out-count shape as `source-97`. |
| `source-99` | `0x140B2BAA4` | `FUN_140B2B990` | Hook | Menu branch converts helper success plus nonzero out count into the availability branch. |
| `source-9a` | `0x140B2D96F` | `FUN_140B2C380` | Hook | Menu update path uses the out count as an availability gate before placement/preview work. |
| `source-9b` | `0x140B2DAA1` | `FUN_140B2C380` | Hook | Adjacent menu update path uses the same row/menuResult inputs and out-count gate. |
| `source-9c` | `0x140B2EF0F` | `FUN_140B2EC50` | Hook | Availability refresh path folds helper success and out count into the item-availability flag. |
| `source-9d` | `0x140B3056B` | `FUN_140B30140` | Hook | Start-placement preview uses nonzero out count to choose the available visual state. |
| `source-9e` | `0x140B325B5` | `FUN_140B32240` | Hook | Alternate preview/list path mirrors `source-9d` with the same out-count visual-state gate. |
| `source-9f` | `0x140B332FC` | `FUN_140B332F0` | Hook | Small helper returns whether the availability helper produced a nonzero out count. |
| `source-a0` | `0x140B36358` | `FUN_140B36180` | Hook | List-row builder passes the out count into the row construction call while preserving return-failure fallback. |
| `source-a1` | `0x140B36861` | `FUN_140B36700` | Hook | Related list-row builder has the same out-count row construction semantics as `source-a0`. |
| `source-a2` | `0x140B37840` | `FUN_140B37790` | Hook | Compact list-row builder passes the helper out count to row construction when the helper succeeds. |
| `source-a3` | `0x140B378EB` | `FUN_140B378A0` | Hook | Alternate compact list-row builder mirrors `source-a2` with the same helper success/out-count contract. |

## Exclusions

None. All fourteen extra same-target references are semantically compatible hook
sites and are represented in the manifest proof.
