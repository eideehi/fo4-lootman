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

bool Function IsLootingTarget(ObjectReference ref)
    If (Utility.IsInMenuMode())
        Return false
    EndIf
    Return ref.IsNearPlayer() && ref.GetParentCell().IsAttached() && !player.WouldBeStealing(ref)
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = GetLogPrefix(2)
    LTMN2:Debug.Log(prefix + "Loot: [ Name: \"" + ref.GetDisplayName() + "\", Id: " + LTMN2:Debug.GetHexID(ref) + " ]")
    LTMN2:Debug.Log(prefix + "  Inventory status before looting: [ Item count: " + ref.GetItemCount() + ", Total weight: " + ref.GetInventoryWeight() + " ]")
    LTMN2:Debug.Log(prefix + "  [ Start looting ]")

    Form[] forms = LTMN2:LootMan.GetLootableItems(ref, properties.LootableInventoryItemType)
    LTMN2:Debug.Log(prefix + "    Total found items: " + forms.Length)

    int itemCount = 0
    int i = forms.Length
    While i
        i -= 1
        itemCount += 1

        LTMN2:Debug.Log(prefix + "    [ Item " + itemCount + " ]")
        LTMN2:Debug.TraceForm(prefix + "      ", forms[i])

        LTMN2:Utils.MoveInventoryItem(ref, properties.LootManRef, forms[i])
    EndWhile

    LTMN2:Debug.Log(prefix + "  Inventory status after looting: [ Item count: " + ref.GetItemCount() + ", Total weight: " + ref.GetInventoryWeight() + " ]")

    If (itemCount > 0 && properties.PlayPickupSound)
        LTMN2:LootMan.PlayPickUpSound(player, ref)
    EndIf
EndFunction
