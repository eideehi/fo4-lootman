Scriptname LTMN:Quest:Methods extends Quest

Actor player
LTMN:Quest:Properties properties
LTMN:Quest:System system

Event OnInit()
    player = Game.GetPlayer()
    properties = Lootman.GetProperties()
    system = Lootman.GetSystem()
EndEvent

; Installation process of Lootman
Function Install()
    system.Install()
EndFunction

; Uninstallation process of Lootman
Function Uninstall()
    system.Uninstall()

    ; Items in the Lootman's possession need to be handed over to the player after the system shuts down.
    MoveInventoryItems(properties.LootmanActor, player, -1, 0)
    MoveInventoryItems(properties.LootmanWorkshop, player, -1, 0)

    properties.Stop()
    self.Stop()
EndFunction

; Open Lootman's inventory
Function OpenInventory()
    properties.LootmanWorkshop.Activate(player, true)
EndFunction

; Toggle auto-looting on/off
Function ToggleLooting()
    If (properties.IsEnabled.GetValueInt() != 1)
        properties.IsEnabled.SetValueInt(1)
        system.ShowMessage(properties.MESSAGE_LOOTMAN_ENABLED)
    Else
        properties.IsEnabled.SetValueInt(0)
        system.ShowMessage(properties.MESSAGE_LOOTMAN_DISABLED)
    EndIf
    properties.ProcessCompleteSound.Play(player)
EndFunction

; Toggle the link status between Lootman's inventory and the nearest workshop.
Function ToggleWorkshopLink()
    WorkshopScript workshop = properties.WorkshopParent.GetWorkshopFromLocation(player.GetCurrentLocation())
    If (workshop && workshop.myLocation)
        If (workshop.myLocation.IsLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan))
            workshop.myLocation.RemoveLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan)
            system.ShowMessage(properties.MESSAGE_UNLINKED_TO_WORKSHOP)
        Else
            workshop.myLocation.AddLinkedLocation(properties.LootmanLocation, properties.WorkshopCaravan)
            system.ShowMessage(properties.MESSAGE_LINKED_TO_WORKSHOP)
        EndIf
    Else
        system.ShowMessage(properties.MESSAGE_WORKSHOP_NOT_FOUND)
    EndIf
    properties.ProcessCompleteSound.Play(player)
EndFunction

; Move items from the player's inventory to Lootman's inventory
Function MoveItemsPlayerToLootman(float filter, float subFilter)
    MoveInventoryItems(player, properties.LootmanWorkshop, filter As int, subFilter As int)
    system.ShowMessage(properties.MESSAGE_PROCESS_COMPLETE)
    properties.ProcessCompleteSound.Play(player)
EndFunction

; Move items from Lootman's inventory to the player's inventory
Function MoveItemsLootmanToPlayer(float filter, float subFilter)
    MoveInventoryItems(properties.LootmanWorkshop, player, filter As int, subFilter As int)
    system.ShowMessage(properties.MESSAGE_PROCESS_COMPLETE)
    properties.ProcessCompleteSound.Play(player)
EndFunction

; Move items from Lootman's inventory to the nearest workshop
Function MoveItemsLootmanToWorkshop(float filter, float subFilter)
    WorkshopScript workshop = properties.WorkshopParent.GetWorkshopFromLocation(player.GetCurrentLocation())
    If (workshop)
        MoveInventoryItems(properties.LootmanWorkshop, workshop, filter As int, subFilter As int)
        system.ShowMessage(properties.MESSAGE_PROCESS_COMPLETE)
    Else
        system.ShowMessage(properties.MESSAGE_WORKSHOP_NOT_FOUND)
    EndIf
    properties.ProcessCompleteSound.Play(player)
EndFunction

; Scrap the items in Lootman's inventory
Function ScrapItemsFromLootman(float filter, float subFilter)
    _ScrapInventoryItems(properties.LootmanWorkshop, filter As int, subFilter As int)
    system.ShowMessage(properties.MESSAGE_PROCESS_COMPLETE)
    properties.ProcessCompleteSound.Play(player)
