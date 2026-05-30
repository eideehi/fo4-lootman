Scriptname LTMN2:Fragments:Terminals:TERM_ConfigLogLevel_FBA extends Terminal
{Log-level page result fragments for the MCM-fallback config terminal (TERM 0xFBA).}

; Property-free. Mirrors the MCM 7-option dropdown; value = index (trace 0 .. off 6).
; The Fragment_Terminal_NN index must equal the item ITID in the TERM VMAD.

Function Fragment_Terminal_01(ObjectReference akTerminalRef)
    LTMN2:Config.SetLogLevel(0)
EndFunction

Function Fragment_Terminal_02(ObjectReference akTerminalRef)
    LTMN2:Config.SetLogLevel(1)
EndFunction

Function Fragment_Terminal_03(ObjectReference akTerminalRef)
    LTMN2:Config.SetLogLevel(2)
EndFunction

Function Fragment_Terminal_04(ObjectReference akTerminalRef)
    LTMN2:Config.SetLogLevel(3)
EndFunction

Function Fragment_Terminal_05(ObjectReference akTerminalRef)
    LTMN2:Config.SetLogLevel(4)
EndFunction

Function Fragment_Terminal_06(ObjectReference akTerminalRef)
    LTMN2:Config.SetLogLevel(5)
EndFunction

Function Fragment_Terminal_07(ObjectReference akTerminalRef)
    LTMN2:Config.SetLogLevel(6)
EndFunction
