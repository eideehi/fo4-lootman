Scriptname LTMN2:Looting:Worker:WorkerBaseACTI extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsACTI += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsACTI -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_ACTI
EndFunction

Function SetTurboMode()
    properties.TurboModeACTI = true
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = GetLogPrefix(2)
    LTMN2:Debug.Log(prefix + "Loot: [ Name: \"" + ref.GetDisplayName() + "\", Id: " + LTMN2:Debug.GetHexID(ref) + " ]")
    If (ref.Activate(properties.LootManRef) && properties.PlayPickupSound)
        LTMN2:LootMan.PlayPickUpSound(player, ref)
    EndIf
EndFunction
