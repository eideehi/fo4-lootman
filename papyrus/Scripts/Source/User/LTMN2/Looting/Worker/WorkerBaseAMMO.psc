Scriptname LTMN2:Looting:Worker:WorkerBaseAMMO extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsAMMO += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsAMMO -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_AMMO
EndFunction

Function SetTurboMode()
    properties.TurboModeAMMO = true
EndFunction
