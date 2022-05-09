Scriptname LTMN2:Looting:Worker:WorkerBaseALCH extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsALCH += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsALCH -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_ALCH
EndFunction

Function SetTurboMode()
    properties.TurboModeALCH = true
EndFunction
