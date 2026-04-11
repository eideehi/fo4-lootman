#include "injection_data.h"
#include "utility.h"

#include <cctype>

namespace injection_data
{
	struct DataInfo
	{
		const char* path;
		Key key;
		std::uint32_t type;
	};

	// Declarative map from JSON path -> runtime key and accepted resolved form types.
	const DataInfo info_list[] = {
		{"/include/activation-block", include_activation_block, Type::kForm | Type::kKeyword},
		{"/include/activator", include_activator, Type::kForm | Type::kKeyword},
		{"/include/featured-item", include_featured_item, Type::kForm | Type::kKeyword},
		{"/include/quest-item", include_quest_item, Type::kForm | Type::kKeyword},
		{"/include/unique-item", include_unique_item, Type::kForm | Type::kKeyword},
		{"/exclude/form", exclude_form, Type::kForm},
		{"/exclude/keyword", exclude_keyword, Type::kKeyword},
		{"/alch-type/alcohol", alch_type_alcohol, Type::kForm | Type::kKeyword},
		{"/alch-type/chemistry", alch_type_chemistry, Type::kForm | Type::kKeyword},
		{"/alch-type/food", alch_type_food, Type::kForm | Type::kKeyword},
		{"/alch-type/nuka-cola", alch_type_nuka_cola, Type::kForm | Type::kKeyword},
		{"/alch-type/stimpak", alch_type_stimpak, Type::kForm | Type::kKeyword},
		{"/alch-type/syringe-ammo", alch_type_syringe_ammo, Type::kForm | Type::kKeyword},
		{"/alch-type/water", alch_type_water, Type::kForm | Type::kKeyword},
		{"/book-type/perk-magazine", book_type_perk_magazine, Type::kForm | Type::kKeyword},
		{"/book-type/park-magazine", book_type_perk_magazine, Type::kForm | Type::kKeyword},  // legacy typo
		{"/misc-type/bobblehead", misc_type_bobblehead, Type::kForm | Type::kKeyword},
		{"/weap-type/grenade", weap_type_grenade, Type::kForm | Type::kKeyword},
		{"/weap-type/mine", weap_type_mine, Type::kForm | Type::kKeyword},
	};

	// Temporary "ModName|FormID" strings loaded from JSON files before runtime resolution.
	std::unordered_map<std::string, std::unordered_set<std::string>> tmp;
	std::unordered_map<Key, std::unordered_set<Value>> data;
	std::unordered_map<Key, std::vector<RE::TESForm*>> formListByKey;
	std::unordered_map<Key, std::vector<RE::BGSKeyword*>> keywordListByKey;
	std::unordered_map<Key, std::vector<RE::BGSLocationRefType*>> locationRefTypeListByKey;
	std::unordered_map<Key, std::unordered_set<RE::TESFormID>> formIDSetByKey;
	// True when some sources fail to load/parse, while still allowing best-effort operation.
	bool degradedMode = false;

	const std::vector<RE::TESForm*> emptyFormList;
	const std::vector<RE::BGSKeyword*> emptyKeywordList;
	const std::vector<RE::BGSLocationRefType*> emptyLocationRefTypeList;
	const std::unordered_set<RE::TESFormID> emptyFormIDSet;

	RE::TESForm* ValueAsForm(const Value& value)
	{
		if (value.IsForm()) return value.data.form;
		if (value.IsKeyword()) return value.data.keyword;
		if (value.IsLocationRefType()) return value.data.location_ref_type;
		return nullptr;
	}

	RE::BGSKeyword* ValueAsKeyword(const Value& value)
	{
		if (value.IsKeyword()) return value.data.keyword;
		if (value.IsLocationRefType() && value.data.location_ref_type) return value.data.location_ref_type->As<RE::BGSKeyword>();
		return nullptr;
	}

	RE::BGSLocationRefType* ValueAsLocationRefType(const Value& value)
	{
		if (value.IsLocationRefType()) return value.data.location_ref_type;
		return nullptr;
	}

	Value Get(const Key key)
	{
		const auto it = data.find(key);
		if (it == data.end()) return Value();
		const auto& values = it->second;
		auto vit = values.begin();
		return vit == values.end() ? Value() : *vit;
	}

	std::vector<Value> GetList(const Key key)
	{
		std::vector<Value> result;
		auto it = data.find(key);
		if (it != data.end())
		{
			result.insert(result.end(), it->second.begin(), it->second.end());
		}
		return result;
	}

	RE::TESForm* GetAsForm(const Key key) { return ValueAsForm(Get(key)); }
	RE::BGSKeyword* GetAsKeyword(const Key key) { return ValueAsKeyword(Get(key)); }
	RE::BGSLocationRefType* GetAsLocationRefType(const Key key) { return ValueAsLocationRefType(Get(key)); }

	std::vector<RE::TESForm*> GetAsFormList(const Key key)
	{
		return GetAsFormListRef(key);
	}

	std::vector<RE::BGSKeyword*> GetAsKeywordList(const Key key)
	{
		return GetAsKeywordListRef(key);
	}

	std::vector<RE::BGSLocationRefType*> GetAsLocationRefTypeList(const Key key)
	{
		return GetAsLocationRefTypeListRef(key);
	}

