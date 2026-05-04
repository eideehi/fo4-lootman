#include "papyrus_lootman_internal.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "log_settings.h"
#include "properties.h"

namespace papyrus_lootman
{
	using namespace RE;

	const char* GetFormEditorIDOrEmpty(const TESForm* form)
	{
		auto* mutableForm = const_cast<TESForm*>(form);
		auto* editorID = mutableForm ? mutableForm->GetFormEditorID() : nullptr;
		return editorID ? editorID : "";
	}

	std::string FormatFormId(TESForm* form)
	{
		if (!form)
		{
			return "None";
		}

		char buffer[9]{};
		std::snprintf(buffer, sizeof(buffer), "%08X", form->formID);
		return buffer;
	}

	std::string GetFormTypeName(ENUM_FORM_ID formType)
	{
		switch (formType)
		{
		case ENUM_FORM_ID::kNONE:
			return "NONE";
		case ENUM_FORM_ID::kACTI:
			return "ACTI";
		case ENUM_FORM_ID::kALCH:
			return "ALCH";
		case ENUM_FORM_ID::kAMMO:
			return "AMMO";
		case ENUM_FORM_ID::kARMO:
			return "ARMO";
		case ENUM_FORM_ID::kBOOK:
			return "BOOK";
		case ENUM_FORM_ID::kCELL:
			return "CELL";
		case ENUM_FORM_ID::kCMPO:
			return "CMPO";
		case ENUM_FORM_ID::kCONT:
			return "CONT";
		case ENUM_FORM_ID::kFACT:
			return "FACT";
		case ENUM_FORM_ID::kINGR:
			return "INGR";
		case ENUM_FORM_ID::kKEYM:
			return "KEYM";
		case ENUM_FORM_ID::kKYWD:
			return "KYWD";
		case ENUM_FORM_ID::kLCTN:
			return "LCTN";
		case ENUM_FORM_ID::kMISC:
			return "MISC";
		case ENUM_FORM_ID::kNPC_:
			return "NPC_";
		case ENUM_FORM_ID::kPERK:
			return "PERK";
		case ENUM_FORM_ID::kQUST:
			return "QUST";
		case ENUM_FORM_ID::kREFR:
			return "REFR";
		case ENUM_FORM_ID::kWEAP:
			return "WEAP";
		default:
			return std::to_string(static_cast<std::uint32_t>(formType));
		}
	}

	std::string GetFormName(TESForm* form)
	{
		if (!form)
		{
			return "";
		}

		if (auto* ref = form->As<TESObjectREFR>())
		{
			if (auto* displayName = ref->GetDisplayFullName(); displayName && displayName[0] != '\0')
			{
				return displayName;
			}

			if (auto* baseForm = ref->GetObjectReference())
			{
				const auto baseName = TESFullName::GetFullName(*baseForm);
				if (!baseName.empty())
				{
					return std::string(baseName);
				}
			}
		}

		const auto fullName = TESFullName::GetFullName(*form);
		if (!fullName.empty())
		{
			return std::string(fullName);
		}

		auto* editorID = GetFormEditorIDOrEmpty(form);
		return editorID ? editorID : "";
	}

	std::uint32_t GetFormType(std::monostate, TESForm* form)
	{
		return !form ? static_cast<std::uint32_t>(ENUM_FORM_ID::kNONE) : static_cast<std::uint32_t>(form->GetFormType());
	}

	void Log(std::monostate, BSFixedString message)
	{
		REX::INFO("[Papyrus] {}", message.c_str());
	}

	std::int32_t GetLogLevel(std::monostate)
	{
		return log_settings::GetLogLevel();
	}

	void SetLogLevel(std::monostate, std::int32_t logLevel)
	{
		log_settings::SetLogLevel(logLevel);
	}

	std::string GetFormTypeIdentifier(std::monostate, TESForm* form)
	{
		if (!form)
		{
			return GetFormTypeName(ENUM_FORM_ID::kNONE);
		}

		return GetFormTypeName(form->GetFormType());
	}

	std::string GetHexID(std::monostate, TESForm* form)
	{
		return FormatFormId(form);
	}

	std::string GetName(std::monostate, TESForm* form)
	{
		return GetFormName(form);
	}

	bool IsFormTypeEquals(std::monostate, TESForm* form, std::uint32_t formType)
	{
		return form && static_cast<std::uint32_t>(form->GetFormType()) == formType;
	}

	void OnUpdateLootManProperty(std::monostate, BSFixedString propertyName)
	{
		properties::Update(propertyName.c_str());
	}
}
