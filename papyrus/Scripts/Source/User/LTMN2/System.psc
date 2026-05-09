Scriptname LTMN2:System extends Quest

; Quest singleton form fixed by LootMan.esp.
LTMN2:System Function GetInstance() global
    Return Game.GetFormFromFile(0x000F99, "LootMan.esp") As LTMN2:System
EndFunction

; Format MOD_VERSION-style integers as major.minor.patch.
string Function GetVersionString(int version) global
    Return (version / 10000) + "." + (version / 100 % 100) + "." + (version % 100)
EndFunction

; Version encoding: Major{1}.Minor{2}.Patch{2}; 10234 is 1.2.34.
int MOD_VERSION = 30000 const

; Timer IDs consumed by OnTimer.
int TIMER_INSTALL = 1 const
int TIMER_INITIALIZE = 2 const
int TIMER_UPDATE = 3 const
int TIMER_LOOTING = 4 const

; Number of registered system messages.
int MESSAGE_COUNT = 11 const

; Enabled native looting form-type bits. Keep synchronized with papyrus_lootman.cpp.
int ENABLE_FORM_TYPE_ACTI = 1 const
int ENABLE_FORM_TYPE_ALCH = 2 const
int ENABLE_FORM_TYPE_AMMO = 4 const
int ENABLE_FORM_TYPE_ARMO = 8 const
int ENABLE_FORM_TYPE_BOOK = 16 const
int ENABLE_FORM_TYPE_CONT = 32 const
int ENABLE_FORM_TYPE_FLOR = 64 const
int ENABLE_FORM_TYPE_INGR = 128 const
int ENABLE_FORM_TYPE_KEYM = 256 const
int ENABLE_FORM_TYPE_MISC = 512 const
int ENABLE_FORM_TYPE_NPC_ = 1024 const
int ENABLE_FORM_TYPE_WEAP = 2048 const

int LOG_LEVEL_DEBUG = 1 const
int LOG_LEVEL_INFO = 2 const

Group MessageId
    ; System message IDs.
    int property MESSAGE_INSTALLED = 1 autoreadonly hidden
    int property MESSAGE_UNINSTALLED = 2 autoreadonly hidden
    int property MESSAGE_ENABLED = 3 autoreadonly hidden
    int property MESSAGE_DISABLED = 4 autoreadonly hidden
    int property MESSAGE_HAS_OVERWEIGHT = 5 autoreadonly hidden
    int property MESSAGE_REMIND_NOT_LOOTING_IN_SETTLEMENT = 6 autoreadonly hidden
    int property MESSAGE_WORKSHOP_NOT_FOUND = 7 autoreadonly hidden
    int property MESSAGE_LINKED_TO_WORKSHOP = 8 autoreadonly hidden
    int property MESSAGE_UNLINKED_TO_WORKSHOP = 9 autoreadonly hidden
    int property MESSAGE_NOT_HAVE_BOBBY_PIN = 10 autoreadonly hidden
    int property MESSAGE_UTILITY_PROCESS_COMPLETE = 11 autoreadonly hidden
EndGroup

Group WorkerManager
    ; Legacy workers are stopped during load, initialize, and uninstall.
    Quest property WorkerManagerACTI auto const mandatory
    Quest property WorkerManagerALCH auto const mandatory
    Quest property WorkerManagerAMMO auto const mandatory
    Quest property WorkerManagerARMO auto const mandatory
    Quest property WorkerManagerBOOK auto const mandatory
    Quest property WorkerManagerCONT auto const mandatory
    Quest property WorkerManagerFLOR auto const mandatory
    Quest property WorkerManagerINGR auto const mandatory
    Quest property WorkerManagerKEYM auto const mandatory
    Quest property WorkerManagerMISC auto const mandatory
    Quest property WorkerManagerNPC_ auto const mandatory
    Quest property WorkerManagerWEAP auto const mandatory
EndGroup

Group Status
    ; Save-version marker for migrations.
    int property CurrentModVersion = 0 auto hidden
EndGroup

Actor player
LTMN2:Properties properties
int[] messageDisplayCount
float[] lastMessageDisplayTime
Location autoLinkedWorkshopLocation

