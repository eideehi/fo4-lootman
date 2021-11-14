#include "Debug.h"

#ifdef _DEBUG

#include <iomanip>
#include <map>
#include <random>
#include <sstream>

#include "f4se/GameExtraData.h"
#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"

const char* _GetFormTypeIdentify(UInt8 formType)
{
    switch (formType)
    {
    case kFormType_ACTI: return "ACTI";
    case kFormType_ALCH: return "ALCH";
    case kFormType_AMMO: return "AMMO";
    case kFormType_ARMO: return "ARMO";
    case kFormType_BOOK: return "BOOK";
    case kFormType_CONT: return "CONT";
    case kFormType_FLOR: return "FLOR";
    case kFormType_INGR: return "INGR";
    case kFormType_KEYM: return "KEYM";
    case kFormType_MISC: return "MISC";
    case kFormType_NPC_: return "NPC_";
    case kFormType_WEAP: return "WEAP";
    case kFormType_TES4: return "TES4";
    case kFormType_GRUP: return "GRUP";
    case kFormType_GMST: return "GMST";
    case kFormType_KYWD: return "KYWD";
    case kFormType_LCRT: return "LCRT";
    case kFormType_AACT: return "AACT";
    case kFormType_TRNS: return "TRNS";
    case kFormType_CMPO: return "CMPO";
    case kFormType_TXST: return "TXST";
    case kFormType_MICN: return "MICN";
    case kFormType_GLOB: return "GLOB";
    case kFormType_DMGT: return "DMGT";
    case kFormType_CLAS: return "CLAS";
    case kFormType_FACT: return "FACT";
    case kFormType_HDPT: return "HDPT";
    case kFormType_EYES: return "EYES";
    case kFormType_RACE: return "RACE";
    case kFormType_SOUN: return "SOUN";
    case kFormType_ASPC: return "ASPC";
    case kFormType_SKIL: return "SKIL";
    case kFormType_MGEF: return "MGEF";
    case kFormType_SCPT: return "SCPT";
    case kFormType_LTEX: return "LTEX";
    case kFormType_ENCH: return "ENCH";
    case kFormType_SPEL: return "SPEL";
    case kFormType_SCRL: return "SCRL";
    case kFormType_TACT: return "TACT";
    case kFormType_DOOR: return "DOOR";
    case kFormType_LIGH: return "LIGH";
    case kFormType_STAT: return "STAT";
    case kFormType_SCOL: return "SCOL";
    case kFormType_MSTT: return "MSTT";
    case kFormType_GRAS: return "GRAS";
    case kFormType_TREE: return "TREE";
    case kFormType_FURN: return "FURN";
    case kFormType_LVLN: return "LVLN";
    case kFormType_IDLM: return "IDLM";
    case kFormType_NOTE: return "NOTE";
    case kFormType_PROJ: return "PROJ";
    case kFormType_HAZD: return "HAZD";
    case kFormType_BNDS: return "BNDS";
    case kFormType_SLGM: return "SLGM";
    case kFormType_TERM: return "TERM";
    case kFormType_LVLI: return "LVLI";
    case kFormType_WTHR: return "WTHR";
    case kFormType_CLMT: return "CLMT";
    case kFormType_SPGD: return "SPGD";
    case kFormType_RFCT: return "RFCT";
    case kFormType_REGN: return "REGN";
    case kFormType_NAVI: return "NAVI";
    case kFormType_CELL: return "CELL";
    case kFormType_REFR: return "REFR";
    case kFormType_ACHR: return "ACHR";
    case kFormType_PMIS: return "PMIS";
    case kFormType_PARW: return "PARW";
    case kFormType_PGRE: return "PGRE";
    case kFormType_PBEA: return "PBEA";
    case kFormType_PFLA: return "PFLA";
    case kFormType_PCON: return "PCON";
    case kFormType_PBAR: return "PBAR";
    case kFormType_PHZD: return "PHZD";
    case kFormType_WRLD: return "WRLD";
    case kFormType_LAND: return "LAND";
    case kFormType_NAVM: return "NAVM";
    case kFormType_TLOD: return "TLOD";
    case kFormType_DIAL: return "DIAL";
    case kFormType_INFO: return "INFO";
    case kFormType_QUST: return "QUST";
    case kFormType_IDLE: return "IDLE";
    case kFormType_PACK: return "PACK";
    case kFormType_CSTY: return "CSTY";
    case kFormType_LSCR: return "LSCR";
    case kFormType_LVSP: return "LVSP";
    case kFormType_ANIO: return "ANIO";
    case kFormType_WATR: return "WATR";
    case kFormType_EFSH: return "EFSH";
    case kFormType_TOFT: return "TOFT";
    case kFormType_EXPL: return "EXPL";
    case kFormType_DEBR: return "DEBR";
    case kFormType_IMGS: return "IMGS";
    case kFormType_IMAD: return "IMAD";
    case kFormType_FLST: return "FLST";
    case kFormType_PERK: return "PERK";
    case kFormType_BPTD: return "BPTD";
    case kFormType_ADDN: return "ADDN";
    case kFormType_AVIF: return "AVIF";
    case kFormType_CAMS: return "CAMS";
    case kFormType_CPTH: return "CPTH";
    case kFormType_VTYP: return "VTYP";
    case kFormType_MATT: return "MATT";
    case kFormType_IPCT: return "IPCT";
    case kFormType_IPDS: return "IPDS";
    case kFormType_ARMA: return "ARMA";
    case kFormType_ECZN: return "ECZN";
    case kFormType_LCTN: return "LCTN";
    case kFormType_MESG: return "MESG";
    case kFormType_RGDL: return "RGDL";
    case kFormType_DOBJ: return "DOBJ";
    case kFormType_DFOB: return "DFOB";
    case kFormType_LGTM: return "LGTM";
    case kFormType_MUSC: return "MUSC";
    case kFormType_FSTP: return "FSTP";
    case kFormType_FSTS: return "FSTS";
    case kFormType_SMBN: return "SMBN";
    case kFormType_SMQN: return "SMQN";
    case kFormType_SMEN: return "SMEN";
    case kFormType_DLBR: return "DLBR";
    case kFormType_MUST: return "MUST";
    case kFormType_DLVW: return "DLVW";
    case kFormType_WOOP: return "WOOP";
    case kFormType_SHOU: return "SHOU";
    case kFormType_EQUP: return "EQUP";
    case kFormType_RELA: return "RELA";
    case kFormType_SCEN: return "SCEN";
    case kFormType_ASTP: return "ASTP";
    case kFormType_OTFT: return "OTFT";
    case kFormType_ARTO: return "ARTO";
    case kFormType_MATO: return "MATO";
    case kFormType_MOVT: return "MOVT";
    case kFormType_SNDR: return "SNDR";
    case kFormType_DUAL: return "DUAL";
    case kFormType_SNCT: return "SNCT";
    case kFormType_SOPM: return "SOPM";
    case kFormType_COLL: return "COLL";
    case kFormType_CLFM: return "CLFM";
    case kFormType_REVB: return "REVB";
    case kFormType_PKIN: return "PKIN";
    case kFormType_RFGP: return "RFGP";
    case kFormType_AMDL: return "AMDL";
    case kFormType_LAYR: return "LAYR";
    case kFormType_COBJ: return "COBJ";
    case kFormType_OMOD: return "OMOD";
    case kFormType_MSWP: return "MSWP";
    case kFormType_ZOOM: return "ZOOM";
    case kFormType_INNR: return "INNR";
    case kFormType_KSSM: return "KSSM";
    case kFormType_AECH: return "AECH";
    case kFormType_SCCO: return "SCCO";
    case kFormType_AORU: return "AORU";
    case kFormType_SCSN: return "SCSN";
    case kFormType_STAG: return "STAG";
    case kFormType_NOCM: return "NOCM";
    case kFormType_LENS: return "LENS";
    case kFormType_LSPR: return "LSPR";
    case kFormType_GDRY: return "GDRY";
    case kFormType_OVIS: return "OVIS";
    default: return "UKWN";
    }
}

