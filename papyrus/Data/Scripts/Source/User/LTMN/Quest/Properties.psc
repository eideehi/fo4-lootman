Scriptname LTMN:Quest:Properties extends Quest

Group ActorValues
    ; Timestamp of the last looting attempt
    ActorValue property LastLootingTimestamp auto const mandatory
EndGroup

Group Forms
    ; Reference to it in the container unlocking process.
    Form property BobbyPin auto const mandatory

    ; Required to play the generator animation
    Form property FusionCore auto const mandatory

    ; Location to be linked to settlement workshop
    Location property LootmanLocation auto const mandatory

    ; Lootman utility items
    Form property OpenInventoryChem auto const mandatory
    Form property ToggleLootingChem auto const mandatory
    Form property ToggleWorkshopLinkChem auto const mandatory
    Form property UtilityHolotape auto const mandatory
EndGroup

Group FormLists
    FormList property ShipmentItemList auto const mandatory

    ; Injectuin data list
    FormList property AllowedActivatorList auto const mandatory
    FormList property AllowedFeaturedItemList auto const mandatory
    FormList property AllowedUniqueItemList auto const mandatory
    FormList property ExcludeFormList auto const mandatory
    FormList property ExcludeKeywordList auto const mandatory
    FormList property ExcludeLocationRefList auto const mandatory
    FormList property IgnorableActivationBlockeList auto const mandatory
    FormList property UniqueItemList auto const mandatory
    FormList property VendorChestList auto const mandatory
    FormList property WeaponTypeGrenadeKeywordList auto const mandatory
    FormList property WeaponTypeMineKeywordList auto const mandatory
EndGroup

Group FormListIdentifies
    string property LIST_IDENTIFY_ALLOWED_ACTIVATOR = "AllowedActivatorList" autoreadonly
    string property LIST_IDENTIFY_ALLOWED_FEATUREDITEM = "AllowedFeaturedItemList" autoreadonly
    string property LIST_IDENTIFY_ALLOWED_UNIQUEITEM = "AllowedUniqueItemList" autoreadonly
    string property LIST_IDENTIFY_EXCLUDE_FORM = "ExcludeFormList" autoreadonly
    string property LIST_IDENTIFY_EXCLUDE_KEYWORD = "ExcludeKeywordList" autoreadonly
    string property LIST_IDENTIFY_EXCLUDE_LOCATIONREF = "ExcludeLocationRefList" autoreadonly
    string property LIST_IDENTIFY_IGNORABLE_ACTIVATION_BLOCKE = "IgnorableActivationBlockeList" autoreadonly
    string property LIST_IDENTIFY_VENDOR_CHEST = "VendorChestList" autoreadonly
    string property LIST_IDENTIFY_WEAPONTYPE_GRENADE_KEYWORD = "WeaponTypeGrenadeKeywordList" autoreadonly
    string property LIST_IDENTIFY_WEAPONTYPE_MINE_KEYWORD = "WeaponTypeMineKeywordList" autoreadonly
EndGroup

Group FormTypes
    int property FORM_TYPE_ACTI = 27 autoreadonly
    int property FORM_TYPE_ALCH = 48 autoreadonly
    int property FORM_TYPE_AMMO = 44 autoreadonly
    int property FORM_TYPE_ARMO = 29 autoreadonly
    int property FORM_TYPE_BOOK = 30 autoreadonly
    int property FORM_TYPE_CONT = 31 autoreadonly
    int property FORM_TYPE_FLOR = 41 autoreadonly
    int property FORM_TYPE_INGR = 33 autoreadonly
    int property FORM_TYPE_KEYM = 47 autoreadonly
    int property FORM_TYPE_MISC = 35 autoreadonly
    int property FORM_TYPE_NPC_ = 45 autoreadonly
    int property FORM_TYPE_WEAP = 43 autoreadonly
EndGroup

