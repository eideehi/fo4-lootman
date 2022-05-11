Scriptname LTMN2:Patch native hidden

Function v2_0_1() global
    LTMN2:Properties properties = LTMN2:Properties.GetInstance()

    properties.EnableInventoryLootingOfALCH = Math.LogicalAnd(properties.LootableInventoryItemType, properties.ITEM_TYPE_ALCH) != 0
    properties.EnableInventoryLootingOfAMMO = Math.LogicalAnd(properties.LootableInventoryItemType, properties.ITEM_TYPE_AMMO) != 0
    properties.EnableInventoryLootingOfARMO = Math.LogicalAnd(properties.LootableInventoryItemType, properties.ITEM_TYPE_ARMO) != 0
    properties.EnableInventoryLootingOfBOOK = Math.LogicalAnd(properties.LootableInventoryItemType, properties.ITEM_TYPE_BOOK) != 0
    properties.EnableInventoryLootingOfINGR = Math.LogicalAnd(properties.LootableInventoryItemType, properties.ITEM_TYPE_INGR) != 0
    properties.EnableInventoryLootingOfKEYM = Math.LogicalAnd(properties.LootableInventoryItemType, properties.ITEM_TYPE_KEYM) != 0
    properties.EnableInventoryLootingOfMISC = Math.LogicalAnd(properties.LootableInventoryItemType, properties.ITEM_TYPE_MISC) != 0
    properties.EnableInventoryLootingOfWEAP = Math.LogicalAnd(properties.LootableInventoryItemType, properties.ITEM_TYPE_WEAP) != 0

    properties.EnableALCHItemAlcohol = Math.LogicalAnd(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_ALCOHOL) != 0
    properties.EnableALCHItemChemistry = Math.LogicalAnd(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_CHEMISTRY) != 0
    properties.EnableALCHItemFood = Math.LogicalAnd(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_FOOD) != 0
    properties.EnableALCHItemNukaCola = Math.LogicalAnd(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_NUKA_COLA) != 0
    properties.EnableALCHItemStimpak = Math.LogicalAnd(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_STIMPAK) != 0
    properties.EnableALCHItemSyringerAmmo = Math.LogicalAnd(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_SYRINGER_AMMO) != 0
    properties.EnableALCHItemWater = Math.LogicalAnd(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_WATER) != 0
    properties.EnableALCHItemOther = Math.LogicalAnd(properties.LootableALCHItemType, properties.ALCH_ITEM_TYPE_OTHER) != 0

    properties.EnableBOOKItemPerkMagazine = Math.LogicalAnd(properties.LootableBOOKItemType, properties.BOOK_ITEM_TYPE_PERKMAGAZINE) != 0
    properties.EnableBOOKItemOther = Math.LogicalAnd(properties.LootableBOOKItemType, properties.BOOK_ITEM_TYPE_OTHER) != 0

    properties.EnableMISCItemBobblehead = Math.LogicalAnd(properties.LootableMISCItemType, properties.MISC_ITEM_TYPE_BOBBLEHEAD) != 0
    properties.EnableMISCItemOther = Math.LogicalAnd(properties.LootableMISCItemType, properties.MISC_ITEM_TYPE_OTHER) != 0

    properties.EnableWEAPItemGrenade = Math.LogicalAnd(properties.LootableWEAPItemType, properties.WEAP_ITEM_TYPE_GRENADE) != 0
    properties.EnableWEAPItemMine = Math.LogicalAnd(properties.LootableWEAPItemType, properties.WEAP_ITEM_TYPE_MINE) != 0
    properties.EnableWEAPItemOther = Math.LogicalAnd(properties.LootableWEAPItemType, properties.WEAP_ITEM_TYPE_OTHER) != 0
EndFunction
