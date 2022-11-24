#pragma once

class BGSConstructibleObject;

namespace constructible_object
{
    void Initialize();

    BGSConstructibleObject* FromCreatedObjectId(UInt32 formId);
}