const char* _MakeIndent(int depth, std::string indent = "  ")
{
    if (depth <= 0) return "";
    std::stringstream ss;
    for (int i = 0; i < depth; i++)
    {
        ss << indent;
    }
    return BSFixedString(ss.str().c_str());
}

// Generate and return a random process ID (a 10-digit random hexadecimal string)
// Pages used for reference: https://stackoverflow.com/questions/12110209/how-to-fill-a-string-with-random-hex-characters#answer-12110369
const char* _GetRandomProcessID()
{
    const char* hex = "0123456789ABCDEF";
    std::random_device rand;

    std::stringstream ss;
    for (int i = 0; i < 10; i++)
    {
        ss << hex[rand() % 16];
    }

    return BSFixedString(ss.str().c_str());
}

void _TraceExtraDataList(const char* processId, ExtraDataList* extraDataList, int indent, bool close)
{
    if (!extraDataList)
    {
        return;
    }

    const char* i1 = _MakeIndent(indent);
    const char* i2 = _MakeIndent(indent + 1);
    const char* i3 = _MakeIndent(indent + 2);

    bool showHeader = false;

    BSExtraData* extraData;
    if (extraDataList->HasType(kExtraData_UniqueID))
    {
        extraData = extraDataList->GetByType(kExtraData_UniqueID);
        ExtraUniqueID* uniqueId = DYNAMIC_CAST(extraData, BSExtraData, ExtraUniqueID);
        if (uniqueId)
        {
            if (!showHeader)
            {
                _MESSAGE("| %s | %s[ ExtraDataList ]", processId, i1);
                showHeader = true;
            }

            _MESSAGE("| %s | %s[ ExtraUniqueID ]", processId, i2);
            _MESSAGE("| %s | %sUniqueID  : %08X", processId, i3, uniqueId->uniqueId);
            _MESSAGE("| %s | %sFormOwner : %08X", processId, i3, uniqueId->formOwner);
        }
    }

    if (extraDataList->HasType(kExtraData_Flags))
    {
        extraData = extraDataList->GetByType(kExtraData_Flags);
        ExtraFlags* flags = DYNAMIC_CAST(extraData, BSExtraData, ExtraFlags);
        if (flags)
        {
            if (!showHeader)
            {
                _MESSAGE("| %s | %s[ ExtraDataList ]", processId, i1);
                showHeader = true;
            }

            _MESSAGE("| %s | %s[ ExtraFlags ]", processId, i2);
            _MESSAGE("| %s | %sFlags: [D=%d, B=%s]", processId, i3, flags->flags, _flags2s(flags->flags));
        }
    }

    if (extraDataList->HasType(kExtraData_ObjectInstance))
    {
        extraData = extraDataList->GetByType(kExtraData_ObjectInstance);
        BGSObjectInstanceExtra* objectInstanceData = DYNAMIC_CAST(extraData, BSExtraData, BGSObjectInstanceExtra);
        if (objectInstanceData && objectInstanceData->data && objectInstanceData->data->forms)
        {
            if (!showHeader)
            {
                _MESSAGE("| %s | %s[ ExtraDataList ]", processId, i1);
                showHeader = true;
            }

            _MESSAGE("| %s | %s[ BGSObjectInstanceExtra ]", processId, i2);

            const char* i4 = _MakeIndent(indent + 3);

            int count = 0;
            BGSObjectInstanceExtra::Data* data = objectInstanceData->data;
            for (UInt32 i = 0; i < (data->blockSize / sizeof(BGSObjectInstanceExtra::Data::Form)); i++)
            {
                _MESSAGE("| %s | %s[ InstanceData_%d ]", processId, i3, count);
                _MESSAGE("| %s | %sID    : %08X", processId, i4, data->forms[i].formId);
                count++;
            }
        }
    }

    if (showHeader && close)
    {
        _MESSAGE("| %s | %s[ ============= ]", processId, i1);
    }
}

