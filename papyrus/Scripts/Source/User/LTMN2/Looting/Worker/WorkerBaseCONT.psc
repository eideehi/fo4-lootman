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
    If (player.WouldBeStealing(ref))
        Return false
    EndIf
    Return !ref.IsLocked() || TryUnlock(ref, player) || TryUnlock(ref, properties.LootManWorkshopRef)
EndFunction

Function LootObject(ObjectReference ref)
    If (properties.IsNotInitialized)
        Return
    EndIf

    int itemCount = LTMN2:LootMan.TransferLootableInventoryItems(ref, properties.LootManRef, properties.LootableInventoryItemType)

    If (itemCount > 0)
        If (properties.PlayContainerAnimation && player.HasDetectionLoS(ref))
            MakeActivatorFriend(ref)
            If (!properties.ActivatorRef.WouldBeStealing(ref))
                ref.Activate(properties.ActivatorRef, true)
            EndIf
        EndIf
    EndIf
EndFunction

bool Function TryUnlock(ObjectReference ref, ObjectReference bobbyPinContainerRef)
    If (properties.IsNotInitialized)
        Return false
    EndIf

    If (!ref.IsLockBroken() && properties.UnlockLockedContainer)
        int level = ref.GetLockLevel()
        int bobbyPinCount = bobbyPinContainerRef.GetItemCount(properties.BobbyPin)

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

        LogWorkerEvent("container_unlock_attempt", GetThreadField() + " ref=" + LTMN2:LootMan.GetHexID(ref) + " bobby_pin_owner=" + LTMN2:LootMan.GetHexID(bobbyPinContainerRef) + " lock_level=" + level + " required_pins=" + consumeCount + " available_pins=" + bobbyPinCount)

        If (consumeCount >= 0 && bobbyPinCount >= consumeCount)
            If (consumeCount > 0)
                bobbyPinContainerRef.RemoveItem(properties.BobbyPin, consumeCount, true)
            EndIf

            ref.Unlock()

            LogWorkerEvent("container_unlock_succeeded", GetThreadField() + " ref=" + LTMN2:LootMan.GetHexID(ref) + " consumed_pins=" + consumeCount)
            Return true
        ElseIf (bobbyPinCount < consumeCount)
            LTMN2:System system = LTMN2:System.GetInstance()
            system.ShowMessage(system.MESSAGE_NOT_HAVE_BOBBY_PIN)
        EndIf
    EndIf

    LogWorkerEvent("container_unlock_failed", GetThreadField() + " ref=" + LTMN2:LootMan.GetHexID(ref))
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