Function LogSystemEvent(string eventName, string fields = "", int logLevel = 2)
    LTMN2:LootMan.LogEvent("system", eventName, fields, logLevel)
EndFunction

string Function FormField(string name, Form target)
    Return name + "=" + LTMN2:LootMan.GetHexID(target)
EndFunction

; Keep registration diagnostics as stable key=value fields for log search.
Function LogWorkshopRegistrationEvent(string role, WorkshopScript workshop, Location workshopLocation, string outcome, string reason, string workshopAction = "none", string locationAction = "none", int workshopIndexBefore = -1, int locationIndexBefore = -1, int workshopIndexAfter = -1, int locationIndexAfter = -1)
    LogSystemEvent("workshop_registration", "workflow=workshop_supply_link operation=ensure_workshop_registered role=" + role + " outcome=" + outcome + " reason=" + reason + " workshop_action=" + workshopAction + " location_action=" + locationAction + " " + FormField("workshop", workshop) + " " + FormField("location", workshopLocation) + " workshop_index_before=" + workshopIndexBefore + " location_index_before=" + locationIndexBefore + " workshop_index_after=" + workshopIndexAfter + " location_index_after=" + locationIndexAfter, LOG_LEVEL_DEBUG)
EndFunction

; First quest init sets the save marker and schedules install.
Event OnInit()
    LogSystemEvent("first_run", "version=" + GetVersionString(MOD_VERSION))

    player = Game.GetPlayer()
    properties = LTMN2:Properties.GetInstance()
    messageDisplayCount = new int[MESSAGE_COUNT]
    lastMessageDisplayTime = new float[MESSAGE_COUNT]

    CurrentModVersion = MOD_VERSION

    RegisterForRemoteEvent(player, "OnPlayerLoadGame")

    StartTimer(5, TIMER_INSTALL)
EndEvent

; Reload transient state after a save load.
Event Actor.OnPlayerLoadGame(Actor akSender)
    LogSystemEvent("load", "version=" + GetVersionString(MOD_VERSION) + " current_version=" + GetVersionString(CurrentModVersion))

    ; Force runtime setup to run again after load.
    properties.IsInitialized = false
    properties.IsNotInitialized = true

    CancelTimer(TIMER_INSTALL)
    CancelTimer(TIMER_INITIALIZE)
    CancelTimer(TIMER_UPDATE)
    CancelTimer(TIMER_LOOTING)

    messageDisplayCount = new int[MESSAGE_COUNT]
    lastMessageDisplayTime = new float[MESSAGE_COUNT]

    ; Run migrations when the saved script version differs from the ESP version.
    If (CurrentModVersion != MOD_VERSION)
        Patch()
    EndIf

    If (properties.IsNotInstalled)
        StartTimer(5, TIMER_INSTALL)
    Else
        StartTimer(3, TIMER_INITIALIZE)
    EndIf
EndEvent

