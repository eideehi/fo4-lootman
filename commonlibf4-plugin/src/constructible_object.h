#pragma once

#include <cstdint>

namespace RE
{
	class BGSConstructibleObject;
}

namespace constructible_object
{
	void Initialize();
	RE::BGSConstructibleObject* FromCreatedObjectId(std::uint32_t formId);
}
