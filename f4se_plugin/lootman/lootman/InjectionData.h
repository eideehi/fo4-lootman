#pragma once

#include "lib/rapidjson/document.h"

using namespace rapidjson;

namespace InjectionData
{
    extern Document formListData;

    bool Initialize();
}