; Refresh settlement state and optional workshop links after player travel.
Event Actor.OnLocationChange(Actor akSender, Location akOldLoc, Location akNewLoc)
    string prefix = "source=papyrus component=system event=location_change"

    WorkshopScript currentWorkshop = LTMN2:Utils.GetCurrentWorkshop(player)
    Location currentWorkshopLocation = none
    If (currentWorkshop)
        currentWorkshopLocation = currentWorkshop.myLocation
    EndIf

    If (properties.NotLootingFromSettlement && akNewLoc != None)
        If (akNewLoc.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || akNewLoc.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement) || currentWorkshop != None)
            properties.IsInSettlement = true
            ShowMessage(MESSAGE_REMIND_NOT_LOOTING_IN_SETTLEMENT)
            LogSystemEvent("settlement_state_changed", FormField("old_location", akOldLoc) + " " + FormField("new_location", akNewLoc) + " in_settlement=true workshop_nearby=" + (currentWorkshop != None))
        Else
            properties.IsInSettlement = false
            LogSystemEvent("settlement_state_changed", FormField("old_location", akOldLoc) + " " + FormField("new_location", akNewLoc) + " in_settlement=false workshop_nearby=" + (currentWorkshop != None))
        EndIf
    EndIf

    If (properties.AutomaticallyLinkAndUnlinkToWorkshop)
        If (autoLinkedWorkshopLocation != None && autoLinkedWorkshopLocation != currentWorkshopLocation)
            If (UnlinkWorkshopLocation(autoLinkedWorkshopLocation, prefix))
                ShowWorkshopMessageImmediate(MESSAGE_UNLINKED_TO_WORKSHOP, autoLinkedWorkshopLocation)
                LogSystemEvent("workshop_unlinked", FormField("location", autoLinkedWorkshopLocation) + " reason=location_change")
            EndIf
            autoLinkedWorkshopLocation = none
        EndIf

        If (autoLinkedWorkshopLocation == None && akOldLoc != None)
            WorkshopScript oldWorkshop = properties.WorkshopParent.GetWorkshopFromLocation(akOldLoc)
            If (oldWorkshop && oldWorkshop.myLocation && oldWorkshop.myLocation != currentWorkshopLocation)
                If (UnlinkWorkshopLocation(oldWorkshop.myLocation, prefix))
                    ShowWorkshopMessageImmediate(MESSAGE_UNLINKED_TO_WORKSHOP, oldWorkshop.myLocation)
                    LogSystemEvent("workshop_unlinked", FormField("location", oldWorkshop.myLocation) + " reason=old_location")
                EndIf
            EndIf
        EndIf

        If (currentWorkshop && currentWorkshop.OwnedByPlayer)
            If (LinkWorkshop(currentWorkshop, prefix))
                ShowWorkshopMessageImmediate(MESSAGE_LINKED_TO_WORKSHOP, currentWorkshop.myLocation)
                LogSystemEvent("workshop_linked", FormField("workshop", currentWorkshop) + " " + FormField("location", currentWorkshop.myLocation) + " reason=location_change")
            EndIf
            currentWorkshopLocation = currentWorkshop.myLocation
            autoLinkedWorkshopLocation = currentWorkshopLocation
        EndIf
    EndIf
EndEvent

bool Function LinkWorkshop(WorkshopScript workshop, string prefix)
    If (!workshop)
        Return false
    EndIf

    bool lootManRegistered = EnsureWorkshopRegistered(properties.LootManWorkshopRef, prefix, "LootMan")
    bool targetRegistered = EnsureWorkshopRegistered(workshop, prefix, "Target")
    If (!lootManRegistered || !targetRegistered)
        LogSystemEvent("workshop_supply_link_registration_incomplete", "workflow=workshop_supply_link operation=link_workshop outcome=degraded reason=registration_incomplete lootman_registered=" + lootManRegistered + " target_registered=" + targetRegistered + " " + FormField("lootman_workshop", properties.LootManWorkshopRef) + " " + FormField("target_workshop", workshop), LOG_LEVEL_INFO)
    EndIf

    Location workshopLocation = workshop.myLocation
    If (!workshopLocation)
        workshopLocation = workshop.GetCurrentLocation()
        workshop.myLocation = workshopLocation
    EndIf

    bool linked = LinkWorkshopLocation(workshopLocation, prefix)
    LTMN2:LootMan.RememberWorkshopSupplyLink(workshopLocation, properties.LootManWorkshopRef, prefix)
    Return linked
EndFunction

