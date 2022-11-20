Scriptname LTMN2:System extends Quest

; Get an instance of this quest
LTMN2:System Function GetInstance() global
    Return Game.GetFormFromFile(0x000F99, "LootMan.esp") As LTMN2:System
EndFunction

; Get mod version string
string Function GetVersionString(int version) global debugOnly
    Return (version / 10000) + "." + (version / 100 % 100) + "." + (version % 100)
EndFunction

; Version as int.
; syntax: Major{1}.Minor{2}.Patch{2}
; example: 10234 // 1.2.34
int MOD_VERSION = 20003 const

; Timer id list
int TIMER_INSTALL = 1 const
int TIMER_INITIALIZE = 2 const
int TIMER_UPDATE = 3 const
int TIMER_LOOTING = 4 const

; Number of registered system messages.
int MESSAGE_COUNT = 11 const

Group MessageId
    ; Message id
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
    ; Looting worker manager
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
    ; LootMan version, used to patch when mods are updated.
    int property CurrentModVersion = 0 auto hidden
EndGroup

; Local variables
Actor player
LTMN2:Properties properties
int[] messageDisplayCount
float[] lastMessageDisplayTime

; LootMan's system initialization
Event OnInit()
    LTMN2:Debug.OpenLog()
    LTMN2:Debug.Log("| System | ---------- | LootMan " + GetVersionString(MOD_VERSION) + " has been running for the first time")

    player = Game.GetPlayer()
    properties = LTMN2:Properties.GetInstance()
    messageDisplayCount = new int[MESSAGE_COUNT]
    lastMessageDisplayTime = new float[MESSAGE_COUNT]

    CurrentModVersion = MOD_VERSION

    RegisterForRemoteEvent(player, "OnPlayerLoadGame")

    StartTimer(5, TIMER_INSTALL)
EndEvent

; Process called when saved data is loaded
Event Actor.OnPlayerLoadGame(Actor akSender)
    LTMN2:Debug.OpenLog()
    LTMN2:Debug.Log("| System | ---------- | LootMan " + GetVersionString(MOD_VERSION) + " has been running")

    ; Reset properties that need to be reset each load
    properties.IsInitialized = false
    properties.IsNotInitialized = true

    CancelTimer(TIMER_INSTALL)
    CancelTimer(TIMER_INITIALIZE)
    CancelTimer(TIMER_UPDATE)
    CancelTimer(TIMER_LOOTING)

    messageDisplayCount = new int[MESSAGE_COUNT]
    lastMessageDisplayTime = new float[MESSAGE_COUNT]

    ; Apply the patches if the save data and the Mod version of the esp do not match due to the LootMan update.
    If (CurrentModVersion != MOD_VERSION)
        Patch()
    EndIf

    If (properties.IsNotInstalled)
        StartTimer(5, TIMER_INSTALL)
    Else
        StartTimer(3, TIMER_INITIALIZE)
    EndIf
EndEvent

