Scriptname LTMN2:Looting:Worker:WorkerBaseNPC_ extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsNPC_ += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsNPC_ -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_NPC_
EndFunction

Function SetTurboMode()
    properties.TurboModeNPC_ = true
EndFunction

Function LootObject(ObjectReference ref)
    If (properties.IsNotInitialized)
        Return
    EndIf

    int itemCount = LTMN2:LootMan.TransferLootableInventoryItems(ref, properties.LootManRef, properties.LootableInventoryItemType)

    If (itemCount > 0 && properties.PlayPickupSound)
        LTMN2:LootMan.PlayPickUpSound(player, ref)
    EndIf
EndFunction
