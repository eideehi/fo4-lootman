Scriptname LTMN:Quest:System extends Quest

Form property Pipboy auto const mandatory
Quest property RadioInstitute auto const mandatory

Quest property LootingACTIQuest auto const mandatory
Quest property LootingALCHQuest auto const mandatory
Quest property LootingAMMOQuest auto const mandatory
Quest property LootingARMOQuest auto const mandatory
Quest property LootingBOOKQuest auto const mandatory
Quest property LootingCONTQuest auto const mandatory
Quest property LootingFLORQuest auto const mandatory
Quest property LootingINGRQuest auto const mandatory
Quest property LootingKEYMQuest auto const mandatory
Quest property LootingMISCQuest auto const mandatory
Quest property LootingNPC_Quest auto const mandatory
Quest property LootingWEAPQuest auto const mandatory

int TIMER_TRY_INSTALL = 1 const
int TIMER_LOOTING = 2 const
int TIMER_UPDATE_STATE = 3 const
int TIMER_INITIALIZE = 4 const

Actor player
LTMN:Quest:Properties properties

float modVersion = 0.0

int messageShowTimer = 0
int messageShowCount = 0

int lastAutomaticallyLinkOrUnlinkToWorkshop = 0
int lastLootingDisabledInSettlement = 0

Event OnInit()
    Lootman.OpenLog();; Debug
    Lootman.Log("| System | Lootman has been running for the first time");; Debug

    player = Game.GetPlayer()
    properties = Lootman.GetProperties()
    modVersion = properties.ModVersion.GetValue()

    StartTimer(5, TIMER_TRY_INSTALL)
EndEvent

Event OnTimer(int aiTimerID)
    If (aiTimerID == TIMER_LOOTING)
        TryLooting()
        StartTimer(properties.ThreadInterval.GetValue(), TIMER_LOOTING)
    ElseIf (aiTimerID == TIMER_UPDATE_STATE)
        UpdaetState()
        StartTimer(1, TIMER_UPDATE_STATE)
    ElseIf (aiTimerID == TIMER_TRY_INSTALL)
        If (!TryInstall())
            StartTimer(1, TIMER_TRY_INSTALL)
        EndIf
    ElseIf (aiTimerID == TIMER_INITIALIZE)
        Initialize()
    EndIf
EndEvent

; An event that register to player. Process that should be performed when loading save data.
Event Actor.OnPlayerLoadGame(Actor akSender)
    Lootman.OpenLog();; Debug
    Lootman.Log("| System | Lootman has been running");; Debug

    ; Reset the number of times the overweight notification message is displayed for each game load
    messageShowCount = 0

    ; Apply the patches if the save data and the Mod version of the esp do not match due to the Lootman update.
    If (modVersion < properties.ModVersion.GetValue())
        Patch()
    EndIf

    Initialize()
EndEvent

