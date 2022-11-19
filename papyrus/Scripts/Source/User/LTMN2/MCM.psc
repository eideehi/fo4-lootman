Scriptname LTMN2:MCM extends Quest
{A class that collectively defines functions called from the MCM menu.}

; Get an instance of this quest.
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
EndGroup

; Local variables
Actor player
LTMN2:Properties properties
LTMN2:System system

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
    ; Reset properties that need to be reset each load
    SetUtilityBusy(false)

    RegisterForExternalEvent("OnMCMSettingChange|LootMan", "OnMCMSettingChange")
EndFunction

Function OnMCMSettingChange(string modName, string id)
    If (modName != "LootMan")
        Return
    EndIf

    string prefix = ("| MCM | " + LTMN2:Debug.GetRandomProcessId() + " | ")
    LTMN2:Debug.Log(prefix + "[ MCM settings have been changed: \"" + id + "\" ]")

    If (id == "AutomaticallyLinkAndUnlinkToWorkshop")
        LTMN2:Debug.Log(prefix + "  [ Adjust the link status between LootMan and the workshop because LootMan's configuration has been changed ]")
        LTMN2:Debug.Log(prefix + "    Automatically link LootMan to workshops: " + properties.AutomaticallyLinkAndUnlinkToWorkshop)
        LTMN2:Debug.Log(prefix + "    Workshop exists at the current location: " + (properties.WorkshopParent.GetWorkshopFromLocation(player.GetCurrentLocation()) != none))

        Location currentLocation = player.GetCurrentLocation()
        If (properties.WorkshopParent.GetWorkshopFromLocation(currentLocation))
            LTMN2:Debug.Log(prefix + "    LootMan is linked to the current location: " + currentLocation.IsLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan))

            bool linkToCurrentLocation = currentLocation.IsLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan)
            If (!linkToCurrentLocation && properties.AutomaticallyLinkAndUnlinkToWorkshop)
                LTMN2:Debug.Log(prefix + "    [ Add the link to the current location ]")
                currentLocation.AddLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan)
                system.ShowMessage(system.MESSAGE_LINKED_TO_WORKSHOP)
            ElseIf (linkToCurrentLocation && properties.AutomaticallyLinkAndUnlinkToWorkshop)
                LTMN2:Debug.Log(prefix + "    [ Remove the link to the current location ]")
                currentLocation.RemoveLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan)
                system.ShowMessage(system.MESSAGE_UNLINKED_TO_WORKSHOP)
            Else
                LTMN2:Debug.Log(prefix + "    [ No need to adjust the link status ]")
            EndIf
        Else
            LTMN2:Debug.Log(prefix + "    [ No need to adjust the link status ]")
        EndIf

    ElseIf (id == "NotLootingFromSettlement")
        Location currentLocation = player.GetCurrentLocation()

        LTMN2:Debug.Log(prefix + "  [ Adjust the flag whether you are staying in the settlement because LootMan's configuration has been changed ]")
        LTMN2:Debug.Log(prefix + "    Not looting from settlement: " + properties.NotLootingFromSettlement)
        LTMN2:Debug.Log(prefix + "    Current location is a settlement (LocTypeSettlement): " + currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeSettlement))
        LTMN2:Debug.Log(prefix + "    Current location is a settlement (LocTypeWorkshopSettlement): " + currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement))

        If (currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || currentLocation.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement))
            properties.IsInSettlement = true
        Else
            properties.IsInSettlement = false
        EndIf

        LTMN2:Debug.Log(prefix + "    [ Player is in the settlement: " + properties.IsInSettlement + " ]")

    ElseIf (id == "WorkerInvokeInterval")
        system.ResetLootingTimer()

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

    LTMN2:Utils.MoveInventoryItems(fromInventory, toInventory, type, 0)
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

    WorkshopScript workshop = properties.WorkshopParent.GetWorkshopFromLocation(player.GetCurrentLocation())
    If (workshop && workshop.myLocation)
        If (workshop.myLocation.IsLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan))
            workshop.myLocation.RemoveLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan)
            system.ShowMessage(system.MESSAGE_UNLINKED_TO_WORKSHOP)
        Else
            workshop.myLocation.AddLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan)
            system.ShowMessage(system.MESSAGE_LINKED_TO_WORKSHOP)
        EndIf
    Else
        system.ShowMessage(system.MESSAGE_WORKSHOP_NOT_FOUND)
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
        Return properties.WorkshopParent.GetWorkshopFromLocation(player.GetCurrentLocation())
    EndIf
    Return none
EndFunction