bool Function EnsureWorkshopRegistered(WorkshopScript workshop, string prefix, string label)
    If (!workshop)
        LogWorkshopRegistrationEvent(label, None, None, "failed", "missing_workshop")
        Return false
    EndIf

    If (!workshop.myLocation)
        workshop.myLocation = workshop.GetCurrentLocation()
    EndIf
    Location workshopLocation = workshop.myLocation
    If (!workshopLocation)
        LogWorkshopRegistrationEvent(label, workshop, None, "failed", "missing_location")
        Return false
    EndIf

    int workshopIndex = properties.WorkshopParent.Workshops.Find(workshop)
    int locationIndex = properties.WorkshopParent.WorkshopLocations.Find(workshopLocation)
    int workshopIndexBefore = workshopIndex
    int locationIndexBefore = locationIndex

    If (workshopIndex >= 0 && locationIndex >= 0)
        LogWorkshopRegistrationEvent(label, workshop, workshopLocation, "ready", "already_registered", "existing", "existing", workshopIndexBefore, locationIndexBefore, workshopIndex, locationIndex)
        Return true
    EndIf

    string workshopAction = "existing"
    string locationAction = "existing"

    If (workshopIndex < 0)
        properties.WorkshopParent.Workshops.Add(workshop)
        workshopIndex = properties.WorkshopParent.Workshops.Length - 1
        workshop.InitWorkshopID(workshopIndex)
        workshopAction = "added"
    EndIf

    If (locationIndex < 0 && workshopIndex >= 0)
        While (properties.WorkshopParent.WorkshopLocations.Length < workshopIndex)
            properties.WorkshopParent.WorkshopLocations.Add(None)
        EndWhile

        If (properties.WorkshopParent.WorkshopLocations.Length == workshopIndex)
            properties.WorkshopParent.WorkshopLocations.Add(workshopLocation)
            locationAction = "added"
        Else
            properties.WorkshopParent.WorkshopLocations[workshopIndex] = workshopLocation
            locationAction = "updated"
        EndIf

        locationIndex = properties.WorkshopParent.WorkshopLocations.Find(workshopLocation)
    EndIf

    If (workshopIndex < 0 || locationIndex < 0)
        LogWorkshopRegistrationEvent(label, workshop, workshopLocation, "failed", "index_missing_after_registration", workshopAction, locationAction, workshopIndexBefore, locationIndexBefore, workshopIndex, locationIndex)
        Return false
    EndIf

    LogWorkshopRegistrationEvent(label, workshop, workshopLocation, "ready", "registered", workshopAction, locationAction, workshopIndexBefore, locationIndexBefore, workshopIndex, locationIndex)
    Return true
EndFunction

Location Function GetLootManWorkshopLocation(string prefix)
    WorkshopScript lootManWorkshop = properties.LootManWorkshopRef
    If (!lootManWorkshop)
        Return none
    EndIf

    If (!lootManWorkshop.myLocation)
        lootManWorkshop.myLocation = lootManWorkshop.GetCurrentLocation()
    EndIf

    Location workshopLocation = lootManWorkshop.myLocation

    If (workshopLocation)
        Return workshopLocation
    EndIf

    Return properties.LootManLocation
EndFunction

bool Function IsWorkshopLinkedToLootMan(Location workshopLocation, string prefix)
    If (!workshopLocation)
        Return false
    EndIf

    Location lootManWorkshopLocation = GetLootManWorkshopLocation(prefix)
    If (lootManWorkshopLocation && workshopLocation.IsLinkedLocation(lootManWorkshopLocation, properties.WorkshopCaravan))
        Return true
    EndIf

    Return false
EndFunction

bool Function LinkWorkshopLocation(Location workshopLocation, string prefix)
    If (!workshopLocation)
        Return false
    EndIf

    Location lootManWorkshopLocation = GetLootManWorkshopLocation(prefix)
    If (!lootManWorkshopLocation)
        Return false
    EndIf

    bool linkedToRegisteredLocation = workshopLocation.IsLinkedLocation(lootManWorkshopLocation, properties.WorkshopCaravan)
    bool linkedToConfiguredLocation = properties.LootManLocation != None && workshopLocation.IsLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan)

    If (!linkedToRegisteredLocation)
        workshopLocation.AddLinkedLocation(lootManWorkshopLocation, properties.WorkshopCaravan)
        If (linkedToConfiguredLocation && properties.LootManLocation != lootManWorkshopLocation)
            workshopLocation.RemoveLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan)
        EndIf
        Return true
    EndIf

    Return false
EndFunction

bool Function UnlinkWorkshopLocation(Location workshopLocation, string prefix)
    If (!workshopLocation)
        Return false
    EndIf

    bool removed = false
    Location lootManWorkshopLocation = GetLootManWorkshopLocation(prefix)
    If (lootManWorkshopLocation && workshopLocation.IsLinkedLocation(lootManWorkshopLocation, properties.WorkshopCaravan))
        workshopLocation.RemoveLinkedLocation(lootManWorkshopLocation, properties.WorkshopCaravan)
        removed = true
    EndIf

    If (properties.LootManLocation != None && properties.LootManLocation != lootManWorkshopLocation && workshopLocation.IsLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan))
        workshopLocation.RemoveLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan)
        removed = true
    EndIf

    If (removed)
        LTMN2:LootMan.ForgetWorkshopSupplyLink(workshopLocation, prefix)
        Return true
    EndIf

    Return false