; An event that register to player. Process that should be performed when location is changed.
Event Actor.OnLocationChange(Actor akSender, Location akOldLoc, Location akNewLoc)
    string prefix = ("| System | " + Lootman.GetRandomProcessID() + " | ");; Debug
    Lootman.Log(prefix + "*** Start the process that will be executed when the location is changed ***");; Debug
    Lootman.Log(prefix + "  Old location: [Name: " + Lootman.GetIdentify(akOldLoc) + ", ID: " + Lootman.GetHexID(akOldLoc) + "]");; Debug
    Lootman.Log(prefix + "  New location: [Name: " + Lootman.GetIdentify(akNewLoc) + ", ID: " + Lootman.GetHexID(akNewLoc) + "]");; Debug

    Lootman.Log(prefix + "  Suppression of automatic looting in settlement: " + (properties.LootingDisabledInSettlement.GetValueInt() == 1));; Debug
    If (properties.LootingDisabledInSettlement.GetValueInt() == 1)
        Lootman.Log(prefix + "    New location is a settlement (LocTypeSettlement): " + akNewLoc.HasKeyword(Game.GetCommonProperties().LocTypeSettlement));; Debug
        Lootman.Log(prefix + "    New location is a settlement (LocTypeWorkshopSettlement): " + akNewLoc.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement));; Debug

        If (akNewLoc.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || akNewLoc.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement))
            properties.IsInSettlement.SetValueInt(1)
            ShowMessage(properties.MESSAGE_PLAYER_IN_SETTLEMENT);; Debug
            Lootman.Log(prefix + "      ** Player are in the settlement **");; Debug
        Else
            properties.IsInSettlement.SetValueInt(0)
            ShowMessage(properties.MESSAGE_PLAYER_OUT_SETTLEMENT);; Debug
            Lootman.Log(prefix + "      ** Player has left the settlement **");; Debug
        EndIf
    EndIf

    Lootman.Log(prefix + "  Automatic workshop links: " + (properties.AutomaticallyLinkOrUnlinkToWorkshop.GetValueInt() == 1));; Debug
    If (properties.AutomaticallyLinkOrUnlinkToWorkshop.GetValueInt() == 1)
        Lootman.Log(prefix + "    Workshops exist in the old location: " + (properties.WorkshopParent.GetWorkshopFromLocation(akOldLoc) != None));; Debug
        If (properties.WorkshopParent.GetWorkshopFromLocation(akOldLoc))
            Lootman.Log(prefix + "      Linked to Lootman: " + akOldLoc.IsLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan));; Debug
            If (akOldLoc.IsLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan))
                Lootman.Log(prefix + "      ** Removed link with Lootman **");; Debug
                akOldLoc.RemoveLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan)
                ShowMessage(properties.MESSAGE_UNLINKED_TO_WORKSHOP);; Debug
            EndIf
        EndIf

        Lootman.Log(prefix + "    Workshops exist in the new location: " + (properties.WorkshopParent.GetWorkshopFromLocation(akNewLoc) != None));; Debug
        If (properties.WorkshopParent.GetWorkshopFromLocation(akNewLoc))
            Lootman.Log(prefix + "      Linked to Lootman: " + akNewLoc.IsLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan));; Debug
            If (!akNewLoc.IsLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan))
                Lootman.Log(prefix + "      ** Added link to Lootman **");; Debug
                akNewLoc.AddLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan)
                ShowMessage(properties.MESSAGE_LINKED_TO_WORKSHOP);; Debug
            EndIf
        EndIf
    EndIf

    Lootman.Log(prefix + "*** The process performed when the location is changed has been completed ***");; Debug
EndEvent

; Event hook when the menu is opened.
Event OnMenuOpenCloseEvent(string asMenuName, bool abOpening)
    If (asMenuName == "PipboyMenu")
        If (abOpening)
            properties.IsPipboyOpen.SetValueInt(1)
        Else
            properties.IsPipboyOpen.SetValueInt(0)
        EndIf
    EndIf
EndEvent

; Events to be registered in Lootman's Actor. Moves the looted item to the appropriate container.
Event ObjectReference.OnItemAdded(ObjectReference akSender, Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akSourceContainer)
    string prefix = ("| System | " + Lootman.GetRandomProcessID() + " | ");; Debug

    ; Shipment will be lost if it is moved to the Wrokshop container using RemoveItem, so add it using AddItem. Don't forget to delete the item from the original container.
    If (properties.ShipmentItemList.HasForm(akBaseItem))
        Lootman.Log(prefix + "*** Lootman is loot the shipments, move to the player's inventory ***");; Debug
        akSender.RemoveItem(akBaseItem, aiItemCount, true)
        properties.LootmanWorkshop.AddItem(akBaseItem, aiItemCount, true)
    EndIf
EndEvent

