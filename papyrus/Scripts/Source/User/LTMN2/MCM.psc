Scriptname LTMN2:MCM extends Quest
{Handles Mod Configuration Menu callbacks and utility actions.}

; Quest singleton form fixed by LootMan.esp.
LTMN2:MCM Function GetInstance() global
    Return Game.GetFormFromFile(0x000F9B, "LootMan.esp") As LTMN2:MCM
EndFunction

Group Constants
    int property TIMER_MOVE_ITEMS = 0 autoreadonly hidden
    int property TIMER_SCRAP_ITEMS = 1 autoreadonly hidden

    int property INVENTORY_PLAYER = 0 autoreadonly hidden
    int property INVENTORY_LOOTMAN = 1 autoreadonly hidden
    int property INVENTORY_WORKSHOP = 2 autoreadonly hidden

    int property MOVE_ITEM_ALL = 0 autoreadonly hidden
    int property MOVE_ITEM_WEAPON = 1 autoreadonly hidden
    int property MOVE_ITEM_ARMOR = 2 autoreadonly hidden
    int property MOVE_ITEM_CONSUMABLE = 3 autoreadonly hidden
    int property MOVE_ITEM_JUNK = 4 autoreadonly hidden
    int property MOVE_ITEM_MODS = 5 autoreadonly hidden
    int property MOVE_ITEM_AMMO = 6 autoreadonly hidden
    int property MOVE_ITEM_BOOK = 7 autoreadonly hidden
    int property MOVE_ITEM_KEY = 8 autoreadonly hidden

    int property SCRAP_ITEM_ALL = 0 autoreadonly hidden
    int property SCRAP_ITEM_WEAPON = 1 autoreadonly hidden
    int property SCRAP_ITEM_ARMOR = 2 autoreadonly hidden
    int property SCRAP_ITEM_JUNK = 3 autoreadonly hidden
EndGroup

Group Status
    bool property IsIdleUtility = true auto hidden
    bool property IsBusyUtility = false auto hidden

    int property MoveItemsFrom = 0 auto hidden
    int property MoveItemsTo = 1 auto hidden
    int property MoveItemsType = 0 auto hidden

    int property ScrapItemsFrom = 0 auto hidden
    int property ScrapItemsType = 0 auto hidden

    int property LogLevel = 2 auto hidden
EndGroup

int LOG_LEVEL_TRACE = 0 const
int LOG_LEVEL_DEBUG = 1 const
int LOG_LEVEL_INFO = 2 const
int LOG_LEVEL_WARN = 3 const
int LOG_LEVEL_OFF = 6 const

Actor player
LTMN2:Properties properties
LTMN2:System system

Function LogMcmEvent(string eventName, string fields = "", int logLevel = 2)
    LTMN2:LootMan.LogEvent("mcm", eventName, fields, logLevel)
EndFunction

string Function FormField(string name, Form target)
    Return name + "=" + LTMN2:LootMan.GetHexID(target)
EndFunction

bool Function IsValidLogLevel(int value)
    Return value >= LOG_LEVEL_TRACE && value <= LOG_LEVEL_OFF
EndFunction

Function SyncLogLevelFromNative(bool refreshMenu = false)
    int previousLogLevel = LogLevel
    int nativeLogLevel = LTMN2:LootMan.GetLogLevel()

    If (!IsValidLogLevel(nativeLogLevel))
        LogMcmEvent("log_level_sync_failed", "reason=out_of_range native_level=" + nativeLogLevel + " previous_level=" + previousLogLevel, LOG_LEVEL_WARN)
        nativeLogLevel = LOG_LEVEL_INFO
    EndIf

    If (LogLevel != nativeLogLevel)
        LogLevel = nativeLogLevel
        LogMcmEvent("log_level_synced", "native_level=" + nativeLogLevel + " previous_level=" + previousLogLevel + " changed=true", LOG_LEVEL_DEBUG)

        If (refreshMenu)
            MCM.RefreshMenu()
        EndIf
    EndIf
EndFunction

Event OnInit()
    player = Game.GetPlayer()
    properties = LTMN2:Properties.GetInstance()
    system = LTMN2:System.GetInstance()
EndEvent

Event OnQuestShutdown()
    UnregisterForExternalEvent("OnMCMSettingChange|LootMan")
EndEvent

Event OnTimer(int aiTimerId)
    If (aiTimerId == TIMER_MOVE_ITEMS)
        MoveItemsInternal()
    ElseIf (aiTimerId == TIMER_SCRAP_ITEMS)
        ScrapItemsInternal()
    EndIf

    Utility.Wait(0.5)
    system.ShowMessageImmediate(system.MESSAGE_UTILITY_PROCESS_COMPLETE)
    SetUtilityBusy(false)
