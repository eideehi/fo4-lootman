Scriptname LTMN2:LootMan native hidden

; Find nearby references of the specified form type. Object references get with this function must be released with the ReleaseObject function after use.
ObjectReference[] Function FindNearbyReferencesWithFormType(ObjectReference ref, int formType) global native

; Find nearby reference IDs of the specified form type. Returned IDs remain locked in native code until released.
int[] Function FindNearbyReferenceIdsWithFormType(ObjectReference ref, int formType) global native

; Get components of specified equipment.
MiscObject:MiscComponent[] Function GetEquipmentComponents(ObjectReference inventoryItem) global native

; Get form's type.
int Function GetFormType(Form form) global native

; Get items of the specified item type in the inventory.
Form[] Function GetInventoryItemsWithItemType(ObjectReference inventoryOwner, int itemType) global native

; Get lootable items of the specified item type in the inventory.
Form[] Function GetLootableItems(ObjectReference inventoryOwner, int itemType) global native

; Transfer lootable items of the specified item type from one inventory to another.
int Function TransferLootableInventoryItems(ObjectReference src, ObjectReference dest, int itemType) global native

; Transfer inventory items of the specified item type from one inventory to another.
int Function TransferInventoryItems(ObjectReference src, ObjectReference dest, int itemType, int subType, Keyword looseModKeyword, bool suppressPlayerMessages) global native

; Loot nearby references of the specified form type in native code.
int Function LootNearbyReferences(ObjectReference player, ObjectReference dest, ObjectReference activator, ObjectReference workshop, int formType, int itemType, bool playPickupSound, bool playContainerAnimation, bool unlockLockedContainer, Form bobbyPin, Perk locksmith01, Perk locksmith02, Perk locksmith03, Perk locksmith04) global native

; Get scrappable items of the specified item type in the inventory.
Form[] Function GetScrappableItems(ObjectReference inventoryOwner, int itemType) global native

; Verify that the item's form type matches the specified value.
bool Function IsFormTypeEquals(Form form, int formType) global native

; Notify the LootMan plugin of property updates.
Function OnUpdateLootManProperty(string propertyName) global native

; Plays object pickup sound effects.
Function PlayPickUpSound(ObjectReference player, ObjectReference obj) global native

; Applies native cleanup after a successful world pickup.
Function FinalizeWorldPickup(ObjectReference ref) global native

; Requests the LootMan plugin to release object references get with FindNearbyReferencesWithFormType.
Function ReleaseObject(int objId) global native