; Called by the timer function. Try to install Lootman. Returns true if there is no need to call this function any more.
bool Function TryInstall()
    string prefix = ("| System | " + Lootman.GetRandomProcessID() + " | ");; Debug

    If (properties.IsInstalled.GetValueInt() == 1)
        Lootman.Log(prefix + "*** Lootman is already installed, the installation process will not be executed ***");; Debug
        Return true
    EndIf

    If (properties.IsUninstalled.GetValueInt() == 1)
        Lootman.Log(prefix + "*** Lootman has been uninstalled, the installation process will not be executed ***");; Debug
        Return true
    EndIf

    Lootman.Log(prefix + "*** Try to install Lootman ***");; Debug

    Lootman.Log(prefix + "  Player has a Pipboy: " + (player.GetItemCount(Pipboy) > 0));; Debug
    Lootman.Log(prefix + "  Institute's radio is ready to receive: " + RadioInstitute.IsRunning());; Debug

    ; If the player has a Pipboy or can receive the Institute's radio, perform the installation
    If (player.GetItemCount(Pipboy) > 0 || RadioInstitute.IsRunning())
        Lootman.Log(prefix + "*** Execute the Lootman installation ***");; Debug
        Install()
        Return true
    Else
        Lootman.Log(prefix + "*** The Lootman installation was not executed ***");; Debug
        Return false
    EndIf
EndFunction

; Install Lootman
Function Install()
    string prefix = ("| System | " + Lootman.GetRandomProcessID() + " | ");; Debug

    If (properties.IsInstalled.GetValueInt() == 1)
        Lootman.Log(prefix + "*** Lootman is already installed, the installation process will not be executed ***");; Debug
        Return
    EndIf

    If (properties.IsUninstalled.GetValueInt() == 1)
        Lootman.Log(prefix + "*** Lootman has been uninstalled, the installation process will not be executed ***");; Debug
        Return
    EndIf

    Lootman.Log(prefix + "*** Start Lootman installation ***");; Debug

    Lootman.Log(prefix + "  ** Register an event **");; Debug
    RegisterForRemoteEvent(player, "OnPlayerLoadGame")
    RegisterForRemoteEvent(player, "OnLocationChange")
    RegisterForMenuOpenCloseEvent("PipboyMenu")
    AddInventoryEventFilter(properties.ShipmentItemList)
    RegisterForRemoteEvent(properties.LootmanActor, "OnItemAdded")

    Lootman.Log(prefix + "  ** Start thread manager **");; Debug
    (LootingACTIQuest As LTMN:Thread:LootingACTIManager).Startup()
    (LootingALCHQuest As LTMN:Thread:LootingALCHManager).Startup()
    (LootingAMMOQuest As LTMN:Thread:LootingAMMOManager).Startup()
    (LootingARMOQuest As LTMN:Thread:LootingARMOManager).Startup()
    (LootingBOOKQuest As LTMN:Thread:LootingBOOKManager).Startup()
    (LootingCONTQuest As LTMN:Thread:LootingCONTManager).Startup()
    (LootingFLORQuest As LTMN:Thread:LootingFLORManager).Startup()
    (LootingINGRQuest As LTMN:Thread:LootingINGRManager).Startup()
    (LootingKEYMQuest As LTMN:Thread:LootingKEYMManager).Startup()
    (LootingMISCQuest As LTMN:Thread:LootingMISCManager).Startup()
    (LootingNPC_Quest As LTMN:Thread:LootingNPC_Manager).Startup()
    (LootingWEAPQuest As LTMN:Thread:LootingWEAPManager).Startup()

    Lootman.Log(prefix + "  ** Start timer function **");; Debug
    StartTimer(1, TIMER_INITIALIZE)
    StartTimer(3, TIMER_UPDATE_STATE)
    StartTimer(5, TIMER_LOOTING)

    Lootman.Log(prefix + "  ** Add utility holotape to the player's inventory **");; Debug
    player.AddItem(properties.UtilityHolotape)

    properties.IsInstalled.SetValueInt(1)

    self.SetObjectiveDisplayed(properties.MESSAGE_LOOTMAN_INSTALLED)
    self.SetObjectiveSkipped(properties.MESSAGE_LOOTMAN_INSTALLED)

    Lootman.Log(prefix + "*** Lootman installation completed ***");; Debug
