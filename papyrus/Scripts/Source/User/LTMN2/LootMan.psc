Scriptname LTMN2:LootMan native hidden

; Return the game workshop id nearest ref.
int Function FindNearestValidWorkshopId(ObjectReference ref) global native

; Write a structured Papyrus event to the native LootMan log.
Function LogEvent(string component, string eventName, string fields = "", int logLevel = 2) global native

; Queue a localized LootMan system HUD notification in native code.
Function ShowSystemMessage(int messageId) global native

; Queue a localized LootMan system HUD notification with a display name token.
Function ShowSystemMessageWithName(int messageId, Form nameSource) global native

; Get the native LootMan log level. Values match trace=0 through off=6.
int Function GetLogLevel() global native

; Set the native LootMan log level. Values match trace=0 through off=6.
Function SetLogLevel(int logLevel) global native

; Format a form id as uppercase hex, or None for missing forms.
string Function GetHexID(Form form) global native

; Register a runtime supply-link mapping for the native workshop hook.
Function RememberWorkshopSupplyLink(Form targetLocation, ObjectReference lootManWorkshop, string prefix) global native

; Remove a runtime supply-link mapping from the native workshop hook.
Function ForgetWorkshopSupplyLink(Form targetLocation, string prefix) global native

; Transfer lootable items of the specified item type from one inventory to another.
int Function TransferLootableInventoryItems(ObjectReference src, ObjectReference dest, int itemType) global native

; Transfer inventory items of the specified item type from one inventory to another.
int Function TransferInventoryItems(ObjectReference src, ObjectReference dest, int itemType, int subType, Keyword looseModKeyword, bool suppressPlayerMessages) global native

; Return whether current runtime state allows looting.
bool Function IsLootingSafe() global native

; Move inventory items for MCM utility actions.
Function MoveInventoryItem(ObjectReference src, ObjectReference dest, Form item, int count = -1, bool silent = true) global native
Function MoveInventoryItems(ObjectReference src, ObjectReference dest, int itemType, int subType = -1, bool silent = true) global native

; Loot nearby references of the specified form type in native code.
int Function LootNearbyReferences(ObjectReference player, ObjectReference dest, ObjectReference activator, ObjectReference workshop, int formType, int itemType, bool playPickupSound, bool playContainerAnimation, bool unlockLockedContainer, Form bobbyPin, Perk locksmith01, Perk locksmith02, Perk locksmith03, Perk locksmith04) global native

; Loot all enabled nearby references in one native pass.
; Result layout: [processed, successful, moved stacks, hit object limit, hit time budget, candidates, ACTI, ALCH, AMMO, ARMO, BOOK, CONT, FLOR, INGR, KEYM, MISC, NPC_, WEAP].
int[] Function LootNearbyEnabledReferences(ObjectReference player, ObjectReference dest, ObjectReference activator, ObjectReference workshop, int enabledFormTypeMask, int itemType, bool playPickupSound, bool playContainerAnimation, bool unlockLockedContainer, Form bobbyPin, Perk locksmith01, Perk locksmith02, Perk locksmith03, Perk locksmith04) global native

; Scrap matching inventory items into componentReceiver.
Function ScrapInventoryItems(ObjectReference inventoryOwner, ObjectReference componentReceiver, int itemType) global native
int[] Function ScrapInventoryItemsWithResults(ObjectReference inventoryOwner, ObjectReference componentReceiver, int itemType) global native

; Refresh native cached properties after Papyrus settings change.
Function OnUpdateLootManProperty(string propertyName) global native

; Play native pickup sound effects for obj.
Function PlayPickUpSound(ObjectReference player, ObjectReference obj) global native

; Run native cleanup after a successful world pickup.
Function FinalizeWorldPickup(ObjectReference ref) global native
