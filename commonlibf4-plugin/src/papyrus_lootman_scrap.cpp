#include "papyrus_lootman_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "constructible_object.h"
#include "form_cache.h"

namespace papyrus_lootman
{
	using namespace form_cache;
	using namespace RE;

	std::uint32_t SaturatingInventoryCount(std::uint64_t count)
	{
		return static_cast<std::uint32_t>(std::min<std::uint64_t>(
			count,
			static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())));
	}

	void AddRawComponentCount(
		std::unordered_map<BGSComponent*, std::uint32_t>& data,
		BGSComponent* component,
		std::uint64_t count)
	{
		if (!component || count == 0)
		{
			return;
		}

		const auto current = data[component];
		data[component] = SaturatingInventoryCount(static_cast<std::uint64_t>(current) + count);
	}

	void MergeComponentData(
		std::unordered_map<BGSComponent*, std::uint32_t>& target,
		const std::unordered_map<BGSComponent*, std::uint32_t>& source,
		std::uint32_t multiplier = 1)
	{
		if (multiplier == 0)
		{
			return;
		}

		for (const auto& [component, count] : source)
		{
			AddRawComponentCount(
				target,
				component,
				static_cast<std::uint64_t>(count) * static_cast<std::uint64_t>(multiplier));
		}
	}

	void ExtractConstructibleComponents(
		const BGSConstructibleObject* object,
		std::unordered_map<BGSComponent*, std::uint32_t>& data)
	{
		if (!object || !object->requiredItems)
		{
			return;
		}

		for (auto& [requiredForm, requiredValue] : *object->requiredItems)
		{
			if (!requiredForm || requiredValue.i <= 0)
			{
				continue;
			}

			if (requiredForm->Is(ENUM_FORM_ID::kMISC))
			{
				auto* miscObject = requiredForm->As<TESObjectMISC>();
				if (!miscObject || !miscObject->componentData)
				{
					continue;
				}

				for (auto& [componentForm, componentValue] : *miscObject->componentData)
				{
					auto* component = componentForm ? componentForm->As<BGSComponent>() : nullptr;
					if (!component || componentValue.i <= 0)
					{
						continue;
					}

					auto* scalar = component->modScrapScalar;
					const auto scale = scalar ? scalar->value : 1.0F;
					if (!std::isfinite(scale) || scale <= 0.0F)
					{
						continue;
					}

					AddRawComponentCount(
						data,
						component,
						static_cast<std::uint64_t>(componentValue.i) * static_cast<std::uint64_t>(requiredValue.i));
				}
				continue;
			}

			auto* component = requiredForm->As<BGSComponent>();
			if (!component)
			{
				continue;
			}

			auto* scalar = component->modScrapScalar;
			const auto scale = scalar ? scalar->value : 1.0F;
			if (!std::isfinite(scale) || scale <= 0.0F)
			{
				continue;
			}

			AddRawComponentCount(data, component, static_cast<std::uint64_t>(requiredValue.i));
		}
	}

	struct ExtractModComponentsCallContext
	{
		const std::vector<BGSMod::Attachment::Mod*>* mods = nullptr;
		std::unordered_map<BGSComponent*, std::uint32_t>* outComponents = nullptr;
	};

	void InvokeExtractModComponentsCall(void* opaque)
	{
		auto* context = static_cast<ExtractModComponentsCallContext*>(opaque);
		if (!context || !context->mods || !context->outComponents)
		{
			return;
		}

		for (const auto& mod : *context->mods)
		{
			if (!mod)
			{
				continue;
			}

			ExtractConstructibleComponents(
				constructible_object::FromCreatedObjectId(mod->formID),
				*context->outComponents);
		}
	}

	bool TryExtractModComponentsSafe(
		const std::vector<BGSMod::Attachment::Mod*>& mods,
		std::unordered_map<BGSComponent*, std::uint32_t>& outComponents)
	{
		ExtractModComponentsCallContext context{
			&mods,
			&outComponents
		};
		return ExecuteSehCallSafe(&InvokeExtractModComponentsCall, &context);
	}

	struct ScrapInventoryItem
	{
		TESForm* form = nullptr;
		TESBoundObject* object = nullptr;
		std::int32_t count = 0;
		std::optional<std::uint32_t> stackIndex;
		bool isMisc = false;
		std::unordered_map<BGSComponent*, std::uint32_t> componentData;
	};

	using ScrapComponentTotals = std::unordered_map<TESObjectMISC*, std::uint32_t>;

	bool IsScrappableInventoryFormType(ENUM_FORM_ID formType)
	{
		return formType == ENUM_FORM_ID::kARMO ||
		       formType == ENUM_FORM_ID::kMISC ||
		       formType == ENUM_FORM_ID::kWEAP;
	}

	void AddAwardedComponent(
		std::vector<std::pair<TESObjectMISC*, std::uint32_t>>& awards,
		TESObjectMISC* item,
		std::uint32_t count)
	{
		if (!item || count == 0)
		{
			return;
		}

		for (auto& [awardedItem, awardedCount] : awards)
		{
			if (awardedItem == item)
			{
				awardedCount = SaturatingInventoryCount(
					static_cast<std::uint64_t>(awardedCount) + static_cast<std::uint64_t>(count));
				return;
			}
		}

		awards.emplace_back(item, count);
	}

	std::uint32_t GetAdjustedScrapComponentCount(
		BGSComponent* component,
		std::uint32_t rawCount,
		bool isMisc,
		std::size_t componentCount)
	{
		if (!component || rawCount == 0)
		{
			return 0;
		}

		if (isMisc)
		{
			return rawCount;
		}

		const auto adjustedRawCount = rawCount / 2;
		if (adjustedRawCount == 0)
		{
			return 0;
		}

		auto* scalar = component->modScrapScalar;
		auto scale = scalar ? scalar->value : 1.0F;
		if (componentCount == 1 && adjustedRawCount == 1 && scale < 1.0F)
		{
			scale = 1.0F;
		}
		if (!std::isfinite(scale) || scale <= 0.0F)
		{
			return 0;
		}

		const auto scaledCount = static_cast<double>(adjustedRawCount) * static_cast<double>(scale);
		if (!std::isfinite(scaledCount) || scaledCount <= 0.0)
		{
			return 0;
		}

		if (scaledCount >= static_cast<double>(std::numeric_limits<std::int32_t>::max()))
		{
			return static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
		}

		return static_cast<std::uint32_t>(scaledCount);
	}

	std::vector<ScrapInventoryItem> CollectScrapInventoryItems(
		TESObjectREFR* inventoryOwner,
		std::uint32_t itemType)
	{
		std::vector<ScrapInventoryItem> scrappableItems;
		if (!inventoryOwner || itemType > all_item)
		{
			return scrappableItems;
		}
		EnsureItemTypeCache();

		auto* inventoryList = inventoryOwner->inventoryList;
		if (!inventoryList)
		{
			return scrappableItems;
		}

		const bool ownerIsPlayer = inventoryOwner->IsPlayerRef();
		const bool ownerIsDead = IsDeadForLooting(inventoryOwner);
		std::vector<BGSMod::Attachment::Mod*> modBuffer;
		std::unordered_map<BGSComponent*, std::uint32_t> baseComponentData;
		std::unordered_map<BGSComponent*, std::uint32_t> modComponentData;
		std::unordered_map<BGSComponent*, std::uint32_t> componentData;
		modBuffer.reserve(8);
		baseComponentData.reserve(16);
		modComponentData.reserve(16);
		componentData.reserve(16);

		ReadLockGuard guard(inventoryList->rwLock);
		scrappableItems.reserve(inventoryList->data.size());

		for (auto& item : inventoryList->data)
		{
			auto* form = item.object;
			auto* object = form ? form->As<TESBoundObject>() : nullptr;
			if (!form || !object)
			{
				continue;
			}

			const auto formType = form->GetFormType();
			if (!IsScrappableInventoryFormType(formType) ||
			    !IsPlayable(form) ||
			    !IsFormTypeMatchesItemType(formType, itemType) ||
			    HasKeyword(form, keyword::unscrappableObject, nullptr) ||
			    HasKeyword(form, keyword::featuredItem, nullptr))
			{
				continue;
			}

			if (ownerIsPlayer && form->formID == 0x0F)
			{
				continue;
			}

			if (formType == ENUM_FORM_ID::kMISC)
			{
				auto* miscObject = form->As<TESObjectMISC>();
				if (!miscObject || !miscObject->componentData)
				{
					continue;
				}

				const bool formIsFavorite = ownerIsPlayer && IsFavorite(form);
				const bool hasFavoriteStack = ownerIsPlayer && HasInventoryFavoriteStack(item);
				bool retainedFormFavorite = false;
				std::vector<ScrapInventoryItem> itemEntries;
				itemEntries.reserve(4);

				std::uint32_t stackIndex = 0;
				for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex)
				{
					InventoryItemInfo stackInfo{};
					if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, inventory_info_full, stackInfo))
					{
						REX::WARN("ScrapInventoryItems: skip stack for {:08X}: stack-info-exception", form->formID);
						continue;
					}

					if (stackInfo.totalCount <= 0 ||
					    stackInfo.featured ||
					    stackInfo.unscrappable ||
					    stackInfo.questItem)
					{
						continue;
					}

					const auto protectedCount = GetPlayerProtectedStackCount(
						form,
						*stack,
						stackInfo,
						ownerIsPlayer,
						ownerIsDead,
						formIsFavorite,
						hasFavoriteStack,
						retainedFormFavorite);
					const auto scrapCount = stackInfo.totalCount - protectedCount;
					if (scrapCount <= 0)
					{
						continue;
					}

					componentData.clear();
					for (auto& [componentForm, componentValue] : *miscObject->componentData)
					{
						auto* component = componentForm ? componentForm->As<BGSComponent>() : nullptr;
						if (!component || componentValue.i <= 0)
						{
							continue;
						}

						AddRawComponentCount(
							componentData,
							component,
							static_cast<std::uint64_t>(componentValue.i) *
								static_cast<std::uint64_t>(scrapCount));
					}

					if (!componentData.empty())
					{
						ScrapInventoryItem entry{
							form,
							object,
							scrapCount,
							stackIndex,
							true,
							{}
						};
						entry.componentData.swap(componentData);
						itemEntries.push_back(std::move(entry));
					}
				}

				for (auto it = itemEntries.rbegin(); it != itemEntries.rend(); ++it)
				{
					scrappableItems.push_back(std::move(*it));
				}
				continue;
			}

			baseComponentData.clear();
			ExtractConstructibleComponents(
				constructible_object::FromCreatedObjectId(form->formID),
				baseComponentData);

			const bool formIsFavorite = ownerIsPlayer && IsFavorite(form);
			const bool hasFavoriteStack = ownerIsPlayer && HasInventoryFavoriteStack(item);
			bool retainedFormFavorite = false;
			std::vector<ScrapInventoryItem> itemEntries;
			itemEntries.reserve(4);
			std::uint32_t stackIndex = 0;
			for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIndex)
			{
				InventoryItemInfo stackInfo{};
				if (!TryGetInventoryStackInfoSafe(*stack, modBuffer, inventory_info_full, stackInfo))
				{
					REX::WARN("ScrapInventoryItems: skip stack for {:08X}: stack-info-exception", form->formID);
					continue;
				}

				if (stackInfo.totalCount <= 0 ||
				    stackInfo.featured ||
				    stackInfo.unscrappable ||
				    stackInfo.questItem)
				{
					continue;
				}

				const auto protectedCount = GetPlayerProtectedStackCount(
					form,
					*stack,
					stackInfo,
					ownerIsPlayer,
					ownerIsDead,
					formIsFavorite,
					hasFavoriteStack,
					retainedFormFavorite);
				const auto scrapCount = stackInfo.totalCount - protectedCount;
				if (scrapCount <= 0)
				{
					continue;
				}

				componentData.clear();
				MergeComponentData(
					componentData,
					baseComponentData,
					static_cast<std::uint32_t>(scrapCount));

				modComponentData.clear();
				if (TryExtractModComponentsSafe(modBuffer, modComponentData))
				{
					MergeComponentData(
						componentData,
						modComponentData,
						static_cast<std::uint32_t>(scrapCount));
				}
				else
				{
					REX::WARN("ScrapInventoryItems: skip mod components for {:08X}: mod-extract-exception", form->formID);
				}

				if (componentData.empty())
				{
					continue;
				}

				ScrapInventoryItem entry{
					form,
					object,
					scrapCount,
					stackIndex,
					false,
					{}
				};
				entry.componentData.swap(componentData);
				itemEntries.push_back(std::move(entry));
			}

			for (auto it = itemEntries.rbegin(); it != itemEntries.rend(); ++it)
			{
				scrappableItems.push_back(std::move(*it));
			}
		}

		return scrappableItems;
	}

	bool RollBackAwardedComponents(
		TESObjectREFR* componentReceiver,
		const std::vector<std::pair<TESObjectMISC*, std::uint32_t>>& awards)
	{
		bool rollbackSucceeded = true;
		for (auto it = awards.rbegin(); it != awards.rend(); ++it)
		{
			auto* item = it->first;
			const auto count = it->second;
			if (!item || count == 0)
			{
				continue;
			}

			if (!TryRemoveScrapSourceSafe(
					componentReceiver,
					item,
					static_cast<std::int32_t>(count),
					std::nullopt))
			{
				rollbackSucceeded = false;
			}
		}
		return rollbackSucceeded;
	}

	ScrapComponentTotals ApplyScrapInventoryItems(
		TESObjectREFR* inventoryOwner,
		TESObjectREFR* componentReceiver,
		const std::vector<ScrapInventoryItem>& scrappableItems)
	{
		ScrapComponentTotals componentTotals;
		componentTotals.reserve(16);
		if (!inventoryOwner || !componentReceiver)
		{
			return componentTotals;
		}

		for (const auto& entry : scrappableItems)
		{
			if (!entry.form || !entry.object || entry.count <= 0 || entry.componentData.empty())
			{
				continue;
			}

			std::vector<std::pair<TESObjectMISC*, std::uint32_t>> awards;
			awards.reserve(entry.componentData.size());
			for (const auto& [component, rawCount] : entry.componentData)
			{
				const auto adjustedCount = GetAdjustedScrapComponentCount(
					component,
					rawCount,
					entry.isMisc,
					entry.componentData.size());
				auto* scrapItem = component ? component->scrapItem : nullptr;
				AddAwardedComponent(awards, scrapItem, adjustedCount);
			}

			if (awards.empty())
			{
				continue;
			}

			std::vector<std::pair<TESObjectMISC*, std::uint32_t>> addedAwards;
			addedAwards.reserve(awards.size());
			bool addedAll = true;
			for (const auto& [scrapItem, count] : awards)
			{
				if (!TryAddInventoryItemSafe(componentReceiver, scrapItem, count, {}))
				{
					REX::WARN(
						"ScrapInventoryItems: component-insert-exception, owner={:08X}, source={:08X}, component={:08X}, count={}",
						inventoryOwner->formID,
						entry.form->formID,
						scrapItem ? scrapItem->formID : 0,
						count);
					addedAll = false;
					break;
				}
				addedAwards.emplace_back(scrapItem, count);
			}

			if (!addedAll)
			{
				const auto rollbackSucceeded = RollBackAwardedComponents(componentReceiver, addedAwards);
				REX::WARN(
					"ScrapInventoryItems: source retained after component insert failure, owner={:08X}, source={:08X}, inserted_components={}, rollback_succeeded={}",
					inventoryOwner->formID,
					entry.form->formID,
					addedAwards.size(),
					rollbackSucceeded);
				continue;
			}

			std::int32_t beforeCount = 0;
			std::int32_t afterCount = 0;
			const bool gotBefore = TryGetReferenceItemCountSafe(inventoryOwner, entry.object, beforeCount);
			const bool removed = TryRemoveScrapSourceSafe(
				inventoryOwner,
				entry.object,
				entry.count,
				entry.stackIndex);
			const bool gotAfter = TryGetReferenceItemCountSafe(inventoryOwner, entry.object, afterCount);
			const bool observedRemoval = removed && (!gotBefore || !gotAfter || afterCount < beforeCount);

			if (!observedRemoval)
			{
				const auto rollbackSucceeded = RollBackAwardedComponents(componentReceiver, addedAwards);
				REX::WARN(
					"ScrapInventoryItems: source removal failed, owner={:08X}, source={:08X}, count={}, stack={}, before={}, after={}, gotBefore={}, gotAfter={}, rollback_succeeded={}",
					inventoryOwner->formID,
					entry.form->formID,
					entry.count,
					entry.stackIndex ? static_cast<std::int32_t>(*entry.stackIndex) : -1,
					beforeCount,
					afterCount,
					gotBefore,
					gotAfter,
					rollbackSucceeded);
				continue;
			}

			for (const auto& [scrapItem, count] : awards)
			{
				componentTotals[scrapItem] = SaturatingInventoryCount(
					static_cast<std::uint64_t>(componentTotals[scrapItem]) +
					static_cast<std::uint64_t>(count));
			}
		}

		return componentTotals;
	}

	std::vector<std::int32_t> FlattenScrapComponentTotals(const ScrapComponentTotals& componentTotals)
	{
		struct FlatComponentEntry
		{
			TESObjectMISC* form = nullptr;
			std::uint32_t count = 0;
			std::string name;
		};

		std::vector<FlatComponentEntry> entries;
		entries.reserve(componentTotals.size());
		for (const auto& [form, count] : componentTotals)
		{
			if (!form || count == 0)
			{
				continue;
			}
			entries.push_back({ form, count, GetFormName(form) });
		}

		std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs)
		{
			if (lhs.count != rhs.count)
			{
				return lhs.count > rhs.count;
			}
			if (lhs.name != rhs.name)
			{
				return lhs.name < rhs.name;
			}
			return lhs.form->formID < rhs.form->formID;
		});

		std::vector<std::int32_t> flattened;
		flattened.reserve(entries.size() * 2);
		for (const auto& entry : entries)
		{
			flattened.push_back(static_cast<std::int32_t>(entry.form->formID));
			flattened.push_back(static_cast<std::int32_t>(entry.count));
		}
		return flattened;
	}

	std::vector<std::int32_t> ScrapInventoryItemsWithResults(
		std::monostate,
		TESObjectREFR* inventoryOwner,
		TESObjectREFR* componentReceiver,
		std::uint32_t itemType)
	{
		const auto startedAt = Clock::now();
		if (!inventoryOwner || !componentReceiver || itemType > all_item)
		{
			return {};
		}

		auto scrappableItems = CollectScrapInventoryItems(inventoryOwner, itemType);
		ScrapComponentTotals componentTotals;
		if (inventoryOwner->IsPlayerRef() || componentReceiver->IsPlayerRef())
		{
			PlayerCharacter::ScopedInventoryChangeMessageContext context(true, false);
			componentTotals = ApplyScrapInventoryItems(
				inventoryOwner,
				componentReceiver,
				scrappableItems);
		}
		else
		{
			componentTotals = ApplyScrapInventoryItems(
				inventoryOwner,
				componentReceiver,
				scrappableItems);
		}

		if (!scrappableItems.empty())
		{
			REX::DEBUG(
				"ScrapInventoryItems: owner={:08X}, receiver={:08X}, itemType={}, candidates={}, components={}, elapsed_ms={:.3f}",
				inventoryOwner->formID,
				componentReceiver->formID,
				itemType,
				scrappableItems.size(),
				componentTotals.size(),
				ElapsedMilliseconds(startedAt));
		}

		return FlattenScrapComponentTotals(componentTotals);
	}

	void ScrapInventoryItems(
		std::monostate,
		TESObjectREFR* inventoryOwner,
		TESObjectREFR* componentReceiver,
		std::uint32_t itemType)
	{
		(void)ScrapInventoryItemsWithResults(
			std::monostate{},
			inventoryOwner,
			componentReceiver,
			itemType);
	}

	std::vector<TESForm*> GetScrappableItems(
		std::monostate, TESObjectREFR* inventoryOwner, std::uint32_t itemType)
	{
		std::vector<TESForm*> result;

		if (!inventoryOwner || itemType > all_item)
		{
			return result;
		}

		auto scrappableItems = CollectScrapInventoryItems(inventoryOwner, itemType);
		result.reserve(scrappableItems.size());

		std::unordered_set<std::uint32_t> seenForms;
		seenForms.reserve(scrappableItems.size());
		for (const auto& item : scrappableItems)
		{
			if (!item.form || !seenForms.insert(item.form->formID).second)
			{
				continue;
			}
			result.push_back(item.form);
		}
		return result;
	}
}
