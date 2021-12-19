Scriptname LTMN:Thread:LootingACTI extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_ACTI
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ")
    LTMN:Debug.Log(prefix + "Loot: [Name: " + ref.GetDisplayName() + ", ID: " + LTMN:Debug.GetHexID(ref) + "]")
    If (ref.Activate(properties.LootmanActor) && properties.PlayPickupSound.GetValueInt() == 1 && properties.LootInPlayerDirectly.GetValueInt() != 1)
        properties.PickupSoundACTI.Play(player)
    EndIf
EndFunction

bool Function IsLootingTarget(ObjectReference ref)
    Form base = ref.GetBaseObject()

    If (!properties.AllowedActivatorList.HasForm(base))
        Return false
    EndIf

    If (ref.IsActivationBlocked() && !properties.IgnorableActivationBlockeList.HasForm(base))
        Return false
    EndIf

    If (!ref.Is3DLoaded() || ref.IsDestroyed())
        Return false
    EndIf

    If (!IsValidObject(ref) || !IsLootableOwnership(ref))
        Return false
    EndIf

    Return IsLootableRarity(base)
EndFunction

Function TraceObject(ObjectReference ref) debugOnly
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ")
    LTMN:Debug.TraceObject(prefix, ref)
    Form base = ref.GetBaseObject()
    LTMN:Debug.TraceForm(prefix + "  ", base)
    LTMN:Debug.Log(prefix + "    Is activator that is allowed to loot: " + properties.AllowedActivatorList.HasForm(base))
    LTMN:Debug.Log(prefix + "    Is allowed to ignore the activation block: " + properties.IgnorableActivationBlockeList.HasForm(base))
EndFunction
