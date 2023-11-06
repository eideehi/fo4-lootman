#pragma once

namespace properties
{
    enum Key
    {
        looting_range,
        max_items_processed_per_thread,
        not_looting_from_settlement,
        lootable_inventory_item_type,
        looting_legendary_only,
        always_looting_explosives,
        lootable_alch_item_type,
        lootable_book_item_type,
        lootable_misc_item_type,
        lootable_weap_item_type,
    };

    enum Type
    {
        null,
        boolean,
        integer,
        decimal,
    };

    struct Value
    {
        Type type;

        union Data
        {
            bool b;
            int i;
            float f;

            Data() : b(false)
            {
            }
        } data;

        Value(): type(null)
        {
        }
    };

    Value Get(Key key);
    bool GetBool(Key key, bool defaultValue = false);
    int GetInt(Key key, int defaultValue = 0);
    float GetFloat(Key key, float defaultValue = 0.0f);

    void Initialize();
    void Update(const char* key);
}
