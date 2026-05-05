#include "papyrus_lootman_internal.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>

#include "log_settings.h"
#include "properties.h"

namespace papyrus_lootman
{
	using namespace RE;

	constexpr std::int32_t kDefaultPapyrusLogLevel = static_cast<std::int32_t>(spdlog::level::info);
	constexpr std::int32_t kMinPapyrusLogLevel = static_cast<std::int32_t>(spdlog::level::trace);
	constexpr std::int32_t kMaxPapyrusLogLevel = static_cast<std::int32_t>(spdlog::level::off);

	const char* GetFormEditorIDOrEmpty(const TESForm* form)
	{
		auto* mutableForm = const_cast<TESForm*>(form);
		auto* editorID = mutableForm ? mutableForm->GetFormEditorID() : nullptr;
		return editorID ? editorID : "";
	}

	std::int32_t NormalizePapyrusLogLevel(const std::int32_t logLevel)
	{
		return logLevel >= kMinPapyrusLogLevel && logLevel <= kMaxPapyrusLogLevel
			? logLevel
			: kDefaultPapyrusLogLevel;
	}

	std::string SanitizeLogToken(const char* value, std::string fallback)
	{
		std::string result;
		if (value)
		{
			for (auto* cursor = value; *cursor != '\0'; ++cursor)
			{
				const auto ch = static_cast<unsigned char>(*cursor);
				if (std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.')
				{
					result.push_back(static_cast<char>(ch));
				}
				else if (std::isspace(ch))
				{
					if (!result.empty() && result.back() != '_')
					{
						result.push_back('_');
					}
				}
				else if (!result.empty() && result.back() != '_')
				{
					result.push_back('_');
				}
			}
		}

		while (!result.empty() && result.back() == '_')
		{
			result.pop_back();
		}
		return result.empty() ? fallback : result;
	}

	std::string SanitizeLogFields(const char* value)
	{
		std::string result;
		if (value)
		{
			bool lastWasSpace = false;
			for (auto* cursor = value; *cursor != '\0'; ++cursor)
			{
				const auto ch = static_cast<unsigned char>(*cursor);
				const auto normalized = std::iscntrl(ch) ? ' ' : static_cast<char>(ch);
				if (std::isspace(static_cast<unsigned char>(normalized)))
				{
					if (!result.empty() && !lastWasSpace)
					{
						result.push_back(' ');
					}
					lastWasSpace = true;
				}
				else
				{
					result.push_back(normalized);
					lastWasSpace = false;
				}
			}
		}

		while (!result.empty() && result.back() == ' ')
		{
			result.pop_back();
		}
		return result;
	}

	void LogPapyrusEvent(
		const char* component,
		const char* eventName,
		const char* fields,
		const std::int32_t logLevel)
	{
		const auto normalizedLogLevel = NormalizePapyrusLogLevel(logLevel);
		if (normalizedLogLevel == static_cast<std::int32_t>(spdlog::level::off))
		{
			return;
		}

		auto message = std::string("[Papyrus] source=papyrus component=") +
			SanitizeLogToken(component, "unknown") +
			" event=" +
			SanitizeLogToken(eventName, "unknown");
		auto fieldText = SanitizeLogFields(fields);
		if (!fieldText.empty())
		{
			message.push_back(' ');
			message += fieldText;
		}

		auto* logger = spdlog::default_logger_raw();
		if (logger)
		{
			logger->log(
				spdlog::source_loc{},
				static_cast<spdlog::level::level_enum>(normalizedLogLevel),
				"{}",
				message);
		}
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
		LogPapyrusEvent("raw", "message", message.c_str(), kDefaultPapyrusLogLevel);
	}

	void LogEvent(
		std::monostate,
		BSFixedString component,
		BSFixedString eventName,
		BSFixedString fields,
		std::int32_t logLevel)
	{
		LogPapyrusEvent(component.c_str(), eventName.c_str(), fields.c_str(), logLevel);
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
