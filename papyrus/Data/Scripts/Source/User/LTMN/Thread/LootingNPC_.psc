Scriptname LTMN:Thread:LootingNPC_ extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_NPC_
EndFunction

bool Function IsToBeSkipped(ObjectReference ref)
    Actor _actor = ref As Actor
    If (_actor && !_actor.IsDead())
        Return false
    EndIf
    Return true
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ")
    LTMN:Debug.Log(prefix + "Loot: [Name: " + ref.GetDisplayName() + ", ID: " + LTMN:Debug.GetHexID(ref) + "]")
    LTMN:Debug.Log(prefix + "  Inventory status before looting: [ItemCount: " + ref.GetItemCount() + ", TotalWeight: " + ref.GetInventoryWeight() + "]")
    LTMN:Debug.Log(prefix + "  ** Do looting **")
    int lootCount = 0
    Form[] forms = LTMN:Lootman.GetInventoryItemsOfFormTypes(ref, GetItemFilters())
    LTMN:Debug.Log(prefix + "    Total items found: " + forms.Length)
    int itemIndex = 1
    int j = forms.Length
    While j
        j -= 1
        Form item = forms[j]
        LTMN:Debug.Log(prefix + "    [Item_" + itemIndex + "]")
        LTMN:Debug.TraceForm(prefix + "      ", item)
        If (IsLootableItem(ref, item))
            lootCount += 1
            LTMN:Debug.Log(prefix + "      Looted items count: " + ref.GetItemCount(item))
            LTMN:QUEST:Methods.MoveInventoryItem(ref, properties.LootmanActor, item)
        Else
            LTMN:Debug.Log(prefix + "      ** Is not lootable item **")
        EndIf
        itemIndex += 1
    EndWhile

    If (lootCount > 0 && properties.PlayPickupSound.GetValueInt() == 1)
        properties.PickupSoundNPC_.Play(player)
    EndIf
    LTMN:Debug.Log(prefix + "  ** Done looting **")
    LTMN:Debug.Log(prefix + "  Inventory status after looting: [ItemCount: " + ref.GetItemCount() + ", TotalWeight: " + ref.GetInventoryWeight() + "]")
EndFunction

bool Function IsLootingTarget(ObjectReference ref)
    Actor refActor = (ref As Actor)
    If (!refActor || !refActor.IsDead())
        Return false
    EndIf

    If (ref == player || ref.GetItemCount() <= 0)
        Return false
    EndIf

    If (!IsValidObject(ref))
        Return false
    EndIf

    Return true
EndFunction

Function TraceObject(ObjectReference ref) debugOnly
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ")
    LTMN:Debug.TraceObject(prefix, ref)
    Actor refActor = (ref As Actor)
    If (refActor)
        LTMN:Debug.Log(prefix + "  Is dead: " + refActor.IsDead())
    EndIf
    LTMN:Debug.Log(prefix + "  Inventory items count: " + ref.GetItemCount())
EndFunction