void _TraceTESForm(const char* processId, TESForm* form, int indent, bool close)
{
    if (!form)
    {
        return;
    }

    const char* i1 = _MakeIndent(indent);
    const char* i2 = _MakeIndent(indent + 1);

    TESFullName* fullName = DYNAMIC_CAST(form, TESForm, TESFullName);

    _MESSAGE("| %s | %s[ TESForm ]", processId, i1);
    _MESSAGE("| %s | %sName  : %s", processId, i2, fullName ? fullName->name.c_str() : form->GetFullName());
    _MESSAGE("| %s | %sID    : %08X", processId, i2, form->formID);
    _MESSAGE("| %s | %sType  : %s", processId, i2, _GetFormTypeIdentify(form->formType));
    _MESSAGE("| %s | %sFlags : [D=%d, B=%s]", processId, i2, form->flags, _flags2s(form->flags));

    if (close)
    {
        _MESSAGE("| %s | %s[ ======= ]", processId, i1);
    }
}

void _TraceTESObjectCELL(const char* processId, TESObjectCELL* cell, int indent, bool close)
{
    if (!cell)
    {
        return;
    }

    const char* i1 = _MakeIndent(indent);
    const char* i2 = _MakeIndent(indent + 1);

    _MESSAGE("| %s | %s[ TESObjectCELL ]", processId, i1);
    _MESSAGE("| %s | %sName  : %s", processId, i2, cell->GetFullName());
    _MESSAGE("| %s | %sID    : %08X", processId, i2, cell->formID);
    _MESSAGE("| %s | %sFlags : [D=%d, B=%s]", processId, i2, cell->flags, _flags2s(cell->flags));

    _TraceExtraDataList(processId, cell->extraDataList, indent + 1);

    if (close)
    {
        _MESSAGE("| %s | %s[ ============= ]", processId, i1);
    }
}

