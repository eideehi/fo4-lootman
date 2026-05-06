# Workshop Start Placement Extra Caller Classification

Binary version: Fallout4 1.11.191

## Decision
- Treat the five extra same-target references to `0x140B30140` as additional `workshop_menu.start_placement` hook sites.
- Do not add `excludedReferences` for this family.
- Preserve existing source IDs `0xA3` and `0xA4`; assign new sources `0xA9` through `0xAD` to the extra callers.

## Evidence Summary
| Call address | Proposed source | Containing context | Argument setup | Nearby calls | Classification |
| --- | --- | --- | --- | --- | --- |
| `0x140B2B2FF` | `0xA9` | Workshop placement setup path | `RCX=RSI`, `EDX=1`, `R8B=1` | Immediately followed by `0x140B2B307 -> 0x140B2EB50` check-placement | Hook-site candidate |
| `0x140B2BB64` | `0xAA` | Workshop placement angle / state path | `RCX=RSI`, `DL=SETA` from placement state compare, `R8B=1` | Continues with same `RSI` workshop menu context | Hook-site candidate |
| `0x140B2DB10` | `0xAB` | Alternate workshop placement angle / state path | `RCX=R15-0x10`, `DL=SETA` from placement state compare, `R8B` remains the path's create-preview value | Falls through to placement continuation path | Hook-site candidate |
| `0x140B2E886` | `0xAC` | Workshop placement finalization path | `RCX=RDI`, `DL` from `RDI+0x358` compare, `R8B=1` | Immediately followed by `0x140B2E88E -> 0x140B2EB50` check-placement | Hook-site candidate |
| `0x140B2EB0F` | `0xAD` | Workshop placement reset / clear path | `RCX=RSI`, `DL=1`, `R8D=0` | Followed by `0x140B2EB19 -> 0x140B30BF0` and `0x140B2EB21 -> 0x140B2F400` state calls | Hook-site candidate |

## Source Reports
- `tools/ghidra/reports/fo4-selected-build-functions.txt`
- `tools/ghidra/reports/fo4-start-placement-extra-windows.txt`
- `tools/ghidra/reports/fo4-workshopmenu-placement-creation-paths.txt`
- `tools/ghidra/reports/fo4-workshopmenu-placement-writes.txt`
