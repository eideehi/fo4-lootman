Scriptname LTMN2:Looting:Worker:WorkerBaseKEYM extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsKEYM += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsKEYM -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_KEYM
EndFunction

Function SetTurboMode()
    properties.TurboModeKEYM = true
EndFunction
