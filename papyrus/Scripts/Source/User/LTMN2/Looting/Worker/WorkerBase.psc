Scriptname LTMN2:Looting:Worker:WorkerBase extends Quest hidden

CustomEvent CallLooting

Actor property player auto hidden
LTMN2:Properties property properties auto hidden

bool threadRunning = false

int LOG_LEVEL_DEBUG = 1 const
int LOG_LEVEL_INFO = 2 const

Function LogWorkerEvent(string eventName, string fields = "", int logLevel = 1)
    LTMN2:LootMan.LogEvent("looting_worker", eventName, fields, logLevel)
EndFunction

string Function GetThreadField() debugOnly
    Return "thread=" + GetThreadId()
EndFunction

Event OnInit()
    player = Game.GetPlayer()
    properties = LTMN2:Properties.GetInstance()
EndEvent

Event OnQuestInit()
    LogWorkerEvent("started", GetThreadField())
    RegisterForCustomEvent(self, "CallLooting")
EndEvent

Event OnQuestShutdown()
    UnregisterForCustomEvent(self, "CallLooting")
    LogWorkerEvent("shutdown", GetThreadField())
EndEvent

; If the looting is possible, the looting process is called
Event LTMN2:Looting:Worker:WorkerBase.CallLooting(LTMN2:Looting:Worker:WorkerBase sender, Var[] args)
    If (sender == self && properties.IsInitialized)
        IncreaseActiveThreadCount()
        threadRunning = true

        Looting()

        threadRunning = false
        DecreaseActiveThreadCount()
    EndIf
EndEvent

Function Initialize()
    LogWorkerEvent("initialized", GetThreadField())
    threadRunning = false
EndFunction

Function Run()
    SendCustomEvent("CallLooting")
EndFunction

bool Function Busy()
    Return threadRunning
EndFunction

Function Looting()
    If (properties.IsNotInitialized || !LTMN2:Utils.IsLootingSafe())
        Return
    EndIf

    int formType = GetFormTypeOfAssignment()
    LogWorkerEvent("pass_started", GetThreadField() + " form_type=" + formType)

    int refCount = LTMN2:LootMan.LootNearbyReferences(player, properties.LootManRef, properties.ActivatorRef, properties.LootManWorkshopRef, formType, properties.LootableInventoryItemType, properties.PlayPickupSound, properties.PlayContainerAnimation, properties.UnlockLockedContainer, properties.BobbyPin, properties.Locksmith01, properties.Locksmith02, properties.Locksmith03, properties.Locksmith04)

    If (properties.WorkerInvokeInterval > 0 && refCount >= properties.MaxItemsProcessedPerThread)
        SetTurboMode()
        LogWorkerEvent("turbo_enabled", GetThreadField() + " form_type=" + formType + " processed=" + refCount + " max_per_thread=" + properties.MaxItemsProcessedPerThread, LOG_LEVEL_INFO)
    EndIf
EndFunction

Function IncreaseActiveThreadCount()
EndFunction

Function DecreaseActiveThreadCount()
EndFunction

int Function GetFormTypeOfAssignment()
    Return 0
EndFunction

Function SetTurboMode()
EndFunction

bool Function IsLootingTarget(ObjectReference ref)
    Return !player.WouldBeStealing(ref)
EndFunction

Function LootObject(ObjectReference ref)
    If (properties.IsNotInitialized)
        Return
    EndIf

    Form baseForm = ref.GetBaseObject()
    int beforeCount = 0
    If (baseForm)
        beforeCount = properties.LootManRef.GetItemCount(baseForm)
    EndIf

    If (properties.PlayPickupSound)
        LTMN2:LootMan.PlayPickUpSound(player, ref)
    EndIf
    properties.LootManRef.AddItem(ref, 1, true)

    int afterCount = beforeCount
    If (baseForm)
        afterCount = properties.LootManRef.GetItemCount(baseForm)
        If (afterCount > beforeCount)
            LTMN2:LootMan.FinalizeWorldPickup(ref)
        EndIf
    EndIf
EndFunction

; Return the thread identifier to be output to the log. Override it in the worker's script
string Function GetThreadId() debugOnly
    Return "UNKNOWN"
EndFunction