; An event that register to player. Process that should be performed when location is changed.
Event Actor.OnLocationChange(Actor akSender, Location akOldLoc, Location akNewLoc)
    string prefix = ("| System | " + LTMN2:Debug.GetRandomProcessID() + " | ")
    LTMN2:Debug.Log(prefix + "[ Update LootMan status as the player's location has changed ]")
    LTMN2:Debug.Log(prefix + "  Old location: [ Name: \"" + LTMN2:Debug.GetName(akOldLoc) + "\", Id: " + LTMN2:Debug.GetHexID(akOldLoc) + " ]")
    LTMN2:Debug.Log(prefix + "  New location: [ Name: \"" + LTMN2:Debug.GetName(akNewLoc) + "\", Id: " + LTMN2:Debug.GetHexID(akNewLoc) + " ]")

    LTMN2:Debug.Log(prefix + "  [ Not looting from settlement: " + properties.NotLootingFromSettlement + " ]")
    If (properties.NotLootingFromSettlement)
        LTMN2:Debug.Log(prefix + "    New location is a settlement: " + akNewLoc.HasKeyword(Game.GetCommonProperties().LocTypeSettlement))
        LTMN2:Debug.Log(prefix + "    New location is a workshop settlement: " + akNewLoc.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement))

        If (akNewLoc.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || akNewLoc.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement))
            properties.IsInSettlement = true
            ShowMessage(MESSAGE_REMIND_NOT_LOOTING_IN_SETTLEMENT)
            LTMN2:Debug.Log(prefix + "      [ Player entered the settlement ]")
        Else
            properties.IsInSettlement = false
            LTMN2:Debug.Log(prefix + "      [ Player leaved the settlement ]")
        EndIf
    EndIf

    LTMN2:Debug.Log(prefix + "  [ Automatically link / unlink to workshop: " + properties.AutomaticallyLinkAndUnlinkToWorkshop + " ]")
    If (properties.AutomaticallyLinkAndUnlinkToWorkshop)
        LTMN2:Debug.Log(prefix + "    Workshops exist in the old location: " + (properties.WorkshopParent.GetWorkshopFromLocation(akOldLoc) != None))
        If (properties.WorkshopParent.GetWorkshopFromLocation(akOldLoc))
            LTMN2:Debug.Log(prefix + "      Linked to LootMan: " + akOldLoc.IsLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan))
            If (akOldLoc.IsLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan))
                LTMN2:Debug.Log(prefix + "      [ Removed link with LootMan ]")
                akOldLoc.RemoveLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan)
                ShowMessage(MESSAGE_UNLINKED_TO_WORKSHOP)
            EndIf
        EndIf

        LTMN2:Debug.Log(prefix + "    Workshops exist in the new location: " + (properties.WorkshopParent.GetWorkshopFromLocation(akNewLoc) != None))
        WorkshopScript workshop = properties.WorkshopParent.GetWorkshopFromLocation(akNewLoc)
        If (workshop && workshop.OwnedByPlayer)
            LTMN2:Debug.Log(prefix + "      Linked to LootMan: " + akNewLoc.IsLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan))
            If (!akNewLoc.IsLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan))
                LTMN2:Debug.Log(prefix + "      [ Added link to LootMan ]")
                akNewLoc.AddLinkedLocation(properties.LootManLocation, properties.WorkshopCaravan)
                ShowMessage(MESSAGE_LINKED_TO_WORKSHOP)
            EndIf
        EndIf
    EndIf
EndEvent

; Called when a specific item is added to LootMan's inventory. The item is moved to the workbench in an appropriate manner so that it does not cause a glitch.
Event ObjectReference.OnItemAdded(ObjectReference akSender, Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akSourceContainer)
    string prefix = ("| System | " + LTMN2:Debug.GetRandomProcessId() + " | ")

    LTMN2:Debug.Log(prefix + "[ LootMan.OnItemAdded called ]")

    ; Shipment will be lost if it is moved to the Wrokshop container using RemoveItem, so add it using AddItem.
    If (properties.ShipmentItemList.HasForm(akBaseItem))
        LTMN2:Debug.Log(prefix + "  [ Move shipment safely to the workshop ]")
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

; Check if LootMan can be installed.
bool Function CanInstall()
    string prefix = ("| System | " + LTMN2:Debug.GetRandomProcessId() + " | ")

    LTMN2:Debug.Log(prefix + "[ Check if LootMan can be installed ]")
    LTMN2:Debug.Log(prefix + "  Player has Pipboy: " + (player.GetItemCount(properties.Pipboy) > 0))
    LTMN2:Debug.Log(prefix + "  Institute's radio is ready to receive: " + properties.RadioInstitute.IsRunning())

    If (player.GetItemCount(properties.Pipboy) > 0 || properties.RadioInstitute.IsRunning())
        LTMN2:Debug.Log(prefix + "  [ LootMan can be installed ]")
        Return true
    Else
        LTMN2:Debug.Log(prefix + "  [ LootMan cannot be installed ]")
        Return false
    EndIf
EndFunction

