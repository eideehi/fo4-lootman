#include "injection_data.h"
#include "utility.h"

#include <cctype>
#include <memory>
#include <shared_mutex>
#include <string_view>

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
		{"/include/legendary-only-exception", include_legendary_only_exception, Type::kForm | Type::kKeyword},
		{"/exclude/form", exclude_form, Type::kForm},
		{"/exclude/keyword", exclude_keyword, Type::kKeyword},
		{"/notify/item", notify_item, Type::kForm | Type::kKeyword},
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

	// Reusable named lists from the top-level "lists" object. Entries may be "ModName|FormID"
	// strings or "$list:<name>" references; they are expanded into tmp after all files load and
	// before form/keyword resolution, so LoadInjectionData only ever sees concrete identifiers.
	std::unordered_map<std::string, std::unordered_set<std::string>> lists;

	// Runtime-resolved injection data. LoadInjectionData rebuilds it on every kGameLoaded (including
	// mid-session save loads), but the loot/validation hot path reads it from VM worker threads. Because the
	// list/set accessors hand out a reference that outlives any internal lock, a shared_mutex find() cannot
	// make them safe; instead the resolved maps live in one immutable snapshot published behind a shared_ptr.
	// A reader copies the current snapshot pointer under a shared_lock, and a Ref accessor returns a
	// snapshot-co-owning shared_ptr, so a rebuild swap can never clear()/rehash a container a worker is still
	// iterating (data race / use-after-free / CTD).
	struct ResolvedData
	{
		std::unordered_map<Key, std::unordered_set<Value>> data;
		std::unordered_map<Key, std::vector<RE::TESForm*>> formListByKey;
		std::unordered_map<Key, std::vector<RE::BGSKeyword*>> keywordListByKey;
		std::unordered_map<Key, std::vector<RE::BGSLocationRefType*>> locationRefTypeListByKey;
		std::unordered_map<Key, std::unordered_set<RE::TESFormID>> formIDSetByKey;
	};

	std::shared_mutex resolvedDataMutex;
	std::shared_ptr<const ResolvedData> resolvedData = std::make_shared<const ResolvedData>();

	std::shared_ptr<const ResolvedData> CurrentResolvedData()
	{
		std::shared_lock<std::shared_mutex> guard(resolvedDataMutex);
		return resolvedData;
	}
	std::uint32_t notifyCategoryMask = 0;
	bool notifyLegendaryEquipment = false;
	// True when some sources fail to load/parse, while still allowing best-effort operation.
	bool degradedMode = false;

	const std::vector<RE::TESForm*> emptyFormList;
	const std::vector<RE::BGSKeyword*> emptyKeywordList;
	const std::vector<RE::BGSLocationRefType*> emptyLocationRefTypeList;
	const std::unordered_set<RE::TESFormID> emptyFormIDSet;
	constexpr std::string_view kNotifyCategoryPath = "/notify/category"sv;
	constexpr std::string_view kNotifyLegendaryEquipmentPath = "/notify/legendary-equipment"sv;
	constexpr std::string_view kListsKey = "lists"sv;
	constexpr std::string_view kListRefPrefix = "$list:"sv;

	// Named lists accept identifier characters only, matching the documented grammar.
	bool IsValidListName(std::string_view name)
	{
		if (name.empty()) return false;
		for (const unsigned char c : name)
		{
			if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
		}
		return true;
	}

	bool IsListRef(const std::string& value)
	{
		return value.compare(0, kListRefPrefix.size(), kListRefPrefix) == 0;
	}

	std::string ListRefName(const std::string& value)
	{
		return value.substr(kListRefPrefix.size());
	}

	// Resolve one named list into concrete (non-reference) identifiers, following nested
	// "$list:<name>" references. Missing, malformed, or cyclic references degrade and are skipped
	// like other invalid injection-data entries, without recursing forever.
	void ExpandListInto(const std::string& name, std::unordered_set<std::string>& out,
		std::unordered_set<std::string>& active)
	{
		if (!IsValidListName(name))
		{
			degradedMode = true;
			REX::WARN(
				"source=native component=injection_data event=list_reference_invalid reason=malformed_name name=\"{}\"",
				utility::SanitizeLogText(name));
			return;
		}

		if (!active.emplace(name).second)
		{
			degradedMode = true;
			REX::WARN(
				"source=native component=injection_data event=list_reference_invalid reason=cycle name=\"{}\"",
				utility::SanitizeLogText(name));
			return;
		}

		const auto it = lists.find(name);
		if (it == lists.end())
		{
			degradedMode = true;
			REX::WARN(
				"source=native component=injection_data event=list_reference_invalid reason=not_found name=\"{}\"",
				utility::SanitizeLogText(name));
			active.erase(name);
			return;
		}

		for (const auto& entry : it->second)
		{
			if (IsListRef(entry))
			{
				ExpandListInto(ListRefName(entry), out, active);
			}
			else
			{
				out.emplace(entry);
			}
		}

		active.erase(name);
	}

	// Replace every "$list:<name>" reference in tmp with the referenced list's concrete entries.
	// Runs after all sorted files are loaded and before form/keyword resolution.
	void ExpandListReferences()
	{
		for (auto& [path, entries] : tmp)
		{
			bool hasRef = false;
			for (const auto& entry : entries)
			{
				if (IsListRef(entry))
				{
					hasRef = true;
					break;
				}
			}
			if (!hasRef) continue;

			std::unordered_set<std::string> expanded;
			for (const auto& entry : entries)
			{
				if (IsListRef(entry))
				{
					std::unordered_set<std::string> active;
					ExpandListInto(ListRefName(entry), expanded, active);
				}
				else
				{
					expanded.emplace(entry);
				}
			}
			entries = std::move(expanded);
		}
	}

	// Merge a file's top-level "lists" object into the reusable named lists. Array members merge
	// across sorted files; a scalar string member replaces that list, matching path semantics.
	void LoadNamedLists(const nlohmann::json& value, const std::filesystem::path& file)
	{
		if (!value.is_object())
		{
			degradedMode = true;
			REX::WARN(
				"source=native component=injection_data event=config_entry_invalid path=/{} file=\"{}\" reason=invalid_type",
				kListsKey,
				file.string());
			return;
		}

		for (const auto& item : value.items())
		{
			const auto& name = item.key();
			const auto& entries = item.value();

			if (!IsValidListName(name))
			{
				degradedMode = true;
				REX::WARN(
					"source=native component=injection_data event=list_definition_invalid reason=malformed_name file=\"{}\" name=\"{}\"",
					file.string(),
					utility::SanitizeLogText(name));
				continue;
			}

			auto [listIt, inserted] = lists.try_emplace(name);
			(void)inserted;
			auto& values = listIt->second;

			if (entries.is_array())
			{
				for (const auto& element : entries)
				{
					if (!element.is_string())
					{
						degradedMode = true;
						REX::WARN(
							"source=native component=injection_data event=list_definition_invalid reason=non_string file=\"{}\" name=\"{}\"",
							file.string(),
							utility::SanitizeLogText(name));
						continue;
					}
					values.emplace(element.get<std::string>());
				}
			}
			else if (entries.is_string())
			{
				// Scalar values are explicit overrides for this list.
				values.clear();
				values.emplace(entries.get<std::string>());
			}
			else
			{
				degradedMode = true;
				REX::WARN(
					"source=native component=injection_data event=list_definition_invalid reason=invalid_type file=\"{}\" name=\"{}\"",
					file.string(),
					utility::SanitizeLogText(name));
			}
		}
	}

	std::string NormalizeNotifyCategoryName(std::string value)
	{
		const auto first = value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos)
		{
			return {};
		}
		const auto last = value.find_last_not_of(" \t\r\n");
		value = value.substr(first, last - first + 1);
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::toupper(c));
		});
		return value;
	}

	std::uint32_t GetNotifyCategoryBit(std::string value)
	{
		value = NormalizeNotifyCategoryName(std::move(value));
		if (value == "ALCH") return notify_alch;
		if (value == "AMMO") return notify_ammo;
		if (value == "ARMO") return notify_armo;
		if (value == "BOOK") return notify_book;
		if (value == "INGR") return notify_ingr;
		if (value == "KEYM") return notify_keym;
		if (value == "MISC") return notify_misc;
		if (value == "WEAP") return notify_weap;
		return 0;
	}

	void LoadNotifyCategory(const nlohmann::json& value, const std::filesystem::path& file)
	{
		std::uint32_t mask = 0;

		if (value.is_array())
		{
			for (const auto& item : value)
			{
				if (!item.is_string())
				{
					degradedMode = true;
					REX::WARN(
						"source=native component=injection_data event=config_entry_invalid path={} file=\"{}\" reason=non_string",
						kNotifyCategoryPath,
						file.string());
					continue;
				}

				const auto category = item.get<std::string>();
				const auto bit = GetNotifyCategoryBit(category);
				if (bit == 0)
				{
					degradedMode = true;
					REX::WARN(
						"source=native component=injection_data event=config_category_unknown path={} file=\"{}\" category=\"{}\"",
						kNotifyCategoryPath,
						file.string(),
						utility::SanitizeLogText(category));
					continue;
				}
				mask |= bit;
			}
			notifyCategoryMask = mask;
			return;
		}

		if (value.is_string())
		{
			const auto category = value.get<std::string>();
			const auto bit = GetNotifyCategoryBit(category);
			if (bit == 0 && !NormalizeNotifyCategoryName(category).empty())
			{
				degradedMode = true;
				REX::WARN(
					"source=native component=injection_data event=config_category_unknown path={} file=\"{}\" category=\"{}\"",
					kNotifyCategoryPath,
					file.string(),
					utility::SanitizeLogText(category));
			}
			notifyCategoryMask = bit;
			return;
		}

		degradedMode = true;
		REX::WARN(
			"source=native component=injection_data event=config_entry_invalid path={} file=\"{}\" reason=invalid_type",
			kNotifyCategoryPath,
			file.string());
	}

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
		const auto snapshot = CurrentResolvedData();
		const auto it = snapshot->data.find(key);
		if (it == snapshot->data.end()) return Value();
		const auto& values = it->second;
		auto vit = values.begin();
		return vit == values.end() ? Value() : *vit;
	}

	std::vector<Value> GetList(const Key key)
	{
		std::vector<Value> result;
		const auto snapshot = CurrentResolvedData();
		auto it = snapshot->data.find(key);
		if (it != snapshot->data.end())
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
		return *GetAsFormListRef(key);
	}

	std::vector<RE::BGSKeyword*> GetAsKeywordList(const Key key)
	{
		return *GetAsKeywordListRef(key);
	}

	std::vector<RE::BGSLocationRefType*> GetAsLocationRefTypeList(const Key key)
	{
		return *GetAsLocationRefTypeListRef(key);
	}

	std::shared_ptr<const std::vector<RE::TESForm*>> GetAsFormListRef(const Key key)
	{
		const auto snapshot = CurrentResolvedData();
		const auto it = snapshot->formListByKey.find(key);
		const auto& list = it == snapshot->formListByKey.end() ? emptyFormList : it->second;
		return std::shared_ptr<const std::vector<RE::TESForm*>>(snapshot, &list);
	}

	std::shared_ptr<const std::vector<RE::BGSKeyword*>> GetAsKeywordListRef(const Key key)
	{
		return GetKeywordListRef(key);
	}

	std::shared_ptr<const std::vector<RE::BGSLocationRefType*>> GetAsLocationRefTypeListRef(const Key key)
	{
		const auto snapshot = CurrentResolvedData();
		const auto it = snapshot->locationRefTypeListByKey.find(key);
		const auto& list = it == snapshot->locationRefTypeListByKey.end() ? emptyLocationRefTypeList : it->second;
		return std::shared_ptr<const std::vector<RE::BGSLocationRefType*>>(snapshot, &list);
	}

	std::shared_ptr<const std::unordered_set<RE::TESFormID>> GetFormIDSet(const Key key)
	{
		const auto snapshot = CurrentResolvedData();
		const auto it = snapshot->formIDSetByKey.find(key);
		const auto& set = it == snapshot->formIDSetByKey.end() ? emptyFormIDSet : it->second;
		return std::shared_ptr<const std::unordered_set<RE::TESFormID>>(snapshot, &set);
	}

	std::shared_ptr<const std::vector<RE::BGSKeyword*>> GetKeywordListRef(const Key key)
	{
		const auto snapshot = CurrentResolvedData();
		const auto it = snapshot->keywordListByKey.find(key);
		const auto& list = it == snapshot->keywordListByKey.end() ? emptyKeywordList : it->second;
		return std::shared_ptr<const std::vector<RE::BGSKeyword*>>(snapshot, &list);
	}

	std::uint32_t GetNotifyCategoryMask()
	{
		return notifyCategoryMask;
	}

	bool GetNotifyLegendaryEquipment()
	{
		return notifyLegendaryEquipment;
	}

	bool HasNotifyFilters()
	{
		return notifyCategoryMask != 0 ||
		       notifyLegendaryEquipment ||
		       !GetFormIDSet(notify_item)->empty() ||
		       !GetKeywordListRef(notify_item)->empty();
	}

	bool Initialize()
	{
		REX::DEBUG("source=native component=injection_data event=initialize_started");
		degradedMode = false;
		tmp.clear();
		lists.clear();
		notifyCategoryMask = 0;
		notifyLegendaryEquipment = false;

		wchar_t modulePath[REX::W32::MAX_PATH]{};
		REX::W32::GetModuleFileNameW(nullptr, modulePath, REX::W32::MAX_PATH);
		std::filesystem::path dir = std::filesystem::path(modulePath).parent_path() / "DATA" / "LootMan";

		std::error_code existsEc;
		if (!std::filesystem::exists(dir, existsEc) || existsEc)
		{
			REX::ERROR(
				"source=native component=injection_data event=directory_missing outcome=failed path=\"{}\" reason=\"{}\"",
				dir.string(),
				existsEc.message());
			return false;
		}

		std::vector<std::filesystem::path> files;
		try
		{
			// directory_iterator construction and increment throw on I/O errors. This runs inside
			// F4SE_PLUGIN_LOAD (no enclosing catch), so an uncaught filesystem_error would std::terminate
			// the game at startup; degrade to whatever we collected instead.
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
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			degradedMode = true;
			REX::ERROR(
				"source=native component=injection_data event=directory_scan_failed path=\"{}\" reason=\"{}\"",
				dir.string(),
				e.what());
		}

		// Stable ordering keeps override behavior deterministic across runs.
		std::sort(files.begin(), files.end());

		for (const auto& file : files)
		{
			std::ifstream ifs(file);
			if (!ifs.is_open())
			{
				degradedMode = true;
				REX::WARN("source=native component=injection_data event=file_open_failed path=\"{}\"", file.string());
				continue;
			}

			nlohmann::json src;
			try
			{
				src = nlohmann::json::parse(ifs);
			}
			catch (const nlohmann::json::parse_error& e)
			{
				// A single malformed supplemental file must not disable the whole
				// mod. The user guide invites third parties to drop their own .json
				// patches into this directory, so a stray syntax error in any one of
				// them should degrade (skip that file) like the sibling error paths
				// above, not fail the entire plugin load.
				degradedMode = true;
				REX::ERROR(
					"source=native component=injection_data event=json_parse_failed path=\"{}\" reason=\"{}\"",
					file.string(),
					e.what());
				continue;
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
							REX::WARN(
								"source=native component=injection_data event=config_entry_invalid path={} file=\"{}\" reason=non_string",
								path,
								file.string());
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
					REX::WARN(
						"source=native component=injection_data event=config_entry_invalid reason=invalid_type value=\"{}\"",
						utility::SanitizeLogText(value.dump()));
				}
			}

			nlohmann::json::json_pointer notifyCategoryPtr{ std::string(kNotifyCategoryPath) };
			if (src.contains(notifyCategoryPtr))
			{
				LoadNotifyCategory(src[notifyCategoryPtr], file);
			}

			nlohmann::json::json_pointer notifyLegendaryEquipmentPtr{ std::string(kNotifyLegendaryEquipmentPath) };
			if (src.contains(notifyLegendaryEquipmentPtr))
			{
				const auto& value = src[notifyLegendaryEquipmentPtr];
				if (value.is_boolean())
				{
					notifyLegendaryEquipment = value.get<bool>();
				}
				else
				{
					degradedMode = true;
					REX::WARN(
						"source=native component=injection_data event=config_entry_invalid path={} file=\"{}\" reason=invalid_type",
						kNotifyLegendaryEquipmentPath,
						file.string());
				}
			}

			if (src.contains(std::string(kListsKey)))
			{
				LoadNamedLists(src[std::string(kListsKey)], file);
			}
		}

		ExpandListReferences();

		REX::DEBUG("source=native component=injection_data event=initialize_completed degraded_mode={}", degradedMode);
		return true;
	}

	void LoadInjectionData()
	{
		REX::DEBUG("source=native component=injection_data event=load_started");
		auto next = std::make_shared<ResolvedData>();

		for (const auto& info : info_list)
		{
			auto key = info.key;

			if (next->data.find(key) == next->data.end())
			{
				next->data.emplace(key, std::unordered_set<Value>());
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
					REX::WARN(
						"source=native component=injection_data event=form_resolution_failed reason=not_found data_id=\"{}\"",
						utility::SanitizeLogText(dataId));
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
						if (next->data[key].emplace(value).second)
						{
							next->locationRefTypeListByKey[key].push_back(locationRefType);
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
						if (next->data[key].emplace(value).second)
						{
							next->keywordListByKey[key].push_back(kw);
						}
						continue;
					}
				}

				if ((info.type & Type::kForm) == 0)
				{
					REX::WARN(
						"source=native component=injection_data event=form_resolution_failed reason=illegal_form_type data_id=\"{}\"",
						utility::SanitizeLogText(dataId));
					continue;
				}

				Value value;
				value.type = Type::kForm;
				value.data.form = form;
				if (next->data[key].emplace(value).second)
				{
					next->formListByKey[key].push_back(form);
					next->formIDSetByKey[key].emplace(form->formID);
				}
			}
		}

		{
			std::unique_lock<std::shared_mutex> guard(resolvedDataMutex);
			resolvedData = std::move(next);
		}

		REX::DEBUG("source=native component=injection_data event=load_completed");
	}
}
