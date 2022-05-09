Scriptname LTMN2:Looting:Worker:WorkerBaseFLOR extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsFLOR += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsFLOR -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_FLOR
EndFunction

Function SetTurboMode()
    properties.TurboModeFLOR = true
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = GetLogPrefix(2)
    LTMN2:Debug.Log(prefix + "Loot: [ Name: \"" + ref.GetDisplayName() + "\", Id: " + LTMN2:Debug.GetHexID(ref) + " ]")
    If (ref.Activate(properties.LootManRef) && properties.PlayPickupSound)
        LTMN2:LootMan.PlayPickUpSound(player, ref)
    EndIf
EndFunction