; Perform LootMan installation.
Function Install()
    string prefix = ("| System | " + LTMN2:Debug.GetRandomProcessId() + " | ")

    If (properties.IsInstalled)
        LTMN2:Debug.Log(prefix + "[ The LootMan installation process was called, but the call is skipped because LootMan is already installed ]")
        Return
    EndIf

    If (properties.IsUninstalled)
        LTMN2:Debug.Log(prefix + "[ The LootMan installation process is called, but the call is skipped because LootMan is already uninstalled ]")
        Return
    EndIf

    properties.IsInstalled = true
    properties.IsNotInstalled = false

    LTMN2:Debug.Log(prefix + "[ Start LootMan installation ]")

    LTMN2:Debug.Log(prefix + "  [ Register an event ]")
    AddInventoryEventFilter(properties.ShipmentItemList)
    RegisterForRemoteEvent(player, "OnLocationChange")
    RegisterForRemoteEvent(properties.LootManRef, "OnItemAdded")

    LTMN2:Debug.Log(prefix + "  [ Start looting worker manager ]")
    WorkerManagerACTI.Start()
    WorkerManagerALCH.Start()
    WorkerManagerAMMO.Start()
    WorkerManagerARMO.Start()
    WorkerManagerBOOK.Start()
    WorkerManagerCONT.Start()
    WorkerManagerFLOR.Start()
    WorkerManagerINGR.Start()
    WorkerManagerKEYM.Start()
    WorkerManagerMISC.Start()
    WorkerManagerNPC_.Start()
    WorkerManagerWEAP.Start()

    StartTimer(1, TIMER_INITIALIZE)

    self.SetObjectiveDisplayed(MESSAGE_INSTALLED)
    self.SetObjectiveSkipped(MESSAGE_INSTALLED)

    LTMN2:Debug.Log(prefix + "  [ LootMan has been installed ]")
EndFunction

; Uninstall LootMan
Function Uninstall()
    string prefix = ("| System | " + LTMN2:Debug.GetRandomProcessId() + " | ")

    If (properties.IsNotInstalled)
        LTMN2:Debug.Log(prefix + "[ The LootMan uninstall process was called, but the call is skipped because LootMan is not yet installed ]")
        Return
    EndIf

    If (properties.IsUninstalled)
        LTMN2:Debug.Log(prefix + "[ The LootMan uninstall process was called, but the call is skipped because LootMan is already uninstalled ]")
        Return
    EndIf

    properties.IsUninstalled = true
    properties.IsNotUninstalled = false

    LTMN2:Debug.Log(prefix + "[ Start LootMan uninstallation ]")

    LTMN2:Debug.Log(prefix + "  [ Stop timer function ]")
    CancelTimer(TIMER_INSTALL)
    CancelTimer(TIMER_INITIALIZE)
    CancelTimer(TIMER_UPDATE)
    CancelTimer(TIMER_LOOTING)

    LTMN2:Debug.Log(prefix + "  [ Unregister an event ]")
    RemoveAllInventoryEventFilters()
    UnregisterForRemoteEvent(player, "OnPlayerLoadGame")
    UnregisterForRemoteEvent(player, "OnLocationChange")
    UnregisterForRemoteEvent(properties.LootManRef, "OnItemAdded")

    LTMN2:Debug.Log(prefix + "  [ Shutdown looting worker manager ]")
    WorkerManagerACTI.Stop()
    WorkerManagerALCH.Stop()
    WorkerManagerAMMO.Stop()
    WorkerManagerARMO.Stop()
    WorkerManagerBOOK.Stop()
    WorkerManagerCONT.Stop()
    WorkerManagerFLOR.Stop()
    WorkerManagerINGR.Stop()
    WorkerManagerKEYM.Stop()
    WorkerManagerMISC.Stop()
    WorkerManagerNPC_.Stop()
    WorkerManagerWEAP.Stop()

    self.SetObjectiveDisplayed(MESSAGE_UNINSTALLED)
    self.SetObjectiveSkipped(MESSAGE_UNINSTALLED)

    LTMN2:Utils.MoveInventoryItems(properties.LootManRef, player, properties.ITEM_TYPE_ALL)
    LTMN2:Utils.MoveInventoryItems(properties.LootManWorkshopRef, player, properties.ITEM_TYPE_ALL)

    LTMN2:Debug.Log(prefix + "[ LootMan has been uninstalled ]")

    properties.IsInstalled = false
    properties.IsInitialized = false

    properties.Stop()
    self.Stop()