EndFunction

Function SetAutoLinkedWorkshopLocation(Location workshopLocation)
    autoLinkedWorkshopLocation = workshopLocation
EndFunction

Location Function GetAutoLinkedWorkshopLocation()
    Return autoLinkedWorkshopLocation
EndFunction

bool Function UnlinkAutoLinkedWorkshopLocation(string prefix)
    If (autoLinkedWorkshopLocation && UnlinkWorkshopLocation(autoLinkedWorkshopLocation, prefix))
        autoLinkedWorkshopLocation = none
        Return true
    EndIf

    autoLinkedWorkshopLocation = none
    Return false
EndFunction

Event ObjectReference.OnItemAdded(ObjectReference akSender, Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akSourceContainer)
    ; Shipments lose their payload when moved to the Workshop container via RemoveItem.
    If (properties.ShipmentItemList.HasForm(akBaseItem))
        LogSystemEvent("shipment_delivered", FormField("item", akBaseItem) + " count=" + aiItemCount)
        akSender.RemoveItem(akBaseItem, aiItemCount, true)
        properties.LootManWorkshopRef.AddItem(akBaseItem, aiItemCount, true)
    EndIf
EndEvent

Event OnTimer(int aiTimerId)
    If (aiTimerId == TIMER_LOOTING)
        Looting()
        ResetLootingTimer()
    ElseIf (aiTimerId == TIMER_UPDATE)
        Update()
        StartTimer(1, TIMER_UPDATE)
    ElseIf (aiTimerId == TIMER_INITIALIZE)
        Initialize()
    ElseIf (aiTimerId == TIMER_INSTALL)
        If (CanInstall())
            Install()
        ElseIf (properties.IsNotInstalled)
            StartTimer(10, TIMER_INSTALL)
        EndIf
    EndIf
EndEvent

; Install waits for either the Pip-Boy or Institute Radio state.
bool Function CanInstall()
    bool hasPipboy = player.GetItemCount(properties.Pipboy) > 0
    bool radioRunning = properties.RadioInstitute.IsRunning()

    If (hasPipboy || radioRunning)
        LogSystemEvent("install_check_passed", "has_pipboy=" + hasPipboy + " radio_running=" + radioRunning, LOG_LEVEL_DEBUG)
        Return true
    Else
        LogSystemEvent("install_check_pending", "has_pipboy=false radio_running=false", LOG_LEVEL_DEBUG)
        Return false
    EndIf
EndFunction

; Register events and start initialization after the install gate passes.
Function Install()
    If (properties.IsInstalled)
        LogSystemEvent("install_skipped", "reason=already_installed", LOG_LEVEL_DEBUG)
        Return
    EndIf

    If (properties.IsUninstalled)
        LogSystemEvent("install_skipped", "reason=already_uninstalled", LOG_LEVEL_DEBUG)
        Return
    EndIf

    properties.IsInstalled = true
    properties.IsNotInstalled = false

    LogSystemEvent("install_started", "version=" + GetVersionString(MOD_VERSION))

    AddInventoryEventFilter(properties.ShipmentItemList)
    RegisterForRemoteEvent(player, "OnLocationChange")
    RegisterForRemoteEvent(properties.LootManRef, "OnItemAdded")

    StartTimer(1, TIMER_INITIALIZE)

    LTMN2:LootMan.ShowSystemMessage(MESSAGE_INSTALLED)

    LogSystemEvent("install_completed", "native_looting_scheduler=true")
EndFunction

