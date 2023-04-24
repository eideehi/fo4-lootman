Scriptname LTMN2:Looting:Worker:WorkerBaseCONT extends LTMN2:Looting:Worker:WorkerBase hidden

Function IncreaseActiveThreadCount()
    properties.ActiveWorkerThreadsCONT += 1
EndFunction

Function DecreaseActiveThreadCount()
    properties.ActiveWorkerThreadsCONT -= 1
EndFunction

int Function GetFormTypeOfAssignment()
    Return properties.FORM_TYPE_CONT
EndFunction

Function SetTurboMode()
    properties.TurboModeCONT = true
EndFunction

bool Function IsLootingTarget(ObjectReference ref)
    If (Utility.IsInMenuMode())
        Return false
    EndIf
    If (!IsLootableDistance(ref) || !ref.Is3DLoaded())
        Return false
    EndIf
    If (player.WouldBeStealing(ref))
        Return false
    EndIf
    Return !ref.IsLocked() || TryUnlock(ref)
EndFunction

Function LootObject(ObjectReference ref)
    If (properties.IsNotInitialized)
        Return
    EndIf

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

    If (itemCount > 0)
        If (properties.PlayContainerAnimation && player.HasDetectionLoS(ref))
            MakeActivatorFriend(ref)
            If (!properties.ActivatorRef.WouldBeStealing(ref))
                ref.Activate(properties.ActivatorRef, true)
            EndIf
        EndIf
    EndIf
EndFunction

bool Function TryUnlock(ObjectReference ref)
    If (properties.IsNotInitialized)
        Return false
    EndIf

    string prefix = GetLogPrefix(2)

    If (!ref.IsLockBroken() && properties.UnlockLockedContainer)
        LTMN2:Debug.Log(prefix + "[ Try to unlock ]")
        LTMN2:Debug.Log(prefix + "  Object: [ Name: \"" + ref.GetDisplayName() + "\", Id: " + LTMN2:Debug.GetHexID(ref) + " ]")

        ObjectReference lootman = properties.LootManWorkshopRef
        int level = ref.GetLockLevel()
        int bobbyPinCount = lootman.GetItemCount(properties.BobbyPin)

        LTMN2:Debug.Log(prefix + "  Lock level: " + level)

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

        LTMN2:Debug.Log(prefix + "  Number of bobby pins required: " + consumeCount)
        LTMN2:Debug.Log(prefix + "  Bobby pin count: " + bobbyPinCount)

        If (consumeCount >= 0 && bobbyPinCount >= consumeCount)
            If (consumeCount > 0)
                lootman.RemoveItem(properties.BobbyPin, consumeCount, true)
            EndIf

            ref.Unlock()

            LTMN2:Debug.Log(prefix + "  [ Unlock successful ]")
            Return true
        ElseIf (bobbyPinCount < consumeCount)
            LTMN2:System system = LTMN2:System.GetInstance()
            system.ShowMessage(system.MESSAGE_NOT_HAVE_BOBBY_PIN)
        EndIf
    EndIf

    LTMN2:Debug.Log(prefix + "  [ Unlock failure ]")
    Return false
EndFunction

Function MakeActivatorFriend(ObjectReference ref)
    If (!properties.ActivatorRef.WouldBeStealing(ref))
        Return
    EndIf

    Faction ownerAsFaction = ref.GetFactionOwner()
    If (ownerAsFaction)
        properties.ActivatorRef.AddToFaction(ownerAsFaction)
    EndIf
    Actor ownerAsActorRef = ref.GetActorRefOwner()
    If (ownerAsActorRef)
        properties.ActivatorRef.SetRelationshipRank(ownerAsActorRef, 1)
    EndIf
    ActorBase ownerAsActor = ref.GetActorOwner()
    If (ownerAsActor && ownerAsActor.IsUnique())
        properties.ActivatorRef.SetRelationshipRank(ownerAsActor.GetUniqueActor(), 1)
    EndIf
EndFunction

Function TraceObject(string logPrefix, ObjectReference ref) debugOnly
    LTMN2:Debug.TraceObject(logPrefix, ref)
    LTMN2:Debug.Log(logPrefix + "  Is locked: " + ref.IsLocked())
    LTMN2:Debug.Log(logPrefix + "  Is workshop: " + (ref Is WorkshopScript))
EndFunction
