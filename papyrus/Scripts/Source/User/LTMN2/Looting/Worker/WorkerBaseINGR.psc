Scriptname LTMN2:Looting:Worker:WorkerBaseINGR extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsINGR += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsINGR -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_INGR
EndFunction

Function SetTurboMode()
    properties.TurboModeINGR = true
EndFunction
