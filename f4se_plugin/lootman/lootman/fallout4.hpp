#pragma once

class TESReactionForm : public BaseFormComponent
{
public:
    enum FIGHT_REACTION : UInt32
    {
        neutral = 0,
        enemy = 1,
        ally = 2,
        friend_ = 3
    };

    struct GROUP_REACTION
    {
    public:
        TESForm* form;
        UInt32 pad08;
        FIGHT_REACTION fightReaction;
    };

    STATIC_ASSERT(sizeof(GROUP_REACTION) == 0x10);

    tList<GROUP_REACTION> reactionList;
    UInt8 pad18;
};

STATIC_ASSERT(sizeof(TESReactionForm) == 0x20);

class TESFaction : public TESForm
{
public:
    enum { kTypeID = kFormType_FACT };

    TESFullName fullName;
    TESReactionForm reactionForm;

    struct FACTION_DATA
    {
        enum FLAG : UInt32
        {
            none = 0,
            vendor = 1 << 14,
            can_be_owner = 1 << 15,
        };

        FLAG flags;
    };

    STATIC_ASSERT(sizeof(FACTION_DATA) == 0x4);

    struct VENDOR_DATA
    {
    public:
        UInt64 pad00[0x28 / 8];
        TESObjectREFR* vendorChest;
        UInt32 pad30;
    };

    STATIC_ASSERT(offsetof(VENDOR_DATA, vendorChest) == 0x28);
    STATIC_ASSERT(sizeof(VENDOR_DATA) == 0x38);

    UInt64 pad50;
    FACTION_DATA factionData;
    UInt64 pad60[(0xA8 - 0x60) / 8];
    VENDOR_DATA vendorData;
    UInt64 padE0[(0xFC - 0xE0) / 8];
    float padFC;
};

STATIC_ASSERT(offsetof(TESFaction, factionData) == 0x58);
STATIC_ASSERT(offsetof(TESFaction, vendorData) == 0xA8);
STATIC_ASSERT(sizeof(TESFaction) == 0x100);

class TESActorBaseDataAlt : public BaseFormComponent
{
public:
    struct FACTION_DATA
    {
    public:
        TESFaction* faction;
        UInt8 rank;
    };

    STATIC_ASSERT(sizeof(FACTION_DATA) == 0x10);

    UInt64 pad08[(0x50 - 0x08) / 8];
    tArray<FACTION_DATA> factions;
};

STATIC_ASSERT(offsetof(TESActorBaseDataAlt, factions) == 0x50);
STATIC_ASSERT(sizeof(TESActorBaseDataAlt) == 0x68);

class BGSEncounterZoneAlt : public TESForm
{
public:
    enum { kTypeID = kFormType_ECZN };

    struct DATA
    {
    public:
        enum FLAG : UInt8
        {
            never_reset = 1 << 0,
            match_pc_below_min = 1 << 1,
            disable_combat_boundary = 1 << 2,
            workshop_zone = 1 << 3
        };

        TESForm* zoneOwner;
        BGSLocation* location;
        UInt8 ownerRank;
        UInt8 minLevel;
        FLAG flags;
        UInt8 maxLevel;
    };

    STATIC_ASSERT(sizeof(DATA) == 0x18);

    DATA data;
    UInt64 pad38[2];
};

STATIC_ASSERT(sizeof(BGSEncounterZoneAlt) == 0x48);

class TESObjectCELLAlt : public TESForm
{
public:
    enum { kTypeID = kFormType_CELL };

    TESFullName fullName;

    enum FLAG : UInt16
    {
        interior = 1 << 0,
        has_water = 1 << 1
    };

    enum STATE : UInt8
    {
        not_loaded,
        unloading,
        loading_data,
        loading,
        loaded,
        detaching,
        attach_queued,
        attaching,
        attached
    };

    UInt64 pad30;
    UInt64 pad38;
    UInt16 cellFlags;
    UInt16 pad42;
    UInt8 cellState;
    UInt8 pad45[3];
    ExtraDataList* extraDataList;
    UInt64 pad50[(0x70 - 0x50) / 8];
    tArray<TESObjectREFR*> objectList;
    UInt64 pad88[(0xC0 - 0x88) / 8];
    SimpleLock lock;
    TESWorldSpace* worldSpace;

    struct LOADED_DATA
    {
    public:
        UInt64 pad00[0x1F0 / 8];
        BGSEncounterZoneAlt* encounterZone;
        UInt64 pad1F8[(0x210 - 0x1F8) / 8];
        UInt32 pad214;
    };

    STATIC_ASSERT(offsetof(LOADED_DATA, encounterZone) == 0x1F0);
    STATIC_ASSERT(sizeof(LOADED_DATA) == 0x218);

    LOADED_DATA* loadedData;
    UInt64 padD8[(0xF0 - 0xD8) / 8];
};

STATIC_ASSERT(offsetof(TESObjectCELLAlt, cellFlags) == 0x40);
STATIC_ASSERT(offsetof(TESObjectCELLAlt, cellState) == 0x44);
STATIC_ASSERT(offsetof(TESObjectCELLAlt, extraDataList) == 0x48);
STATIC_ASSERT(offsetof(TESObjectCELLAlt, objectList) == 0x70);
STATIC_ASSERT(offsetof(TESObjectCELLAlt, lock) == 0xC0);
STATIC_ASSERT(offsetof(TESObjectCELLAlt, worldSpace) == 0xC8);
STATIC_ASSERT(offsetof(TESObjectCELLAlt, loadedData) == 0xD0);
STATIC_ASSERT(sizeof(TESObjectCELLAlt) == 0xF0);

class TESWorldSpace : public TESForm
{
public:
    enum { kTypeID = kFormType_WRLD };

    TESFullName fullName;
    TESTexture texture;

    struct CELL_ENTRY
    {
        UInt32 key;
        TESObjectCELLAlt* value;

        bool operator==(const UInt32 a_key) const { return key == a_key; }
        operator UInt32() const { return key; }

        static inline UInt32 GetHash(const UInt32* key)
        {
            UInt32 hash;
            CalculateCRC32_32(&hash, *key, 0);
            return hash;
        }
    };

    tHashSet<CELL_ENTRY, UInt32> cells;
    UInt64 pad70[(0x188 - 0x70) / 8];
    TESWorldSpace* parentWorld;
    UInt64 pad190[(0x228 - 0x190) / 8];
    BGSEncounterZoneAlt* encounterZone;
    UInt64 pad230[(0x2C8 - 0x230) / 8];
    UInt8 pad2C8;
};

STATIC_ASSERT(offsetof(TESWorldSpace, cells) == 0x40);
STATIC_ASSERT(offsetof(TESWorldSpace, parentWorld) == 0x188);
STATIC_ASSERT(offsetof(TESWorldSpace, encounterZone) == 0x228);
STATIC_ASSERT(sizeof(TESWorldSpace) == 0x2D0);
