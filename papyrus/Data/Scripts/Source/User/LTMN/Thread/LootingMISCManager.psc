Scriptname LTMN:Thread:LootingMISCManager extends Quest

Quest property LootingMISCQuest auto const mandatory

LTMN:Thread:Worker:LootingMISC01 thread01
LTMN:Thread:Worker:LootingMISC02 thread02
LTMN:Thread:Worker:LootingMISC03 thread03
LTMN:Thread:Worker:LootingMISC04 thread04
LTMN:Thread:Worker:LootingMISC05 thread05
LTMN:Thread:Worker:LootingMISC06 thread06
LTMN:Thread:Worker:LootingMISC07 thread07
LTMN:Thread:Worker:LootingMISC08 thread08

Event OnInit()
    thread01 = LootingMISCQuest As LTMN:Thread:Worker:LootingMISC01
    thread02 = LootingMISCQuest As LTMN:Thread:Worker:LootingMISC02
    thread03 = LootingMISCQuest As LTMN:Thread:Worker:LootingMISC03
    thread04 = LootingMISCQuest As LTMN:Thread:Worker:LootingMISC04
    thread05 = LootingMISCQuest As LTMN:Thread:Worker:LootingMISC05
    thread06 = LootingMISCQuest As LTMN:Thread:Worker:LootingMISC06
    thread07 = LootingMISCQuest As LTMN:Thread:Worker:LootingMISC07
    thread08 = LootingMISCQuest As LTMN:Thread:Worker:LootingMISC08
EndEvent

Function Startup()
    LTMN:Debug.OpenLog()
    LTMN:Debug.Log("| Loot @ MISC | *** Start thread manager ***")

    thread01.Initialize()
    thread02.Initialize()
    thread03.Initialize()
    thread04.Initialize()
    thread05.Initialize()
    thread06.Initialize()
    thread07.Initialize()
    thread08.Initialize()
EndFunction

Function Shutdown()
    LTMN:Debug.OpenLog()
    LTMN:Debug.Log("| Loot @ MISC | *** Shutdown thread manager ***")

    thread01.Finalize()
    thread02.Finalize()
    thread03.Finalize()
    thread04.Finalize()
    thread05.Finalize()
    thread06.Finalize()
    thread07.Finalize()
    thread08.Finalize()
EndFunction

Function TryLooting()
    int threadLimit = LTMN:Lootman.GetProperties().ThreadLimitMISC.GetValueInt()
    If (!thread01.Busy() && threadLimit >= 1)
        thread01.Run()
    ElseIf (!thread02.Busy() && threadLimit >= 2)
        thread02.Run()
    ElseIf (!thread03.Busy() && threadLimit >= 3)
        thread03.Run()
    ElseIf (!thread04.Busy() && threadLimit >= 4)
        thread04.Run()
    ElseIf (!thread05.Busy() && threadLimit >= 5)
        thread05.Run()
    ElseIf (!thread06.Busy() && threadLimit >= 6)
        thread06.Run()
    ElseIf (!thread07.Busy() && threadLimit >= 7)
        thread07.Run()
    ElseIf (!thread08.Busy() && threadLimit >= 8)
        thread08.Run()
    EndIf
EndFunction
