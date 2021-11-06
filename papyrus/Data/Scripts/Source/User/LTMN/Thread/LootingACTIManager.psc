Scriptname LTMN:Thread:LootingACTIManager extends Quest

Quest property LootingACTIQuest auto const mandatory

LTMN:Thread:Worker:LootingACTI01 thread01
LTMN:Thread:Worker:LootingACTI02 thread02
LTMN:Thread:Worker:LootingACTI03 thread03
LTMN:Thread:Worker:LootingACTI04 thread04
LTMN:Thread:Worker:LootingACTI05 thread05
LTMN:Thread:Worker:LootingACTI06 thread06
LTMN:Thread:Worker:LootingACTI07 thread07
LTMN:Thread:Worker:LootingACTI08 thread08

Event OnInit()
    thread01 = LootingACTIQuest As LTMN:Thread:Worker:LootingACTI01
    thread02 = LootingACTIQuest As LTMN:Thread:Worker:LootingACTI02
    thread03 = LootingACTIQuest As LTMN:Thread:Worker:LootingACTI03
    thread04 = LootingACTIQuest As LTMN:Thread:Worker:LootingACTI04
    thread05 = LootingACTIQuest As LTMN:Thread:Worker:LootingACTI05
    thread06 = LootingACTIQuest As LTMN:Thread:Worker:LootingACTI06
    thread07 = LootingACTIQuest As LTMN:Thread:Worker:LootingACTI07
    thread08 = LootingACTIQuest As LTMN:Thread:Worker:LootingACTI08
EndEvent

Function Startup()
    Lootman.OpenLog();; Debug
    Lootman.Log("| Loot @ ACTI | *** Start thread manager ***");; Debug

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
    Lootman.OpenLog();; Debug
    Lootman.Log("| Loot @ ACTI | *** Shutdown thread manager ***");; Debug

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
    int threadLimit = Lootman.GetProperties().ThreadLimitACTI.GetValueInt()
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
