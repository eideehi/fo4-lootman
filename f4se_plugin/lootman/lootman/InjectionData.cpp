#include "InjectionData.h"

#include <fstream>
#include <filesystem>

#include "f4se_common/Utilities.h"

#include "lib/rapidjson/istreamwrapper.h"

#include "lib/rapidjson/stringbuffer.h"
#include "lib/rapidjson/prettywriter.h"

namespace InjectionData
{
    Document formListData;

    bool Initialize()
    {
        _MESSAGE(">>   Lootman injection data initialization start.");

        const char* identifies[] = {
            "AllowedActivatorList",
            "AllowedFeaturedItemList",
            "AllowedUniqueItemList",
            "ExcludeFormList",
            "ExcludeKeywordList",
            "ExcludeLocationRefList",
            "IgnorableActivationBlockeList",
            "VendorChestList",
            "WeaponTypeGrenadeKeywordList",
            "WeaponTypeMineKeywordList"
        };

        const std::tr2::sys::path dir = (GetRuntimeDirectory() + "DATA\\Lootman");
        if (!std::tr2::sys::exists(dir))
        {
            _FATALERROR(">>     Couldn't get the directory for data injection. [%s]", dir.string().c_str());
            return false;
        }

        formListData.SetObject();

        // Pages used for reference: https://stackoverflow.com/questions/40013355/how-to-merge-two-json-file-using-rapidjson
        auto merge = [](Value& dest, Value& src, Document::AllocatorType& allocator, const char* key)
        {
            auto srcIt = src.FindMember(key);
            if (srcIt != src.MemberEnd())
            {
                auto destIt = dest.FindMember(key);
                if (destIt != dest.MemberEnd())
                {
                    if (srcIt->value.GetType() != destIt->value.GetType())
                    {
                        return false;
                    }

                    if (srcIt->value.IsArray())
                    {
                        for (auto it = srcIt->value.Begin(); it != srcIt->value.End(); ++it)
                        {
                            Value destVal;
                            destVal.CopyFrom(*it, allocator);
                            destIt->value.PushBack(destVal, allocator);
                        }
                    }
                }
                else
                {
                    Value destName;
                    destName.CopyFrom(srcIt->name, allocator);

                    Value destVal;
                    destVal.CopyFrom(srcIt->value, allocator);

                    dest.AddMember(destName, destVal, allocator);
                }
            }
            return true;
        };

        // Pages used for reference: https://qiita.com/sukakako/items/c329878ce8d622bfd801
        for (std::tr2::sys::directory_iterator it(dir); it != std::tr2::sys::directory_iterator(); it++)
        {
            std::tr2::sys::path file = dir / (std::tr2::sys::path)*it;
            _MESSAGE(">>     Check file path [%s]", file.string().c_str());
            if (std::tr2::sys::is_regular_file(file) && _stricmp(file.extension().c_str(), ".JSON") == 0)
            {
                _MESSAGE(">>     Inject data from [%s]", file.string().c_str());

                std::ifstream ifs(file);
                IStreamWrapper isw(ifs);

                Document d;
                d.ParseStream(isw);

                if (d.HasParseError())
                {
                    _FATALERROR(">>       Json parse error. [ErrorCode: %d]", d.GetParseError());
                    return false;
                }

                for (auto item : identifies)
                {
                    if (!merge(formListData, d, formListData.GetAllocator(), item))
                    {
                        StringBuffer sb;
                        PrettyWriter<StringBuffer> writer(sb);
                        d.Accept(writer);
                        _FATALERROR(">>       Illegal json:");
                        _FATALERROR(sb.GetString());
                        return false;
                    }
                }
            }
        }

        StringBuffer sb;
        PrettyWriter<StringBuffer> writer(sb);
        formListData.Accept(writer);

        _MESSAGE(">>     Merged json:");
        _MESSAGE(sb.GetString());

        _MESSAGE(">>   Lootman injection data initialization end.");
        return true;
    }
}