EndFunction

; Initialize LootMan
Function Initialize()
    string prefix = ("| System | " + LTMN2:Debug.GetRandomProcessId() + " | ")

    If (properties.IsNotInstalled)
        LTMN2:Debug.Log(prefix + "[ The LootMan initialization process was called, but the call is skipped because LootMan is not yet installed ]")
        Return
    EndIf

    If (properties.IsInitialized)
        LTMN2:Debug.Log(prefix + "[ The LootMan initialization process was called, but the call is skipped because LootMan is already initialized ]")
        Return
    EndIf

    If (!properties.IsNotUninstalled)
        LTMN2:Debug.Log(prefix + "[ The LootMan initialization process was called, but the call is skipped because LootMan is already uninstalled ]")
        Return
    EndIf

    LTMN2:Debug.Log(prefix + "[ Start LootMan initialization ]")

    LTMN2:LootMan.OnUpdateLootManProperty("")
    LTMN2:MCM.GetInstance().Initialize()

    StartTimer(1, TIMER_UPDATE)
    If (properties.WorkerInvokeInterval > 0)
        StartTimer(3, TIMER_LOOTING)
    EndIf

    LTMN2:Debug.Log(prefix + "  [ LootMan initialization completed ]")

    properties.IsInitialized = true
    properties.IsNotInitialized = false
EndFunction

; Apply patches for migration
Function Patch()
    string prefix = ("| System | " + LTMN2:Debug.GetRandomProcessId() + " | ")

    LTMN2:Debug.Log(prefix + "[ LootMan patching started ]")
    LTMN2:Debug.Log(prefix + "  Current version: " + GetVersionString(CurrentModVersion))

    ; Apply the patches if the save data and the Mod version of the esp do not match due to the LootMan update.
    If (CurrentModVersion < 20001)
        LTMN2:Patch.v2_0_1()
    EndIf

    CurrentModVersion = MOD_VERSION

    LTMN2:Debug.Log(prefix + "  Update to version: " + GetVersionString(CurrentModVersion))
EndFunction

; Update LootMan status
Function Update()
    string prefix = ("| System | " + LTMN2:Debug.GetRandomProcessId() + " | ")

    If (properties.IsNotInstalled)
        LTMN2:Debug.Log(prefix + "[ The LootMan update process was called, but the call is skipped because LootMan is not yet installed ]")
        Return
    EndIf

    If (properties.IsNotInitialized)
        LTMN2:Debug.Log(prefix + "[ The LootMan update process was called, but the call is skipped because LootMan is not yet initialized ]")
        Return
    EndIf

    If (properties.IsUninstalled)
        LTMN2:Debug.Log(prefix + "[ The LootMan update process was called, but the call is skipped because LootMan is already uninstalled ]")
        Return
    EndIf

    If (!properties.EnableLootMan)
        Return
    EndIf

    LTMN2:Debug.Log(prefix + "[ Start updating the state ]")

    LTMN2:Debug.Log(prefix + "  [ Check LootMan (Actor) inventory ]")
    LTMN2:Debug.Log(prefix + "    Item count: " + properties.LootManRef.GetItemCount())
    If (properties.LootManRef.GetItemCount() > 0)
        If (properties.LootIsDeliverToPlayer)
            LTMN2:Debug.Log(prefix + "    [ Move items from LootMan to player ]")
            LTMN2:Utils.MoveInventoryItems(properties.LootManRef, player, properties.ITEM_TYPE_ALL, -1, false)
        Else
            LTMN2:Debug.Log(prefix + "    [ Move items from LootMan to workshop ]")
            LTMN2:Utils.MoveInventoryItems(properties.LootManRef, properties.LootManWorkshopRef, properties.ITEM_TYPE_ALL)
        EndIf
        LTMN2:Debug.Log(prefix + "      Item count of after processing: " + properties.LootManRef.GetItemCount())
    EndIf

    LTMN2:Debug.Log(prefix + "  [ Check LootMan overweight ]")
    LTMN2:Debug.Log(prefix + "    Ignore overweight: " + properties.IgnoreOverweight)
    If (!properties.IgnoreOverweight)
        LTMN2:Debug.Log(prefix + "    Currently in an overweight state: " + properties.IsOverweight)
        LTMN2:Debug.Log(prefix + "    LootMan workshop inventory weight: " + properties.LootManWorkshopRef.GetInventoryWeight())
        LTMN2:Debug.Log(prefix + "    Weight carrying capacity: " + properties.CarryWeight)

        bool wasOverweight = properties.IsOverweight
        bool overweight = properties.LootManWorkshopRef.GetInventoryWeight() > properties.CarryWeight
        If (!wasOverweight && overweight)
            properties.IsOverweight = true
            LTMN2:Debug.Log(prefix + "    [ LootMan is now overweight ]")
        ElseIf (wasOverweight && overweight)
            ShowMessage(MESSAGE_HAS_OVERWEIGHT)
        ElseIf (wasOverweight && !overweight)
            properties.IsOverweight = false
            LTMN2:Debug.Log(prefix + "    [ LootMan is no longer overweight ]")
        EndIf
    ElseIf (properties.IsOverweight)
        properties.IsOverweight = false
    EndIf

    float time = Utility.GetCurrentRealTime()
    int messageId = MESSAGE_COUNT
    While messageId
        messageId -= 1
        If (lastMessageDisplayTime[messageId] > 0 && (time - lastMessageDisplayTime[messageId]) >= 600)
            lastMessageDisplayTime[messageId] = 0
            messageDisplayCount[messageId] = 0
        EndIf
    EndWhile
