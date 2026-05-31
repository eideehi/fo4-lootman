Scriptname LTMN2:Fragments:Terminals:TERM_ConfigUtility_FBB extends Terminal
{Utilities & system page result fragments for the MCM-fallback config terminal (TERM 0xFBB).}

; Property-free. Each item runs an existing LTMN2:MCM action, which is already
; install-state guarded. The Fragment_Terminal_NN index must equal the item ITID
; in the TERM VMAD. The former "Open LootMan storage" and "Loot now" items were
; removed: the container UI cannot present over an open terminal, and ExecuteLooting
; had no effect when run from a terminal fragment.

Function Fragment_Terminal_01(ObjectReference akTerminalRef)
    LTMN2:MCM.GetInstance().Install()
EndFunction

Function Fragment_Terminal_02(ObjectReference akTerminalRef)
    LTMN2:MCM.GetInstance().Uninstall()
EndFunction
