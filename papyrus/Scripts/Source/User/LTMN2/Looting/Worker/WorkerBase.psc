Scriptname LTMN2:Looting:Worker:WorkerBase extends Quest hidden

CustomEvent CallLooting

Event OnInit()
EndEvent

Event OnQuestInit()
EndEvent

Event OnQuestShutdown()
EndEvent

Event LTMN2:Looting:Worker:WorkerBase.CallLooting(LTMN2:Looting:Worker:WorkerBase sender, Var[] args)
EndEvent

Function Initialize()
EndFunction

Function Run()
EndFunction

bool Function Busy()
    Return false
EndFunction

Function Looting()
EndFunction

Function IncreaseActiveThreadCount()
EndFunction

Function DecreaseActiveThreadCount()
EndFunction

int Function GetFormTypeOfAssignment()
    Return 0
EndFunction

bool Function IsLootingTarget(ObjectReference ref)
    Return false
EndFunction

Function LootObject(ObjectReference ref)
EndFunction

string Function GetThreadId()
    Return "UNKNOWN"
EndFunction