void _TraceTESObjectREFR(const char* processId, TESObjectREFR* ref, int indent, bool close)
{
    if (!ref)
    {
        return;
    }

    const char* i1 = _MakeIndent(indent);
    const char* i2 = _MakeIndent(indent + 1);

    NiPoint3 pos = ref->pos;

    _MESSAGE("| %s | %s[ TESObjectREFR ]", processId, i1);
    _MESSAGE("| %s | %sName  : %s", processId, i2, CALL_MEMBER_FN(ref, GetReferenceName)());
    _MESSAGE("| %s | %sID    : %08X", processId, i2, ref->formID);
    _MESSAGE("| %s | %sPos   : [X=%f, Y=%f, Z=%f]", processId, i2, pos.x, pos.y, pos.z);
    _MESSAGE("| %s | %sFlags : [D=%d, B=%s]", processId, i2, ref->flags, _flags2s(ref->flags));
    //_MESSAGE("| %s | %sHandle: %08X", processId, i2, ref->CreateRefHandle());

    _TraceTESForm(processId, ref->baseForm, indent + 1);

    TESObjectREFR::LoadedData* loadedData = ref->unkF0;
    if (loadedData)
    {
        const char* i3 = _MakeIndent(indent + 2);
        _MESSAGE("| %s | %s[ Loaded Data ]", processId, i2);
        _MESSAGE("| %s | %sFlags : [D=%d, B=%s]", processId, i3, loadedData->flags, _flags2s(loadedData->flags));
    }

    _TraceExtraDataList(processId, ref->extraDataList, indent + 1);

    if (close)
    {
        _MESSAGE("| %s | %s[ ============= ]", processId, i1);
    }
}

void _TraceBGSInventoryItem(const char* processId, BGSInventoryItem* item, int indent, bool close)
{
    if (!item)
    {
        return;
    }

    const char* i1 = _MakeIndent(indent);
    const char* i2 = _MakeIndent(indent + 1);

    _MESSAGE("| %s | %s[ BGSInventoryItem ]", processId, i1);

    _TraceTESForm(processId, item->form, indent + 1);

    const char* i3 = _MakeIndent(indent + 2);

    item->stack->Visit([&](BGSInventoryItem::Stack* stack)
    {
        _MESSAGE("| %s | %s[ Stack ]", processId, i2);
        _MESSAGE("| %s | %sCount    : %d", processId, i3, stack->count);
        _MESSAGE("| %s | %sRefCount : %d", processId, i3, stack->m_refCount);
        _MESSAGE("| %s | %sFlags    : [D=%d, B=%s]", processId, i3, stack->flags, _flags2s(stack->flags));

        _TraceExtraDataList(processId, stack->extraData, indent + 2);

        return true;
    });

    if (close)
    {
        _MESSAGE("| %s | %s[ ================ ]", processId, i1);
    }
}

std::map<UInt32, UInt32> _referenceIdByFlags;

void _TraceReferenceFlags(const char* processId, TESObjectREFR* ref, int indent, bool close)
{
    if (!ref)
    {
        return;
    }

    UInt32 referenceId = ref->formID;
    UInt32 flags = ref->flags;

    if (_referenceIdByFlags[referenceId] == flags)
    {
        return;
    }

    _referenceIdByFlags[referenceId] = flags;

    _MESSAGE("| %s | %s{ Flags has been updated }", processId, _MakeIndent(indent));
}

#endif
