Scriptname LTMN2:Fragments:Terminals:TERM_ConfigUtility_FBB extends Terminal
{Utilities & system page result fragments for the MCM-fallback config terminal (TERM 0xFBB).}

; Property-free. Each item runs an existing LTMN2:MCM action, which is already
; install-state guarded. The Fragment_Terminal_NN index must equal the item ITID
; in the TERM VMAD. The former "Open LootMan storage" item was removed: a container
; UI cannot present over an open terminal, so it could never work from here.

Function Fragment_Terminal_01(ObjectReference akTerminalRef)
    LTMN2:MCM.GetInstance().ExecuteLooting()
EndFunction

Function Fragment_Terminal_02(ObjectReference akTerminalRef)
    LTMN2:MCM.GetInstance().Install()
EndFunction

Function Fragment_Terminal_03(ObjectReference akTerminalRef)
    LTMN2:MCM.GetInstance().Uninstall()
EndFunction
