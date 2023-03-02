#include "injection_data.hpp"

#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>

#include "f4se_common/Utilities.h"

#include "f4se/GameForms.h"
#include "f4se/GameRTTI.h"

#include "lib/rapidjson/istreamwrapper.h"
#include "lib/rapidjson/pointer.h"
#include "lib/rapidjson/prettywriter.h"
#include "lib/rapidjson/stringbuffer.h"

#include "logging.hpp"
#include "utility.hpp"

template <>
struct std::hash<injection_data::Value>
{
    std::size_t operator()(const injection_data::Value& value) const
    {
        std::size_t hash = 0;
        if (value.IsForm())
        {
            hash = value.data.form->formID;
        }
        else if (value.IsKeyword())
        {
            hash = value.data.keyword->formID;
        }
        else if (value.IsLocationRefType())
        {
            hash = value.data.location_ref_type->formID;
        }
        return hash;
    }
};

namespace injection_data
{
    struct DataInfo
    {
        const char* path;
        Key key;
        int type;
    };

    const DataInfo info_list[] = {
        {"/include/activation-block", include_activation_block, Type::form | Type::keyword},
        {"/include/activator", include_activator, Type::form | Type::keyword},
        {"/include/featured-item", include_featured_item, Type::form | Type::keyword},
        {"/include/unique-item", include_unique_item, Type::form | Type::keyword},
        {"/exclude/form", exclude_form, Type::form},
        {"/exclude/keyword", exclude_keyword, Type::keyword},
        {"/alch-type/alcohol", alch_type_alcohol, Type::form | Type::keyword},
        {"/alch-type/chemistry", alch_type_chemistry, Type::form | Type::keyword},
        {"/alch-type/food", alch_type_food, Type::form | Type::keyword},
        {"/alch-type/nuka-cola", alch_type_nuka_cola, Type::form | Type::keyword},
        {"/alch-type/stimpak", alch_type_stimpak, Type::form | Type::keyword},
        {"/alch-type/syringe-ammo", alch_type_syringe_ammo, Type::form | Type::keyword},
        {"/alch-type/water", alch_type_water, Type::form | Type::keyword},
        {"/book-type/park-magazine", book_type_perk_magazine, Type::form | Type::keyword},
        {"/misc-type/bobblehead", misc_type_bobblehead, Type::form | Type::keyword},
        {"/weap-type/grenade", weap_type_grenade, Type::form | Type::keyword},
        {"/weap-type/mine", weap_type_mine, Type::form | Type::keyword},
    };

    std::unordered_map<std::string, std::unordered_set<std::string>> tmp;
    std::unordered_map<Key, std::unordered_set<Value>> data;

    TESForm* ValueAsForm(const Value value)
    {
        if (value.IsForm())
        {
            return value.data.form;
        }
        if (value.IsKeyword())
        {
            return value.data.keyword;
        }
        if (value.IsLocationRefType())
        {
            return value.data.location_ref_type;
        }
        return nullptr;
    }

    BGSKeyword* ValueAsKeyword(const Value value)
    {
        if (value.IsKeyword())
        {
            return value.data.keyword;
        }
        if (value.IsLocationRefType())
        {
            return DYNAMIC_CAST(value.data.location_ref_type, BGSLocationRefType, BGSKeyword);
        }
        return nullptr;
    }

    BGSLocationRefType* ValueAsLocationRefType(const Value value)
    {
        if (value.IsLocationRefType())
        {
            return value.data.location_ref_type;
        }
        return nullptr;
    }

    Value Get(const Key key)
    {
        const auto values = data.at(key);
        const auto it = values.begin();
        return it == values.end() ? Value() : *it;
    }

    std::vector<Value> GetList(const Key key)
    {
        std::vector<Value> result;
        result.insert(result.end(), data[key].begin(), data[key].end());
        return result;
    }

    TESForm* GetAsForm(const Key key)
    {
        return ValueAsForm(Get(key));
    }

    BGSKeyword* GetAsKeyword(const Key key)
    {
        return ValueAsKeyword(Get(key));
    }

    BGSLocationRefType* GetAsLocationRefType(const Key key)
    {
        return ValueAsLocationRefType(Get(key));
    }

    std::vector<TESForm*> GetAsFormList(const Key key)
    {
        std::vector<TESForm*> result;
        for (const auto& value : GetList(key))
        {
            auto form = ValueAsForm(value);
            if (form)
            {
                result.push_back(form);
            }
        }
        return result;
    }

    std::vector<BGSKeyword*> GetAsKeywordList(const Key key)
    {
        std::vector<BGSKeyword*> result;
        for (const auto& value : GetList(key))
        {
            auto keyword = ValueAsKeyword(value);
            if (keyword)
            {
                result.push_back(keyword);
            }
        }
        return result;
    }