EndFunction

; Uninstall Lootman
Function Uninstall()
    string prefix = ("| System | " + Lootman.GetRandomProcessID() + " | ");; Debug

    If (properties.IsInstalled.GetValueInt() != 1)
        Lootman.Log(prefix + "*** Lootman is not installed, the uninstallation process will not be executed ***");; Debug
        Return
    EndIf

    If (properties.IsUninstalled.GetValueInt() == 1)
        Lootman.Log(prefix + "*** Lootman has already been uninstalled, the uninstall process will not be executed ***");; Debug
        Return
    EndIf

    Lootman.Log(prefix + "*** Start Lootman uninstallation ***");; Debug

    properties.IsUninstalled.SetValueInt(1)

    Lootman.Log(prefix + "  ** Stop timer function **");; Debug
    CancelTimer(TIMER_TRY_INSTALL)
    CancelTimer(TIMER_LOOTING)
    CancelTimer(TIMER_UPDATE_STATE)
    CancelTimer(TIMER_INITIALIZE)

    Lootman.Log(prefix + "  ** Shutdown thread manager **");; Debug
    (LootingACTIQuest As LTMN:Thread:LootingACTIManager).Shutdown()
    (LootingALCHQuest As LTMN:Thread:LootingALCHManager).Shutdown()
    (LootingAMMOQuest As LTMN:Thread:LootingAMMOManager).Shutdown()
    (LootingARMOQuest As LTMN:Thread:LootingARMOManager).Shutdown()
    (LootingBOOKQuest As LTMN:Thread:LootingBOOKManager).Shutdown()
    (LootingCONTQuest As LTMN:Thread:LootingCONTManager).Shutdown()
    (LootingFLORQuest As LTMN:Thread:LootingFLORManager).Shutdown()
    (LootingINGRQuest As LTMN:Thread:LootingINGRManager).Shutdown()
    (LootingKEYMQuest As LTMN:Thread:LootingKEYMManager).Shutdown()
    (LootingMISCQuest As LTMN:Thread:LootingMISCManager).Shutdown()
    (LootingNPC_Quest As LTMN:Thread:LootingNPC_Manager).Shutdown()
    (LootingWEAPQuest As LTMN:Thread:LootingWEAPManager).Shutdown()

    LootingACTIQuest.Stop()
    LootingALCHQuest.Stop()
    LootingAMMOQuest.Stop()
    LootingARMOQuest.Stop()
    LootingBOOKQuest.Stop()
    LootingCONTQuest.Stop()
    LootingFLORQuest.Stop()
    LootingINGRQuest.Stop()
    LootingKEYMQuest.Stop()
    LootingMISCQuest.Stop()
    LootingNPC_Quest.Stop()
    LootingWEAPQuest.Stop()

    Lootman.Log(prefix + "  ** Unregister an event **");; Debug
    UnregisterForRemoteEvent(player, "OnPlayerLoadGame")
    UnregisterForRemoteEvent(player, "OnLocationChange")
    UnregisterForMenuOpenCloseEvent("PipboyMenu")
    UnregisterForRemoteEvent(properties.LootmanActor, "OnItemAdded")

    self.SetObjectiveDisplayed(properties.MESSAGE_LOOTMAN_UNINSTALLED)
    self.SetObjectiveSkipped(properties.MESSAGE_LOOTMAN_UNINSTALLED)

    Lootman.Log(prefix + "*** Lootman uninstallation completed ***");; Debug

    self.Stop()
EndFunction

