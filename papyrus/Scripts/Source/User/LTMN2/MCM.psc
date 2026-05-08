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
    system.ShowMessage(system.MESSAGE_UTILITY_PROCESS_COMPLETE)
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

    string prefix = "source=papyrus component=mcm event=setting_changed id=" + id
    LogMcmEvent("setting_changed", "id=" + id)

    If (id == "AutomaticallyLinkAndUnlinkToWorkshop")
        WorkshopScript workshop = LTMN2:Utils.GetCurrentWorkshop(player)
        Location workshopLocation = none
        If (workshop)
            workshopLocation = workshop.myLocation
        EndIf

        If (workshop && workshop.OwnedByPlayer)
            If (properties.AutomaticallyLinkAndUnlinkToWorkshop)
                If (system.LinkWorkshop(workshop, prefix))
                    system.ShowMessage(system.MESSAGE_LINKED_TO_WORKSHOP)
                    LogMcmEvent("workshop_linked", "id=" + id + " " + FormField("workshop", workshop) + " " + FormField("location", workshop.myLocation))
                EndIf
                workshopLocation = workshop.myLocation
                system.SetAutoLinkedWorkshopLocation(workshopLocation)
            Else
                bool removedLink = system.UnlinkAutoLinkedWorkshopLocation(prefix)
                If (!removedLink)
                    removedLink = system.UnlinkWorkshopLocation(workshopLocation, prefix)
                EndIf
                If (removedLink)
                    system.ShowMessage(system.MESSAGE_UNLINKED_TO_WORKSHOP)
                    LogMcmEvent("workshop_unlinked", "id=" + id + " " + FormField("location", workshopLocation))
                EndIf
            EndIf
        ElseIf (!properties.AutomaticallyLinkAndUnlinkToWorkshop)
            If (system.UnlinkAutoLinkedWorkshopLocation(prefix))
                system.ShowMessage(system.MESSAGE_UNLINKED_TO_WORKSHOP)
                LogMcmEvent("workshop_unlinked", "id=" + id + " reason=auto_link_disabled")
            Else
                LogMcmEvent("workshop_link_unchanged", "id=" + id + " reason=no_auto_link", LOG_LEVEL_DEBUG)
            EndIf
        Else
            LogMcmEvent("workshop_link_unchanged", "id=" + id + " workshop_nearby=" + (workshop != none), LOG_LEVEL_DEBUG)
        EndIf

    ElseIf (id == "NotLootingFromSettlement")
        Location currentLocation = player.GetCurrentLocation()
        WorkshopScript currentWorkshop = LTMN2:Utils.GetCurrentWorkshop(player)

        If (currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement) || currentWorkshop != None)
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

    ElseIf (id == "EnableInventoryLootingOfALCH")
        properties.LootableInventoryItemType = Math.LogicalXor(properties.LootableInventoryItemType, properties.ITEM_TYPE_ALCH)
    ElseIf (id == "EnableInventoryLootingOfAMMO")
        properties.LootableInventoryItemType = Math.LogicalXor(properties.LootableInventoryItemType, properties.ITEM_TYPE_AMMO)
    ElseIf (id == "EnableInventoryLootingOfARMO")
        properties.LootableInventoryItemType = Math.LogicalXor(properties.LootableInventoryItemType, properties.ITEM_TYPE_ARMO)
    ElseIf (id == "EnableInventoryLootingOfBOOK")
        properties.LootableInventoryItemType = Math.LogicalXor(properties.LootableInventoryItemType, properties.ITEM_TYPE_BOOK)
    ElseIf (id == "EnableInventoryLootingOfINGR")
        properties.LootableInventoryItemType = Math.LogicalXor(properties.LootableInventoryItemType, properties.ITEM_TYPE_INGR)
    ElseIf (id == "EnableInventoryLootingOfKEYM")
        properties.LootableInventoryItemType = Math.LogicalXor(properties.LootableInventoryItemType, properties.ITEM_TYPE_KEYM)
    ElseIf (id == "EnableInventoryLootingOfMISC")
        properties.LootableInventoryItemType = Math.LogicalXor(properties.LootableInventoryItemType, properties.ITEM_TYPE_MISC)
    ElseIf (id == "EnableInventoryLootingOfWEAP")
        properties.LootableInventoryItemType = Math.LogicalXor(properties.LootableInventoryItemType, properties.ITEM_TYPE_WEAP)

    ElseIf (id == "EnableALCHItemAlcohol")
        properties.LootableALCHItemType = Math.LogicalXor(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_ALCOHOL)
    ElseIf (id == "EnableALCHItemChemistry")
        properties.LootableALCHItemType = Math.LogicalXor(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_CHEMISTRY)
    ElseIf (id == "EnableALCHItemFood")
        properties.LootableALCHItemType = Math.LogicalXor(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_FOOD)
    ElseIf (id == "EnableALCHItemNukaCola")
        properties.LootableALCHItemType = Math.LogicalXor(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_NUKA_COLA)
    ElseIf (id == "EnableALCHItemStimpak")
        properties.LootableALCHItemType = Math.LogicalXor(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_STIMPAK)
    ElseIf (id == "EnableALCHItemSyringerAmmo")
        properties.LootableALCHItemType = Math.LogicalXor(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_SYRINGER_AMMO)
    ElseIf (id == "EnableALCHItemWater")
        properties.LootableALCHItemType = Math.LogicalXor(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_WATER)
    ElseIf (id == "EnableALCHItemOther")
        properties.LootableALCHItemType = Math.LogicalXor(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_OTHER)

    ElseIf (id == "EnableBOOKItemPerkMagazine")
        properties.LootableBOOKItemType = Math.LogicalXor(properties.LootableBOOKItemType, properties.BOOK_ITEM_TYPE_PERKMAGAZINE)
    ElseIf (id == "EnableBOOKItemOther")
        properties.LootableBOOKItemType = Math.LogicalXor(properties.LootableBOOKItemType, properties.BOOK_ITEM_TYPE_OTHER)

    ElseIf (id == "EnableMISCItemBobblehead")
        properties.LootableMISCItemType = Math.LogicalXor(properties.LootableMISCItemType, properties.MISC_ITEM_TYPE_BOBBLEHEAD)
    ElseIf (id == "EnableMISCItemOther")
        properties.LootableMISCItemType = Math.LogicalXor(properties.LootableMISCItemType, properties.MISC_ITEM_TYPE_OTHER)

    ElseIf (id == "EnableWEAPItemGrenade")
        properties.LootableWEAPItemType = Math.LogicalXor(properties.LootableWEAPItemType, properties.WEAP_ITEM_TYPE_GRENADE)
    ElseIf (id == "EnableWEAPItemMine")
        properties.LootableWEAPItemType = Math.LogicalXor(properties.LootableWEAPItemType, properties.WEAP_ITEM_TYPE_MINE)
    ElseIf (id == "EnableWEAPItemOther")
        properties.LootableWEAPItemType = Math.LogicalXor(properties.LootableWEAPItemType, properties.WEAP_ITEM_TYPE_OTHER)
    EndIf

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

    If (MoveItemsType < MOVE_ITEM_ALL && MoveItemsType > MOVE_ITEM_KEY)
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

    If (ScrapItemsType < SCRAP_ITEM_ALL && ScrapItemsType > SCRAP_ITEM_JUNK)
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
    If (properties.EnableLootMan)
        system.ShowMessage(system.MESSAGE_ENABLED)
    Else
        system.ShowMessage(system.MESSAGE_DISABLED)
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
            system.ShowMessage(system.MESSAGE_UNLINKED_TO_WORKSHOP)
            LogMcmEvent("workshop_unlinked", FormField("workshop", workshop) + " " + FormField("location", workshopLocation) + " reason=manual_toggle")
        Else
            If (system.LinkWorkshop(workshop, prefix))
                system.ShowMessage(system.MESSAGE_LINKED_TO_WORKSHOP)
                LogMcmEvent("workshop_linked", FormField("workshop", workshop) + " " + FormField("location", workshop.myLocation) + " reason=manual_toggle")
            EndIf
        EndIf
    Else
        system.ShowMessage(system.MESSAGE_WORKSHOP_NOT_FOUND)
        LogMcmEvent("workshop_link_failed", "reason=no_workshop", LOG_LEVEL_DEBUG)
    EndIf
EndFunction

Function ExecuteLooting()
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf

    system.Looting()
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