    std::vector<BGSLocationRefType*> GetAsLocationRefTypeList(const Key key)
    {
        std::vector<BGSLocationRefType*> result;
        for (const auto& value : GetList(key))
        {
            auto locationRefType = ValueAsLocationRefType(value);
            if (locationRefType)
            {
                result.push_back(locationRefType);
            }
        }
        return result;
    }

    bool Initialize()
    {
        const auto prefix = "| INITIALIZE |";
        logging::Message("%s   [ Start initialization of injection data ]", prefix);

        const std::tr2::sys::path dir = (GetRuntimeDirectory() + "DATA\\LootMan");
        if (!exists(dir))
        {
            logging::Fatal("%s     Couldn't get the directory for data injection: \"%s\"", prefix, dir.string().c_str());
            return false;
        }

        logging::Message("%s     Search for a json file for data injection from \"%s\"", prefix, dir.string().c_str());

        // Web page that referred to: https://qiita.com/sukakako/items/c329878ce8d622bfd801
        for (std::tr2::sys::directory_iterator dit(dir); dit != std::tr2::sys::directory_iterator(); ++dit)
        {
            std::tr2::sys::path file = dir / static_cast<std::tr2::sys::path>(*dit);
            if (!is_regular_file(file) || _stricmp(file.extension().c_str(), ".json") != 0)
            {
                continue;
            }

            logging::Message("%s     Load injection data from \"%s\"", prefix, file.string().c_str());

            std::ifstream ifs(file);
            rapidjson::IStreamWrapper isw(ifs);

            rapidjson::Document src;
            src.ParseStream(isw);

            if (src.HasParseError())
            {
                logging::Fatal("%s       Json parse error: [ Error code: %d ]", prefix, src.GetParseError());
                return false;
            }

            for (auto info : info_list)
            {
                auto path = info.path;
                if (rapidjson::Value* value = rapidjson::Pointer(path).Get(src))
                {
                    logging::Message("%s       Load: \"%s\"", prefix, path);

                    if (tmp.find(path) == tmp.end())
                    {
                        std::unordered_set<std::string> set;
                        tmp.emplace(path, set);
                    }

                    if (value->IsArray())
                    {
                        for (const auto& item : value->GetArray())
                        {
                            tmp.at(path).emplace(item.GetString());
                        }
                    }
                    else if (value->IsString())
                    {
                        auto set = tmp.at(path);
                        set.clear();
                        set.emplace(value->GetString());
                    }
                    else
                    {
                        tmp.erase(path);
                        rapidjson::StringBuffer sb;
                        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
                        value->Accept(writer);
                        _ERROR("%s         Entry type is illegal:", prefix);
                        _ERROR(sb.GetString());
                    }
                }
            }
        }

        logging::Message("%s     Initialization of injection data is complete", prefix);
        return true;
    }

    void LoadInjectionData()
    {
        const auto prefix = "| INITIALIZE |";
        logging::Message("%s   [ Start loading injection data ]", prefix);

        for (const auto info : info_list)
        {
            auto key = info.key;

            if (data.find(key) == data.end())
            {
                std::unordered_set<Value> set;
                data.emplace(key, set);
            }

            logging::Message("%s     Load: \"%s\"", prefix, info.path);

            auto it = tmp.find(info.path);
            if (it == tmp.end())
            {
                logging::Message("%s       Is Empty", prefix);
                continue;
            }

            for (const auto& dataId : it->second)
            {
                const auto id = std::string(dataId);

                TESForm* form = utility::LookupForm(id);
                if (!form)
                {
                    _WARNING("%s     << WARNING: \"%s\" is not found >>", id.c_str());
                    continue;
                }

                if (info.type & Type::location_ref_type)
                {
                    const auto locationRefType = DYNAMIC_CAST(form, TESForm, BGSLocationRefType);
                    if (locationRefType)
                    {
                        Value value;
                        value.type = Type::location_ref_type;
                        value.data.location_ref_type = locationRefType;
                        data[key].emplace(value);
                        logging::Message("%s       LocationRefType: %08X", prefix, form->formID);
                        continue;
                    }
                }

                if (info.type & Type::keyword)
                {
                    const auto keyword = DYNAMIC_CAST(form, TESForm, BGSKeyword);
                    if (keyword)
                    {
                        Value value;
                        value.type = Type::keyword;
                        value.data.keyword = keyword;
                        data[key].emplace(value);
                        logging::Message("%s       Keyword: %08X", prefix, form->formID);
                        continue;
                    }
                }

                if ((info.type & Type::form) == 0)
                {
                    _WARNING("%s     << WARNING: \"%s\" is illegal form type >>", id.c_str());
                    continue;
                }

                Value value;
                value.type = Type::form;
                value.data.form = form;
                data[key].emplace(value);
                logging::Message("%s       Form: %08X", prefix, form->formID);
            }
        }

        tmp.clear();
        logging::Message("%s     Loading of injection data is complete", prefix);
    }
}
