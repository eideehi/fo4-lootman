Scriptname LTMN2:Fragments:Terminals:TERM_ConfigGeneral_FB8 extends Terminal
{General page result fragments for the MCM-fallback config terminal (TERM 0xFB8).}

; Property-free: each Fragment_Terminal_NN runs one LTMN2:Config / LTMN2:MCM call,
; bound to the matching item ITID. Numeric steppers show the new value on the HUD;
; the localized label is carried by the item result text. The Fragment_Terminal_NN
; index must equal the item ITID in the TERM VMAD -- verify against the donor in
; the Checkpoint-4 xEdit pass.

Function Fragment_Terminal_01(ObjectReference akTerminalRef)
    LTMN2:MCM.GetInstance().ToggleEnableLootMan()
EndFunction

Function Fragment_Terminal_02(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("DisplaySystemMessage")
EndFunction

Function Fragment_Terminal_03(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("PlayPickupSound")
EndFunction

Function Fragment_Terminal_04(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("PlayContainerAnimation")
EndFunction

Function Fragment_Terminal_05(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("IgnoreOverweight")
EndFunction

Function Fragment_Terminal_06(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("LootIsDeliverToPlayer")
EndFunction

Function Fragment_Terminal_07(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("LootingWithoutLogs")
EndFunction

Function Fragment_Terminal_08(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("NotLootingFromSettlement")
EndFunction

Function Fragment_Terminal_09(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("AutomaticallyLinkAndUnlinkToWorkshop")
EndFunction

Function Fragment_Terminal_10(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("UnlockLockedContainer")
EndFunction

Function Fragment_Terminal_11(ObjectReference akTerminalRef)
    LTMN2:Config.AdjustFloat("LootingRange", 0.5, 1.0, 256.0)
EndFunction

Function Fragment_Terminal_12(ObjectReference akTerminalRef)
    LTMN2:Config.AdjustFloat("LootingRange", -0.5, 1.0, 256.0)
EndFunction

Function Fragment_Terminal_13(ObjectReference akTerminalRef)
    LTMN2:Config.AdjustInt("CarryWeight", 100, 100, 10000)
EndFunction

Function Fragment_Terminal_14(ObjectReference akTerminalRef)
    LTMN2:Config.AdjustInt("CarryWeight", -100, 100, 10000)
EndFunction
