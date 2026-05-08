#include "papyrus_lootman_internal.h"

#include <chrono>
#include <excpt.h>

namespace papyrus_lootman
{
	using namespace RE;

	double ElapsedMilliseconds(const Clock::time_point& startedAt)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - startedAt).count();
	}

	int SehFilterRecoverable(unsigned long code)
	{
		constexpr unsigned long kAccessViolation = 0xC0000005UL;
		constexpr unsigned long kDatatypeMisalignment = 0x80000002UL;
		switch (code)
		{
		case kAccessViolation:
		case kDatatypeMisalignment:
			return 1;
		default:
			return 0;
		}
	}

	bool ExecuteSehCallSafe(SehCall call, void* context)
	{
#if defined(_MSC_VER)
		__try
		{
			call(context);
			return true;
		}
		__except (SehFilterRecoverable(GetExceptionCode()))
		{
			return false;
		}
#else
		call(context);
		return true;
#endif
	}

	struct FindNearestWorkshopCallContext
	{
		TESObjectREFR* ref = nullptr;
		TESObjectREFR* workshop = nullptr;
	};

	void InvokeFindNearestValidWorkshopCall(void* opaque)
	{
		auto* context = static_cast<FindNearestWorkshopCallContext*>(opaque);
		context->workshop = Workshop::FindNearestValidWorkshop(*context->ref);
	}

	TESObjectREFR* TryFindNearestValidWorkshop(TESObjectREFR* ref)
	{
		if (!ref)
		{
			return nullptr;
		}

		FindNearestWorkshopCallContext context{ ref };
		if (!ExecuteSehCallSafe(&InvokeFindNearestValidWorkshopCall, &context))
		{
			REX::WARN("source=native component=runtime event=nearest_workshop_lookup_failed ref={:08X}", ref->formID);
			return nullptr;
		}

		auto* workshop = context.workshop;
		if (!workshop ||
			!workshop->extraList ||
			!workshop->extraList->HasType(EXTRA_DATA_TYPE::kWorkshop))
		{
			return nullptr;
		}

		return workshop;
	}
}
