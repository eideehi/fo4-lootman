Scriptname LTMN:Thread:LootingWEAPManager extends Quest

Quest property LootingWEAPQuest auto const mandatory

LTMN:Thread:Worker:LootingWEAP01 thread01
LTMN:Thread:Worker:LootingWEAP02 thread02
LTMN:Thread:Worker:LootingWEAP03 thread03
LTMN:Thread:Worker:LootingWEAP04 thread04
LTMN:Thread:Worker:LootingWEAP05 thread05
LTMN:Thread:Worker:LootingWEAP06 thread06
LTMN:Thread:Worker:LootingWEAP07 thread07
LTMN:Thread:Worker:LootingWEAP08 thread08

Event OnInit()
    thread01 = LootingWEAPQuest As LTMN:Thread:Worker:LootingWEAP01
    thread02 = LootingWEAPQuest As LTMN:Thread:Worker:LootingWEAP02
    thread03 = LootingWEAPQuest As LTMN:Thread:Worker:LootingWEAP03
    thread04 = LootingWEAPQuest As LTMN:Thread:Worker:LootingWEAP04
    thread05 = LootingWEAPQuest As LTMN:Thread:Worker:LootingWEAP05
    thread06 = LootingWEAPQuest As LTMN:Thread:Worker:LootingWEAP06
    thread07 = LootingWEAPQuest As LTMN:Thread:Worker:LootingWEAP07
    thread08 = LootingWEAPQuest As LTMN:Thread:Worker:LootingWEAP08
EndEvent

Function Startup()
    LTMN:Debug.OpenLog()
    LTMN:Debug.Log("| Loot @ WEAP | *** Start thread manager ***")

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
    LTMN:Debug.Log("| Loot @ WEAP | *** Shutdown thread manager ***")

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
    int threadLimit = LTMN:Lootman.GetProperties().ThreadLimitWEAP.GetValueInt()
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
