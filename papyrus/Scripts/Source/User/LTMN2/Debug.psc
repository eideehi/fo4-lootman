Scriptname LTMN2:Debug native debugOnly hidden

; Get the form type identifier.
string Function GetFormTypeIdentifier(Form form) global native

; Convert the form id to a hexadecimal string.
string Function GetHexId(Form form) global native

; Get the item type identifier.
string Function GetItemTypeIdentifier(int itemType) global native

; Get the simple name of the Form.
string Function GetName(Form form) global native

; Get the current millisecond.
string Function GetMilliseconds() global native

; Generate a random 10-digit hexadecimal string.
string Function GetRandomProcessId() global native


; Output the LootMan user log.
Function Log(string msg) global
    Debug.TraceUser("LootMan", "[" + GetMilliseconds() + "ms] " + msg)
EndFunction

; Open the LootMan user log.
Function OpenLog() global
    Debug.OpenUserLog("LootMan")
EndFunction

; Output the trace log of an object.
Function TraceObject(string prefix, ObjectReference ref) global
    LTMN2:Properties _properties = LTMN2:Properties.GetInstance()
    Actor _player = Game.GetPlayer()

    Log(prefix + "Object: [ Name: \"" + ref.GetDisplayName() + "\", Id: " + GetHexId(ref) + " ]")
    Log(prefix + "  Is near player: " + ref.IsNearPlayer())
    Log(prefix + "  Position: [ X: " + ref.X + ", Y: " + ref.Y + ", Z: " + ref.Z + " ]")
    Log(prefix + "  Distance to player: " + (ref.GetDistance(_player) / 100) + "m")
    Log(prefix + "  Is disabled: " + ref.IsDisabled())
    Log(prefix + "  Is deleted: " + ref.IsDeleted())
    Log(prefix + "  Is 3D loaded: " + ref.Is3DLoaded())
    Log(prefix + "  Is destroyed: " + ref.IsDestroyed())
    Log(prefix + "  Is activation blocked: " + ref.IsActivationBlocked())
    Log(prefix + "  Player has direct line-of-sight: " + _player.HasDirectLOS(ref))
    Log(prefix + "  Player has detection line-of-sight: " + _player.HasDetectionLOS(ref))
    Log(prefix + "  Is quest item: " + ref.IsQuestItem())
    Log(prefix + "  Has owner: " + ref.HasOwner())
    Log(prefix + "  Is owned by player: " + ref.IsOwnedBy(_player))

    If (ref.GetActorRefOwner() || ref.GetActorOwner())
        Actor ownerRef = ref.GetActorRefOwner()
        If (ownerRef)
            Log(prefix + "  Owner: [ Name: \"" + GetName(ownerRef) + "\", Id: " + GetHexId(ownerRef) + " ]")
            Log(prefix + "    Is dead: " + ownerRef.IsDead())
            Log(prefix + "    Relationship rank with the player: " + ownerRef.GetRelationshipRank(_player))
        Else
            ActorBase ownerBase = ref.GetActorOwner()
            Log(prefix + "  Owner: [ Name: \"" + GetName(ownerBase) + "\", Id: " + GetHexId(ownerBase) + " ]")
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
            Log(prefix + "  Faction that object belongs to: [ Name: \"" + GetName(factionOwner) + "\", Id: " + GetHexId(factionOwner) + " ]")
            Log(prefix + "    Relationship rank with the player: " + factionOwner.GetFactionReaction(_player))
        EndIf
    EndIf

    If (ref.GetParentCell())
        Cell parentCell = ref.GetParentCell()
        Log(prefix + "  Parent cell: [ Name: \"" + GetName(parentCell) + "\", Id: " + GetHexId(parentCell) + " ]")
        Log(prefix + "    Is attached: " + parentCell.IsAttached())
        ActorBase owner = parentCell.GetActorOwner()
        If (owner)
            Log(prefix + "    Cell owner: [ Name: \"" + GetName(owner) + "\", Id: " + GetHexId(owner) + " ]")
            Log(prefix + "      Is unique: " + owner.IsUnique())
            If (owner.IsUnique())
                Actor ownerActor = owner.GetUniqueActor()
                Log(prefix + "      Is dead: " + ownerActor.IsDead())
                Log(prefix + "      Relationship rank with the player: " + ownerActor.GetRelationshipRank(_player))
            EndIf
        EndIf

        Faction factionOwner = parentCell.GetFactionOwner()
        If (factionOwner)
            Log(prefix + "    Faction that cell belongs to: [ Name: \"" + GetName(factionOwner) + "\", Id: " + GetHexId(factionOwner) + " ]")
            Log(prefix + "      Relationship rank with the player: " + factionOwner.GetFactionReaction(_player))
        EndIf
    EndIf

    Location loc = ref.GetCurrentLocation()
    If (loc)
        Log(prefix + "  Current location: [ Name: \"" + GetName(loc) + "\", Id: " + GetHexId(loc) + " ]")
        Log(prefix + "    Is loaded: " + loc.IsLoaded())
        Log(prefix + "    Is settlement: " + (loc.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || loc.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement)))
    EndIf
EndFunction

; Output the trace log of the form.
Function TraceForm(string prefix, Form base) global
    LTMN2:Properties _properties = LTMN2:Properties.GetInstance()
    Log(prefix + "Base item: [ Name: \"" + GetName(base) + "\", Id: " + GetHexId(base) + " ]")
    Log(prefix + "  Form type: " + GetFormTypeIdentifier(base))
EndFunction
