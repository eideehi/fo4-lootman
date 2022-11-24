Scriptname LTMN2:Utils native hidden

; Move all items of a given form type from inventory to inventory.
Function MoveInventoryItems(ObjectReference src, ObjectReference dest, int itemType, int subType = -1, bool silent = true) global
    LTMN2:Properties properties = LTMN2:Properties.Getinstance()

    string prefix = "| MoveInventoryItems | "
    LTMN2:Debug.Log(prefix + "[ Start moving items ]")
    LTMN2:Debug.Log(prefix + "  From: " + src.GetDisplayName())
    LTMN2:Debug.Log(prefix + "  To: " + dest.GetDisplayName())
    LTMN2:Debug.Log(prefix + "  Target item type: " + LTMN2:Debug.GetItemTypeIdentifier(itemType))
    LTMN2:Debug.Log(prefix + "  Target subtype: " + subType)

    bool allowMiscType = Math.LogicalAnd(itemType, properties.ITEM_TYPE_MISC) != 0

    Form[] items = LTMN2:LootMan.GetInventoryItemsWithItemType(src, itemType)
    int i = items.Length
    While i
        i -= 1
        Form item = items[i]

        If (item && allowMiscType && subType != -1 && LTMN2:LootMan.IsFormTypeEquals(item, properties.FORM_TYPE_MISC))
            bool isLooseModItem = item.HasKeyword(properties.ObjectTypeLooseMod)
            If ((subType == 0 && isLooseModItem) || (subType == 1 && !isLooseModItem))
                item = none
            EndIf
        EndIf

        If (item)
            MoveInventoryItem(src, dest, item, -1, silent)
        EndIf
    EndWhile
EndFunction

; Moves items of the specified form from inventory to inventory in the specified quantity. If the number of items is less than the specified quantity, everything in your possession is moved.
Function MoveInventoryItem(ObjectReference src, ObjectReference dest, Form item, int count = -1, bool silent = true) global
    int _count = count
    If (_count < 0)
        _count = src.GetItemCount(item)
    EndIf

    LTMN2:Debug.Log("| MoveInventoryItem | Move the item '" + LTMN2:Debug.GetName(item) + " x" + _count + "' from " + src.GetDisplayName() + " to " + dest.GetDisplayName())

    While (_count > 0)
        If (_count <= 65535)
            src.RemoveItem(item, -1, silent, dest)
            _count = 0
        Else
            src.RemoveItem(item, 65535, silent, dest)
            _count -= 65535
        EndIf
    EndWhile
EndFunction

