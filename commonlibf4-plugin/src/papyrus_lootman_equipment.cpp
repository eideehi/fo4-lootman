#include "papyrus_lootman_internal.h"

#include <cstdint>
#include <vector>

#include "form_cache.h"

namespace papyrus_lootman
{
	using namespace form_cache;
	using namespace RE;

	inline constexpr std::uint32_t kLegendaryModFlag = 1u << 4;
	inline constexpr std::uint32_t kWeaponTargetKeywords = 18;
	inline constexpr std::uint32_t kArmorTargetKeywords = 11;

	inline bool IsLegendaryMod(const BGSMod::Attachment::Mod* mod)
	{
		return mod && (mod->formFlags & kLegendaryModFlag) != 0;
	}

	bool GetMods(ExtraDataList* extraDataList, std::vector<BGSMod::Attachment::Mod*>* list)
	{
		if (!list) return false;
		list->clear();
		if (!extraDataList) return false;

		auto objectModData = extraDataList->GetByType<BGSObjectInstanceExtra>();
		if (!objectModData || !objectModData->values) return false;

		auto indexData = objectModData->GetIndexData();
		if (indexData.empty()) return false;

		bool found = false;
		for (auto& entry : indexData)
		{
			auto form = TESForm::GetFormByID(entry.objectID);
			auto objectMod = form ? form->As<BGSMod::Attachment::Mod>() : nullptr;
			if (objectMod)
			{
				list->push_back(objectMod);
				found = true;
			}
		}
		return found;
	}

	EquipmentData GetEquipmentData(ExtraDataList* extraDataList, std::vector<BGSMod::Attachment::Mod*>* buffer)
	{
		EquipmentData equipmentData;
		if (!GetMods(extraDataList, buffer))
		{
			return equipmentData;
		}

		for (const auto& mod : *buffer)
		{
			if (IsLegendaryMod(mod))
			{
				equipmentData.isLegendary = true;
			}

			// Read the property-mod block directly out of the BSTDataBuffer<2> that
			// BGSMod::Container inherits from, instead of going through
			// `BGSMod::Attachment::Mod::GetData(Data&)`. The REL::Relocation behind
			// that member function crashes in the currently targeted game build,
			// while `GetBuffer<T>(id)` is a pure in-memory template helper so it
			// stays safe even if the game function address has shifted. Block id 1
			// (`BLOCKIDS::kPMOD`) is the property-mod list.
			const auto propModSpan = mod->GetBuffer<const BGSMod::Property::Mod>(
				static_cast<std::uint8_t>(BGSMod::Property::BLOCKIDS::kPMOD));
			for (const auto& propMod : propModSpan)
			{
				if (propMod.op != BGSMod::Property::OP::kAdd) continue;
				if (propMod.type != BGSMod::Property::TYPE::kForm) continue;

				if (propMod.target == kWeaponTargetKeywords || propMod.target == kArmorTargetKeywords)
				{
					auto form = propMod.data.form;
					if (!form) continue;

					if (keyword::featuredItem && form->formID == keyword::featuredItem->formID)
					{
						equipmentData.isFeaturedItem = true;
					}
					else if (keyword::unscrappableObject && form->formID == keyword::unscrappableObject->formID)
					{
						equipmentData.isUnscrappable = true;
					}
				}
			}
		}

		return equipmentData;
	}

	struct GetEquipmentDataCallContext
	{
		ExtraDataList* extraDataList;
		std::vector<BGSMod::Attachment::Mod*>* buffer;
		EquipmentData data;
	};

	void InvokeGetEquipmentDataCall(void* opaque)
	{
		auto* context = static_cast<GetEquipmentDataCallContext*>(opaque);
		context->data = GetEquipmentData(context->extraDataList, context->buffer);
	}

	bool TryGetEquipmentDataSafe(ExtraDataList* extraDataList,
		std::vector<BGSMod::Attachment::Mod*>* buffer,
		EquipmentData& outData)
	{
		GetEquipmentDataCallContext context{
			extraDataList,
			buffer,
			{}
		};
		if (!ExecuteSehCallSafe(&InvokeGetEquipmentDataCall, &context))
		{
			return false;
		}
		outData = context.data;
		return true;
	}
}
