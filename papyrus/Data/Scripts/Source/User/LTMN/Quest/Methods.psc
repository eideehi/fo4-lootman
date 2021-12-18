Scriptname LTMN:Quest:Methods extends Quest

Actor player
LTMN:Quest:Properties properties
LTMN:Quest:System system

Event OnInit()
    player = Game.GetPlayer()
    properties = LTMN:Lootman.GetProperties()
    system = LTMN:Lootman.GetSystem()
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
    string prefix = ("| Utility | " + LTMN:Debug.GetRandomProcessID() + " | ")
    LTMN:Debug.Log(prefix + "*** Start scrap process ***")
    LTMN:Quest:Properties _properties = LTMN:Lootman.GetProperties()
    Form[] items = LTMN:Lootman.GetInventoryItemsOfFormType(ref, filter)
    LTMN:Debug.Log(prefix + "  Total items found: " + items.Length)
    int itemIndex = 1
    int i = items.Length
    While i
        i -= 1
        Form item = items[i]
        LTMN:Debug.Log(prefix + "  [Item_" + itemIndex + "]")
        LTMN:Debug.Log(prefix + "    Name: " + LTMN:Debug.GetIdentify(item))
        If (_IsScrapableItem(item))
            int formType = LTMN:Lootman.GetFormType(item)
            int itemCount = ref.GetItemCount(item)
            int perTime = 1
            If (formType == _properties.FORM_TYPE_MISC)
                If (itemCount > 65535)
                    perTime = 65535
                Else
                    perTime = itemCount
                EndIf
            EndIf
            LTMN:Debug.Log(prefix + "    Form type: " + LTMN:Debug.GetFormTypeIdentify(item))
            LTMN:Debug.Log(prefix + "    Count: " + itemCount)
            LTMN:Debug.Log(prefix + "    ** Do scrapping **")
            int loopCount = 0
            While itemCount
                LTMN:Debug.Log(prefix + "      [Loop_" + loopCount + "]")
                LTMN:Debug.Log(prefix + "        Item count remaining: " + itemCount)
                LTMN:Debug.Log(prefix + "        Scrap limits per loop: " + perTime)
                MiscObject:MiscComponent[] components = new MiscObject:MiscComponent[0]
                ObjectReference obj = None

                If (formType == _properties.FORM_TYPE_MISC)
                    components = (item As MiscObject).GetMiscComponents()
                Else
                    obj = ref.DropObject(item, 1)
                    If (obj)
                        LTMN:Debug.Log(prefix + "        Actual item: " + obj.GetDisplayName())
                        components = LTMN:Lootman.GetEquipmentScrapComponents(obj)
                    EndIf
                EndIf

                LTMN:Debug.Log(prefix + "        Total components: " + components.Length)
                int componentIndex = 1
                int j = components.Length
                While j
                    j -= 1
                    MiscObject:MiscComponent miscComponent = components[j]
                    If (miscComponent)
                        LTMN:Debug.Log(prefix + "        [Component_" + componentIndex + "]")
                        LTMN:Debug.Log(prefix + "          Component count: " + miscComponent.count)
                        LTMN:Debug.Log(prefix + "          Scrap item: " + LTMN:Debug.GetIdentify(miscComponent.object.GetScrapItem()))
                        LTMN:Debug.Log(prefix + "          Scrap scaler: " + miscComponent.object.GetScrapScalar().GetValue())

                        int scrapCount = miscComponent.count
                        If (formType != _properties.FORM_TYPE_MISC)
                            scrapCount = (scrapCount * miscComponent.object.GetScrapScalar().GetValue()) As int
                        EndIf

                        If ((scrapCount * perTime) > 65535)
                            perTime = 65535 / miscComponent.count
                        EndIf
                        componentIndex += 1
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
                            LTMN:Debug.Log(prefix + "        Acquired components: " + LTMN:Debug.GetIdentify(scrapItem) + " x" + acquiredCount)
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
                        LTMN:Debug.Log(prefix + "        Remove item: " + obj.GetDisplayName() + " x" + perTime)
                    Else
                        ref.RemoveItem(item, perTime, true)
                        LTMN:Debug.Log(prefix + "        Remove item: " + LTMN:Debug.GetIdentify(item) + " x" + perTime)
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

                loopCount += 1
            EndWhile

            LTMN:Debug.Log(prefix + "    ** Done scrapping **")
        Else
            LTMN:Debug.Log(prefix + "    ** Is not scrapable **")
        EndIf
        itemIndex += 1
    EndWhile
    LTMN:Debug.Log(prefix + "*** End scrap process ***")
EndFunction

; Verify that the item is scrapable (Internal function)
bool Function _IsScrapableItem(Form item) global
    LTMN:Quest:Properties _properties = LTMN:Lootman.GetProperties()
    Return !item.HasKeyword(_properties.UnscrappableObject) && !item.HasKeyword(_properties.FeaturedItem)
EndFunction

; Move items from inventory to inventory
Function MoveInventoryItems(ObjectReference src, ObjectReference dest, int filter, int subFilter) global
    LTMN:Quest:Properties _properties = LTMN:Lootman.GetProperties()

    bool moveFromPlayer = (src == Game.GetPlayer())
    Actor srcAsActor = (src As Actor)
    Form[] favorites = FavoritesManager.GetFavorites()

    bool filterIsMisc = (filter == _properties.FORM_TYPE_MISC)
    bool moveTheJunk = filterIsMisc && (subFilter == 0)
    bool moveTheMod = !moveTheJunk
    Keyword ObjectTypeLooseMod = _properties.ObjectTypeLooseMod

    Form[] items = LTMN:Lootman.GetInventoryItemsOfFormType(src, filter)
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
