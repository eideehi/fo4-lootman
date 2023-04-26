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

bool Function IsLootingTarget(ObjectReference ref)
    If (Utility.IsInMenuMode())
        Return false
    EndIf
    Return IsLootableDistance(ref) && ref.Is3DLoaded()
EndFunction

Function LootObject(ObjectReference ref)
    If (properties.IsNotInitialized)
        Return
    EndIf

    string prefix = GetLogPrefix(2)
    LTMN2:Debug.Log(prefix + "Loot: [ Name: \"" + ref.GetDisplayName() + "\", Id: " + LTMN2:Debug.GetHexID(ref) + " ]")
    If (ref.Activate(properties.LootManRef) && properties.PlayPickupSound)
        LTMN2:LootMan.PlayPickUpSound(player, ref)
    EndIf
EndFunction