; Return stored items and stop quest-owned runtime state.
Function Uninstall()
    If (properties.IsNotInstalled)
        LogSystemEvent("uninstall_skipped", "reason=not_installed", LOG_LEVEL_DEBUG)
        Return
    EndIf

    If (properties.IsUninstalled)
        LogSystemEvent("uninstall_skipped", "reason=already_uninstalled", LOG_LEVEL_DEBUG)
        Return
    EndIf

    properties.IsUninstalled = true
    properties.IsNotUninstalled = false

    LogSystemEvent("uninstall_started")

    CancelTimer(TIMER_INSTALL)
    CancelTimer(TIMER_INITIALIZE)
    CancelTimer(TIMER_UPDATE)
    CancelTimer(TIMER_LOOTING)

    RemoveAllInventoryEventFilters()
    UnregisterForRemoteEvent(player, "OnPlayerLoadGame")
    UnregisterForRemoteEvent(player, "OnLocationChange")
    UnregisterForRemoteEvent(properties.LootManRef, "OnItemAdded")

    StopLegacyWorkerManagers()

    LTMN2:LootMan.ShowSystemMessage(MESSAGE_UNINSTALLED)

    LTMN2:Utils.MoveInventoryItems(properties.LootManRef, player, properties.ITEM_TYPE_ALL)
    LTMN2:Utils.MoveInventoryItems(properties.LootManWorkshopRef, player, properties.ITEM_TYPE_ALL)

    LogSystemEvent("uninstall_completed", "items_returned=true")

    properties.IsInstalled = false
    properties.IsInitialized = false

    properties.Stop()
    self.Stop()
EndFunction

; Old saves may still have worker quests running after native looting took over.
Function StopLegacyWorkerManagers()
    If (WorkerManagerACTI)
        WorkerManagerACTI.Stop()
    EndIf
    If (WorkerManagerALCH)
        WorkerManagerALCH.Stop()
    EndIf
    If (WorkerManagerAMMO)
        WorkerManagerAMMO.Stop()
    EndIf
    If (WorkerManagerARMO)
        WorkerManagerARMO.Stop()
    EndIf
    If (WorkerManagerBOOK)
        WorkerManagerBOOK.Stop()
    EndIf
    If (WorkerManagerCONT)
        WorkerManagerCONT.Stop()
    EndIf
    If (WorkerManagerFLOR)
        WorkerManagerFLOR.Stop()
    EndIf
    If (WorkerManagerINGR)
        WorkerManagerINGR.Stop()
    EndIf
    If (WorkerManagerKEYM)
        WorkerManagerKEYM.Stop()
    EndIf
    If (WorkerManagerMISC)
        WorkerManagerMISC.Stop()
    EndIf
    If (WorkerManagerNPC_)
        WorkerManagerNPC_.Stop()
    EndIf
    If (WorkerManagerWEAP)
        WorkerManagerWEAP.Stop()
    EndIf
EndFunction

; Initialize runtime hooks after install or save load.
Function Initialize()
    If (properties.IsNotInstalled)
        LogSystemEvent("initialize_skipped", "reason=not_installed", LOG_LEVEL_DEBUG)
        Return
    EndIf

    If (properties.IsInitialized)
        LogSystemEvent("initialize_skipped", "reason=already_initialized", LOG_LEVEL_DEBUG)
        Return
    EndIf

    If (!properties.IsNotUninstalled)
        LogSystemEvent("initialize_skipped", "reason=already_uninstalled", LOG_LEVEL_DEBUG)
        Return
    EndIf

    LogSystemEvent("initialize_started")

    LTMN2:LootMan.OnUpdateLootManProperty("")
    LTMN2:MCM.GetInstance().Initialize()

    StopLegacyWorkerManagers()

    StartTimer(1, TIMER_UPDATE)
    If (properties.WorkerInvokeInterval > 0)
        StartTimer(3, TIMER_LOOTING)
    EndIf

    properties.IsInitialized = true
    properties.IsNotInitialized = false
    LogSystemEvent("initialize_completed", "worker_interval=" + properties.WorkerInvokeInterval)
EndFunction

; Apply save migrations up to MOD_VERSION.
Function Patch()
    int fromVersion = CurrentModVersion
    LogSystemEvent("patch_started", "from_version=" + GetVersionString(fromVersion) + " to_version=" + GetVersionString(MOD_VERSION))

    ; Patch order is cumulative.
    If (CurrentModVersion < 20001)
        LTMN2:Patch.v2_0_1()
    EndIf
    If (CurrentModVersion < 30000)
        LTMN2:Patch.v3_0_0()
    EndIf

    CurrentModVersion = MOD_VERSION

    LogSystemEvent("patch_completed", "from_version=" + GetVersionString(fromVersion) + " current_version=" + GetVersionString(CurrentModVersion))
