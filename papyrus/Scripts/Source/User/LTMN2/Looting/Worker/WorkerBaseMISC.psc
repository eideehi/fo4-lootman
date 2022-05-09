Scriptname LTMN2:Looting:Worker:WorkerBaseMISC extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsMISC += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsMISC -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_MISC
EndFunction

Function SetTurboMode()
    properties.TurboModeMISC = true
EndFunction