EndFunction

; Scrap items in the inventory (Internal function)
Function _ScrapInventoryItems(ObjectReference ref, int filter, int subFilter) global
    string prefix = ("| Utility | " + Lootman.GetRandomProcessID() + " | ");; Debug
    Lootman.Log(prefix + "*** Start scrap process ***");; Debug
    LTMN:Quest:Properties _properties = Lootman.GetProperties()
    Form[] items = Lootman.GetInventoryItemsOfFormType(ref, filter)
    Lootman.Log(prefix + "  Total items found: " + items.Length);; Debug
    int itemIndex = 1;; Debug
    int i = items.Length
    While i
        i -= 1
        Form item = items[i]
        Lootman.Log(prefix + "  [Item_" + itemIndex + "]");; Debug
        Lootman.Log(prefix + "    Name: " + Lootman.GetIdentify(item));; Debug
        If (_IsScrapableItem(item))
            int formType = Lootman.GetFormType(item)
            int itemCount = ref.GetItemCount(item)
            int perTime = 1
            If (formType == _properties.FORM_TYPE_MISC)
                If (itemCount > 65535)
                    perTime = 65535
                Else
                    perTime = itemCount
                EndIf
            EndIf
            Lootman.Log(prefix + "    Form type: " + formType);; Debug
            Lootman.Log(prefix + "    Count: " + itemCount);; Debug
            Lootman.Log(prefix + "    ** Do scrapping **");; Debug
            int loopCount = 0;; Debug
            While itemCount
                Lootman.Log(prefix + "      [Loop_" + loopCount + "]");; Debug
                Lootman.Log(prefix + "        Item count remaining: " + itemCount);; Debug
                Lootman.Log(prefix + "        Scrap limits per loop: " + perTime);; Debug
                MiscObject:MiscComponent[] components = new MiscObject:MiscComponent[0]
                ObjectReference obj = None

                If (formType == _properties.FORM_TYPE_MISC)
                    components = (item As MiscObject).GetMiscComponents()
                Else
                    obj = ref.DropObject(item, 1)
                    If (obj)
                        Lootman.Log(prefix + "        Actual item: " + obj.GetDisplayName());; Debug
                        components = Lootman.GetEquipmentScrapComponents(obj)
                    EndIf
                EndIf

                Lootman.Log(prefix + "        Total components: " + components.Length);; Debug
                int componentIndex = 1;; Debug
                int j = components.Length
                While j
                    j -= 1
                    MiscObject:MiscComponent miscComponent = components[j]
                    If (miscComponent)
                        Lootman.Log(prefix + "        [Component_" + componentIndex + "]");; Debug
                        Lootman.Log(prefix + "          Component count: " + miscComponent.count);; Debug
                        Lootman.Log(prefix + "          Scrap item: " + Lootman.GetIdentify(miscComponent.object.GetScrapItem()));; Debug
                        Lootman.Log(prefix + "          Scrap scaler: " + miscComponent.object.GetScrapScalar().GetValue());; Debug

                        int scrapCount = miscComponent.count
                        If (formType != _properties.FORM_TYPE_MISC)
                            scrapCount = (scrapCount * miscComponent.object.GetScrapScalar().GetValue()) As int
                        EndIf

                        If ((scrapCount * perTime) > 65535)
                            perTime = 65535 / miscComponent.count
                        EndIf
                        componentIndex += 1;; Debug
                    EndIf
                EndWhile

                j = components.Length
                While j
                    j -= 1
                    MiscObject:MiscComponent miscComponent = components[j]
                    If (miscComponent)
                        MiscObject scrapItem = miscComponent.object.GetScrapItem()
                        int scrapCount = miscComponent.count
                        If (formType != _properties.FORM_TYPE_MISC)
                            scrapCount = (scrapCount * miscComponent.object.GetScrapScalar().GetValue()) As int
                        EndIf

                        If (scrapItem && scrapCount)
                            int acquiredCount = scrapCount * perTime
                            _properties.TemporaryContainer.AddItem(scrapItem, acquiredCount, true)
                            _properties.TemporaryContainer.RemoveItem(scrapItem, acquiredCount, true, ref)
                            Lootman.Log(prefix + "        Acquired components: " + Lootman.GetIdentify(scrapItem) + " x" + acquiredCount);; Debug
                        EndIf
                    EndIf
                EndWhile

                If (components.Length > 0)
                    If (obj)
                        obj.Drop(true)
                        obj.Delete()
                        If (perTime > 1)
                            ref.RemoveItem(item, perTime - 1, true)
                        EndIf
                        Lootman.Log(prefix + "        Remove item: " + obj.GetDisplayName() + " x" + perTime);; Debug
                    Else
                        ref.RemoveItem(item, perTime, true)
                        Lootman.Log(prefix + "        Remove item: " + Lootman.GetIdentify(item) + " x" + perTime);; Debug
                    EndIf

                    itemCount -= perTime
                    If (itemCount < perTime)
                        perTime = itemCount
                    EndIf
                Else
                    If (obj)
                        ref.AddItem(obj, 1)
                    EndIf
                    itemCount = 0
                EndIf

                loopCount += 1;; Debug
            EndWhile

            Lootman.Log(prefix + "    ** Done scrapping **");; Debug
        Else;; Debug
            Lootman.Log(prefix + "    ** Is not scrapable **");; Debug
        EndIf
        itemIndex += 1;; Debug
    EndWhile
    Lootman.Log(prefix + "*** End scrap process ***");; Debug