EndFunction

Function DeliverLootManInventory()
    If (properties.LootManRef.GetItemCount() > 0)
        int movedItems = 0
        string destination = "workshop"
        If (properties.LootIsDeliverToPlayer)
            destination = "player"
            movedItems = LTMN2:LootMan.TransferInventoryItems(properties.LootManRef, player, properties.ITEM_TYPE_ALL, -1, properties.ObjectTypeLooseMod, properties.LootingWithoutLogs)
        Else
            movedItems = LTMN2:LootMan.TransferInventoryItems(properties.LootManRef, properties.LootManWorkshopRef, properties.ITEM_TYPE_ALL, -1, properties.ObjectTypeLooseMod, properties.LootingWithoutLogs)
        EndIf
        LogSystemEvent("lootman_inventory_delivered", "destination=" + destination + " moved_forms=" + movedItems + " remaining_count=" + properties.LootManRef.GetItemCount() + " suppress_messages=" + properties.LootingWithoutLogs)
    EndIf
EndFunction

; Deliver queued loot, refresh carry state, and expire message throttles.
Function Update()
    If (properties.IsNotInstalled)
        Return
    EndIf

    If (properties.IsNotInitialized)
        Return
    EndIf

    If (properties.IsUninstalled)
        Return
    EndIf

    If (!properties.EnableLootMan)
        Return
    EndIf

    DeliverLootManInventory()

    If (!properties.IgnoreOverweight)
        bool wasOverweight = properties.IsOverweight
        float workshopWeight = properties.LootManWorkshopRef.GetInventoryWeight()
        bool overweight = workshopWeight > properties.CarryWeight
        If (!wasOverweight && overweight)
            properties.IsOverweight = true
            ShowMessage(MESSAGE_HAS_OVERWEIGHT)
            LogSystemEvent("overweight_changed", "overweight=true workshop_weight=" + workshopWeight + " carry_weight=" + properties.CarryWeight)
        ElseIf (wasOverweight && overweight)
            ShowMessage(MESSAGE_HAS_OVERWEIGHT)
        ElseIf (wasOverweight && !overweight)
            properties.IsOverweight = false
            LogSystemEvent("overweight_changed", "overweight=false workshop_weight=" + workshopWeight + " carry_weight=" + properties.CarryWeight)
        EndIf
    ElseIf (properties.IsOverweight)
        properties.IsOverweight = false
        LogSystemEvent("overweight_changed", "overweight=false reason=ignored")
    EndIf

    float time = Utility.GetCurrentRealTime()
    int messageId = MESSAGE_COUNT
    While messageId
        messageId -= 1
        ResetMessageThrottleIfExpired(messageId, time)
    EndWhile
EndFunction

int Function GetEnabledLootingFormTypeMask()
    int mask = 0
    If (properties.EnableObjectLootingOfACTI)
        mask += ENABLE_FORM_TYPE_ACTI
    EndIf
    If (properties.EnableObjectLootingOfALCH)
        mask += ENABLE_FORM_TYPE_ALCH
    EndIf
    If (properties.EnableObjectLootingOfAMMO)
        mask += ENABLE_FORM_TYPE_AMMO
    EndIf
    If (properties.EnableObjectLootingOfARMO)
        mask += ENABLE_FORM_TYPE_ARMO
    EndIf
    If (properties.EnableObjectLootingOfBOOK)
        mask += ENABLE_FORM_TYPE_BOOK
    EndIf
    If (properties.EnableObjectLootingOfCONT)
        mask += ENABLE_FORM_TYPE_CONT
    EndIf
    If (properties.EnableObjectLootingOfFLOR)
        mask += ENABLE_FORM_TYPE_FLOR
    EndIf
    If (properties.EnableObjectLootingOfINGR)
        mask += ENABLE_FORM_TYPE_INGR
    EndIf
    If (properties.EnableObjectLootingOfKEYM)
        mask += ENABLE_FORM_TYPE_KEYM
    EndIf
    If (properties.EnableObjectLootingOfMISC)
        mask += ENABLE_FORM_TYPE_MISC
    EndIf
    If (properties.EnableObjectLootingOfNPC_)
        mask += ENABLE_FORM_TYPE_NPC_
    EndIf
    If (properties.EnableObjectLootingOfWEAP)
        mask += ENABLE_FORM_TYPE_WEAP
    EndIf
    Return mask