	const std::vector<RE::TESForm*>& GetAsFormListRef(const Key key)
	{
		const auto it = formListByKey.find(key);
		return it == formListByKey.end() ? emptyFormList : it->second;
	}

	const std::vector<RE::BGSKeyword*>& GetAsKeywordListRef(const Key key)
	{
		return GetKeywordListRef(key);
	}

	const std::vector<RE::BGSLocationRefType*>& GetAsLocationRefTypeListRef(const Key key)
	{
		const auto it = locationRefTypeListByKey.find(key);
		return it == locationRefTypeListByKey.end() ? emptyLocationRefTypeList : it->second;
	}

	const std::unordered_set<RE::TESFormID>& GetFormIDSet(const Key key)
	{
		const auto it = formIDSetByKey.find(key);
		return it == formIDSetByKey.end() ? emptyFormIDSet : it->second;
	}

	const std::vector<RE::BGSKeyword*>& GetKeywordListRef(const Key key)
	{
		const auto it = keywordListByKey.find(key);
		return it == keywordListByKey.end() ? emptyKeywordList : it->second;
	}

	bool Initialize()
	{
		REX::DEBUG("[ Start initialization of injection data ]");
		degradedMode = false;
		tmp.clear();

		wchar_t modulePath[REX::W32::MAX_PATH]{};
		REX::W32::GetModuleFileNameW(nullptr, modulePath, REX::W32::MAX_PATH);
		std::filesystem::path dir = std::filesystem::path(modulePath).parent_path() / "DATA" / "LootMan";

		if (!std::filesystem::exists(dir))
		{
			REX::ERROR("Couldn't get the directory for data injection: \"{}\"", dir.string());
			return false;
		}

		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(dir))
		{
			const auto& file = entry.path();
			if (!entry.is_regular_file()) continue;

			auto ext = file.extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (ext == ".json")
			{
				files.push_back(file);
			}
		}

		// Stable ordering keeps override behavior deterministic across runs.
		std::sort(files.begin(), files.end());

		for (const auto& file : files)
		{
			std::ifstream ifs(file);
			if (!ifs.is_open())
			{
				degradedMode = true;
				REX::WARN("Failed to open file: \"{}\"", file.string());
				continue;
			}

			nlohmann::json src;
			try
			{
				src = nlohmann::json::parse(ifs);
			}
			catch (const nlohmann::json::parse_error& e)
			{
				REX::ERROR("Json parse error in \"{}\": {}", file.string(), e.what());
				return false;
			}

			for (const auto& info : info_list)
			{
				auto path = std::string(info.path);
				nlohmann::json::json_pointer ptr(path);

				if (!src.contains(ptr))
				{
					continue;
				}

				auto& value = src[ptr];

				auto [tmpIt, inserted] = tmp.try_emplace(path);
				(void)inserted;
				auto& values = tmpIt->second;

				if (value.is_array())
				{
					for (const auto& item : value)
					{
						if (!item.is_string())
						{
							degradedMode = true;
							REX::WARN("Non-string entry ignored in {} from {}", path, file.string());
							continue;
						}
						values.emplace(item.get<std::string>());
					}
				}
				else if (value.is_string())
				{
					// Scalar values are explicit overrides for this path.
					values.clear();
					values.emplace(value.get<std::string>());
				}
				else
				{
					degradedMode = true;
					tmp.erase(tmpIt);
					REX::WARN("Entry type is illegal and ignored: {}", value.dump());
				}
			}
		}

		REX::DEBUG("  Initialization of injection data is complete (degraded_mode={})", degradedMode);
		return true;
	}

	void LoadInjectionData()
	{
		REX::DEBUG("[ Start loading injection data ]");

		for (const auto& info : info_list)
		{
			auto key = info.key;

			if (data.find(key) == data.end())
			{
				data.emplace(key, std::unordered_set<Value>());
			}

			auto it = tmp.find(info.path);
			if (it == tmp.end())
			{
				continue;
			}

			for (const auto& dataId : it->second)
			{
				RE::TESForm* form = utility::LookupForm(dataId);
				if (!form)
				{
					REX::WARN("\"{}\" is not found", dataId);
					continue;
				}

				if (info.type & Type::kLocationRefType)
				{
					auto locationRefType = form->As<RE::BGSLocationRefType>();
					if (locationRefType)
					{
						Value value;
						value.type = Type::kLocationRefType;
						value.data.location_ref_type = locationRefType;
						if (data[key].emplace(value).second)
						{
							locationRefTypeListByKey[key].push_back(locationRefType);
						}
						continue;
					}
				}

				if (info.type & Type::kKeyword)
				{
					auto kw = form->As<RE::BGSKeyword>();
					if (kw)
					{
						Value value;
						value.type = Type::kKeyword;
						value.data.keyword = kw;
						if (data[key].emplace(value).second)
						{
							keywordListByKey[key].push_back(kw);
						}
						continue;
					}
				}

				if ((info.type & Type::kForm) == 0)
				{
					REX::WARN("\"{}\" is illegal form type", dataId);
					continue;
				}

				Value value;
				value.type = Type::kForm;
				value.data.form = form;
				if (data[key].emplace(value).second)
				{
					formListByKey[key].push_back(form);
					formIDSetByKey[key].emplace(form->formID);
				}
			}
		}

		REX::DEBUG("  Loading of injection data is complete");
	}
}