EndEvent

Function Initialize()
    ; Mirror native log level and reset utility state on load.
    SetUtilityBusy(false)
    SyncLogLevelFromNative(true)

    RegisterForExternalEvent("OnMCMSettingChange|LootMan", "OnMCMSettingChange")
EndFunction

Function OnMCMSettingChange(string modName, string id)
    If (modName != "LootMan")
        Return
    EndIf

    ApplySettingSideEffects(id)
EndFunction

; Apply the per-id side effects and the native cache refresh for one changed
; setting. Shared by the MCM external-event callback and the holotape terminal
; facade (LTMN2:Config) so both front-ends drive one identical state machine and
; cannot drift. Members are lazily resolved here because the terminal can invoke
; this before OnInit has populated them, which would otherwise None-deref.
Function ApplySettingSideEffects(string id)
    If (!properties)
        properties = LTMN2:Properties.GetInstance()
    EndIf
    If (!system)
        system = LTMN2:System.GetInstance()
    EndIf
    If (!player)
        player = Game.GetPlayer()
    EndIf

    ; The MCM switcher (or LTMN2:Config.FlipBool) has already written the bool this
    ; id names. Rebuild the packed masks from the bools so the native loot gate
    ; agrees with what the switch shows - whatever state the pair was left in by an
    ; earlier update that never got this far.
    ;
    ; Deliberately above the install-state guard below. Rebuilding a mask from its
    ; own backing bool is pure state repair: it is correct in every install state,
    ; and the caller has already mutated the bool by the time we get here. Skipping
    ; it for a not-yet-installed or uninstalled save would strand the pair for the
    ; rest of the session, because nothing else - not Install, not Initialize -
    ; recomputes. The workshop, log-level and native side effects below are the
    ; things that genuinely need an installed mod; this is not one of them.
    If (properties.IsPackedSubtypeSetting(id))
        properties.RecomputePackedMasks()
    EndIf

    ; Skip side effects unless the mod is in an installed, usable state. MCM hides
    ; its config controls until install, but the holotape terminal can reach this
    ; entry directly, so guard here too. IsNotInitialized is intentionally not
    ; gated: MCM can fire legitimately in the brief post-load re-init window.
    If (properties.IsNotInstalled || properties.IsUninstalled)
        LogMcmEvent("setting_change_skipped", "id=" + id + " reason=not_installed", LOG_LEVEL_DEBUG)
        Return
    EndIf

    string prefix = "source=papyrus component=mcm event=setting_changed id=" + id

    If (id == "AutomaticallyLinkAndUnlinkToWorkshop")
        WorkshopScript workshop = LTMN2:Utils.GetCurrentWorkshop(player)
        Location workshopLocation = none
        Location removedWorkshopLocation = none
        If (workshop)
            workshopLocation = workshop.myLocation
        EndIf

        If (workshop && workshop.OwnedByPlayer)
            If (properties.AutomaticallyLinkAndUnlinkToWorkshop)
                If (system.LinkWorkshop(workshop, prefix))
                    system.ShowWorkshopMessageImmediate(system.MESSAGE_LINKED_TO_WORKSHOP, workshop.myLocation)
                    LogMcmEvent("workshop_linked", "id=" + id + " " + FormField("workshop", workshop) + " " + FormField("location", workshop.myLocation))
                EndIf
                workshopLocation = workshop.myLocation
                system.SetAutoLinkedWorkshopLocation(workshopLocation)
            Else
                removedWorkshopLocation = system.GetAutoLinkedWorkshopLocation()
                bool removedLink = system.UnlinkAutoLinkedWorkshopLocation(prefix)
                If (!removedLink)
                    removedWorkshopLocation = workshopLocation
                    removedLink = system.UnlinkWorkshopLocation(workshopLocation, prefix)
                EndIf
                If (removedLink)
                    system.ShowWorkshopMessageImmediate(system.MESSAGE_UNLINKED_TO_WORKSHOP, removedWorkshopLocation)
                    LogMcmEvent("workshop_unlinked", "id=" + id + " " + FormField("location", removedWorkshopLocation))
                EndIf
            EndIf
        ElseIf (!properties.AutomaticallyLinkAndUnlinkToWorkshop)
            removedWorkshopLocation = system.GetAutoLinkedWorkshopLocation()
            If (system.UnlinkAutoLinkedWorkshopLocation(prefix))
                system.ShowWorkshopMessageImmediate(system.MESSAGE_UNLINKED_TO_WORKSHOP, removedWorkshopLocation)
                LogMcmEvent("workshop_unlinked", "id=" + id + " " + FormField("location", removedWorkshopLocation) + " reason=auto_link_disabled")
            Else
                LogMcmEvent("workshop_link_unchanged", "id=" + id + " reason=no_auto_link", LOG_LEVEL_DEBUG)
            EndIf
        Else
            LogMcmEvent("workshop_link_unchanged", "id=" + id + " workshop_nearby=" + (workshop != none), LOG_LEVEL_DEBUG)
        EndIf

    ElseIf (id == "EnableLootingInSettlement")
        Location currentLocation = player.GetCurrentLocation()
        WorkshopScript currentWorkshop = LTMN2:Utils.GetCurrentWorkshop(player)
        bool isSettlementLocation = false

        If (currentLocation)
            isSettlementLocation = currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement)
        EndIf

        If (isSettlementLocation || currentWorkshop != None)
            properties.IsInSettlement = true
        Else
            properties.IsInSettlement = false
        EndIf

        LogMcmEvent("settlement_state_changed", "id=" + id + " " + FormField("location", currentLocation) + " workshop_nearby=" + (currentWorkshop != None) + " in_settlement=" + properties.IsInSettlement)

    ElseIf (id == "WorkerInvokeInterval")
        system.ResetLootingTimer()

    ElseIf (id == "LogLevel")
        LTMN2:LootMan.SetLogLevel(LogLevel)
        SyncLogLevelFromNative(true)
    EndIf

    ; Logged after the state change, not before it. LogMcmEvent is an LTMN2:LootMan
    ; native, so on an install where lootman.dll never loaded it aborts this frame -
    ; and doing that first is exactly what used to strand a packed mask behind the
    ; bool MCM had already written. The mask rebuild above calls no LTMN2:LootMan
    ; native, so it survives a missing lootman.dll: by the time this line can fail,
    ; the Papyrus-side state is already committed. (It does use Math.LogicalOr, an
    ; F4SE native, which is bound - F4SE is a hard prerequisite and is loaded in
    ; exactly that failure mode; only lootman.dll is absent.)
    LogMcmEvent("setting_changed", "id=" + id)

    LTMN2:LootMan.OnUpdateLootManProperty(id)
