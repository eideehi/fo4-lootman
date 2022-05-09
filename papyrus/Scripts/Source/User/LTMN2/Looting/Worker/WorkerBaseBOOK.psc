Scriptname LTMN2:Looting:Worker:WorkerBaseBOOK extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsBOOK += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsBOOK -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_BOOK
EndFunction

Function SetTurboMode()
    properties.TurboModeBOOK = true
EndFunction