; Initialize Lootman
Function Initialize()
    string prefix = ("| System | " + Lootman.GetRandomProcessID() + " | ");; Debug

    If (properties.IsInitializing.GetValueInt() == 1)
        Lootman.Log(prefix + "*** Lootman is already in the process of initialization, the initialization process will not be executed ***");; Debug
        Return
    EndIf

    If (properties.IsUninstalled.GetValueInt() == 1)
        Lootman.Log(prefix + "*** Lootman has been uninstalled, the initialization process will not be executed ***");; Debug
        Return
    EndIf

    Lootman.Log(prefix + "*** Start Lootman initialization ***");; Debug

    properties.IsInitializing.SetValueInt(1)

    Lootman.Log(prefix + "  ** Clear form list **");; Debug
    _ClearFormList(properties.AllowedActivatorList)
    _ClearFormList(properties.AllowedFeaturedItemList)
    _ClearFormList(properties.AllowedUniqueItemList)
    _ClearFormList(properties.ExcludeFormList)
    _ClearFormList(properties.ExcludeKeywordList)
    _ClearFormList(properties.ExcludeLocationRefList)
    _ClearFormList(properties.IgnorableActivationBlockeList)
    _ClearFormList(properties.VendorChestList)
    _ClearFormList(properties.WeaponTypeGrenadeKeywordList)
    _ClearFormList(properties.WeaponTypeMineKeywordList)

    Lootman.Log(prefix + "  ** Setup form list **");; Debug
    _SetupFormList(properties.AllowedActivatorList, properties.LIST_IDENTIFY_ALLOWED_ACTIVATOR)
    _SetupFormList(properties.AllowedFeaturedItemList, properties.LIST_IDENTIFY_ALLOWED_FEATUREDITEM)
    _SetupFormList(properties.AllowedUniqueItemList, properties.LIST_IDENTIFY_ALLOWED_UNIQUEITEM)
    _SetupFormList(properties.ExcludeFormList, properties.LIST_IDENTIFY_EXCLUDE_FORM)
    _SetupFormList(properties.ExcludeKeywordList, properties.LIST_IDENTIFY_EXCLUDE_KEYWORD)
    _SetupFormList(properties.ExcludeLocationRefList, properties.LIST_IDENTIFY_EXCLUDE_LOCATIONREF)
    _SetupFormList(properties.IgnorableActivationBlockeList, properties.LIST_IDENTIFY_IGNORABLE_ACTIVATION_BLOCKE)
    _SetupFormList(properties.VendorChestList, properties.LIST_IDENTIFY_VENDOR_CHEST)
    _SetupFormList(properties.WeaponTypeGrenadeKeywordList, properties.LIST_IDENTIFY_WEAPONTYPE_GRENADE_KEYWORD)
    _SetupFormList(properties.WeaponTypeMineKeywordList, properties.LIST_IDENTIFY_WEAPONTYPE_MINE_KEYWORD)

    properties.IsInitializing.SetValueInt(0)

    Lootman.Log(prefix + "*** Lootman initialization completed ***");; Debug
EndFunction

; Apply patches for migration
Function Patch()
    string prefix = ("| System | " + Lootman.GetRandomProcessID() + " | ");; Debug

    Lootman.Log(prefix + "*** Start patching Lootman ***");; Debug
    Lootman.Log(prefix + "  Current version: " + modVersion);; Debug

    ; Patches for version 1.3.6
    If (modVersion < 1.0306)
        Lootman.Log(prefix + "  Patches for version 1.3.6");; Debug
        AddInventoryEventFilter(properties.ShipmentItemList)
        RegisterForRemoteEvent(properties.LootmanActor, "OnItemAdded")
    EndIf

    modVersion = properties.ModVersion.GetValue()

    Lootman.Log(prefix + "  Update to version: " + modVersion);; Debug
    Lootman.Log(prefix + "*** Lootman patching completed ***");; Debug
EndFunction

