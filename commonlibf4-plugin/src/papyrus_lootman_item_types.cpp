#include "papyrus_lootman_internal.h"

#include <cstdint>

namespace papyrus_lootman
{
	using namespace RE;

	bool UsesWorldReferenceTransfer(ENUM_FORM_ID formType)
	{
		return formType == ENUM_FORM_ID::kALCH ||
		       formType == ENUM_FORM_ID::kAMMO ||
		       formType == ENUM_FORM_ID::kARMO ||
		       formType == ENUM_FORM_ID::kBOOK ||
		       formType == ENUM_FORM_ID::kINGR ||
		       formType == ENUM_FORM_ID::kKEYM ||
		       formType == ENUM_FORM_ID::kMISC ||
		       formType == ENUM_FORM_ID::kWEAP;
	}

	bool IsFormTypeMatch(ENUM_FORM_ID formType, ENUM_FORM_ID matchingType)
	{
		if (formType == matchingType) return true;
		if (matchingType == ENUM_FORM_ID::kTotal)
		{
			return formType == ENUM_FORM_ID::kACTI || formType == ENUM_FORM_ID::kALCH
			    || formType == ENUM_FORM_ID::kAMMO || formType == ENUM_FORM_ID::kARMO
			    || formType == ENUM_FORM_ID::kBOOK || formType == ENUM_FORM_ID::kCONT
			    || formType == ENUM_FORM_ID::kFLOR || formType == ENUM_FORM_ID::kINGR
			    || formType == ENUM_FORM_ID::kKEYM || formType == ENUM_FORM_ID::kMISC
			    || formType == ENUM_FORM_ID::kNPC_ || formType == ENUM_FORM_ID::kWEAP;
		}
		return false;
	}

	bool IsItemTypeMatch(std::uint32_t itemType, std::uint32_t matchingType)
	{
		return (itemType & matchingType) == matchingType;
	}

	bool IsFormTypeMatchesItemType(ENUM_FORM_ID formType, std::uint32_t itemType)
	{
		return (formType == ENUM_FORM_ID::kALCH && IsItemTypeMatch(itemType, alch))
		    || (formType == ENUM_FORM_ID::kAMMO && IsItemTypeMatch(itemType, ammo))
		    || (formType == ENUM_FORM_ID::kARMO && IsItemTypeMatch(itemType, armo))
		    || (formType == ENUM_FORM_ID::kBOOK && IsItemTypeMatch(itemType, book))
		    || (formType == ENUM_FORM_ID::kINGR && IsItemTypeMatch(itemType, ingr))
		    || (formType == ENUM_FORM_ID::kKEYM && IsItemTypeMatch(itemType, keym))
		    || (formType == ENUM_FORM_ID::kMISC && IsItemTypeMatch(itemType, misc))
		    || (formType == ENUM_FORM_ID::kWEAP && IsItemTypeMatch(itemType, weap));
	}

	enum EnabledFormTypeBits : std::uint32_t
	{
		enable_ACTI = 1u << 0,
		enable_ALCH = 1u << 1,
		enable_AMMO = 1u << 2,
		enable_ARMO = 1u << 3,
		enable_BOOK = 1u << 4,
		enable_CONT = 1u << 5,
		enable_FLOR = 1u << 6,
		enable_INGR = 1u << 7,
		enable_KEYM = 1u << 8,
		enable_MISC = 1u << 9,
		enable_NPC_ = 1u << 10,
		enable_WEAP = 1u << 11,
	};

	std::uint32_t FormTypeToEnabledBit(ENUM_FORM_ID formType)
	{
		switch (formType)
		{
		case ENUM_FORM_ID::kACTI:
			return enable_ACTI;
		case ENUM_FORM_ID::kALCH:
			return enable_ALCH;
		case ENUM_FORM_ID::kAMMO:
			return enable_AMMO;
		case ENUM_FORM_ID::kARMO:
			return enable_ARMO;
		case ENUM_FORM_ID::kBOOK:
			return enable_BOOK;
		case ENUM_FORM_ID::kCONT:
			return enable_CONT;
		case ENUM_FORM_ID::kFLOR:
			return enable_FLOR;
		case ENUM_FORM_ID::kINGR:
			return enable_INGR;
		case ENUM_FORM_ID::kKEYM:
			return enable_KEYM;
		case ENUM_FORM_ID::kMISC:
			return enable_MISC;
		case ENUM_FORM_ID::kNPC_:
			return enable_NPC_;
		case ENUM_FORM_ID::kWEAP:
			return enable_WEAP;
		default:
			return 0;
		}
	}

	std::int32_t FormTypeToBucketIndex(ENUM_FORM_ID formType)
	{
		switch (formType)
		{
		case ENUM_FORM_ID::kACTI:
			return 0;
		case ENUM_FORM_ID::kALCH:
			return 1;
		case ENUM_FORM_ID::kAMMO:
			return 2;
		case ENUM_FORM_ID::kARMO:
			return 3;
		case ENUM_FORM_ID::kBOOK:
			return 4;
		case ENUM_FORM_ID::kCONT:
			return 5;
		case ENUM_FORM_ID::kFLOR:
			return 6;
		case ENUM_FORM_ID::kINGR:
			return 7;
		case ENUM_FORM_ID::kKEYM:
			return 8;
		case ENUM_FORM_ID::kMISC:
			return 9;
		case ENUM_FORM_ID::kNPC_:
			return 10;
		case ENUM_FORM_ID::kWEAP:
			return 11;
		default:
			return -1;
		}
	}
}
