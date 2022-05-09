Scriptname LTMN2:Looting:Worker:WorkerBaseARMO extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsARMO += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsARMO -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_ARMO
EndFunction

Function SetTurboMode()
    properties.TurboModeARMO = true
EndFunction
