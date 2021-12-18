Scriptname LTMN:Debug native debugOnly hidden

; Get and return the identifier of the form type
string Function GetFormTypeIdentify(Form form) global native

; Converts the form ID to a hexadecimal string and returns it
string Function GetHexID(Form form) global native

; Get and return the form's identify
string Function GetIdentify(Form form) global native

; Get and return the current millisecond
string Function GetMilliseconds() global native

; Generate and return a random process ID
string Function GetRandomProcessID() global native


; Output the Lootman user log
Function Log(string msg) global
    Debug.TraceUser("Lootman", "[" + GetMilliseconds() + "ms] " + msg)
EndFunction

; Open the Lootman user log
Function OpenLog() global
    Debug.OpenUserLog("Lootman")
EndFunction

; Output the trace log of the global variables
float[] Function TraceGlobal(string prefix, float[] cache) global
    If (cache == None)
        cache = new float[0]
    EndIf

    LTMN:Debug.Log(prefix + "*** Start trace global variables ***")

    LTMN:Quest:Properties properties = LTMN:Lootman.GetProperties()
    _TraceGlobal(prefix + "  ", "IsEnabled", cache, 0, properties.IsEnabled, true)
    _TraceGlobal(prefix + "  ", "IsInSettlement", cache, 1, properties.IsInSettlement, true)
    _TraceGlobal(prefix + "  ", "IsOverweight", cache, 2, properties.IsOverweight, true)
    _TraceGlobal(prefix + "  ", "IsPipboyOpen", cache, 3, properties.IsPipboyOpen, true)
    _TraceGlobal(prefix + "  ", "AutomaticallyLinkOrUnlinkToWorkshop", cache, 4, properties.AutomaticallyLinkOrUnlinkToWorkshop, true)
    _TraceGlobal(prefix + "  ", "CarryWeight", cache, 5, properties.CarryWeight, true)
    _TraceGlobal(prefix + "  ", "DisplaySystemMessage", cache, 6, properties.DisplaySystemMessage, true)
    _TraceGlobal(prefix + "  ", "IgnoreOverweight", cache, 7, properties.IgnoreOverweight, true)
    _TraceGlobal(prefix + "  ", "LootingDisabledInSettlement", cache, 8, properties.LootingDisabledInSettlement, true)
    _TraceGlobal(prefix + "  ", "LootingRange", cache, 9, properties.LootingRange, true)
    _TraceGlobal(prefix + "  ", "LootInPlayerDirectly", cache, 10, properties.LootInPlayerDirectly, true)
    _TraceGlobal(prefix + "  ", "PlayContainerAnimation", cache, 11, properties.PlayContainerAnimation, true)
    _TraceGlobal(prefix + "  ", "PlayPickupSound", cache, 12, properties.PlayPickupSound, true)
    _TraceGlobal(prefix + "  ", "ThreadInterval", cache, 13, properties.ThreadInterval, false)
    _TraceGlobal(prefix + "  ", "ThreadAllowedWorkingTime", cache, 14, properties.ThreadAllowedWorkingTime, true)
    _TraceGlobal(prefix + "  ", "ExpirationToSkipLooting", cache, 15, properties.ExpirationToSkipLooting, true)
    _TraceGlobal(prefix + "  ", "AllowContainerUnlock", cache, 16, properties.AllowContainerUnlock, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitACTI", cache, 17, properties.ThreadLimitACTI, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitALCH", cache, 18, properties.ThreadLimitALCH, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitAMMO", cache, 19, properties.ThreadLimitAMMO, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitARMO", cache, 20, properties.ThreadLimitARMO, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitBOOK", cache, 21, properties.ThreadLimitBOOK, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitCONT", cache, 22, properties.ThreadLimitCONT, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitFLOR", cache, 23, properties.ThreadLimitFLOR, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitINGR", cache, 24, properties.ThreadLimitINGR, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitKEYM", cache, 25, properties.ThreadLimitKEYM, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitMISC", cache, 26, properties.ThreadLimitMISC, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitNPC_", cache, 27, properties.ThreadLimitNPC_, true)
    _TraceGlobal(prefix + "  ", "ThreadLimitWEAP", cache, 28, properties.ThreadLimitWEAP, true)
    _TraceGlobal(prefix + "  ", "TargetFilterContainer", cache, 29, properties.TargetFilterContainer, true)
    _TraceGlobal(prefix + "  ", "TargetFilterCorpse", cache, 30, properties.TargetFilterCorpse, true)
    _TraceGlobal(prefix + "  ", "TargetFilterObject", cache, 31, properties.TargetFilterObject, true)
    _TraceGlobal(prefix + "  ", "CategoryFilterACTI", cache, 32, properties.CategoryFilterACTI, true)
    _TraceGlobal(prefix + "  ", "CategoryFilterALCH", cache, 33, properties.CategoryFilterALCH, true)
    _TraceGlobal(prefix + "  ", "CategoryFilterAMMO", cache, 34, properties.CategoryFilterAMMO, true)
    _TraceGlobal(prefix + "  ", "CategoryFilterARMO", cache, 35, properties.CategoryFilterARMO, true)
    _TraceGlobal(prefix + "  ", "CategoryFilterBOOK", cache, 36, properties.CategoryFilterBOOK, true)
    _TraceGlobal(prefix + "  ", "CategoryFilterFLOR", cache, 37, properties.CategoryFilterFLOR, true)
    _TraceGlobal(prefix + "  ", "CategoryFilterINGR", cache, 38, properties.CategoryFilterINGR, true)
    _TraceGlobal(prefix + "  ", "CategoryFilterKEYM", cache, 39, properties.CategoryFilterKEYM, true)
    _TraceGlobal(prefix + "  ", "CategoryFilterMISC", cache, 40, properties.CategoryFilterMISC, true)
    _TraceGlobal(prefix + "  ", "CategoryFilterWEAP", cache, 41, properties.CategoryFilterWEAP, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterAlcohol", cache, 42, properties.AdvancedFilterAlcohol, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterChem", cache, 43, properties.AdvancedFilterChem, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterFood", cache, 44, properties.AdvancedFilterFood, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterNukaCola", cache, 45, properties.AdvancedFilterNukaCola, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterStimpak", cache, 46, properties.AdvancedFilterStimpak, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterSyringerAmmo", cache, 47, properties.AdvancedFilterSyringerAmmo, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterWater", cache, 48, properties.AdvancedFilterWater, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterOtherAlchemy", cache, 49, properties.AdvancedFilterOtherAlchemy, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterPerkMagazine", cache, 50, properties.AdvancedFilterPerkMagazine, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterOtherBook", cache, 51, properties.AdvancedFilterOtherBook, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterGrenade", cache, 52, properties.AdvancedFilterGrenade, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterMine", cache, 53, properties.AdvancedFilterMine, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterOtherWeapon", cache, 54, properties.AdvancedFilterOtherWeapon, true)
    _TraceGlobal(prefix + "  ", "AdvancedFilterLegendaryOnly", cache, 55, properties.AdvancedFilterLegendaryOnly, true)

    LTMN:Debug.Log(prefix + "*** Trace global variables is complete ***")

    Return cache
EndFunction

Function _TraceGlobal(string prefix, string globalName, float[] cache, int index, GlobalVariable value, bool asInt) global
    float floatValue = value.GetValue()
    If (cache.Length < index + 1)
        cache.Add(floatValue)
        If (asInt)
            Log(prefix + globalName + ": " + (floatValue as int))
        Else
            Log(prefix + globalName + ": " + floatValue)
        EndIf
    ElseIf (cache[index] != floatValue)
        cache[index] = floatValue
        If (asInt)
            Log(prefix + globalName + ": " + (floatValue as int) + " [CHANGED]")
        Else
            Log(prefix + globalName + ": " + floatValue + " [CHANGED]")
        EndIf
    EndIf
EndFunction

; Output the trace log of an object
Function TraceObject(string prefix, ObjectReference ref) global
    LTMN:Quest:Properties _properties = LTMN:Lootman.GetProperties()
    Actor _player = Game.GetPlayer()

    Log(prefix + "Object: [Name: " + ref.GetDisplayName() + ", ID: " + GetHexID(ref) + "]")
    Log(prefix + "  Position: [X: " + ref.X + ", Y: " + ref.Y + ", Z: " + ref.Z + "]")
    Log(prefix + "  Distance to player: " + ref.GetDistance(_player))
    Log(prefix + "  Is disabled: " + ref.IsDisabled())
    Log(prefix + "  Is deleted: " + ref.IsDeleted())
    Log(prefix + "  Is 3D loaded: " + ref.Is3DLoaded())
    Log(prefix + "  Is destroyed: " + ref.IsDestroyed())
    Log(prefix + "  Is activation blocked: " + ref.IsActivationBlocked())
    Log(prefix + "  Player has direct line-of-sight: " + _player.HasDirectLOS(ref))
    Log(prefix + "  Player has detection line-of-sight: " + _player.HasDetectionLOS(ref))
    Log(prefix + "  Is quest item: " + ref.IsQuestItem())
    Log(prefix + "  Is interaction blocked: " + ref.HasKeyword(_properties.BlockWorkshopInteraction))
    Log(prefix + "  Has keywords that excludes it from looting: " + ref.HasKeywordInFormList(_properties.ExcludeKeywordList))

    int i = _properties.ExcludeLocationRefList.GetSize()
    While i
        i -= 1
        Form item = _properties.ExcludeLocationRefList.GetAt(i)
        LocationRefType locRefType = (item As LocationRefType)
        If (locRefType && ref.HasLocRefType(locRefType))
            Log(prefix + "  Has location reference type that excludes it from looting: " + ref.HasLocRefType(locRefType))
            Log(prefix + "    Location reference type: [Name: " + GetIdentify(locRefType) + ", ID: " + GetHexID(locRefType) + "]")
        EndIf
    EndWhile

    Log(prefix + "  Has owner: " + ref.HasOwner())
    Log(prefix + "  Is owned by player: " + ref.IsOwnedBy(_player))

    If (ref.GetActorRefOwner() || ref.GetActorOwner())
        Actor ownerRef = ref.GetActorRefOwner()
        If (ownerRef)
            Log(prefix + "  Owner: [Name: " + GetIdentify(ownerRef) + ", ID: " + GetHexID(ownerRef) + "]")
            Log(prefix + "    Is dead: " + ownerRef.IsDead())
            Log(prefix + "    Relationship rank with the player: " + ownerRef.GetRelationshipRank(_player))
        Else
            ActorBase ownerBase = ref.GetActorOwner()
            Log(prefix + "  Owner: [Name: " + GetIdentify(ownerBase) + ", ID: " + GetHexID(ownerBase) + "]")
            Log(prefix + "    Is unique: " + ownerBase.IsUnique())
            If (ownerBase.IsUnique())
                Actor ownerActor = ownerBase.GetUniqueActor()
                Log(prefix + "    Is dead: " + ownerActor.IsDead())
                Log(prefix + "    Relationship rank with the player: " + ownerActor.GetRelationshipRank(_player))
            EndIf
        EndIf
    EndIf

    If (ref.GetFactionOwner())
        Faction factionOwner = ref.GetFactionOwner()
        If (factionOwner)
            Log(prefix + "  Faction that object belongs to: [Name: " + GetIdentify(factionOwner) + ", ID: " + GetHexID(factionOwner) + "]")
            Log(prefix + "    Relationship rank with the player: " + factionOwner.GetFactionReaction(_player))
        EndIf
    EndIf

    If (ref.GetParentCell())
        Cell parentCell = ref.GetParentCell()
        Log(prefix + "  Parent cell: [Name: " + GetIdentify(parentCell) + ", ID: " + GetHexID(parentCell) + "]")
        ActorBase owner = parentCell.GetActorOwner()
        If (owner)
            Log(prefix + "    Cell owner: [Name: " + GetIdentify(owner) + ", ID: " + GetHexID(owner) + "]")
            Log(prefix + "      Is unique: " + owner.IsUnique())
            If (owner.IsUnique())
                Actor ownerActor = owner.GetUniqueActor()
                Log(prefix + "      Is dead: " + ownerActor.IsDead())
                Log(prefix + "      Relationship rank with the player: " + ownerActor.GetRelationshipRank(_player))
            EndIf
        EndIf

        Faction factionOwner = parentCell.GetFactionOwner()
        If (factionOwner)
            Log(prefix + "    Faction that cell belongs to: [Name: " + GetIdentify(factionOwner) + ", ID: " + GetHexID(factionOwner) + "]")
            Log(prefix + "      Relationship rank with the player: " + factionOwner.GetFactionReaction(_player))
        EndIf
    EndIf

    Location loc = ref.GetCurrentLocation()
    If (loc)
        Log(prefix + "  Current location: [Name: " + GetIdentify(loc) + ", ID: " + GetHexID(loc) + "]")
        Log(prefix + "    Is settlement: " + (loc.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || loc.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement)))
    EndIf
EndFunction

; Output the trace log of the form
Function TraceForm(string prefix, Form base) global
    LTMN:Quest:Properties _properties = LTMN:Lootman.GetProperties()
    Log(prefix + "Base item: [Name: " + GetIdentify(base) + ", ID: " + GetHexID(base) + "]")
    Log(prefix + "  Form type: " + GetFormTypeIdentify(base))

    Log(prefix + "  Is featured item: " + base.HasKeyword(_properties.FeaturedItem))
    If (base.HasKeyword(_properties.FeaturedItem))
        Log(prefix + "    Looting is allowed: " + _properties.AllowedFeaturedItemList.HasForm(base))
    EndIf

    Log(prefix + "  Is unique item: " + _properties.UniqueItemList.HasForm(base))
    If (_properties.UniqueItemList.HasForm(base))
        Log(prefix + "    Looting is allowed: " + _properties.AllowedUniqueItemList.HasForm(base))
    EndIf

    Log(prefix + "  Excluded from the looting: " + _properties.ExcludeFormList.HasForm(base))
    Log(prefix + "  Has keywords that excludes it from looting: " + base.HasKeywordInFormList(_properties.ExcludeKeywordList))
EndFunction