EndFunction

; Verify that the item is scrapable (Internal function)
bool Function _IsScrapableItem(Form item) global
    LTMN:Quest:Properties _properties = Lootman.GetProperties()
    Return !item.HasKeyword(_properties.UnscrappableObject) && !item.HasKeyword(_properties.FeaturedItem)
EndFunction

; Move items from inventory to inventory
Function MoveInventoryItems(ObjectReference src, ObjectReference dest, int filter, int subFilter) global
    LTMN:Quest:Properties _properties = Lootman.GetProperties()

    bool moveFromPlayer = (src == Game.GetPlayer())
    Actor srcAsActor = (src As Actor)
    Form[] favorites = FavoritesManager.GetFavorites()

    bool filterIsMisc = (filter == _properties.FORM_TYPE_MISC)
    bool moveTheJunk = filterIsMisc && (subFilter == 0)
    bool moveTheMod = !moveTheJunk
    Keyword ObjectTypeLooseMod = _properties.ObjectTypeLooseMod

    Form[] items = Lootman.GetInventoryItemsOfFormType(src, filter)
    int i = items.Length
    While i
        i -= 1
        Form item = items[i]

        If (item && srcAsActor)
            If (!srcAsActor.IsDead() && srcAsActor.IsEquipped(item))
                item = None
            EndIf
        EndIf

        If (item && moveFromPlayer)
            If (_IsFavoriteItem(favorites, item) || item.GetFormID() == 0x0000000F)
                item = None
            EndIf
        EndIf

        If (item && filterIsMisc)
            bool itemIsLooseMod = item.HasKeyword(ObjectTypeLooseMod)
            If ((moveTheJunk && itemIsLooseMod) || (moveTheMod && !itemIsLooseMod))
                item = None
            EndIf
        EndIf

        If (item)
            int count = src.GetItemCount(item)
            While (count > 0)
                If (count <= 65535)
                    src.RemoveItem(item, -1, true, dest)
                    count = 0
                Else
                    src.RemoveItem(item, 65535, true, dest)
                    count -= 65535
                EndIf
            EndWhile
        EndIf
    EndWhile
EndFunction

; Verify that the form is a favorite item (Internal function)
bool Function _IsFavoriteItem(Form[] favorites, Form item) global
    int i = favorites.Length
    While i
        i -= 1
        Form favorite = favorites[i]
        If (favorite && item.GetFormID() == favorite.GetFormID())
            Return true
        EndIf
    EndWhile
    Return false
EndFunction