; Update the status of Lootman
Function UpdaetState()
    string prefix = ("| System | " + Lootman.GetRandomProcessID() + " | ");; Debug

    If (properties.IsInitializing.GetValueInt() == 1)
        Lootman.Log(prefix + "*** Lootman is in the process of initialization, the state update will not be executed ***");; Debug
        Return
    EndIf

    If (properties.IsUninstalled.GetValueInt() == 1)
        Lootman.Log(prefix + "*** Lootman has been uninstalled, the state update will not be executed ***");; Debug
        Return
    EndIf

    If (!IsWorldActive())
        Lootman.Log(prefix + "*** World is not in active ***");; Debug
        Return
    EndIf

    Lootman.Log(prefix + "*** Start updating the state ***");; Debug

    Lootman.Log(prefix + "  Lootman's (Actor) inventory weight: " + properties.LootmanActor.GetInventoryWeight());; Debug
    If (properties.LootmanActor.GetInventoryWeight() > 0)
        Lootman.Log(prefix + "    ** Move items from actor to workshop **");; Debug
        LTMN:Quest:Methods.MoveInventoryItems(properties.LootmanActor, properties.LootmanWorkshop, -1, 0)
        Lootman.Log(prefix + "      Inventory weight after processing: " + properties.LootmanActor.GetInventoryWeight());; Debug
    EndIf

    If (properties.ActivatorActor.GetInventoryWeight() > 0)
        properties.ActivatorActor.RemoveAllItems()
    EndIf

    Lootman.Log(prefix + "  Check for overweight in Lootman inventory: " + (properties.IgnoreOverweight.GetValueInt() != 1));; Debug
    If (properties.IgnoreOverweight.GetValueInt() != 1)
        Lootman.Log(prefix + "    Currently in an overweight state: " + (properties.IsOverweight.GetValueInt() != 0));; Debug
        Lootman.Log(prefix + "    Lootman's (Container) inventory weight: " + properties.LootmanWorkshop.GetInventoryWeight());; Debug
        Lootman.Log(prefix + "    Weight carrying capacity: " + properties.CarryWeight.GetValueInt());; Debug

        bool wasOverweight = properties.IsOverweight.GetValueInt() != 0
        bool overweight = properties.LootmanWorkshop.GetInventoryWeight() > properties.CarryWeight.GetValue()
        If (!wasOverweight && overweight)
            properties.IsOverweight.SetValueInt(1)
            Lootman.Log(prefix + "    ** Overweight has been resolved **");; Debug
        ElseIf (wasOverweight && overweight)
            ; Display an overweight notification message every 30 seconds; no message is displayed after the third time.
            If (messageShowCount < 3)
                messageShowTimer -= 1
            EndIf
            If (messageShowTimer <= 0)
                ShowMessage(properties.MESSAGE_HAS_OVERWEIGHT)
                messageShowTimer = 30
                messageShowCount += 1
            EndIf
        ElseIf (wasOverweight && !overweight)
            properties.IsOverweight.SetValueInt(0)
            ShowMessage(properties.MESSAGE_SOLVED_OVERWEIGHT)
            messageShowCount = 0
            Lootman.Log(prefix + "    ** Now in an overweight state **");; Debug
        EndIf
    ElseIf (properties.IsOverweight.GetValueInt() != 0)
        properties.IsOverweight.SetValueInt(0)
    EndIf

    Lootman.Log(prefix + "  Automatically link Lootman to workshops: " + (properties.AutomaticallyLinkOrUnlinkToWorkshop.GetValueInt() == 1));; Debug
    int AutomaticallyLinkOrUnlinkToWorkshop = properties.AutomaticallyLinkOrUnlinkToWorkshop.GetValueInt()
    If (AutomaticallyLinkOrUnlinkToWorkshop != lastAutomaticallyLinkOrUnlinkToWorkshop)
        Lootman.Log(prefix + "    ** Adjust the link status between Lootman and the workshop because Lootman's configuration has been changed **");; Debug
        Lootman.Log(prefix + "      Workshop exists at the current location: " + (properties.WorkshopParent.GetWorkshopFromLocation(player.GetCurrentLocation()) != None));; Debug

        Location currentLocation = player.GetCurrentLocation()
        If (properties.WorkshopParent.GetWorkshopFromLocation(currentLocation))
            Lootman.Log(prefix + "      Link to current location: " + currentLocation.IsLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan));; Debug
            bool linkToCurrentLocation = currentLocation.IsLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan)
            If (!linkToCurrentLocation && AutomaticallyLinkOrUnlinkToWorkshop == 1)
                Lootman.Log(prefix + "      ** Add the link to the current location **");; Debug
                currentLocation.AddLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan)
                ShowMessage(properties.MESSAGE_LINKED_TO_WORKSHOP);; Debug
            ElseIf (linkToCurrentLocation && AutomaticallyLinkOrUnlinkToWorkshop != 0)
                Lootman.Log(prefix + "      ** Remove the link to the current location **");; Debug
                currentLocation.RemoveLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan)
                ShowMessage(properties.MESSAGE_UNLINKED_TO_WORKSHOP);; Debug
            Else;; Debug
                Lootman.Log(prefix + "      ** No need to adjust the link status **");; Debug
            EndIf
        Else;; Debug
            Lootman.Log(prefix + "      ** No need to adjust the link status **");; Debug
        EndIf
        lastAutomaticallyLinkOrUnlinkToWorkshop = AutomaticallyLinkOrUnlinkToWorkshop
    EndIf

    Lootman.Log(prefix + "  Suppress automatic looting in settlement: " + (properties.LootingDisabledInSettlement.GetValueInt() == 1));; Debug
    int currentLootingDisabledInSettlement = properties.LootingDisabledInSettlement.GetValueInt()
    If (currentLootingDisabledInSettlement != lastLootingDisabledInSettlement)
        Location currentLocation = player.GetCurrentLocation()
        Lootman.Log(prefix + "    ** Adjust the flag whether you are staying in the settlement because Lootman's configuration has been changed **");; Debug
        Lootman.Log(prefix + "      Current location is a settlement (LocTypeSettlement): " + currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeSettlement));; Debug
        Lootman.Log(prefix + "      Current location is a settlement (LocTypeWorkshopSettlement): " + currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement));; Debug

        If (currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement))
            properties.IsInSettlement.SetValueInt(1)
        Else
            properties.IsInSettlement.SetValueInt(0)
        EndIf

        Lootman.Log(prefix + "      Player is in the settlement: " + properties.IsInSettlement.GetValueInt());; Debug
        lastLootingDisabledInSettlement = currentLootingDisabledInSettlement
    EndIf

    Lootman.Log(prefix + "  ** Reset timestamp for expired objects **");; Debug
    ObjectReference[] refs = Lootman.GetAllExpiredObject(player, properties.LootingRange.GetValueInt(), Utility.GetCurrentRealTime(), properties.ExpirationToSkipLooting.GetValue())
    Lootman.Log(prefix + "    Object count: " + refs.Length);; Debug
    int i = refs.Length
    While i
        i -= 1
        ObjectReference ref = refs[i]
        If (Lootman.IsValidRef(ref))
            LTMN:Quest:Methods.TraceObject(prefix + "    ", ref);; Debug
            ref.SetValue(properties.LastLootingTimestamp, 0)
        EndIf
    EndWhile

    Lootman.Log(prefix + "*** State update is complete ***");; Debug
