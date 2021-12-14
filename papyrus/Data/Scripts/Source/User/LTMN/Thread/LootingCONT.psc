Scriptname LTMN:Thread:LootingCONT extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_CONT
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

    If (lootCount > 0)
        If (properties.PlayContainerAnimation.GetValueInt() == 1)
            ref.Activate(properties.ActivatorActor)
        ElseIf (properties.PlayPickupSound.GetValueInt() == 1)
            properties.PickupSoundNPC_.Play(player)
        EndIf
    EndIf
    Lootman.Log(prefix + "  ** Done looting **");; Debug
    Lootman.Log(prefix + "  Inventory status after looting: [ItemCount: " + ref.GetItemCount() + ", TotalWeight: " + ref.GetInventoryWeight() + "]");; Debug
EndFunction

bool Function IsLootingTarget(ObjectReference ref)
    Form base = ref.GetBaseObject()

    If (!ref.Is3DLoaded() || (ref.IsActivationBlocked() && !properties.IgnorableActivationBlockeList.HasForm(base)) || ref.GetItemCount() <= 0)
        Return false
    EndIf

    If (!IsValidObject(ref) || !IsLootableOwnership(ref))
        Return false
    EndIf

    If (Lootman.IsLinkedToWorkshop(ref) || ref Is WorkshopScript)
        Return false
    EndIf

    If (properties.VendorChestList.HasForm(base))
        Return false
    EndIf

    If (!IsLootableRarity(base))
        Return false
    EndIf

    Return !ref.IsLocked() || _TryUnlock(ref)
EndFunction

Function TraceObject(ObjectReference ref);; Debug
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    LTMN:Quest:Methods.TraceObject(prefix, ref);; Debug
    Lootman.Log(prefix + "  Is locked: " + ref.IsLocked());; Debug
    Lootman.Log(prefix + "  Stored items count: " + ref.GetItemCount());; Debug
    Lootman.Log(prefix + "  Is linked to workshop: " + Lootman.IsLinkedToWorkshop(ref));; Debug
    Lootman.Log(prefix + "  Is workshop: " + (ref Is WorkshopScript));; Debug
    Form base = ref.GetBaseObject();; Debug
    LTMN:Quest:Methods.TraceForm(prefix + "  ", base);; Debug
    Lootman.Log(prefix + "    Is vendor chest: " + properties.VendorChestList.HasForm(base));; Debug
EndFunction;; Debug

bool Function _TryUnlock(ObjectReference ref)
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug

    If (!ref.IsLockBroken() && properties.AllowContainerUnlock.GetValueInt() == 1)
        Lootman.Log(prefix + "*** Try to unlock ***");; Debug
        Lootman.Log(prefix + "  Object: [Name: " + ref.GetDisplayName() + ", ID: " + Lootman.GetHexID(ref) + "]");; Debug

        ObjectReference _lootman = properties.LootmanWorkshop
        int level = ref.GetLockLevel()
        int bobbyPinCount = _lootman.GetItemCount(properties.BobbyPin)

        Lootman.Log(prefix + "  Lock level: " + level);; Debug

        int consumeCount = -1
        If (level == 100 && player.HasPerk(properties.Locksmith03))
            consumeCount = 4
        ElseIf (level == 75 && player.HasPerk(properties.Locksmith02))
            consumeCount = 3
        ElseIf (level == 50 && player.HasPerk(properties.Locksmith01))
            consumeCount = 2
        ElseIf (level < 50)
            consumeCount = 1
        EndIf

        If (consumeCount > 0 && player.HasPerk(properties.Locksmith04))
            consumeCount = 0
        EndIf

        Lootman.Log(prefix + "  Consume count: " + consumeCount);; Debug
        Lootman.Log(prefix + "  Bobby pin count: " + bobbyPinCount);; Debug

        If (consumeCount >= 0 && bobbyPinCount >= consumeCount)
            If (consumeCount > 0)
                _lootman.RemoveItem(properties.BobbyPin, consumeCount, true)
            EndIf

            ref.Unlock()

            Lootman.Log(prefix + "*** Unlock successful ***");; Debug
            Return true
        EndIf
    EndIf

    Lootman.Log(prefix + "*** Unlock failure ***");; Debug
    Return false
EndFunction
