Scriptname LTMN2:Properties extends Quest

; Get an instance of this quest.
LTMN2:Properties Function GetInstance() global
    Return Game.GetFormFromFile(0x000F9A, "LootMan.esp") As LTMN2:Properties
EndFunction

Group Constants
    ; Item type
    int property ITEM_TYPE_ALCH = 1 autoreadonly hidden
    int property ITEM_TYPE_AMMO = 2 autoreadonly hidden
    int property ITEM_TYPE_ARMO = 4 autoreadonly hidden
    int property ITEM_TYPE_BOOK = 8 autoreadonly hidden
    int property ITEM_TYPE_INGR = 16 autoreadonly hidden
    int property ITEM_TYPE_KEYM = 32 autoreadonly hidden
    int property ITEM_TYPE_MISC = 64 autoreadonly hidden
    int property ITEM_TYPE_WEAP = 128 autoreadonly hidden
    int property ITEM_TYPE_ALL = 255 autoreadonly hidden

    int property ALCH_ITEM_TYPE_ALCOHOL = 1 autoreadonly hidden
    int property ALCH_ITEM_TYPE_CHEMISTRY = 2 autoreadonly hidden
    int property ALCH_ITEM_TYPE_FOOD = 4 autoreadonly hidden
    int property ALCH_ITEM_TYPE_NUKA_COLA = 8 autoreadonly hidden
    int property ALCH_ITEM_TYPE_STIMPAK = 16 autoreadonly hidden
    int property ALCH_ITEM_TYPE_SYRINGER_AMMO = 32 autoreadonly hidden
    int property ALCH_ITEM_TYPE_WATER = 64 autoreadonly hidden
    int property ALCH_ITEM_TYPE_OTHER = 128 autoreadonly hidden

    int property BOOK_ITEM_TYPE_PERKMAGAZINE = 1 autoreadonly hidden
    int property BOOK_ITEM_TYPE_OTHER = 2 autoreadonly hidden

    int property MISC_ITEM_TYPE_BOBBLEHEAD = 1 autoreadonly hidden
    int property MISC_ITEM_TYPE_OTHER = 2 autoreadonly hidden

    int property WEAP_ITEM_TYPE_GRENADE = 1 autoreadonly hidden
    int property WEAP_ITEM_TYPE_MINE = 2 autoreadonly hidden
    int property WEAP_ITEM_TYPE_OTHER = 4 autoreadonly hidden
EndGroup

Group Status
    ; Flags related to LootMan installation and initialization. Used as a prerequisite for a specific process or feature.
    bool property IsInstalled = false auto hidden
    bool property IsNotInstalled = true auto hidden
    bool property IsInitialized = false auto hidden
    bool property IsNotInitialized = true auto hidden
    bool property IsUninstalled = false auto hidden
    bool property IsNotUninstalled = true auto hidden

    ; Set by System.ProbeNativePlugin on every load: true whenever F4SE reports no
    ; loaded "lootman" plugin. Save-safe because the compile-time default covers
    ; older saves. The probe runs from a short delayed timer, so a save loaded
    ; straight into MCM can still read the previous session's value for a few
    ; seconds; the next probe overwrites it.
    bool property IsNativePluginMissing = false auto hidden

    bool property IsInSettlement = false auto hidden
    bool property IsOverweight = false auto hidden

    int property MaxItemsProcessedPerThread = 6 auto hidden

    int property LootableInventoryItemType = 255 auto hidden
    int property LootableALCHItemType = 255 auto hidden
    int property LootableBOOKItemType = 3 auto hidden
    int property LootableMISCItemType = 2 auto hidden
    int property LootableWEAPItemType = 7 auto hidden
EndGroup

