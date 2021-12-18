Scriptname LTMN:Thread:LootingALCH extends LTMN:Thread:Looting hidden

int Function GetFindObjectFormType()
    Return properties.FORM_TYPE_ALCH
EndFunction

Function LootObject(ObjectReference ref)
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ")
    LTMN:Debug.Log(prefix + "Loot: [Name: " + ref.GetDisplayName() + ", ID: " + LTMN:Debug.GetHexID(ref) + "]")
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

Function TraceObject(ObjectReference ref) debugOnly
    string prefix = ("| Looting @ " + GetThreadID() + " | " + GetProcessID() + " |     ")
    LTMN:Debug.TraceObject(prefix, ref)
    Form base = ref.GetBaseObject()
    LTMN:Debug.TraceForm(prefix + "  ", base)
    LTMN:Debug.Log(prefix + "    Is alcohol: " + base.HasKeyword(properties.ObjectTypeAlcohol))
    LTMN:Debug.Log(prefix + "    Is chemical: " + base.HasKeyword(properties.ObjectTypeChem))
    LTMN:Debug.Log(prefix + "    Is food: " + base.HasKeyword(properties.ObjectTypeFood))
    LTMN:Debug.Log(prefix + "    Is Nuka-Cola: " + base.HasKeyword(properties.ObjectTypeNukaCola))
    LTMN:Debug.Log(prefix + "    Is stimpak: " + base.HasKeyword(properties.ObjectTypeStimpak))
    LTMN:Debug.Log(prefix + "    Is syringer ammo:" + base.HasKeyword(properties.ObjectTypeSyringerAmmo))
    LTMN:Debug.Log(prefix + "    Is water: " + base.HasKeyword(properties.ObjectTypeWater))
EndFunction
