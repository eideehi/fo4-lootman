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
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    Lootman.Log(prefix + "Loot: [Name: " + ref.GetDisplayName() + ", ID: " + Lootman.GetHexID(ref) + "]");; Debug
    Lootman.Log(prefix + "  Inventory status before looting: [ItemCount: " + ref.GetItemCount() + ", TotalWeight: " + ref.GetInventoryWeight() + "]");; Debug
    Lootman.Log(prefix + "  ** Do looting **");; Debug
    int lootCount = 0
    Form[] forms = Lootman.GetInventoryItemsOfFormTypes(ref, GetItemFilters())
    Lootman.Log(prefix + "    Total items found: " + forms.Length);; Debug
    int itemIndex = 1;; Debug
    int j = forms.Length
    While j
        j -= 1
        Form item = forms[j]
        Lootman.Log(prefix + "    [Item_" + itemIndex + "]");; Debug
        LTMN:Quest:Methods.TraceForm(prefix + "      ", item);; Debug
        If (IsLootableItem(ref, item))
            lootCount += 1
            Lootman.Log(prefix + "      Looted items count: " + ref.GetItemCount(item));; Debug
            int count = ref.GetItemCount(item)
            While (count > 0)
                If (count <= 65535)
                    ref.RemoveItem(item, -1, properties.LootInPlayerDirectly.GetValueInt() != 1, GetLootingActor())
                    count = 0
                Else
                    ref.RemoveItem(item, 65535, properties.LootInPlayerDirectly.GetValueInt() != 1, GetLootingActor())
                    count -= 65535
                EndIf
            EndWhile
        Else;; Debug
            Lootman.Log(prefix + "      ** Is not lootable item **");; Debug
        EndIf
        itemIndex += 1;; Debug
    EndWhile

    If (lootCount > 0 && properties.PlayPickupSound.GetValueInt() == 1)
        properties.PickupSoundNPC_.Play(player)
    EndIf
    Lootman.Log(prefix + "  ** Done looting **");; Debug
    Lootman.Log(prefix + "  Inventory status after looting: [ItemCount: " + ref.GetItemCount() + ", TotalWeight: " + ref.GetInventoryWeight() + "]");; Debug
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

Function TraceObject(ObjectReference ref);; Debug
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    LTMN:Quest:Methods.TraceObject(prefix, ref);; Debug
    Actor refActor = (ref As Actor);; Debug
    If (refActor);; Debug
        Lootman.Log(prefix + "  Is dead: " + refActor.IsDead());; Debug
    EndIf;; Debug
    Lootman.Log(prefix + "  Inventory items count: " + ref.GetItemCount());; Debug
EndFunction;; Debug
