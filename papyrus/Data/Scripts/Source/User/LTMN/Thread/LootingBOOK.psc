Scriptname LTMN:Thread:LootingBOOK extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_BOOK
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ")
    LTMN:Debug.Log(prefix + "Loot: [Name: " + ref.GetDisplayName() + ", ID: " + LTMN:Debug.GetHexID(ref) + "]")
    properties.LootmanActor.AddItem(ref, 1)
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
    If (base.HasKeyword(properties.PerkMagazine))
        Return properties.AdvancedFilterPerkMagazine.GetValueInt() == 1
    EndIf

    If (properties.AdvancedFilterOtherBook.GetValueInt() != 1)
        Return false
    EndIf

    Return IsLootableRarity(base)
EndFunction

Function TraceObject(ObjectReference ref) debugOnly
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ")
    LTMN:Debug.TraceObject(prefix, ref)
    LTMN:Debug.Log(prefix + "  Is perk magazine: " + ref.HasKeyword(properties.PerkMagazine))
    Form base = ref.GetBaseObject()
    LTMN:Debug.TraceForm(prefix + "  ", base)
    LTMN:Debug.Log(prefix + "    Is perk magazine: " + base.HasKeyword(properties.PerkMagazine))
EndFunction
