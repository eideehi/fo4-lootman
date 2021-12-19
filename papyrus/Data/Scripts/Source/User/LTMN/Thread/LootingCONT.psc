Scriptname LTMN:Thread:LootingCONT extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_CONT
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

    If (lootCount > 0)
        If (properties.PlayContainerAnimation.GetValueInt() == 1)
            ref.Activate(properties.ActivatorActor)
        ElseIf (properties.PlayPickupSound.GetValueInt() == 1)
            properties.PickupSoundNPC_.Play(player)
        EndIf
    EndIf
    LTMN:Debug.Log(prefix + "  ** Done looting **")
    LTMN:Debug.Log(prefix + "  Inventory status after looting: [ItemCount: " + ref.GetItemCount() + ", TotalWeight: " + ref.GetInventoryWeight() + "]")
EndFunction

bool Function IsLootingTarget(ObjectReference ref)
    Form base = ref.GetBaseObject()

    If (!ref.Is3DLoaded() || (ref.IsActivationBlocked() && !properties.IgnorableActivationBlockeList.HasForm(base)) || ref.GetItemCount() <= 0)
        Return false
    EndIf

    If (!IsValidObject(ref) || !IsLootableOwnership(ref))
        Return false
    EndIf

    If (LTMN:Lootman.IsLinkedToWorkshop(ref) || ref Is WorkshopScript)
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

Function TraceObject(ObjectReference ref) debugOnly
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ")
    LTMN:Debug.TraceObject(prefix, ref)
    LTMN:Debug.Log(prefix + "  Is locked: " + ref.IsLocked())
    LTMN:Debug.Log(prefix + "  Stored items count: " + ref.GetItemCount())
    LTMN:Debug.Log(prefix + "  Is linked to workshop: " + LTMN:Lootman.IsLinkedToWorkshop(ref))
    LTMN:Debug.Log(prefix + "  Is workshop: " + (ref Is WorkshopScript))
    Form base = ref.GetBaseObject()
    LTMN:Debug.TraceForm(prefix + "  ", base)
    LTMN:Debug.Log(prefix + "    Is vendor chest: " + properties.VendorChestList.HasForm(base))
EndFunction

bool Function _TryUnlock(ObjectReference ref)
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ")

    If (!ref.IsLockBroken() && properties.AllowContainerUnlock.GetValueInt() == 1)
        LTMN:Debug.Log(prefix + "*** Try to unlock ***")
        LTMN:Debug.Log(prefix + "  Object: [Name: " + ref.GetDisplayName() + ", ID: " + LTMN:Debug.GetHexID(ref) + "]")

        ObjectReference lootman = properties.LootmanWorkshop
        int level = ref.GetLockLevel()
        int bobbyPinCount = lootman.GetItemCount(properties.BobbyPin)

        LTMN:Debug.Log(prefix + "  Lock level: " + level)

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

        LTMN:Debug.Log(prefix + "  Consume count: " + consumeCount)
        LTMN:Debug.Log(prefix + "  Bobby pin count: " + bobbyPinCount)

        If (consumeCount >= 0 && bobbyPinCount >= consumeCount)
            If (consumeCount > 0)
                lootman.RemoveItem(properties.BobbyPin, consumeCount, true)
            EndIf

            ref.Unlock()

            LTMN:Debug.Log(prefix + "*** Unlock successful ***")
            Return true
        EndIf
    EndIf

    LTMN:Debug.Log(prefix + "*** Unlock failure ***")
    Return false
EndFunction