Group Config
    bool property EnableLootMan = true auto hidden
    bool property DisplaySystemMessage = true auto hidden
    bool property PlayPickupSound = true auto hidden
    bool property PlayContainerAnimation = true auto hidden
    float property LootingRange = 6.0 auto hidden
    float property WorkerInvokeInterval = 1.0 auto hidden
    int property MaxLootableObjectsPerPass = 32 auto hidden
    int property MaxContainersPerPass = 4 auto hidden
    int property MaxActorsPerPass = 4 auto hidden
    int property MaxActivationRefsPerPass = 8 auto hidden
    bool property UseLootingTimeBudget = false auto hidden
    float property LootingTimeBudgetMs = 4.0 auto hidden
    int property CarryWeight = 1000 auto hidden
    bool property EnableCarryWeightLimit = false auto hidden
    ; Legacy property kept so existing saves can migrate their setting.
    bool property IgnoreOverweight = true auto hidden
    bool property LootIsDeliverToPlayer = false auto hidden
    bool property DisplayPickupMessage = false auto hidden
    ; Legacy property kept so existing saves can migrate their setting.
    bool property LootingWithoutLogs = true auto hidden
    ; Legacy property kept so existing saves can migrate their setting.
    bool property DeliveredToPlayerWithoutLogs = false auto hidden
    bool property EnableLootingInSettlement = false auto hidden
    ; Legacy property kept so existing saves can migrate their setting.
    bool property NotLootingFromSettlement = true auto hidden
    bool property AutomaticallyLinkAndUnlinkToWorkshop = false auto hidden
    bool property PauseLootingInWorkshopMode = false auto hidden
    bool property UnlockLockedContainer = true auto hidden

    bool property EnableObjectLootingOfACTI = true auto hidden
    bool property EnableObjectLootingOfALCH = true auto hidden
    bool property EnableObjectLootingOfAMMO = true auto hidden
    bool property EnableObjectLootingOfARMO = true auto hidden
    bool property EnableObjectLootingOfBOOK = true auto hidden
    bool property EnableObjectLootingOfCONT = true auto hidden
    bool property EnableObjectLootingOfFLOR = true auto hidden
    bool property EnableObjectLootingOfINGR = true auto hidden
    bool property EnableObjectLootingOfKEYM = true auto hidden
    bool property EnableObjectLootingOfMISC = true auto hidden
    bool property EnableObjectLootingOfNPC_ = true auto hidden
    bool property EnableObjectLootingOfWEAP = true auto hidden

    bool property EnableInventoryLootingOfALCH = true auto hidden
    bool property EnableInventoryLootingOfAMMO = true auto hidden
    bool property EnableInventoryLootingOfARMO = true auto hidden
    bool property EnableInventoryLootingOfBOOK = true auto hidden
    bool property EnableInventoryLootingOfINGR = true auto hidden
    bool property EnableInventoryLootingOfKEYM = true auto hidden
    bool property EnableInventoryLootingOfMISC = true auto hidden
    bool property EnableInventoryLootingOfWEAP = true auto hidden

    bool property LootingLegendaryOnly = false auto hidden
    bool property AlwaysLootingExplosives = false auto hidden
    bool property AlwaysLootingClothing = false auto hidden

    bool property EnableALCHItemAlcohol = true auto hidden
    bool property EnableALCHItemChemistry = true auto hidden
    bool property EnableALCHItemFood = true auto hidden
    bool property EnableALCHItemNukaCola = true auto hidden
    bool property EnableALCHItemStimpak = true auto hidden
    bool property EnableALCHItemSyringerAmmo = true auto hidden
    bool property EnableALCHItemWater = true auto hidden
    bool property EnableALCHItemOther = true auto hidden

    bool property EnableBOOKItemPerkMagazine = true auto hidden
    bool property EnableBOOKItemOther = true auto hidden

    bool property EnableMISCItemBobblehead = false auto hidden
    bool property EnableMISCItemOther = true auto hidden

    bool property EnableWEAPItemGrenade = true auto hidden
    bool property EnableWEAPItemMine = true auto hidden
    bool property EnableWEAPItemOther = true auto hidden
EndGroup

Group System
    ; Used for initialization
    Form property Pipboy auto const mandatory
    Quest property RadioInstitute auto const mandatory

    Actor property LootManRef auto const mandatory
    Actor property ActivatorRef auto const mandatory
    WorkshopScript property LootManWorkshopRef auto const mandatory
    ObjectReference property TemporaryContainerRef auto const mandatory
    Location property LootManLocation auto const mandatory

    ; Used to link / unlink workshops
    WorkshopParentScript property WorkshopParent auto const mandatory
    Keyword property WorkshopCaravan auto const mandatory

    ; Vanilla form list
    FormList property ShipmentItemList auto const mandatory

    ; Used to unlock container
    Form property BobbyPin auto const mandatory
    Perk property Locksmith01 auto const mandatory
    Perk property Locksmith02 auto const mandatory
    Perk property Locksmith03 auto const mandatory
    Perk property Locksmith04 auto const mandatory

    ; Used to utility function
    Keyword property ObjectTypeLooseMod auto const mandatory