Group Globals
    ; TODO: Reuse the next time you add more globals
    GlobalVariable property ModVersion auto const mandatory

    ; Lootman flags
    GlobalVariable property IsEnabled auto const mandatory
    GlobalVariable property IsInitializing auto const mandatory
    GlobalVariable property IsInSettlement auto const mandatory
    GlobalVariable property IsInstalled auto const mandatory
    GlobalVariable property IsOverweight auto const mandatory
    GlobalVariable property IsPipboyOpen auto const mandatory
    GlobalVariable property IsUninstalled auto const mandatory

    ; General settings
    GlobalVariable property AutomaticallyLinkOrUnlinkToWorkshop auto const mandatory
    GlobalVariable property CarryWeight auto const mandatory
    GlobalVariable property DisplaySystemMessage auto const mandatory
    GlobalVariable property IgnoreOverweight auto const mandatory
    GlobalVariable property LootingDisabledInSettlement auto const mandatory
    GlobalVariable property LootingRange auto const mandatory
    GlobalVariable property LootInPlayerDirectly auto const mandatory
    GlobalVariable property PlayContainerAnimation auto const mandatory
    GlobalVariable property PlayPickupSound auto const mandatory
    GlobalVariable property ThreadInterval auto const mandatory
    GlobalVariable property ThreadAllowedWorkingTime auto const mandatory
    GlobalVariable property ExpirationToSkipLooting auto const mandatory
    GlobalVariable property AllowContainerUnlock auto const mandatory

    ; Thread limit
    GlobalVariable property ThreadLimitACTI auto const mandatory
    GlobalVariable property ThreadLimitALCH auto const mandatory
    GlobalVariable property ThreadLimitAMMO auto const mandatory
    GlobalVariable property ThreadLimitARMO auto const mandatory
    GlobalVariable property ThreadLimitBOOK auto const mandatory
    GlobalVariable property ThreadLimitCONT auto const mandatory
    GlobalVariable property ThreadLimitFLOR auto const mandatory
    GlobalVariable property ThreadLimitINGR auto const mandatory
    GlobalVariable property ThreadLimitKEYM auto const mandatory
    GlobalVariable property ThreadLimitMISC auto const mandatory
    GlobalVariable property ThreadLimitNPC_ auto const mandatory
    GlobalVariable property ThreadLimitWEAP auto const mandatory

    ; Target filter
    GlobalVariable property TargetFilterContainer auto const mandatory
    GlobalVariable property TargetFilterCorpse auto const mandatory
    GlobalVariable property TargetFilterObject auto const mandatory

    ; Category filter
    GlobalVariable property CategoryFilterACTI auto const mandatory
    GlobalVariable property CategoryFilterALCH auto const mandatory
    GlobalVariable property CategoryFilterAMMO auto const mandatory
    GlobalVariable property CategoryFilterARMO auto const mandatory
    GlobalVariable property CategoryFilterBOOK auto const mandatory
    GlobalVariable property CategoryFilterFLOR auto const mandatory
    GlobalVariable property CategoryFilterINGR auto const mandatory
    GlobalVariable property CategoryFilterKEYM auto const mandatory
    GlobalVariable property CategoryFilterMISC auto const mandatory
    GlobalVariable property CategoryFilterWEAP auto const mandatory

    ; Advanced filter for ALCH
    GlobalVariable property AdvancedFilterAlcohol auto const mandatory
    GlobalVariable property AdvancedFilterChem auto const mandatory
    GlobalVariable property AdvancedFilterFood auto const mandatory
    GlobalVariable property AdvancedFilterNukaCola auto const mandatory
    GlobalVariable property AdvancedFilterStimpak auto const mandatory
    GlobalVariable property AdvancedFilterSyringerAmmo auto const mandatory
    GlobalVariable property AdvancedFilterWater auto const mandatory
    GlobalVariable property AdvancedFilterOtherAlchemy auto const mandatory

    ; Advanced filter for BOOK
    GlobalVariable property AdvancedFilterPerkMagazine auto const mandatory
    GlobalVariable property AdvancedFilterOtherBook auto const mandatory

    ; Advanced filter for WEAP
    GlobalVariable property AdvancedFilterGrenade auto const mandatory
    GlobalVariable property AdvancedFilterMine auto const mandatory
    GlobalVariable property AdvancedFilterOtherWeapon auto const mandatory

    ; Advanced filter for Equipment
    GlobalVariable property AdvancedFilterLegendaryOnly auto const mandatory
EndGroup

Group Keywords
    Keyword property BlockWorkshopInteraction auto const mandatory
    Keyword property FeaturedItem auto const mandatory
    Keyword property LootingMarker auto const mandatory
    Keyword property ObjectTypeAlcohol auto const mandatory
    Keyword property ObjectTypeChem auto const mandatory
    Keyword property ObjectTypeFood auto const mandatory
    Keyword property ObjectTypeLooseMod auto const mandatory
    Keyword property ObjectTypeNukaCola auto const mandatory
    Keyword property ObjectTypeStimpak auto const mandatory
    Keyword property ObjectTypeSyringerAmmo auto const mandatory
    Keyword property ObjectTypeWater auto const mandatory
    Keyword property PerkMagazine auto const mandatory
    Keyword property UnscrappableObject auto const mandatory
    Keyword property WorkshopCaravan auto const mandatory
EndGroup

Group MessageId
    int property MESSAGE_LOOTMAN_INSTALLED = 1 autoreadonly
    int property MESSAGE_LOOTMAN_DISABLED = 2 autoreadonly
    int property MESSAGE_LOOTMAN_ENABLED = 3 autoreadonly
    int property MESSAGE_HAS_OVERWEIGHT = 4 autoreadonly
    int property MESSAGE_SOLVED_OVERWEIGHT = 5 autoreadonly
    int property MESSAGE_UNLINKED_TO_WORKSHOP = 6 autoreadonly
    int property MESSAGE_LINKED_TO_WORKSHOP = 7 autoreadonly
    int property MESSAGE_WORKSHOP_NOT_FOUND = 8 autoreadonly
    int property MESSAGE_PROCESS_COMPLETE = 9 autoreadonly
    int property MESSAGE_LOOTMAN_UNINSTALLED = 10 autoreadonly
    int property MESSAGE_PLAYER_IN_SETTLEMENT = 11 autoreadonly
    int property MESSAGE_PLAYER_OUT_SETTLEMENT = 12 autoreadonly
EndGroup

Group References
    Actor property ActivatorActor auto const mandatory
    Actor property LootmanActor auto const mandatory
    ObjectReference property TemporaryContainer auto const mandatory
    WorkshopScript property LootmanWorkshop auto const mandatory
    WorkshopParentScript property WorkshopParent auto const mandatory
EndGroup

Group Sounds
    Sound property PickupSoundACTI auto const mandatory
    Sound property PickupSoundFLOR auto const mandatory
    Sound property PickupSoundNPC_ auto const mandatory
    Sound property ProcessCompleteSound auto const mandatory
EndGroup

Group Perks
    Perk property Locksmith01 auto const mandatory
    Perk property Locksmith02 auto const mandatory
    Perk property Locksmith03 auto const mandatory
    Perk property Locksmith04 auto const mandatory
EndGroup