EndFunction

; Try to start a thread to loot
Function TryLooting()
    If (properties.IsInitializing.GetValueInt() == 1 || properties.IsUninstalled.GetValueInt() == 1)
        Return
    EndIf

    If (!IsWorldActive())
        Return
    EndIf

    If (properties.TargetFilterObject.GetValueInt() == 1)
        If (properties.CategoryFilterACTI.GetValueInt() == 1)
            (LootingACTIQuest As LTMN:Thread:LootingACTIManager).TryLooting()
        EndIf
        If (properties.CategoryFilterALCH.GetValueInt() == 1)
            (LootingALCHQuest As LTMN:Thread:LootingALCHManager).TryLooting()
        EndIf
        If (properties.CategoryFilterAMMO.GetValueInt() == 1)
            (LootingAMMOQuest As LTMN:Thread:LootingAMMOManager).TryLooting()
        EndIf
        If (properties.CategoryFilterARMO.GetValueInt() == 1)
            (LootingARMOQuest As LTMN:Thread:LootingARMOManager).TryLooting()
        EndIf
        If (properties.CategoryFilterBOOK.GetValueInt() == 1)
            (LootingBOOKQuest As LTMN:Thread:LootingBOOKManager).TryLooting()
        EndIf
        If (properties.CategoryFilterFLOR.GetValueInt() == 1)
            (LootingFLORQuest As LTMN:Thread:LootingFLORManager).TryLooting()
        EndIf
        If (properties.CategoryFilterINGR.GetValueInt() == 1)
            (LootingINGRQuest As LTMN:Thread:LootingINGRManager).TryLooting()
        EndIf
        If (properties.CategoryFilterKEYM.GetValueInt() == 1)
            (LootingKEYMQuest As LTMN:Thread:LootingKEYMManager).TryLooting()
        EndIf
        If (properties.CategoryFilterMISC.GetValueInt() == 1)
            (LootingMISCQuest As LTMN:Thread:LootingMISCManager).TryLooting()
        EndIf
        If (properties.CategoryFilterWEAP.GetValueInt() == 1)
            (LootingWEAPQuest As LTMN:Thread:LootingWEAPManager).TryLooting()
        EndIf
    EndIf

    If (properties.TargetFilterContainer.GetValueInt() == 1)
        (LootingCONTQuest As LTMN:Thread:LootingCONTManager).TryLooting()
    EndIf
    If (properties.TargetFilterCorpse.GetValueInt() == 1)
        (LootingNPC_Quest As LTMN:Thread:LootingNPC_Manager).TryLooting()
    EndIf
