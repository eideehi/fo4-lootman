Scriptname LTMN:Thread:LootingACTI extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_ACTI
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    Lootman.Log(prefix + "Loot: [Name: " + ref.GetDisplayName() + ", ID: " + Lootman.GetHexID(ref) + "]");; Debug
    If (ref.Activate(GetLootingActor()) && properties.PlayPickupSound.GetValueInt() == 1 && properties.LootInPlayerDirectly.GetValueInt() != 1)
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

Function TraceObject(ObjectReference ref);; Debug
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    LTMN:Quest:Methods.TraceObject(prefix, ref);; Debug
    Form base = ref.GetBaseObject();; Debug
    LTMN:Quest:Methods.TraceForm(prefix + "  ", base);; Debug
    Lootman.Log(prefix + "    Is activator that is allowed to loot: " + properties.AllowedActivatorList.HasForm(base));; Debug
    Lootman.Log(prefix + "    Is allowed to ignore the activation block: " + properties.IgnorableActivationBlockeList.HasForm(base));; Debug
EndFunction;; Debug
