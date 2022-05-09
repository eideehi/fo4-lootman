#pragma once

#include "f4se/GameExtraData.h"

class BGSRefAlias;
class BGSLocationRefType;
class BGSEncounterZoneAlt;

class ExtraAliasInstanceArray : public BSExtraData
{
public:
    struct ALIAS_DATA
    {
    public:
        TESQuest* quest;
        BGSRefAlias* alias;
        UInt64 pad10;
    };

    STATIC_ASSERT(sizeof(ALIAS_DATA) == 0x18);

    tArray<ALIAS_DATA> aliases;
    BSReadWriteLock aliasesLock;
};

STATIC_ASSERT(sizeof(ExtraAliasInstanceArray) == 0x38);

class ExtraOwnership : public BSExtraData
{
public:
    TESForm* owner;
};

STATIC_ASSERT(sizeof(ExtraOwnership) == 0x20);

class ExtraEncounterZone : public BSExtraData
{
public:
    BGSEncounterZoneAlt* encounterZone;
};

STATIC_ASSERT(sizeof(ExtraEncounterZone) == 0x20);
