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

; Output the trace log of an object
Function TraceObject(string prefix, ObjectReference ref) global
    LTMN:Quest:Properties _properties = Lootman.GetProperties()
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

; Output the trace log of an form (Debug only)
Function TraceForm(string prefix, Form base) global
    LTMN:Quest:Properties _properties = Lootman.GetProperties()
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
