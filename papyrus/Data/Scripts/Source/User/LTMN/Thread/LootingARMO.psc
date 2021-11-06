Scriptname LTMN:Thread:LootingARMO extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_ARMO
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    Lootman.Log(prefix + "Loot: [Name: " + ref.GetDisplayName() + ", ID: " + Lootman.GetHexID(ref) + "]");; Debug
    GetLootingActor().AddItem(ref, 1)
    If (properties.PlayPickupSound.GetValueInt() == 1 && properties.LootInPlayerDirectly.GetValueInt() != 1)
        ref.Activate(properties.ActivatorActor, true)
    EndIf
EndFunction

bool Function IsLootingTarget(ObjectReference ref)
    If (!ref.Is3DLoaded())
        Return false
    EndIf

    If (!IsValidObject(ref) || !IsLootableOwnership(ref))
        Return false
    EndIf

    Form base = ref.GetBaseObject()
    If (!IsLootableRarity(base))
        Return false
    EndIf

    Return properties.AdvancedFilterLegendaryOnly.GetValueInt() != 1 || Lootman.IsLegendaryItem(ref)
EndFunction

Function TraceObject(ObjectReference ref);; Debug
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    LTMN:Quest:Methods.TraceObject(prefix, ref);; Debug
    Lootman.Log(prefix + "  Is legendary item: " + Lootman.IsLegendaryItem(ref));; Debug
EndFunction;; Debug
