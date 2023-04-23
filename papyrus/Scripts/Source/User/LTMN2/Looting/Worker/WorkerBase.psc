Scriptname LTMN2:Looting:Worker:WorkerBase extends Quest hidden

CustomEvent CallLooting

Actor property player auto hidden
LTMN2:Properties property properties auto hidden

bool threadRunning = false

string processId = "0000000000"

Event OnInit()
    player = Game.GetPlayer()
    properties = LTMN2:Properties.GetInstance()
EndEvent

Event OnQuestInit()
    LTMN2:Debug.Log("| Looting Worker @ " + GetThreadId() + " | ---------- | Worker started")
    RegisterForCustomEvent(self, "CallLooting")
EndEvent

Event OnQuestShutdown()
    UnregisterForCustomEvent(self, "CallLooting")
    LTMN2:Debug.Log("| Looting Worker @ " + GetThreadId() + " | ---------- | Worker shutdown")
EndEvent

; If the looting is possible, the looting process is called
Event LTMN2:Looting:Worker:WorkerBase.CallLooting(LTMN2:Looting:Worker:WorkerBase sender, Var[] args)
    If (sender == self && properties.IsInitialized)
        IncreaseActiveThreadCount()
        threadRunning = true

        processId = LTMN2:Debug.GetRandomProcessId()
        Looting()

        threadRunning = false
        DecreaseActiveThreadCount()
    EndIf
EndEvent

Function Initialize()
    LTMN2:Debug.Log("| Looting Worker @ " + GetThreadId() + " | ---------- |     Initialize worker")
    threadRunning = false
EndFunction

Function Run()
    SendCustomEvent("CallLooting")
EndFunction

bool Function Busy()
    Return threadRunning
EndFunction

Function Looting()
    If (properties.IsNotInitialized)
        Return
    EndIf

    string prefix = GetLogPrefix()
    LTMN2:Debug.Log(prefix + "[ Start looting ]")

    ObjectReference[] refs = LTMN2:LootMan.FindNearbyReferencesWithFormType(player, GetFormTypeOfAssignment())

    If (properties.WorkerInvokeInterval > 0 && refs.Length >= properties.MaxItemsProcessedPerThread)
        SetTurboMode()
    EndIf

    int objectIndex = 1
    int i = refs.Length
    While i
        i -= 1

        ObjectReference ref = refs[i]
        If (ref)
            int id = ref.GetFormID()

            LTMN2:Debug.Log(prefix + "  [ Object " + objectIndex + " ]")
            TraceObject(prefix + "    ", ref)
            TraceForm(prefix + "    ", ref.GetBaseObject())

            If (IsLootingTarget(ref))
                LootObject(ref)
            Else
                LTMN2:Debug.Log(prefix + "    [ Is not a target of looting ]")
            EndIf

            LTMN2:LootMan.ReleaseObject(id)
            objectIndex += 1
        EndIf
    EndWhile
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
    If (Utility.IsInMenuMode())
        Return false
    EndIf
    If (!ref.IsNearPlayer() || !ref.GetParentCell().IsAttached() || !ref.Is3DLoaded())
        Return false
    EndIf
    Return !player.WouldBeStealing(ref)
EndFunction

Function LootObject(ObjectReference ref)
    If (properties.IsNotInitialized)
        Return
    EndIf

    string prefix = GetLogPrefix(2)
    LTMN2:Debug.Log(prefix + "Loot: [ Name: " + ref.GetDisplayName() + ", Id: " + LTMN2:Debug.GetHexID(ref) + " ]")
    If (properties.PlayPickupSound)
        LTMN2:LootMan.PlayPickUpSound(player, ref)
    EndIf
    properties.LootManRef.AddItem(ref, 1, true)
EndFunction

; Return the process id to be output to the log
string Function GetProcessId() debugOnly
    Return processId
EndFunction

; Return the thread identifier to be output to the log. Override it in the worker's script
string Function GetThreadId() debugOnly
    Return "UNKNOWN"
EndFunction

string Function GetLogPrefix(int indent = 0) debugOnly
    string indentString = ""
    While indent
        indent -= 1
        indentString += "  "
    EndWhile
    return ("| Looting Worker @ " + GetThreadID() + " | " + GetProcessId() + " | " + indentString)
EndFunction

; Output the trace log of an object
Function TraceObject(string logPrefix, ObjectReference ref) debugOnly
    LTMN2:Debug.TraceObject(logPrefix, ref)
EndFunction

; Output the trace log of a form
Function TraceForm(string logPrefix, Form baseForm) debugOnly
    LTMN2:Debug.TraceForm(logPrefix, baseForm)
EndFunction
