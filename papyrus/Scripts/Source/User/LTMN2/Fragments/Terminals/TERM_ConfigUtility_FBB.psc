Scriptname LTMN2:Fragments:Terminals:TERM_ConfigUtility_FBB extends Terminal
{Utilities & system page result fragments for the MCM-fallback config terminal (TERM 0xFBB).}

; Property-free. Each item runs an existing LTMN2:MCM action, which is already
; install-state guarded. The Fragment_Terminal_NN index must equal the item ITID
; in the TERM VMAD. "Open LootMan storage" activates a container; whether that
; presents over an open terminal is a Checkpoint-4 in-game gate (close-first notice).

Function Fragment_Terminal_01(ObjectReference akTerminalRef)
    LTMN2:MCM.GetInstance().ExecuteLooting()
EndFunction

Function Fragment_Terminal_02(ObjectReference akTerminalRef)
    LTMN2:MCM.GetInstance().OpenLootManInventory()
EndFunction

Function Fragment_Terminal_03(ObjectReference akTerminalRef)
    LTMN2:MCM.GetInstance().Install()
EndFunction

Function Fragment_Terminal_04(ObjectReference akTerminalRef)
    LTMN2:MCM.GetInstance().Uninstall()
EndFunction