EndGroup

; ---------------------------------------------------------------------------
; Packed subtype masks
; ---------------------------------------------------------------------------
;
; Every packed subtype option is stored twice: as the Config bool the MCM
; switcher writes and the player reads, and as one bit of a Status mask the
; native loot gate reads. The bool is the authority, because it is the value the
; player actually chose and the only one the UI can show.
;
; The masks used to be kept in step by flipping the matching bit with
; Math.LogicalXor from LTMN2:MCM.ApplySettingSideEffects. A flip is only correct
; while the pair already agrees and the callback runs exactly once per bool
; change, and neither holds: that callback returns early when the mod is not
; installed, and it aborts on its first LTMN2:LootMan native when lootman.dll
; failed to load - in both cases after MCM has already written the bool. A single
; missed flip inverted the option permanently, because nothing outside a
; version-bump migration ever reconciled the two again.
;
; Deriving instead of flipping removes the failure entirely: the mask has no state
; of its own to lose.

; Return bit when enabled and 0 otherwise, so one row of the mask table below fits
; on one line.
int Function MaskBit(bool enabled, int bit)
    If (enabled)
        Return bit
    EndIf
    Return 0
EndFunction

; Rebuild every packed mask from its backing bools. This is the only writer of the
; five Lootable*ItemType values.
;
; Total and idempotent: each mask is assembled from zero, so every bit is set or
; cleared to match its bool on every call, and calling it twice changes nothing.
; One call therefore repairs any divergence, whatever produced it.
;
; Callable from the load path before the native plugin is known to be there: it
; touches no LTMN2:LootMan function. Math.LogicalOr is an F4SE native, and F4SE is
; loaded in the failure mode this guards against - only lootman.dll is missing.
Function RecomputePackedMasks()
    int inventoryMask = 0
    inventoryMask = Math.LogicalOr(inventoryMask, MaskBit(EnableInventoryLootingOfALCH, ITEM_TYPE_ALCH))
    inventoryMask = Math.LogicalOr(inventoryMask, MaskBit(EnableInventoryLootingOfAMMO, ITEM_TYPE_AMMO))
    inventoryMask = Math.LogicalOr(inventoryMask, MaskBit(EnableInventoryLootingOfARMO, ITEM_TYPE_ARMO))
    inventoryMask = Math.LogicalOr(inventoryMask, MaskBit(EnableInventoryLootingOfBOOK, ITEM_TYPE_BOOK))
    inventoryMask = Math.LogicalOr(inventoryMask, MaskBit(EnableInventoryLootingOfINGR, ITEM_TYPE_INGR))
    inventoryMask = Math.LogicalOr(inventoryMask, MaskBit(EnableInventoryLootingOfKEYM, ITEM_TYPE_KEYM))
    inventoryMask = Math.LogicalOr(inventoryMask, MaskBit(EnableInventoryLootingOfMISC, ITEM_TYPE_MISC))
    inventoryMask = Math.LogicalOr(inventoryMask, MaskBit(EnableInventoryLootingOfWEAP, ITEM_TYPE_WEAP))
    LootableInventoryItemType = inventoryMask

    int alchMask = 0
    alchMask = Math.LogicalOr(alchMask, MaskBit(EnableALCHItemAlcohol, ALCH_ITEM_TYPE_ALCOHOL))
    alchMask = Math.LogicalOr(alchMask, MaskBit(EnableALCHItemChemistry, ALCH_ITEM_TYPE_CHEMISTRY))
    alchMask = Math.LogicalOr(alchMask, MaskBit(EnableALCHItemFood, ALCH_ITEM_TYPE_FOOD))
    alchMask = Math.LogicalOr(alchMask, MaskBit(EnableALCHItemNukaCola, ALCH_ITEM_TYPE_NUKA_COLA))
    alchMask = Math.LogicalOr(alchMask, MaskBit(EnableALCHItemStimpak, ALCH_ITEM_TYPE_STIMPAK))
    alchMask = Math.LogicalOr(alchMask, MaskBit(EnableALCHItemSyringerAmmo, ALCH_ITEM_TYPE_SYRINGER_AMMO))
    alchMask = Math.LogicalOr(alchMask, MaskBit(EnableALCHItemWater, ALCH_ITEM_TYPE_WATER))
    alchMask = Math.LogicalOr(alchMask, MaskBit(EnableALCHItemOther, ALCH_ITEM_TYPE_OTHER))
    LootableALCHItemType = alchMask

    int bookMask = 0
    bookMask = Math.LogicalOr(bookMask, MaskBit(EnableBOOKItemPerkMagazine, BOOK_ITEM_TYPE_PERKMAGAZINE))
    bookMask = Math.LogicalOr(bookMask, MaskBit(EnableBOOKItemOther, BOOK_ITEM_TYPE_OTHER))
    LootableBOOKItemType = bookMask

    int miscMask = 0
    miscMask = Math.LogicalOr(miscMask, MaskBit(EnableMISCItemBobblehead, MISC_ITEM_TYPE_BOBBLEHEAD))
    miscMask = Math.LogicalOr(miscMask, MaskBit(EnableMISCItemOther, MISC_ITEM_TYPE_OTHER))
    LootableMISCItemType = miscMask

    int weapMask = 0
    weapMask = Math.LogicalOr(weapMask, MaskBit(EnableWEAPItemGrenade, WEAP_ITEM_TYPE_GRENADE))
    weapMask = Math.LogicalOr(weapMask, MaskBit(EnableWEAPItemMine, WEAP_ITEM_TYPE_MINE))
    weapMask = Math.LogicalOr(weapMask, MaskBit(EnableWEAPItemOther, WEAP_ITEM_TYPE_OTHER))
    LootableWEAPItemType = weapMask
