Scriptname LTMN2:Looting:Worker:WorkerBaseWEAP extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsWEAP += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsWEAP -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_WEAP
EndFunction

Function SetTurboMode()
    properties.TurboModeWEAP = true
EndFunction
