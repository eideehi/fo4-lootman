Scriptname LTMN:Thread:LootingAMMOManager extends Quest

Quest property LootingAMMOQuest auto const mandatory

LTMN:Thread:Worker:LootingAMMO01 thread01
LTMN:Thread:Worker:LootingAMMO02 thread02
LTMN:Thread:Worker:LootingAMMO03 thread03
LTMN:Thread:Worker:LootingAMMO04 thread04
LTMN:Thread:Worker:LootingAMMO05 thread05
LTMN:Thread:Worker:LootingAMMO06 thread06
LTMN:Thread:Worker:LootingAMMO07 thread07
LTMN:Thread:Worker:LootingAMMO08 thread08

Event OnInit()
    thread01 = LootingAMMOQuest As LTMN:Thread:Worker:LootingAMMO01
    thread02 = LootingAMMOQuest As LTMN:Thread:Worker:LootingAMMO02
    thread03 = LootingAMMOQuest As LTMN:Thread:Worker:LootingAMMO03
    thread04 = LootingAMMOQuest As LTMN:Thread:Worker:LootingAMMO04
    thread05 = LootingAMMOQuest As LTMN:Thread:Worker:LootingAMMO05
    thread06 = LootingAMMOQuest As LTMN:Thread:Worker:LootingAMMO06
    thread07 = LootingAMMOQuest As LTMN:Thread:Worker:LootingAMMO07
    thread08 = LootingAMMOQuest As LTMN:Thread:Worker:LootingAMMO08
EndEvent

Function Startup()
    Lootman.OpenLog();; Debug
    Lootman.Log("| Loot @ AMMO | *** Start thread manager ***");; Debug

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
    Lootman.Log("| Loot @ AMMO | *** Shutdown thread manager ***");; Debug

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
    int threadLimit = Lootman.GetProperties().ThreadLimitAMMO.GetValueInt()
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