EndFunction

; Display the message set in Quest Objectives
Function ShowMessage(int messageId)
    If (properties.DisplaySystemMessage.GetValueInt() == 1)
        self.SetObjectiveDisplayed(messageId)
        self.SetObjectiveSkipped(messageId)
    EndIf
EndFunction

; Verify that the world is in an active.
bool Function IsWorldActive()
    If (player.GetParentCell() == None || !player.GetParentCell().IsLoaded())
        Return false
    EndIf
    If (Utility.IsInMenuMode())
        Return false
    EndIf
    Return true
EndFunction

; Clear the form list (Internal function)
Function _ClearFormList(FormList list) global
    string prefix = ("| System | " + Lootman.GetRandomProcessID() + " | ");; Debug
    Lootman.Log(prefix + "** Delete all items in the form list **");; Debug
    Lootman.Log(prefix + "  Form list: [ID: " + Lootman.GetHexID(list) + "]");; Debug

    int i = list.GetSize()
    While i
        i -= 1
        Form item = list.GetAt(i)
        Lootman.Log(prefix + "    Remove: [Name: " + Lootman.GetIdentify(item) + ", ID: " + Lootman.GetHexID(item) + "]");; Debug
        list.RemoveAddedForm(item)
    EndWhile

    Lootman.Log(prefix + "** Delete all items in the form list has been completed **");; Debug
EndFunction

; Setup the form list (Internal function)
Function _SetupFormList(FormList list, string identify) global
    string prefix = ("| System | " + Lootman.GetRandomProcessID() + " | ");; Debug
    Lootman.Log(prefix + "** Add items to the form list **");; Debug
    Lootman.Log(prefix + "  Form list: [Name: " + identify + ", ID: " + Lootman.GetHexID(list) + "]");; Debug

    Form[] items = Lootman.GetInjectionDataForList(identify)
    int i = items.Length
    While i
        i -= 1
        Form item = items[i]
        list.AddForm(item)
        Lootman.Log(prefix + "  Add: [Name: " + Lootman.GetIdentify(item) + ", ID: " + Lootman.GetHexID(item) + "]");; Debug
    EndWhile

    Lootman.Log(prefix + "** Items has been added to the form list **");; Debug
EndFunction
