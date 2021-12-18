Scriptname LTMN:Thread:LootingALCH extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_ALCH
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

    Return IsLootableAlchemyItem(base)
EndFunction

Function TraceObject(ObjectReference ref);; Debug
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ");; Debug
    LTMN:Debug.TraceObject(prefix, ref);; Debug
    Form base = ref.GetBaseObject();; Debug
    LTMN:Debug.TraceForm(prefix + "  ", base);; Debug
    LTMN:Debug.Log(prefix + "    Is alcohol: " + base.HasKeyword(properties.ObjectTypeAlcohol));; Debug
    LTMN:Debug.Log(prefix + "    Is chemical: " + base.HasKeyword(properties.ObjectTypeChem));; Debug
    LTMN:Debug.Log(prefix + "    Is food: " + base.HasKeyword(properties.ObjectTypeFood));; Debug
    LTMN:Debug.Log(prefix + "    Is Nuka-Cola: " + base.HasKeyword(properties.ObjectTypeNukaCola));; Debug
    LTMN:Debug.Log(prefix + "    Is stimpak: " + base.HasKeyword(properties.ObjectTypeStimpak));; Debug
    LTMN:Debug.Log(prefix + "    Is syringer ammo:" + base.HasKeyword(properties.ObjectTypeSyringerAmmo));; Debug
    LTMN:Debug.Log(prefix + "    Is water: " + base.HasKeyword(properties.ObjectTypeWater));; Debug
EndFunction;; Debug
