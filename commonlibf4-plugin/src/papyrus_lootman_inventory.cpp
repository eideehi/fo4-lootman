#include "papyrus_lootman_internal.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "constructible_object.h"

namespace papyrus_lootman
{
	using namespace std::literals;
	using namespace RE;

	ExtraDataList* FindExtraDataForInventoryItem(
		TESObjectREFR* container, std::uint16_t uniqueID, TESForm*& outBaseForm)
	{
		outBaseForm = nullptr;
		if (!container || uniqueID == 0) return nullptr;

		auto inventoryList = container->inventoryList;
		if (!inventoryList) return nullptr;

		ReadLockGuard guard(inventoryList->rwLock);
		for (auto& item : inventoryList->data)
		{
			if (!item.object) continue;
			for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get())
			{
				if (!stack->extra) continue;
				auto extraUID = stack->extra->GetByType<ExtraUniqueID>();
				if (extraUID && extraUID->uniqueID == uniqueID)
				{
					outBaseForm = item.object;
					return stack->extra.get();
				}
			}
		}

		return nullptr;
	}

	std::vector<MiscComponent> GetEquipmentComponents(
		std::monostate, GameScript::RefrOrInventoryObj inventoryItem)
	{
		std::vector<MiscComponent> result;

		TESForm* baseForm = nullptr;
		ExtraDataList* extraDataList = nullptr;

		if (inventoryItem.Reference())
		{
			auto ref = inventoryItem.Reference();
			baseForm = ref->GetObjectReference();
			extraDataList = ref->extraList.get();
		}
		else if (inventoryItem.Container() && inventoryItem.UniqueID())
		{
			extraDataList = FindExtraDataForInventoryItem(
				inventoryItem.Container(), inventoryItem.UniqueID(), baseForm);
		}

		if (!baseForm || !extraDataList)
		{
			return std::move(result);
		}

		auto formType = baseForm->GetFormType();
		if (formType != ENUM_FORM_ID::kARMO && formType != ENUM_FORM_ID::kWEAP)
		{
			return std::move(result);
		}

		std::unordered_map<BGSComponent*, std::uint32_t> data;

		auto extractComponents = [&data](const BGSConstructibleObject* obj)
		{
			if (!obj || !obj->requiredItems) return;

			for (auto& [compForm, compVal] : *obj->requiredItems)
			{
				if (!compForm) continue;
				auto count = compVal.i;
				if (count == 0) continue;

				if (compForm->Is(ENUM_FORM_ID::kMISC))
				{
					auto miscObj = compForm->As<TESObjectMISC>();
					if (miscObj && miscObj->componentData)
					{
						for (auto& [miscCompForm, miscCompVal] : *miscObj->componentData)
						{
							auto miscComp = miscCompForm ? miscCompForm->As<BGSComponent>() : nullptr;
							if (!miscComp) continue;
							auto miscCount = miscCompVal.i;
							if (miscCount == 0) continue;
							auto scalar = miscComp->modScrapScalar;
							auto scale = !scalar ? 1.0f : scalar->value;
							if (scale <= 0.0f) continue;
							data[miscComp] += static_cast<std::uint32_t>(miscCount * count);
						}
					}
					continue;
				}

				auto comp = compForm->As<BGSComponent>();
				if (!comp) continue;
				auto scalar = comp->modScrapScalar;
				auto scale = !scalar ? 1.0f : scalar->value;
				if (scale <= 0.0f) continue;
				data[comp] += static_cast<std::uint32_t>(count);
			}
		};

		// Base item components
		extractComponents(constructible_object::FromCreatedObjectId(baseForm->formID));

		// Mod components
		std::vector<BGSMod::Attachment::Mod*> mods;
		GetMods(extraDataList, &mods);
		for (const auto& objectMod : mods)
		{
			extractComponents(constructible_object::FromCreatedObjectId(objectMod->formID));
		}

		for (const auto& [comp, count] : data)
		{
			MiscComponent component;
			component.insert("object"sv, comp);
			component.insert("count"sv, static_cast<std::int32_t>(count / 2));
			result.push_back(std::move(component));
		}

		return std::move(result);
	}

	std::int32_t GetRawInventoryItemCount(const BGSInventoryItem& item)
	{
		std::uint64_t count = 0;
		for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get())
		{
			count += stack->count;
		}

		return static_cast<std::int32_t>(std::min<std::uint64_t>(
			count,
			static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())));
	}

	void AddComponentTotal(
		std::vector<std::pair<TESObjectMISC*, std::int32_t>>& totals,
		TESObjectMISC* scrapItem,
		std::int32_t count)
	{
		if (!scrapItem || count == 0)
		{
			return;
		}

		for (auto& [item, total] : totals)
		{
			if (item == scrapItem)
			{
				total += count;
				return;
			}
		}

		totals.emplace_back(scrapItem, count);
	}

	void LogInventoryDiagnostics(std::monostate, TESObjectREFR* inventoryOwner, BSFixedString prefix)
	{
		const auto prefixText = prefix.c_str();
		if (!inventoryOwner)
		{
			REX::INFO("[Papyrus] {}        Raw inventory: missing owner", prefixText);
			return;
		}

		auto inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList)
		{
			REX::INFO(
				"[Papyrus] {}        Raw inventory: no inventory list for owner={:08X}",
				prefixText,
				inventoryOwner->formID);
			return;
		}

		std::vector<std::pair<TESObjectMISC*, std::int32_t>> componentTotals;
		std::int32_t totalItemCount = 0;
		std::uint32_t rawIndex = 1;

		ReadLockGuard guard(inventoryList->rwLock);
		REX::INFO("[Papyrus] {}        Raw inventory entries: {}", prefixText, inventoryList->data.size());

		for (auto& item : inventoryList->data)
		{
			auto* form = item.object;
			if (!form)
			{
				REX::INFO("[Papyrus] {}        [ Raw item {}: missing base form ]", prefixText, rawIndex++);
				continue;
			}

			const auto itemCount = GetRawInventoryItemCount(item);
			totalItemCount += itemCount;
			REX::INFO(
				"[Papyrus] {}        [ Raw item {} ] base={:08X} \"{}\", type={}, count={}",
				prefixText,
				rawIndex,
				form->formID,
				GetFormName(form),
				GetFormTypeName(form->GetFormType()),
				itemCount);

			auto* misc = form->As<TESObjectMISC>();
			if (!misc || !misc->componentData || misc->componentData->empty())
			{
				REX::INFO("[Papyrus] {}          Misc components: 0", prefixText);
				++rawIndex;
				continue;
			}

			REX::INFO("[Papyrus] {}          Misc components: {}", prefixText, misc->componentData->size());
			std::uint32_t componentIndex = 1;
			for (auto& [componentForm, componentValue] : *misc->componentData)
			{
				auto* component = componentForm ? componentForm->As<BGSComponent>() : nullptr;
				if (!component)
				{
					REX::INFO(
						"[Papyrus] {}          [ Component {}: invalid component form ]",
						prefixText,
						componentIndex++);
					continue;
				}

				const auto perItemCount = componentValue.i;
				const auto totalComponentCount = itemCount * perItemCount;
				auto* scrapItem = component->scrapItem;
				REX::INFO(
					"[Papyrus] {}          [ Component {} ] component={:08X} \"{}\", scrap={:08X} \"{}\", perItem={}, total={}",
					prefixText,
					componentIndex,
					component->formID,
					GetFormName(component),
					scrapItem ? scrapItem->formID : 0,
					GetFormName(scrapItem),
					perItemCount,
					totalComponentCount);
				AddComponentTotal(componentTotals, scrapItem, totalComponentCount);
				++componentIndex;
			}

			++rawIndex;
		}

		std::sort(componentTotals.begin(), componentTotals.end(), [](const auto& lhs, const auto& rhs)
		{
			const auto lhsId = lhs.first ? lhs.first->formID : 0;
			const auto rhsId = rhs.first ? rhs.first->formID : 0;
			return lhsId < rhsId;
		});

		REX::INFO("[Papyrus] {}        Raw total item count: {}", prefixText, totalItemCount);
		REX::INFO("[Papyrus] {}        Component totals: {}", prefixText, componentTotals.size());
		std::uint32_t componentTotalIndex = 1;
		for (const auto& [scrapItem, count] : componentTotals)
		{
			REX::INFO(
				"[Papyrus] {}        [ Component total {} ] scrap={:08X} \"{}\", count={}",
				prefixText,
				componentTotalIndex++,
				scrapItem ? scrapItem->formID : 0,
				GetFormName(scrapItem),
				count);
		}
	}
}
