Scriptname LTMN2:LootMan native hidden

; Find nearby references of the specified form type. Object references get with this function must be released with the ReleaseObject function after use.
ObjectReference[] Function FindNearbyReferencesWithFormType(ObjectReference ref, int formType) global native

; Find nearby reference IDs of the specified form type. Returned IDs remain locked in native code until released.
int[] Function FindNearbyReferenceIdsWithFormType(ObjectReference ref, int formType) global native

; Find the current valid workshop through the game workshop system.
int Function FindNearestValidWorkshopId(ObjectReference ref) global native

; Get components of specified equipment.
MiscObject:MiscComponent[] Function GetEquipmentComponents(ObjectReference inventoryItem) global native

; Get form's type.
int Function GetFormType(Form form) global native

; Output a diagnostic message to the native LootMan log.
Function Log(string msg) global native

; Get the native LootMan log level. Values match trace=0 through off=6.
int Function GetLogLevel() global native

; Set the native LootMan log level. Values match trace=0 through off=6.
Function SetLogLevel(int logLevel) global native

; Get the form type identifier.
string Function GetFormTypeIdentifier(Form form) global native

; Convert the form id to a hexadecimal string.
string Function GetHexID(Form form) global native

; Get the simple name of the Form.
string Function GetName(Form form) global native

; Output raw inventory diagnostics to the native LootMan log.
Function LogInventoryDiagnostics(ObjectReference inventoryOwner, string prefix) global native

; Output native workshop supply-line diagnostics to the native LootMan log.
Function LogWorkshopSupplyDiagnostics(ObjectReference targetWorkshop, ObjectReference lootManWorkshop, string prefix) global native

; Remember a workshop link for the native shared-workshop-container hook.
Function RememberWorkshopSupplyLink(Form targetLocation, ObjectReference lootManWorkshop, string prefix) global native

; Remove a remembered workshop link from the native shared-workshop-container hook.
Function ForgetWorkshopSupplyLink(Form targetLocation, string prefix) global native

; Get items of the specified item type in the inventory.
Form[] Function GetInventoryItemsWithItemType(ObjectReference inventoryOwner, int itemType) global native

; Get lootable items of the specified item type in the inventory.
Form[] Function GetLootableItems(ObjectReference inventoryOwner, int itemType) global native

; Transfer lootable items of the specified item type from one inventory to another.
int Function TransferLootableInventoryItems(ObjectReference src, ObjectReference dest, int itemType) global native

; Transfer inventory items of the specified item type from one inventory to another.
int Function TransferInventoryItems(ObjectReference src, ObjectReference dest, int itemType, int subType, Keyword looseModKeyword, bool suppressPlayerMessages) global native

; Gating helpers for the looting pipeline.
bool Function IsLootingSafe() global native

; Move inventory items for utility actions.
Function MoveInventoryItem(ObjectReference src, ObjectReference dest, Form item, int count = -1, bool silent = true) global native
Function MoveInventoryItems(ObjectReference src, ObjectReference dest, int itemType, int subType = -1, bool silent = true) global native

; Loot nearby references of the specified form type in native code.
int Function LootNearbyReferences(ObjectReference player, ObjectReference dest, ObjectReference activator, ObjectReference workshop, int formType, int itemType, bool playPickupSound, bool playContainerAnimation, bool unlockLockedContainer, Form bobbyPin, Perk locksmith01, Perk locksmith02, Perk locksmith03, Perk locksmith04) global native

; Loot all enabled nearby references in one native pass. The result is [processed objects, successful objects, moved inventory stacks, hit object limit, hit time budget, candidate objects, ACTI, ALCH, AMMO, ARMO, BOOK, CONT, FLOR, INGR, KEYM, MISC, NPC_, WEAP].
int[] Function LootNearbyEnabledReferences(ObjectReference player, ObjectReference dest, ObjectReference activator, ObjectReference workshop, int enabledFormTypeMask, int itemType, bool playPickupSound, bool playContainerAnimation, bool unlockLockedContainer, Form bobbyPin, Perk locksmith01, Perk locksmith02, Perk locksmith03, Perk locksmith04) global native

; Get scrappable items of the specified item type in the inventory.
Form[] Function GetScrappableItems(ObjectReference inventoryOwner, int itemType) global native

; Scrap helpers.
Function ScrapInventoryItems(ObjectReference inventoryOwner, ObjectReference componentReceiver, int itemType) global native
int[] Function ScrapInventoryItemsWithResults(ObjectReference inventoryOwner, ObjectReference componentReceiver, int itemType) global native

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