EndFunction

Function Looting()
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf
    If (!properties.EnableLootMan)
        Return
    EndIf
    If (properties.IsOverweight && !properties.IgnoreOverweight)
        Return
    EndIf
    If (properties.IsInSettlement && properties.NotLootingFromSettlement)
        Return
    EndIf

    (WorkerManagerACTI As LTMN2:Looting:WorkerManagerACTI).Looting()
    (WorkerManagerALCH As LTMN2:Looting:WorkerManagerALCH).Looting()
    (WorkerManagerAMMO As LTMN2:Looting:WorkerManagerAMMO).Looting()
    (WorkerManagerARMO As LTMN2:Looting:WorkerManagerARMO).Looting()
    (WorkerManagerBOOK As LTMN2:Looting:WorkerManagerBOOK).Looting()
    (WorkerManagerFLOR As LTMN2:Looting:WorkerManagerFLOR).Looting()
    (WorkerManagerINGR As LTMN2:Looting:WorkerManagerINGR).Looting()
    (WorkerManagerKEYM As LTMN2:Looting:WorkerManagerKEYM).Looting()
    (WorkerManagerMISC As LTMN2:Looting:WorkerManagerMISC).Looting()
    (WorkerManagerWEAP As LTMN2:Looting:WorkerManagerWEAP).Looting()
    (WorkerManagerCONT As LTMN2:Looting:WorkerManagerCONT).Looting()
    (WorkerManagerNPC_ As LTMN2:Looting:WorkerManagerNPC_).Looting()
EndFunction

Function ResetLootingTimer()
    If (properties.WorkerInvokeInterval > 0)
        StartTimer(properties.WorkerInvokeInterval, TIMER_LOOTING)
    EndIf
EndFunction

; Display the message set in Quest Objectives
Function ShowMessage(int messageId, float interval = 30.0, int maxDisplayCount = 3)
    If (properties.IsNotInstalled || properties.IsNotInitialized || properties.IsUninstalled)
        Return
    EndIf
    If (!properties.EnableLootMan)
        Return
    EndIf
    If (!properties.DisplaySystemMessage)
        Return
    EndIf

    int arrayIndex = messageId - 1
    int count = messageDisplayCount[arrayIndex]
    If (count >= maxDisplayCount)
        Return
    EndIf

    float time = Utility.GetCurrentRealTime()
    If ((time - lastMessageDisplayTime[arrayIndex]) < interval)
        Return
    EndIf

    messageDisplayCount[arrayIndex] = count + 1
    lastMessageDisplayTime[arrayIndex] = time

    self.SetObjectiveDisplayed(messageId)
    self.SetObjectiveSkipped(messageId)
EndFunction