; Scrap all items of the specified form type present in the inventory.
Function ScrapInventoryItems(ObjectReference ref, int itemType) global
    string prefix = ("| ScrapInventoryItems | " + LTMN2:Debug.GetRandomProcessId() + " | ")
    LTMN2:Debug.Log(prefix + "[ Start scrapping inventory items ]")

    LTMN2:Properties properties = LTMN2:Properties.GetInstance()
    Form[] items = LTMN2:LootMan.GetScrappableItems(ref, itemType)

    LTMN2:Debug.Log(prefix + "  Inventory owner: " + ref.GetDisplayName())
    LTMN2:Debug.Log(prefix + "  Target item type: " + LTMN2:Debug.GetItemTypeIdentifier(itemType))
    LTMN2:Debug.Log(prefix + "  Total number of items found: " + items.Length)

    int itemIndex = 1
    int i = items.Length
    While i
        i -= 1
        Form item = items[i]

        LTMN2:Debug.Log(prefix + "  [ Item " + itemIndex + " ]")
        LTMN2:Debug.Log(prefix + "    Name: " + LTMN2:Debug.GetName(item))

        bool isMiscItem = LTMN2:LootMan.IsFormTypeEquals(item, properties.FORM_TYPE_MISC)
        int itemCount = ref.GetItemCount(item)
        int scrapPerTime = 1
        If (isMiscItem)
            If (itemCount > 65535)
                scrapPerTime = 65535
            Else
                scrapPerTime = itemCount
            EndIf
        EndIf

        LTMN2:Debug.Log(prefix + "    Form type: " + LTMN2:Debug.GetFormTypeIdentifier(item))
        LTMN2:Debug.Log(prefix + "    Count: " + itemCount)
        LTMN2:Debug.Log(prefix + "    [ Scrap " + scrapPerTime + " pieces each ]")

        int loopCount = 0
        While itemCount
            LTMN2:Debug.Log(prefix + "      [ Scrap " + loopCount + " ]")
            LTMN2:Debug.Log(prefix + "        Remaining item count: " + itemCount)
            LTMN2:Debug.Log(prefix + "        Scrap limits per time: " + scrapPerTime)
            MiscObject:MiscComponent[] components = new MiscObject:MiscComponent[0]
            ObjectReference obj = none

            If (isMiscItem)
                components = (item As MiscObject).GetMiscComponents()
            Else
                ref.RemoveItem(item, i, true, properties.TemporaryContainerRef)
                obj = properties.TemporaryContainerRef.DropObject(item, 1)
                If (obj)
                    LTMN2:Debug.Log(prefix + "        Actual item: " + obj.GetDisplayName())
                    If (!obj.IsQuestItem())
                        components = LTMN2:LootMan.GetEquipmentComponents(obj)
                    Else
                        LTMN2:Debug.Log(prefix + "          [ Skip this as it is a quest item ]")
                    EndIf
                EndIf
            EndIf

            LTMN2:Debug.Log(prefix + "        Total components: " + components.Length)
            int componentIndex = 1
            int j = components.Length
            While j
                j -= 1
                MiscObject:MiscComponent miscComponent = components[j]
                If (miscComponent)
                    LTMN2:Debug.Log(prefix + "        [Component " + componentIndex + "]")
                    LTMN2:Debug.Log(prefix + "          Component count: " + miscComponent.count)
                    LTMN2:Debug.Log(prefix + "          Scrap item: " + LTMN2:Debug.GetName(miscComponent.object.GetScrapItem()))
                    LTMN2:Debug.Log(prefix + "          Scrap scaler: " + miscComponent.object.GetScrapScalar().GetValue())

                    int scrapCount = miscComponent.count
                    If (!isMiscItem)
                        scrapCount = (scrapCount * miscComponent.object.GetScrapScalar().GetValue()) As int
                    EndIf

                    If ((scrapCount * scrapPerTime) > 65535)
                        scrapPerTime = 65535 / miscComponent.count
                    EndIf
                    componentIndex += 1
                EndIf
            EndWhile

            bool scrapped = false
            j = components.Length
            While j
                j -= 1
                MiscObject:MiscComponent miscComponent = components[j]
                If (miscComponent)
                    MiscObject scrapItem = miscComponent.object.GetScrapItem()
                    int scrapCount = miscComponent.count
                    If (!isMiscItem)
                        scrapCount = (scrapCount * miscComponent.object.GetScrapScalar().GetValue()) As int
                    EndIf

                    If (scrapItem && scrapCount)
                        scrapped = true
                        int acquiredCount = scrapCount * scrapPerTime
                        properties.TemporaryContainerRef.AddItem(scrapItem, acquiredCount, true)
                        properties.TemporaryContainerRef.RemoveItem(scrapItem, acquiredCount, true, ref)
                        LTMN2:Debug.Log(prefix + "        Acquired components: " + LTMN2:Debug.GetName(scrapItem) + " x" + acquiredCount)
                    EndIf
                EndIf
            EndWhile

            If (scrapped)
                If (obj)
                    obj.Drop(true)
                    obj.Delete()
                    If (scrapPerTime > 1)
                        ref.RemoveItem(item, scrapPerTime - 1, true)
                    EndIf
                    LTMN2:Debug.Log(prefix + "        Remove: " + obj.GetDisplayName() + " x" + scrapPerTime)
                Else
                    ref.RemoveItem(item, scrapPerTime, true)
                    LTMN2:Debug.Log(prefix + "        Remove: " + LTMN2:Debug.GetName(item) + " x" + scrapPerTime)
                EndIf

                itemCount -= scrapPerTime
                If (itemCount < scrapPerTime)
                    scrapPerTime = itemCount
                EndIf
            Else
                If (obj)
                    ref.AddItem(obj, 1, true)
                EndIf
                itemCount = 0
            EndIf

            loopCount += 1
        EndWhile

        itemIndex += 1
    EndWhile
EndFunction
