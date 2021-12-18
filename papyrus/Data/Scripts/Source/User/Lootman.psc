Scriptname Lootman native hidden

; Return the system quest for Lootman
LTMN:Quest:System Function GetSystem() global
    Return Game.GetFormFromFile(0x0098F7, "Lootman.esp") As LTMN:Quest:System
EndFunction

; Return the functions quest for Lootman
LTMN:Quest:Methods Function GetFunctions() global
    Return Game.GetFormFromFile(0x009917, "Lootman.esp") As LTMN:Quest:Methods
EndFunction

; Return the properties quest for Lootman
LTMN:Quest:Properties Function GetProperties() global
    Return Game.GetFormFromFile(0x0098B4, "Lootman.esp") As LTMN:Quest:Properties
EndFunction

; Retrieves and returns the looting targets that exist within a certain range, starting from the specified object
ObjectReference[] Function FindAllLootingTarget(ObjectReference ref, int range, int formType) global native

; Retrieves and returns objects that are past the period of time when they are no longer subject to looting
ObjectReference[] Function GetAllExpiredObject(ObjectReference ref, int range, float currentTime, float expiration) global native

; Return the result of scrapping an object. The object must be a weapon or armor
MiscObject:MiscComponent[] Function GetEquipmentScrapComponents(ObjectReference ref) global native

; Get and returns the form type of the form
int Function GetFormType(Form form) global native

; Get and return the injection data to be registered in the form list
Form[] Function GetInjectionDataForList(string identify) global native

; Get and return only items of a specified form type from an inventory of object references
Form[] Function GetInventoryItemsOfFormTypes(ObjectReference ref, int[] formTypes) global native

; Get and return only items of a specified form type from an inventory of object references
Form[] Function GetInventoryItemsOfFormType(ObjectReference ref, int formType) global
    int[] formTypes = new int[1]
    formTypes[0] = formType
    Return GetInventoryItemsOfFormTypes(ref, formTypes)
EndFunction

; Verify the existence of the specified item's legendary in the object's inventory. Returns false if the item is not playable, or if it is neither a weapon nor armor
bool Function HasLegendaryItem(ObjectReference ref, Form form) global native

; Verify the object is a Legendary item. Returns false if the object is not playable, or if it is neither a weapon nor armor
bool Function IsLegendaryItem(ObjectReference ref) global native

; Verify the object is linked to the workshop
bool Function IsLinkedToWorkshop(ObjectReference ref) global native

; Verify that the object reference is a valid
bool Function IsValidRef(ObjectReference ref) global native