EndFunction

; True when id names one of the 23 bools RecomputePackedMasks reads. Kept beside
; that table on purpose: the two lists have to name the same settings, and a
; policy test pins them to each other.
bool Function IsPackedSubtypeSetting(string id)
    ; Inventory item-type filter
    If (id == "EnableInventoryLootingOfALCH")
        Return true
    ElseIf (id == "EnableInventoryLootingOfAMMO")
        Return true
    ElseIf (id == "EnableInventoryLootingOfARMO")
        Return true
    ElseIf (id == "EnableInventoryLootingOfBOOK")
        Return true
    ElseIf (id == "EnableInventoryLootingOfINGR")
        Return true
    ElseIf (id == "EnableInventoryLootingOfKEYM")
        Return true
    ElseIf (id == "EnableInventoryLootingOfMISC")
        Return true
    ElseIf (id == "EnableInventoryLootingOfWEAP")
        Return true

    ; ALCH subtype filter
    ElseIf (id == "EnableALCHItemAlcohol")
        Return true
    ElseIf (id == "EnableALCHItemChemistry")
        Return true
    ElseIf (id == "EnableALCHItemFood")
        Return true
    ElseIf (id == "EnableALCHItemNukaCola")
        Return true
    ElseIf (id == "EnableALCHItemStimpak")
        Return true
    ElseIf (id == "EnableALCHItemSyringerAmmo")
        Return true
    ElseIf (id == "EnableALCHItemWater")
        Return true
    ElseIf (id == "EnableALCHItemOther")
        Return true

    ; BOOK subtype filter
    ElseIf (id == "EnableBOOKItemPerkMagazine")
        Return true
    ElseIf (id == "EnableBOOKItemOther")
        Return true

    ; MISC subtype filter
    ElseIf (id == "EnableMISCItemBobblehead")
        Return true
    ElseIf (id == "EnableMISCItemOther")
        Return true

    ; WEAP subtype filter
    ElseIf (id == "EnableWEAPItemGrenade")
        Return true
    ElseIf (id == "EnableWEAPItemMine")
        Return true
    ElseIf (id == "EnableWEAPItemOther")
        Return true
    EndIf

    Return false
EndFunction
