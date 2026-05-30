Scriptname LTMN2:Fragments:Terminals:TERM_ConfigObjectFilter_FB9 extends Terminal
{Object-filter page result fragments for the MCM-fallback config terminal (TERM 0xFB9).}

; Property-free single-toggle items. Object filters are plain bools (native rebuilds
; the form-type mask from them), so Toggle is safe -- no ToggleBit here. The
; Fragment_Terminal_NN index must equal the item ITID in the TERM VMAD.

Function Fragment_Terminal_01(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfACTI")
EndFunction

Function Fragment_Terminal_02(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfALCH")
EndFunction

Function Fragment_Terminal_03(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfAMMO")
EndFunction

Function Fragment_Terminal_04(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfARMO")
EndFunction

Function Fragment_Terminal_05(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfBOOK")
EndFunction

Function Fragment_Terminal_06(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfCONT")
EndFunction

Function Fragment_Terminal_07(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfFLOR")
EndFunction

Function Fragment_Terminal_08(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfINGR")
EndFunction

Function Fragment_Terminal_09(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfKEYM")
EndFunction

Function Fragment_Terminal_10(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfMISC")
EndFunction

Function Fragment_Terminal_11(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfNPC_")
EndFunction

Function Fragment_Terminal_12(ObjectReference akTerminalRef)
    LTMN2:Config.Toggle("EnableObjectLootingOfWEAP")
EndFunction
