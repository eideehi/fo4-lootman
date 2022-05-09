Scriptname LTMN2:Properties extends Quest

; Get an instance of this quest.
LTMN2:Properties Function GetInstance() global
    Return Game.GetFormFromFile(0x000F9A, "LootMan.esp") As LTMN2:Properties
EndFunction

Group Constants
    ; Form type
    int property FORM_TYPE_ACTI = 27 autoreadonly hidden
    int property FORM_TYPE_ALCH = 48 autoreadonly hidden
    int property FORM_TYPE_AMMO = 44 autoreadonly hidden
    int property FORM_TYPE_ARMO = 29 autoreadonly hidden
    int property FORM_TYPE_BOOK = 30 autoreadonly hidden
    int property FORM_TYPE_CONT = 31 autoreadonly hidden
    int property FORM_TYPE_FLOR = 41 autoreadonly hidden
    int property FORM_TYPE_INGR = 33 autoreadonly hidden
    int property FORM_TYPE_KEYM = 47 autoreadonly hidden
    int property FORM_TYPE_MISC = 35 autoreadonly hidden
    int property FORM_TYPE_NPC_ = 45 autoreadonly hidden
    int property FORM_TYPE_WEAP = 43 autoreadonly hidden
    int property FORM_TYPE_ALL = 159 autoreadonly hidden

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

    bool property IsInSettlement = false auto hidden
    bool property IsOverweight = false auto hidden

    int property ActiveWorkerThreadsACTI = 0 auto hidden
    int property ActiveWorkerThreadsALCH = 0 auto hidden
    int property ActiveWorkerThreadsAMMO = 0 auto hidden
    int property ActiveWorkerThreadsARMO = 0 auto hidden
    int property ActiveWorkerThreadsBOOK = 0 auto hidden
    int property ActiveWorkerThreadsCONT = 0 auto hidden
    int property ActiveWorkerThreadsFLOR = 0 auto hidden
    int property ActiveWorkerThreadsINGR = 0 auto hidden
    int property ActiveWorkerThreadsKEYM = 0 auto hidden
    int property ActiveWorkerThreadsMISC = 0 auto hidden
    int property ActiveWorkerThreadsNPC_ = 0 auto hidden
    int property ActiveWorkerThreadsWEAP = 0 auto hidden

    int property MaxItemsProcessedPerThread = 6 auto hidden

    bool property TurboModeACTI = false auto hidden
    bool property TurboModeALCH = false auto hidden
    bool property TurboModeAMMO = false auto hidden
    bool property TurboModeARMO = false auto hidden
    bool property TurboModeBOOK = false auto hidden
    bool property TurboModeCONT = false auto hidden
    bool property TurboModeFLOR = false auto hidden
    bool property TurboModeINGR = false auto hidden
    bool property TurboModeKEYM = false auto hidden
    bool property TurboModeMISC = false auto hidden
    bool property TurboModeNPC_ = false auto hidden
    bool property TurboModeWEAP = false auto hidden

    int property LootableInventoryItemType = 255 auto hidden
    int property LootableALCHItemType = 255 auto hidden
    int property LootableBOOKItemType = 3 auto hidden
    int property LootableMISCItemType = 3 auto hidden
    int property LootableWEAPItemType = 7 auto hidden
EndGroup

Group Config
    bool property EnableLootMan = true auto hidden
    bool property DisplaySystemMessage = true auto hidden
    bool property PlayPickupSound = true auto hidden
    bool property PlayContainerAnimation = true auto hidden
    float property LootingRange = 6.0 auto hidden
    float property WorkerInvokeInterval = 1.0 auto hidden
    int property CarryWeight = 1000 auto hidden
    bool property IgnoreOverweight = true auto hidden
    bool property LootIsDeliverToPlayer = false auto hidden
    bool property NotLootingFromSettlement = true auto hidden
    bool property AutomaticallyLinkAndUnlinkToWorkshop = false auto hidden
    bool property UnlockLockedContainer = true auto hidden

    int property MaxWorkerThreadsACTI = 4 auto hidden
    int property MaxWorkerThreadsALCH = 4 auto hidden
    int property MaxWorkerThreadsAMMO = 4 auto hidden
    int property MaxWorkerThreadsARMO = 4 auto hidden
    int property MaxWorkerThreadsBOOK = 4 auto hidden
    int property MaxWorkerThreadsCONT = 4 auto hidden
    int property MaxWorkerThreadsFLOR = 4 auto hidden
    int property MaxWorkerThreadsINGR = 1 auto hidden
    int property MaxWorkerThreadsKEYM = 4 auto hidden
    int property MaxWorkerThreadsMISC = 4 auto hidden
    int property MaxWorkerThreadsNPC_ = 4 auto hidden
    int property MaxWorkerThreadsWEAP = 4 auto hidden

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

    bool property EnableALCHItemAlcohol = true auto hidden
    bool property EnableALCHItemChemistry = true auto hidden
    bool property EnableALCHItemFood = true auto hidden
    bool property EnableALCHItemNukaCola = true auto hidden
    bool property EnableALCHItemStimpak = true auto hidden
    bool property EnableALCHItemSyringerAmmo = true auto hidden
    bool property EnableALCHItemWater = true auto hidden
    bool property EnableALCHItemOther = true auto hidden

    bool property EnableBOOKItemPerkMagazine = false auto hidden
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
