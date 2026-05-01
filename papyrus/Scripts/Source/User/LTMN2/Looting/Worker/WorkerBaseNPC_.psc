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

    string prefix = GetLogPrefix(2)
    LTMN2:Debug.Log(prefix + "Loot: [ Name: \"" + ref.GetDisplayName() + "\", Id: " + LTMN2:Debug.GetHexID(ref) + " ]")
    int beforeCount = ref.GetItemCount()
    LTMN2:Debug.Log(prefix + "  Inventory status before looting: [ Item count: " + beforeCount + ", Total weight: " + ref.GetInventoryWeight() + " ]")
    LTMN2:Debug.Log(prefix + "  [ Start looting ]")

    int itemCount = LTMN2:LootMan.TransferLootableInventoryItems(ref, properties.LootManRef, properties.LootableInventoryItemType)
    LTMN2:Debug.Log(prefix + "    Total moved item stacks: " + itemCount)

    int afterCount = ref.GetItemCount()
    LTMN2:Debug.Log(prefix + "  Inventory status after looting: [ Item count: " + afterCount + ", Total weight: " + ref.GetInventoryWeight() + " ]")

    If (itemCount > 0 && properties.PlayPickupSound)
        LTMN2:LootMan.PlayPickUpSound(player, ref)
    EndIf
EndFunction