; Output the trace log of an object (Debug only)
Function TraceObject(string prefix, ObjectReference ref) global;; Debug
    LTMN:Quest:Properties _properties = Lootman.GetProperties();; Debug
    Actor _player = Game.GetPlayer();; Debug

    Lootman.Log(prefix + "Object: [Name: " + ref.GetDisplayName() + ", ID: " + Lootman.GetHexID(ref) + "]");; Debug
    Lootman.Log(prefix + "  Position: [X: " + ref.X + ", Y: " + ref.Y + ", Z: " + ref.Z + "]");; Debug
    Lootman.Log(prefix + "  Distance to player: " + ref.GetDistance(_player));; Debug
    Lootman.Log(prefix + "  Is disabled: " + ref.IsDisabled());; Debug
    Lootman.Log(prefix + "  Is deleted: " + ref.IsDeleted());; Debug
    Lootman.Log(prefix + "  Is 3D loaded: " + ref.Is3DLoaded());; Debug
    Lootman.Log(prefix + "  Is destroyed: " + ref.IsDestroyed());; Debug
    Lootman.Log(prefix + "  Is activation blocked: " + ref.IsActivationBlocked());; Debug
    Lootman.Log(prefix + "  Player has direct line-of-sight: " + _player.HasDirectLOS(ref));; Debug
    Lootman.Log(prefix + "  Player has detection line-of-sight: " + _player.HasDetectionLOS(ref));; Debug
    Lootman.Log(prefix + "  Is quest item: " + ref.IsQuestItem());; Debug
    Lootman.Log(prefix + "  Is interaction blocked: " + ref.HasKeyword(_properties.BlockWorkshopInteraction));; Debug
    Lootman.Log(prefix + "  Has keywords that excludes it from looting: " + ref.HasKeywordInFormList(_properties.ExcludeKeywordList));; Debug

    int i = _properties.ExcludeLocationRefList.GetSize();; Debug
    While i;; Debug
        i -= 1;; Debug
        Form item = _properties.ExcludeLocationRefList.GetAt(i);; Debug
        LocationRefType locRefType = (item As LocationRefType);; Debug
        If (locRefType && ref.HasLocRefType(locRefType));; Debug
            Lootman.Log(prefix + "  Has location reference type that excludes it from looting: " + ref.HasLocRefType(locRefType));; Debug
            Lootman.Log(prefix + "    Location reference type: [Name: " + Lootman.GetIdentify(locRefType) + ", ID: " + Lootman.GetHexID(locRefType) + "]");; Debug
        EndIf;; Debug
    EndWhile;; Debug

    Lootman.Log(prefix + "  Has owner: " + ref.HasOwner());; Debug
    Lootman.Log(prefix + "  Is owned by player: " + ref.IsOwnedBy(_player));; Debug

    If (ref.GetActorRefOwner() || ref.GetActorOwner());; Debug
        Actor ownerRef = ref.GetActorRefOwner();; Debug
        If (ownerRef);; Debug
            Lootman.Log(prefix + "  Owner: [Name: " + Lootman.GetIdentify(ownerRef) + ", ID: " + Lootman.GetHexID(ownerRef) + "]");; Debug
            Lootman.Log(prefix + "    Is dead: " + ownerRef.IsDead());; Debug
            Lootman.Log(prefix + "    Relationship rank with the player: " + ownerRef.GetRelationshipRank(_player));; Debug
        Else;; Debug
            ActorBase ownerBase = ref.GetActorOwner();; Debug
            Lootman.Log(prefix + "  Owner: [Name: " + Lootman.GetIdentify(ownerBase) + ", ID: " + Lootman.GetHexID(ownerBase) + "]");; Debug
            Lootman.Log(prefix + "    Is unique: " + ownerBase.IsUnique());; Debug
            If (ownerBase.IsUnique());; Debug
                Actor ownerActor = ownerBase.GetUniqueActor();; Debug
                Lootman.Log(prefix + "    Is dead: " + ownerActor.IsDead());; Debug
                Lootman.Log(prefix + "    Relationship rank with the player: " + ownerActor.GetRelationshipRank(_player));; Debug
            EndIf;; Debug
        EndIf;; Debug
    EndIf;; Debug

    If (ref.GetFactionOwner());; Debug
        Faction factionOwner = ref.GetFactionOwner();; Debug
        If (factionOwner);; Debug
            Lootman.Log(prefix + "  Faction that object belongs to: [Name: " + Lootman.GetIdentify(factionOwner) + ", ID: " + Lootman.GetHexID(factionOwner) + "]");; Debug
            Lootman.Log(prefix + "    Relationship rank with the player: " + factionOwner.GetFactionReaction(_player));; Debug
        EndIf;; Debug
    EndIf;; Debug

    If (ref.GetParentCell());; Debug
        Cell parentCell = ref.GetParentCell();; Debug
        Lootman.Log(prefix + "  Parent cell: [Name: " + Lootman.GetIdentify(parentCell) + ", ID: " + Lootman.GetHexID(parentCell) + "]");; Debug
        ActorBase owner = parentCell.GetActorOwner();; Debug
        If (owner);; Debug
            Lootman.Log(prefix + "    Cell owner: [Name: " + Lootman.GetIdentify(owner) + ", ID: " + Lootman.GetHexID(owner) + "]");; Debug
            Lootman.Log(prefix + "      Is unique: " + owner.IsUnique());; Debug
            If (owner.IsUnique());; Debug
                Actor ownerActor = owner.GetUniqueActor();; Debug
                Lootman.Log(prefix + "      Is dead: " + ownerActor.IsDead());; Debug
                Lootman.Log(prefix + "      Relationship rank with the player: " + ownerActor.GetRelationshipRank(_player));; Debug
            EndIf;; Debug
        EndIf;; Debug

        Faction factionOwner = parentCell.GetFactionOwner();; Debug
        If (factionOwner);; Debug
            Lootman.Log(prefix + "    Faction that cell belongs to: [Name: " + Lootman.GetIdentify(factionOwner) + ", ID: " + Lootman.GetHexID(factionOwner) + "]");; Debug
            Lootman.Log(prefix + "      Relationship rank with the player: " + factionOwner.GetFactionReaction(_player));; Debug
        EndIf;; Debug
    EndIf;; Debug

    Location loc = ref.GetCurrentLocation();; Debug
    If (loc);; Debug
        Lootman.Log(prefix + "  Current location: [Name: " + Lootman.GetIdentify(loc) + ", ID: " + Lootman.GetHexID(loc) + "]");; Debug
        Lootman.Log(prefix + "    Is settlement: " + (loc.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || loc.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement)));; Debug
    EndIf;; Debug
