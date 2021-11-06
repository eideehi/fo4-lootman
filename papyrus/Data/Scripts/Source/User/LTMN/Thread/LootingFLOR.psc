Scriptname LTMN:Thread:LootingFLOR extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_FLOR
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    Lootman.Log(prefix + "Loot: [Name: " + ref.GetDisplayName() + ", ID: " + Lootman.GetHexID(ref) + "]");; Debug
    If (ref.Activate(GetLootingActor()) && properties.PlayPickupSound.GetValueInt() == 1 && properties.LootInPlayerDirectly.GetValueInt() != 1)
        properties.PickupSoundFLOR.Play(player)
    EndIf
EndFunction

bool Function IsLootingTarget(ObjectReference ref)
    Form base = ref.GetBaseObject()

    If (!ref.Is3DLoaded() || ref.IsDestroyed() || (ref.IsActivationBlocked() && !properties.IgnorableActivationBlockeList.HasForm(base)))
        Return false
    EndIf

    If (!IsValidObject(ref) || !IsLootableOwnership(ref))
        Return false
    EndIf

    Return IsLootableRarity(ref.GetBaseObject())
EndFunction

Function TraceObject(ObjectReference ref);; Debug
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    LTMN:Quest:Methods.TraceObject(prefix, ref);; Debug
    Lootman.Log(prefix + "  Is linked to workshop: " + Lootman.IsLinkedToWorkshop(ref));; Debug
EndFunction;; Debug
