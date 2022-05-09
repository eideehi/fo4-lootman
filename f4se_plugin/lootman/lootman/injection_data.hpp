#pragma once

#include <vector>

#include "f4se/GameForms.h"

class TESForm;
class BGSKeyword;
class BGSLocationRefType;

namespace injection_data
{
    enum Key
    {
        include_activation_block,
        include_activator,
        include_featured_item,
        include_unique_item,
        exclude_form,
        exclude_keyword,
        alch_type_alcohol,
        alch_type_chemistry,
        alch_type_food,
        alch_type_nuka_cola,
        alch_type_stimpak,
        alch_type_syringe_ammo,
        alch_type_water,
        book_type_perk_magazine,
        misc_type_bobblehead,
        weap_type_grenade,
        weap_type_mine,
    };

    enum Type
    {
        form = 1 << 0,
        keyword = 1 << 1,
        location_ref_type = 1 << 2,
    };

    struct Value
    {
        Type type;

        union Data
        {
            TESForm* form;
            BGSKeyword* keyword;
            BGSLocationRefType* location_ref_type;

            Data() : form(nullptr)
            {
            }
        } data;

        Value() : type(form)
        {
        }

        bool operator==(const Value& other) const
        {
            if (type != other.type) return false;
            if (IsForm()) return data.form->formID == other.data.form->formID;
            if (IsKeyword()) return data.keyword->formID == other.data.keyword->formID;
            if (IsLocationRefType()) return data.location_ref_type->formID == other.data.location_ref_type->formID;
            return false;
        }

        bool IsForm() const
        {
            return (type & form) != 0;
        }

        bool IsKeyword() const
        {
            return (type & keyword) != 0;
        }

        bool IsLocationRefType() const
        {
            return (type & location_ref_type) != 0;
        }
    };

    Value Get(Key key);
    TESForm* GetAsForm(Key key);
    BGSKeyword* GetAsKeyword(Key key);
    BGSLocationRefType* GetAsLocationRefType(Key key);

    std::vector<Value> GetList(Key key);
    std::vector<TESForm*> GetAsFormList(Key key);
    std::vector<BGSKeyword*> GetAsKeywordList(Key key);
    std::vector<BGSLocationRefType*> GetAsLocationRefTypeList(Key key);

    bool Initialize();

    void LoadInjectionData();
}