EndFunction

Function MoveItems()
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf

    If (IsBusyUtility)
        Return
    EndIf

    SetUtilityBusy(true)
    MCM.RefreshMenu()

    StartTimer(0.5, TIMER_MOVE_ITEMS)
EndFunction

Function MoveItemsInternal()
    ObjectReference fromInventory = GetTargetInventory(MoveItemsFrom)
    ObjectReference toInventory = GetTargetInventory(MoveItemsTo)
    If (!fromInventory || !toInventory || fromInventory == toInventory)
        Return
    EndIf

    If (MoveItemsType < MOVE_ITEM_ALL || MoveItemsType > MOVE_ITEM_KEY)
        Return
    EndIf

    int type = 0
    int sub = -1
    If (MoveItemsType == MOVE_ITEM_ALL)
        type = properties.ITEM_TYPE_ALL
    ElseIf (MoveItemsType == MOVE_ITEM_WEAPON)
        type = properties.ITEM_TYPE_WEAP
    ElseIf (MoveItemsType == MOVE_ITEM_ARMOR)
        type = properties.ITEM_TYPE_ARMO
    ElseIf (MoveItemsType == MOVE_ITEM_CONSUMABLE)
        type = properties.ITEM_TYPE_ALCH
    ElseIf (MoveItemsType == MOVE_ITEM_JUNK)
        type = properties.ITEM_TYPE_MISC
        sub = 0
    ElseIf (MoveItemsType == MOVE_ITEM_MODS)
        type = properties.ITEM_TYPE_MISC
        sub = 1
    ElseIf (MoveItemsType == MOVE_ITEM_AMMO)
        type = properties.ITEM_TYPE_AMMO
    ElseIf (MoveItemsType == MOVE_ITEM_BOOK)
        type = properties.ITEM_TYPE_BOOK
    ElseIf (MoveItemsType == MOVE_ITEM_KEY)
        type = properties.ITEM_TYPE_KEYM
    EndIf

    LTMN2:Utils.MoveInventoryItems(fromInventory, toInventory, type, sub)
EndFunction

Function ScrapItems()
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf

    If (IsBusyUtility)
        Return
    EndIf

    SetUtilityBusy(true)
    MCM.RefreshMenu()

    StartTimer(0.5, TIMER_SCRAP_ITEMS)
