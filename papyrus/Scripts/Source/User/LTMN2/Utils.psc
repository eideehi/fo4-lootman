Scriptname LTMN2:Utils native hidden

WorkshopScript Function GetCurrentWorkshop(ObjectReference ref) global
    If (!ref)
        Return none
    EndIf

    LTMN2:Properties properties = LTMN2:Properties.GetInstance()
    Location currentLocation = ref.GetCurrentLocation()
    If (currentLocation)
        WorkshopScript locationWorkshop = properties.WorkshopParent.GetWorkshopFromLocation(currentLocation)
        If (locationWorkshop)
            Return locationWorkshop
        EndIf
    EndIf

    int workshopId = LTMN2:LootMan.FindNearestValidWorkshopId(ref)
    If (workshopId != 0)
        Return Game.GetForm(workshopId) As WorkshopScript
    EndIf

    Return none
EndFunction

; Move all items of a given form type from inventory to inventory.
Function MoveInventoryItems(ObjectReference src, ObjectReference dest, int itemType, int subType = -1, bool silent = true) global
    LTMN2:LootMan.MoveInventoryItems(src, dest, itemType, subType, silent)
EndFunction

; Moves items of the specified form from inventory to inventory in the specified quantity. If the number of items is less than the specified quantity, everything in your possession is moved.
Function MoveInventoryItem(ObjectReference src, ObjectReference dest, Form item, int count = -1, bool silent = true) global
    LTMN2:LootMan.MoveInventoryItem(src, dest, item, count, silent)
EndFunction

; Scrap all items of the specified form type present in the inventory.
Function ScrapInventoryItems(ObjectReference ref, int itemType) global
    LTMN2:LootMan.ScrapInventoryItems(ref, ref, itemType)
EndFunction

; Scrap all items of the specified form type and return flattened component pairs: [componentFormId, count, ...].
int[] Function ScrapInventoryItemsWithResults(ObjectReference ref, int itemType) global
    Return LTMN2:LootMan.ScrapInventoryItemsWithResults(ref, ref, itemType)
EndFunction

bool Function IsLootingSafe() global
    Return LTMN2:LootMan.IsLootingSafe()
EndFunction
