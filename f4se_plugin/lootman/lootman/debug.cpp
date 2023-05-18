#include "debug.hpp"

#include <iomanip>
#include <random>
#include <sstream>

#include "f4se/GameExtraData.h"
#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"

namespace debug
{
    std::string FormId2HexS(const UInt32 id)
    {
        std::stringstream ss;
        ss << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << id;
        return ss.str();
    }

    // Generate and return a random process id
    // Pages used for reference: https://stackoverflow.com/questions/12110209/how-to-fill-a-string-with-random-hex-characters#answer-12110369
    const char* GetRandomProcessId()
    {
        std::random_device rand;
        std::stringstream ss;

        for (int i = 0; i < 10; ++i)
        {
            const auto hex = "0123456789ABCDEF";
            ss << hex[rand() % 16];
        }

        return BSFixedString(ss.str().c_str());
    }

    const char* GetFormTypeIdentifier(const UInt8 formType)
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

    const char* GetItemTypeIdentifier(const UInt32 itemType)
    {
        std::vector<std::string> identifies;
        if ((itemType >> 0) & 1) identifies.emplace_back(GetFormTypeIdentifier(kFormType_ALCH));
        if ((itemType >> 1) & 1) identifies.emplace_back(GetFormTypeIdentifier(kFormType_AMMO));
        if ((itemType >> 2) & 1) identifies.emplace_back(GetFormTypeIdentifier(kFormType_ARMO));
        if ((itemType >> 3) & 1) identifies.emplace_back(GetFormTypeIdentifier(kFormType_BOOK));
        if ((itemType >> 4) & 1) identifies.emplace_back(GetFormTypeIdentifier(kFormType_INGR));
        if ((itemType >> 5) & 1) identifies.emplace_back(GetFormTypeIdentifier(kFormType_KEYM));
        if ((itemType >> 6) & 1) identifies.emplace_back(GetFormTypeIdentifier(kFormType_MISC));
        if ((itemType >> 7) & 1) identifies.emplace_back(GetFormTypeIdentifier(kFormType_WEAP));

        auto it = identifies.begin();
        if (it == identifies.end())
        {
            return "";
        }

        std::string result = *it;
        for (++it; it != identifies.end(); ++it)
        {
            result += ", " + *it;
        }
        return BSFixedString(result.c_str());
    }

    const char* GetDisplayName(TESForm* form, ExtraDataList* extraDataList)
    {
        if (form)
        {
            if (extraDataList)
            {
                BSExtraData* extraData = extraDataList->GetByType(ExtraDataType::kExtraData_TextDisplayData);
                if (extraData)
                {
                    const auto displayText = DYNAMIC_CAST(extraData, BSExtraData, ExtraTextDisplayData);
                    if (displayText)
                    {
                        return *CALL_MEMBER_FN(displayText, GetReferenceName)(form);
                    }
                }
            }

            const auto fullName = DYNAMIC_CAST(form, TESForm, TESFullName);
            if (fullName)
            {
                return fullName->name;
            }
        }

        return BSFixedString();
    }

    const char* GetName(TESForm* form)
    {
        if (form)
        {
            const auto fullName = DYNAMIC_CAST(form, TESForm, TESFullName);
            if (fullName && strlen(fullName->name))
            {
                return fullName->name;
            }
            return form->GetEditorID();
        }

        return BSFixedString();
    }

    const char* Form2S(TESForm* form)
    {
        if (!form)
        {
            return "TESForm: NULL";
        }

        std::stringstream ss;

        ss << "TESForm: [ Name: \"" << GetName(form)
        << "\", Id: " << FormId2HexS(form->formID)
        << ", Type: " << GetFormTypeIdentifier(form->formType)
        << ", Flags: " << Flag2S(form->flags)
        << " ]";

        return BSFixedString(ss.str().c_str());
    }

    const char* Cell2S(TESObjectCELL* cell)
    {
        if (!cell)
        {
            return "TESObjectCELL: NULL";
        }

        std::stringstream ss;

        ss << "TESObjectCELL: [ Name: \"" << GetName(cell)
        << "\", Id: " << FormId2HexS(cell->formID)
        << ", State: " << (cell->unk44 & 0xFF)
        << ", Flags: " << Flag2S(cell->flags)
        << " ]";

        return BSFixedString(ss.str().c_str());
    }

    const char* Ref2S(TESObjectREFR* ref)
    {
        if (!ref)
        {
            return "TESObjectREFR: NULL";
        }

        const NiPoint3 pos = ref->pos;

        std::stringstream ss;

        ss << "TESObjectREFR: [ Name: \"" << CALL_MEMBER_FN(ref, GetReferenceName)()
        << "\", Id: " << FormId2HexS(ref->formID)
        << ", Pos: [ X: " << pos.x << ", Y: " << pos.y << ", Z: " << pos.z
        << " ], Flags: " << Flag2S(ref->flags)
        << " ]";

        return BSFixedString(ss.str().c_str());
    }

    const char* InvItem2S(BGSInventoryItem* item)
    {
        if (!item)
        {
            return "BGSInventoryItem: NULL";
        }

        UInt32 count = 0;
        // ReSharper disable once CppMsExtBindingRValueToLvalueReference
        item->stack->Visit([&count](const BGSInventoryItem::Stack* stack)
        {
            count += stack->count;
            return true;
        });

        std::stringstream ss;

        ss << "BGSInventoryItem: [ " << Form2S(item->form);
        ss << ", Count: " << count;
        ss << " ]";

        return BSFixedString(ss.str().c_str());
    }
}