EndFunction

Function Looting(bool force = false)
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf
    If (!force && !properties.EnableLootMan)
        Return
    EndIf
    If (properties.IsOverweight && !properties.IgnoreOverweight)
        Return
    EndIf
    If (properties.IsInSettlement && properties.NotLootingFromSettlement)
        Return
    EndIf
    If (!LTMN2:Utils.IsLootingSafe())
        Return
    EndIf

    int enabledFormTypeMask = GetEnabledLootingFormTypeMask()
    If (enabledFormTypeMask <= 0)
        Return
    EndIf

    int[] result = LTMN2:LootMan.LootNearbyEnabledReferences(player, properties.LootManRef, properties.ActivatorRef, properties.LootManWorkshopRef, enabledFormTypeMask, properties.LootableInventoryItemType, properties.PlayPickupSound, properties.PlayContainerAnimation, properties.UnlockLockedContainer, properties.BobbyPin, properties.Locksmith01, properties.Locksmith02, properties.Locksmith03, properties.Locksmith04)
    If (result.Length >= 6 && (result[3] > 0 || result[4] > 0))
        LogSystemEvent("native_loot_pass_limited", "processed=" + result[0] + " successful=" + result[1] + " hit_object_limit=" + result[3] + " hit_time_budget=" + result[4] + " candidates=" + result[5])
    EndIf
EndFunction

Function ResetLootingTimer()
    If (properties.WorkerInvokeInterval > 0)
        StartTimer(properties.WorkerInvokeInterval, TIMER_LOOTING)
    EndIf
EndFunction

bool Function CanShowSystemMessage(int messageId)
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return false
    EndIf
    If (!properties.DisplaySystemMessage)
        Return false
    EndIf
    If (messageId <= 0 || messageId > MESSAGE_COUNT)
        Return false
    EndIf

    Return true
EndFunction

Function ResetMessageThrottleIfExpired(int arrayIndex, float time)
    If (lastMessageDisplayTime[arrayIndex] > 0 && (time - lastMessageDisplayTime[arrayIndex]) >= 600)
        lastMessageDisplayTime[arrayIndex] = 0
        messageDisplayCount[arrayIndex] = 0
    EndIf
EndFunction

; Throttle system messages to avoid repeated HUD spam.
Function ShowMessage(int messageId, float interval = 30.0, int maxDisplayCount = 0)
    If (!CanShowSystemMessage(messageId))
        Return
    EndIf
    If (!properties.EnableLootMan)
        Return
    EndIf

    int arrayIndex = messageId - 1
    float time = Utility.GetCurrentRealTime()
    ResetMessageThrottleIfExpired(arrayIndex, time)

    int count = messageDisplayCount[arrayIndex]
    If (maxDisplayCount > 0 && count >= maxDisplayCount)
        Return
    EndIf

    If (count > 0 && (time - lastMessageDisplayTime[arrayIndex]) < interval)
        Return
    EndIf

    messageDisplayCount[arrayIndex] = count + 1
    lastMessageDisplayTime[arrayIndex] = time

    LTMN2:LootMan.ShowSystemMessage(messageId)
EndFunction

Function ShowMessageImmediate(int messageId)
    If (!CanShowSystemMessage(messageId))
        Return
    EndIf

    LTMN2:LootMan.ShowSystemMessage(messageId)
EndFunction

Function ShowWorkshopMessageImmediate(int messageId, Location workshopLocation)
    If (!CanShowSystemMessage(messageId))
        Return
    EndIf

    LTMN2:LootMan.ShowSystemMessageWithName(messageId, workshopLocation)
EndFunction
