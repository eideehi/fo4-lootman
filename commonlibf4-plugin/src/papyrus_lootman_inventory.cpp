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
			REX::DEBUG(
				"source=native component=inventory_diagnostics event=owner_missing context=\"{}\" outcome=failed reason=missing_owner",
				prefixText);
			return;
		}

		auto inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList)
		{
			REX::DEBUG(
				"source=native component=inventory_diagnostics event=inventory_list_missing context=\"{}\" outcome=failed reason=no_inventory_list owner={:08X}",
				prefixText,
				inventoryOwner->formID);
			return;
		}

		std::vector<std::pair<TESObjectMISC*, std::int32_t>> componentTotals;
		std::int32_t totalItemCount = 0;
		std::uint32_t rawIndex = 1;

		ReadLockGuard guard(inventoryList->rwLock);
		REX::DEBUG(
			"source=native component=inventory_diagnostics event=entries context=\"{}\" owner={:08X} count={}",
			prefixText,
			inventoryOwner->formID,
			inventoryList->data.size());

		for (auto& item : inventoryList->data)
		{
			auto* form = item.object;
			if (!form)
			{
				REX::DEBUG(
					"source=native component=inventory_diagnostics event=item_missing_base context=\"{}\" owner={:08X} item_index={}",
					prefixText,
					inventoryOwner->formID,
					rawIndex++);
				continue;
			}

			const auto itemCount = GetRawInventoryItemCount(item);
			totalItemCount += itemCount;
			REX::DEBUG(
				"source=native component=inventory_diagnostics event=item context=\"{}\" owner={:08X} item_index={} base={:08X} name=\"{}\" item_type={} count={}",
				prefixText,
				inventoryOwner->formID,
				rawIndex,
				form->formID,
				GetFormName(form),
				GetFormTypeName(form->GetFormType()),
				itemCount);

			auto* misc = form->As<TESObjectMISC>();
			if (!misc || !misc->componentData || misc->componentData->empty())
			{
				REX::DEBUG(
					"source=native component=inventory_diagnostics event=component_count context=\"{}\" owner={:08X} item_index={} base={:08X} count=0",
					prefixText,
					inventoryOwner->formID,
					rawIndex,
					form->formID);
				++rawIndex;
				continue;
			}

			REX::DEBUG(
				"source=native component=inventory_diagnostics event=component_count context=\"{}\" owner={:08X} item_index={} base={:08X} count={}",
				prefixText,
				inventoryOwner->formID,
				rawIndex,
				form->formID,
				misc->componentData->size());
			std::uint32_t componentIndex = 1;
			for (auto& [componentForm, componentValue] : *misc->componentData)
			{
				auto* component = componentForm ? componentForm->As<BGSComponent>() : nullptr;
				if (!component)
				{
					REX::DEBUG(
						"source=native component=inventory_diagnostics event=component_invalid context=\"{}\" owner={:08X} item_index={} component_index={} outcome=failed reason=invalid_component_form",
						prefixText,
						inventoryOwner->formID,
						rawIndex,
						componentIndex++);
					continue;
				}

				const auto perItemCount = componentValue.i;
				const auto totalComponentCount = itemCount * perItemCount;
				auto* scrapItem = component->scrapItem;
				REX::DEBUG(
					"source=native component=inventory_diagnostics event=component context=\"{}\" owner={:08X} item_index={} component_index={} component={:08X} component_name=\"{}\" scrap={:08X} scrap_name=\"{}\" per_item={} total={}",
					prefixText,
					inventoryOwner->formID,
					rawIndex,
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

		REX::DEBUG(
			"source=native component=inventory_diagnostics event=summary context=\"{}\" owner={:08X} total_items={} component_totals={}",
			prefixText,
			inventoryOwner->formID,
			totalItemCount,
			componentTotals.size());
		std::uint32_t componentTotalIndex = 1;
		for (const auto& [scrapItem, count] : componentTotals)
		{
			REX::DEBUG(
				"source=native component=inventory_diagnostics event=component_total context=\"{}\" owner={:08X} component_total_index={} scrap={:08X} scrap_name=\"{}\" count={}",
				prefixText,
				inventoryOwner->formID,
				componentTotalIndex++,
				scrapItem ? scrapItem->formID : 0,
				GetFormName(scrapItem),
				count);
		}
	}
}
