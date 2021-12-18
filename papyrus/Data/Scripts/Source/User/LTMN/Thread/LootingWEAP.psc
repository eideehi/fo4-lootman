Scriptname LTMN:Thread:LootingWEAP extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_WEAP
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    LTMN:Debug.Log(prefix + "Loot: [Name: " + ref.GetDisplayName() + ", ID: " + LTMN:Debug.GetHexID(ref) + "]");; Debug
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

    If (properties.AdvancedFilterLegendaryOnly.GetValueInt() == 1 && !LTMN:Lootman.IsLegendaryItem(ref))
        Return false
    EndIf

    Return IsLootableWeaponItem(base)
EndFunction

Function TraceObject(ObjectReference ref);; Debug
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    LTMN:Debug.TraceObject(prefix, ref);; Debug
    LTMN:Debug.Log(prefix + "  Is legendary item: " + LTMN:Lootman.IsLegendaryItem(ref));; Debug
    Form base = ref.GetBaseObject();; Debug
    LTMN:Debug.TraceForm(prefix + "  ", base);; Debug
    LTMN:Debug.Log(prefix + "    Is grenade: " + base.HasKeywordInFormList(properties.WeaponTypeGrenadeKeywordList));; Debug
    LTMN:Debug.Log(prefix + "    Is mine: " + base.HasKeywordInFormList(properties.WeaponTypeMineKeywordList));; Debug
EndFunction;; Debug
