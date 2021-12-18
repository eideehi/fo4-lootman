Scriptname LTMN:Thread:LootingALCHManager extends Quest

Quest property LootingALCHQuest auto const mandatory

LTMN:Thread:Worker:LootingALCH01 thread01
LTMN:Thread:Worker:LootingALCH02 thread02
LTMN:Thread:Worker:LootingALCH03 thread03
LTMN:Thread:Worker:LootingALCH04 thread04
LTMN:Thread:Worker:LootingALCH05 thread05
LTMN:Thread:Worker:LootingALCH06 thread06
LTMN:Thread:Worker:LootingALCH07 thread07
LTMN:Thread:Worker:LootingALCH08 thread08

Event OnInit()
    thread01 = LootingALCHQuest As LTMN:Thread:Worker:LootingALCH01
    thread02 = LootingALCHQuest As LTMN:Thread:Worker:LootingALCH02
    thread03 = LootingALCHQuest As LTMN:Thread:Worker:LootingALCH03
    thread04 = LootingALCHQuest As LTMN:Thread:Worker:LootingALCH04
    thread05 = LootingALCHQuest As LTMN:Thread:Worker:LootingALCH05
    thread06 = LootingALCHQuest As LTMN:Thread:Worker:LootingALCH06
    thread07 = LootingALCHQuest As LTMN:Thread:Worker:LootingALCH07
    thread08 = LootingALCHQuest As LTMN:Thread:Worker:LootingALCH08
EndEvent

Function Startup()
    LTMN:Debug.OpenLog()
    LTMN:Debug.Log("| Loot @ ALCH | *** Start thread manager ***")

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
    LTMN:Debug.Log("| Loot @ ALCH | *** Shutdown thread manager ***")

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
    int threadLimit = LTMN:Lootman.GetProperties().ThreadLimitALCH.GetValueInt()
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