EndFunction

Function ScrapItemsInternal()
    ObjectReference fromInventory = GetTargetInventory(ScrapItemsFrom)
    If (!fromInventory)
        Return
    EndIf

    If (ScrapItemsType < SCRAP_ITEM_ALL || ScrapItemsType > SCRAP_ITEM_JUNK)
        Return
    EndIf

    int type = 0
    If (ScrapItemsType == SCRAP_ITEM_ALL)
        type = properties.ITEM_TYPE_WEAP + properties.ITEM_TYPE_ARMO + properties.ITEM_TYPE_MISC
    ElseIf (ScrapItemsType == SCRAP_ITEM_WEAPON)
        type = properties.ITEM_TYPE_WEAP
    ElseIf (ScrapItemsType == SCRAP_ITEM_ARMOR)
        type = properties.ITEM_TYPE_ARMO
    ElseIf (ScrapItemsType == SCRAP_ITEM_JUNK)
        type = properties.ITEM_TYPE_MISC
    EndIf

    LTMN2:Utils.ScrapInventoryItems(fromInventory, type)
EndFunction

Function Install()
    If (properties.IsNotInstalled)
        system.Install()
        MCM.RefreshMenu()
    EndIf
EndFunction

Function Uninstall()
    If (properties.IsInstalled)
        system.Uninstall()
        self.Stop()
        MCM.RefreshMenu()
    EndIf
EndFunction

Function ToggleEnableLootMan()
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf

    properties.EnableLootMan = !properties.EnableLootMan
    ApplySettingSideEffects("EnableLootMan")

    If (properties.EnableLootMan)
        system.ShowMessageImmediate(system.MESSAGE_ENABLED)
    Else
        system.ShowMessageImmediate(system.MESSAGE_DISABLED)
    EndIf
EndFunction

Function OpenLootManInventory()
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf

    properties.LootManWorkshopRef.Activate(player, true)
EndFunction

Function ToggleLinkToWorkshop()
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf

    string prefix = "source=papyrus component=mcm event=workshop_link_toggle"
    WorkshopScript workshop = LTMN2:Utils.GetCurrentWorkshop(player)
    If (workshop)
        Location workshopLocation = workshop.myLocation
        If (!workshopLocation)
            workshopLocation = workshop.GetCurrentLocation()
            workshop.myLocation = workshopLocation
        EndIf

        If (system.IsWorkshopLinkedToLootMan(workshopLocation, prefix))
            system.UnlinkWorkshopLocation(workshopLocation, prefix)
            system.ShowWorkshopMessageImmediate(system.MESSAGE_UNLINKED_TO_WORKSHOP, workshopLocation)
            LogMcmEvent("workshop_unlinked", FormField("workshop", workshop) + " " + FormField("location", workshopLocation) + " reason=manual_toggle")
        Else
            If (system.LinkWorkshop(workshop, prefix))
                system.ShowWorkshopMessageImmediate(system.MESSAGE_LINKED_TO_WORKSHOP, workshop.myLocation)
                LogMcmEvent("workshop_linked", FormField("workshop", workshop) + " " + FormField("location", workshop.myLocation) + " reason=manual_toggle")
            EndIf
        EndIf
    Else
        system.ShowMessageImmediate(system.MESSAGE_WORKSHOP_NOT_FOUND)
        LogMcmEvent("workshop_link_failed", "reason=no_workshop", LOG_LEVEL_INFO)
    EndIf
EndFunction

Function ExecuteLooting()
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf

    system.Looting(true)
    system.DeliverLootManInventory()
EndFunction

Function DumpNearbyObjectDiagnostics()
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf

    string context = "source=papyrus component=mcm event=nearby_object_diagnostics"
    LogMcmEvent("nearby_object_diagnostics_started", "")
    int rowsLogged = LTMN2:LootMan.DumpNearbyObjectDiagnostics(player, context)
    LogMcmEvent("nearby_object_diagnostics_completed", "rows_logged=" + rowsLogged)
EndFunction

Function SetUtilityBusy(bool busy)
    IsIdleUtility = !busy
    IsBusyUtility = busy
EndFunction

ObjectReference Function GetTargetInventory(int target)
    If (target == INVENTORY_PLAYER)
        Return player
    ElseIf (target == INVENTORY_LOOTMAN)
        Return properties.LootManWorkshopRef
    ElseIf (target == INVENTORY_WORKSHOP)
        Return LTMN2:Utils.GetCurrentWorkshop(player)
    EndIf
    Return none
EndFunction