EndFunction;; Debug

; Output the trace log of an form (Debug only)
Function TraceForm(string prefix, Form base) global;; Debug
    LTMN:Quest:Properties _properties = Lootman.GetProperties();; Debug
    Lootman.Log(prefix + "Base item: [Name: " + Lootman.GetIdentify(base) + ", ID: " + Lootman.GetHexID(base) + "]");; Debug
    Lootman.Log(prefix + "  Form type: " + Lootman.GetFormType(base));; Debug

    Lootman.Log(prefix + "  Is featured item: " + base.HasKeyword(_properties.FeaturedItem));; Debug
    If (base.HasKeyword(_properties.FeaturedItem));; Debug
        Lootman.Log(prefix + "    Looting is allowed: " + _properties.AllowedFeaturedItemList.HasForm(base));; Debug
    EndIf;; Debug

    Lootman.Log(prefix + "  Is unique item: " + _properties.UniqueItemList.HasForm(base));; Debug
    If (_properties.UniqueItemList.HasForm(base));; Debug
        Lootman.Log(prefix + "    Looting is allowed: " + _properties.AllowedUniqueItemList.HasForm(base));; Debug
    EndIf;; Debug

    Lootman.Log(prefix + "  Excluded from the looting: " + _properties.ExcludeFormList.HasForm(base));; Debug
    Lootman.Log(prefix + "  Has keywords that excludes it from looting: " + base.HasKeywordInFormList(_properties.ExcludeKeywordList));; Debug
EndFunction;; Debug
