#pragma once

#include "papyrus_lootman.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <RE/Fallout.h>
#include <RE/B/BSScriptUtil.h>

#include "injection_data.h"

namespace papyrus_lootman
{
	enum Generic : std::uint32_t
	{
		alch = 1 << 0,
		ammo = 1 << 1,
		armo = 1 << 2,
		book = 1 << 3,
		ingr = 1 << 4,
		keym = 1 << 5,
		misc = 1 << 6,
		weap = 1 << 7,
		all_item = alch | ammo | armo | book | ingr | keym | misc | weap,
	};

	enum ALCH : std::uint32_t
	{
		alcohol = 1 << 0,
		chemistry = 1 << 1,
		food = 1 << 2,
		nuka_cola = 1 << 3,
		stimpak = 1 << 4,
		syringe_ammo = 1 << 5,
		water = 1 << 6,
		other_alchemy = 1 << 7,
	};

	enum BOOK : std::uint32_t
	{
		perkmagazine = 1 << 0,
		other_book = 1 << 1,
	};

	enum MISC : std::uint32_t
	{
		bobblehead = 1 << 0,
		other_miscellaneous = 1 << 1,
	};

	enum WEAP : std::uint32_t
	{
		grenade = 1 << 0,
		mine = 1 << 1,
		other_weapon = 1 << 2,
	};

	struct InventoryItemInfo
	{
		bool dropped = false;
		bool featured = false;
		bool unscrappable = false;
		bool equipped = false;
		bool legendary = false;
		bool questItem = false;
		std::int32_t totalCount = 0;
	};

	struct EquipmentData
	{
		bool isLegendary = false;
		bool isFeaturedItem = false;
		bool isUnscrappable = false;
	};

	enum InventoryInfoFlags : std::uint32_t
	{
		inventory_info_basic = 0,
		inventory_info_equipment = 1 << 0,
		inventory_info_quest = 1 << 1,
		inventory_info_full = inventory_info_equipment | inventory_info_quest,
	};

	struct MatchCache
	{
		std::unordered_map<std::uint64_t, bool> results;
	};

	struct PropertiesSnapshot
	{
		bool notLootingFromSettlement = false;
		bool lootingLegendaryOnly = false;
		bool alwaysLootingExplosives = false;
		int lootableAlchItemType = 0;
		int lootableBookItemType = 0;
		int lootableMiscItemType = 0;
		int lootableWeapItemType = 0;
		int lootableInventoryItemType = 0;

		static PropertiesSnapshot Capture();
	};

	using Clock = std::chrono::steady_clock;

	struct LootCapacityContext
	{
		bool enabled = false;
		bool valid = false;
		float projectedWeight = 0.0F;
		float limit = 0.0F;

		bool CanAccept(float unitWeight, std::int32_t count, float& outWeight) const;
		void Accept(float weight);
	};

	inline constexpr std::size_t kMaxItemsProcessedPerThreadLimit = 10000;

	struct LootPassBudget
	{
		Clock::time_point startedAt = Clock::now();
		bool useTimeBudget = false;
		double timeBudgetMs = 0.0;
		std::size_t hardMaxObjects = kMaxItemsProcessedPerThreadLimit;
		std::size_t maxObjects = 32;
		std::size_t maxContainers = 4;
		std::size_t maxActors = 4;
		std::size_t maxActivationRefs = 8;
		std::size_t processedObjects = 0;
		std::size_t processedContainers = 0;
		std::size_t processedActors = 0;
		std::size_t processedActivationRefs = 0;
		bool hitObjectLimit = false;
		bool hitTimeBudget = false;

		static LootPassBudget Capture();
		bool ShouldStop();
		bool CanProcessCategory(RE::ENUM_FORM_ID formType) const;
		void MarkProcessed(RE::ENUM_FORM_ID formType);
	};

	extern std::mutex lootCapacityLock;

	template <class T>
	struct ReadLockGuard
	{
		explicit ReadLockGuard(T& a_lock) :
			lock(a_lock)
		{
			lock.lock_read();
		}

		~ReadLockGuard()
		{
			lock.unlock_read();
		}

		T& lock;
	};

	bool PackFormSafe(
		RE::BSScript::Variable& a_var,
		RE::TESForm* form,
		std::uint32_t vmTypeID);

	inline bool PackObjectReferenceSafe(
		RE::BSScript::Variable& a_var,
		RE::TESObjectREFR* ref)
	{
		return PackFormSafe(
			a_var,
			ref,
			static_cast<std::uint32_t>(RE::TESObjectREFR::FORM_ID));
	}

	struct MiscComponent
	{
		static constexpr const char* name = "MiscObject#MiscComponent";

		MiscComponent()
		{
			if (_proxy)
			{
				return;
			}

			auto* game = RE::GameVM::GetSingleton();
			auto vm = game ? game->GetVM() : nullptr;
			RE::BSFixedString typeName(name);
			if (!vm || !vm->CreateStruct(typeName, _proxy) || !_proxy)
			{
				assert(false);
				REX::ERROR("source=native component=papyrus_struct event=create_failed type=\"{}\"", name);
			}
		}

		template <class T>
		std::optional<T> find(std::string_view a_name, bool a_quiet = false) const
		{
			if (_proxy && _proxy->type)
			{
				const auto& mappings = _proxy->type->varNameIndexMap;
				const auto it = mappings.find(a_name);
				if (it != mappings.end())
				{
					const auto& var = _proxy->variables[it->second];
					return RE::BSScript::detail::UnpackVariable<T>(var);
				}
			}

			if (!a_quiet)
			{
				REX::ERROR(
					"source=native component=papyrus_struct event=field_lookup_failed field=\"{}\" type=\"{}\"",
					a_name,
					name);
			}

			return std::nullopt;
		}

		template <class T>
		bool insert(std::string_view a_name, T&& a_val)
		{
			if (_proxy && _proxy->type)
			{
				auto& mappings = _proxy->type->varNameIndexMap;
				const auto it = mappings.find(a_name);
				if (it != mappings.end())
				{
					auto& var = _proxy->variables[it->second];
					RE::BSScript::detail::PackVariable(var, std::forward<T>(a_val));
					return true;
				}
			}

			REX::ERROR(
				"source=native component=papyrus_struct event=field_pack_failed field=\"{}\" type=\"{}\"",
				a_name,
				name);
			return false;
		}

	private:
		friend struct RE::BSScript::detail::wrapper_accessor;

		explicit MiscComponent(RE::BSTSmartPointer<RE::BSScript::Struct> a_proxy) noexcept :
			_proxy(std::move(a_proxy))
		{
			assert(_proxy != nullptr);
		}

		[[nodiscard]] RE::BSTSmartPointer<RE::BSScript::Struct> get_proxy() const&
		{
			return _proxy;
		}

		[[nodiscard]] RE::BSTSmartPointer<RE::BSScript::Struct> get_proxy() &&
		{
			return std::move(_proxy);
		}

		RE::BSTSmartPointer<RE::BSScript::Struct> _proxy;
	};

	void InstallEncounterZoneResetSuppressionHooks();
	void InstallWorkbenchSharedContainerHooks();
	void InstallWorkshopMaterialProbeHooks();
	void ResetTransientState();

	using SehCall = void (*)(void*);

	double ElapsedMilliseconds(const Clock::time_point& startedAt);
	int SehFilterRecoverable(unsigned long code);
	bool ExecuteSehCallSafe(SehCall call, void* context);
	RE::TESObjectREFR* TryFindNearestValidWorkshop(RE::TESObjectREFR* ref);
	bool GetMods(
		RE::ExtraDataList* extraDataList,
		std::vector<RE::BGSMod::Attachment::Mod*>* list);
	EquipmentData GetEquipmentData(
		RE::ExtraDataList* extraDataList,
		std::vector<RE::BGSMod::Attachment::Mod*>* buffer);
	bool TryGetEquipmentDataSafe(
		RE::ExtraDataList* extraDataList,
		std::vector<RE::BGSMod::Attachment::Mod*>* buffer,
		EquipmentData& outData);
	bool UsesWorldReferenceTransfer(RE::ENUM_FORM_ID formType);
	bool IsFormTypeMatch(RE::ENUM_FORM_ID formType, RE::ENUM_FORM_ID matchingType);
	bool IsFormTypeMatchesItemType(RE::ENUM_FORM_ID formType, std::uint32_t itemType);
	std::uint32_t FormTypeToEnabledBit(RE::ENUM_FORM_ID formType);
	std::int32_t FormTypeToBucketIndex(RE::ENUM_FORM_ID formType);
	bool IsPlayable(const RE::TESForm* form);
	bool IsFavorite(const RE::TESForm* form);
	bool HasInventoryFavoriteStack(const RE::BGSInventoryItem& item);
	std::int32_t GetPlayerProtectedStackCount(
		const RE::TESForm* form,
		const RE::BGSInventoryItem::Stack& stack,
		const InventoryItemInfo& stackInfo,
		bool ownerIsPlayer,
		bool ownerIsDead,
		bool formIsFavorite,
		bool hasFavoriteStack,
		bool& retainedFormFavorite);
	std::int32_t GetPlayerTransferProtectedStackCount(
		const RE::TESForm* form,
		const RE::BGSInventoryItem::Stack& stack,
		const InventoryItemInfo& stackInfo,
		bool ownerIsPlayer,
		bool ownerIsDead,
		bool formIsFavorite,
		bool hasFavoriteStack,
		bool& retainedFormFavorite);
	bool IsDeadForLooting(const RE::TESObjectREFR* ref);
	bool IsSettlement(const RE::BGSEncounterZone* zone);
	bool IsOwnerEmptyOrFriend(RE::TESForm* owner);
	bool CheckPrecondition(const RE::TESObjectREFR* ref);
	bool IsDeferredActivationAmmoCandidate(RE::TESObjectREFR* ref, RE::TESForm* baseForm = nullptr);
	bool IsQuestItem(RE::ExtraDataList* extraDataList);
	std::string GetInventoryItemDisplayNameSafe(
		const RE::BGSInventoryItem& item,
		RE::TESForm* fallbackForm,
		std::uint32_t stackIndex);
	bool IsSpecialContainerReference(RE::TESObjectREFR* ref, RE::TESForm* baseForm = nullptr);
	void QueueLootItemNotification(
		RE::TESForm* form,
		const std::string& itemName,
		std::int32_t count,
		const InventoryItemInfo& info,
		MatchCache* matchCache = nullptr);
	bool ShouldNotifyLootDestination(RE::TESObjectREFR* dest);
	InventoryItemInfo BuildWorldReferenceNotificationInfo(
		RE::TESObjectREFR* ref,
		RE::TESBoundObject* object,
		std::int32_t count);
	RE::TBO_InstanceData* GetInstanceData(const RE::ExtraDataList* extraDataList);
	RE::TBO_InstanceData* GetInstanceData(const RE::TESObjectREFR* ref);
	bool HasKeyword(const RE::TESForm* form, RE::BGSKeyword* kw, RE::TBO_InstanceData* data = nullptr);
	bool HasKeyword(
		const RE::TESForm* form,
		const std::vector<RE::BGSKeyword*>& keywords,
		RE::TBO_InstanceData* data = nullptr);
	bool MatchesAny(const RE::TESForm* form, const injection_data::Key& key);
	bool MatchesAnyCached(
		const RE::TESForm* form,
		const injection_data::Key& key,
		MatchCache* cache);
	const char* GetFormEditorIDOrEmpty(const RE::TESForm* form);
	std::string GetFormName(RE::TESForm* form);
	std::string GetFormTypeName(RE::ENUM_FORM_ID formType);
	void EnsureItemTypeCache();
	ALCH GetALCHType(const RE::TESForm* form);
	BOOK GetBOOKType(const RE::TESForm* form);
	MISC GetMISCType(const RE::TESForm* form);
	WEAP GetWEAPType(const RE::TESForm* form);
	InventoryItemInfo BuildFallbackStackInfo(const RE::BGSInventoryItem::Stack& stack);
	bool TryGetInventoryItemInfoSafe(
		const RE::BGSInventoryItem& item,
		std::vector<RE::BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags,
		InventoryItemInfo& outInfo);
	bool TryGetInventoryStackInfoSafe(
		const RE::BGSInventoryItem::Stack& stack,
		std::vector<RE::BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags,
		InventoryItemInfo& outInfo);
	bool EnsureContainerInventoryListForLootScan(RE::TESObjectREFR* ref, RE::TESForm* baseForm);
	bool TryGetReferenceItemCountSafe(
		RE::TESObjectREFR* ref,
		RE::TESBoundObject* object,
		std::int32_t& outCount);
	std::int32_t GetObservedMovedCount(
		std::int32_t beforeCount,
		std::int32_t afterCount,
		bool gotBefore,
		bool gotAfter,
		std::int32_t fallbackCount);
	std::int32_t GetObservedTransferCount(
		std::int32_t srcBefore,
		std::int32_t srcAfter,
		bool gotSrcBefore,
		bool gotSrcAfter,
		std::int32_t destBefore,
		std::int32_t destAfter,
		bool gotDestBefore,
		bool gotDestAfter,
		std::int32_t fallbackCount);
	bool TryGetItemUnitWeightSafe(
		RE::TESBoundObject* object,
		RE::TBO_InstanceData* instanceData,
		float& outWeight);
	LootCapacityContext BuildLootCapacityContext(
		RE::TESObjectREFR* player,
		RE::TESObjectREFR* pendingContainer,
		RE::TESObjectREFR* workshop);
	LootCapacityContext BuildDirectTransferCapacityContext(RE::TESObjectREFR* dest);
	std::int32_t GetWorldReferenceItemCount(RE::TESObjectREFR* ref);
	bool TryAddWorldReferenceToContainerSafe(
		RE::TESObjectREFR* dest,
		RE::TESObjectREFR* ref,
		std::int32_t count);
	bool TryAddInventoryItemSafe(
		RE::TESObjectREFR* dest,
		RE::TESBoundObject* object,
		std::uint32_t count,
		RE::BSTSmartPointer<RE::ExtraDataList> extra);
	bool TryActivateRefSafe(
		RE::TESObjectREFR* ref,
		RE::TESObjectREFR* actionRef,
		bool defaultProcessingOnly);
	bool TryRemoveItemsSafe(
		RE::TESObjectREFR* ref,
		RE::TESBoundObject* object,
		std::int32_t count);
	bool TryMoveInventoryItemSafe(
		RE::TESObjectREFR* src,
		RE::TESObjectREFR* dest,
		RE::TESBoundObject* object,
		std::int32_t count,
		std::optional<std::uint32_t> stackIndex = std::nullopt);
	bool TryRemoveScrapSourceSafe(
		RE::TESObjectREFR* owner,
		RE::TESBoundObject* object,
		std::int32_t count,
		std::optional<std::uint32_t> stackIndex);
	bool ShouldPreserveStackExtraForTransfer(
		RE::TESBoundObject* object,
		const RE::BGSInventoryItem::Stack& stack,
		std::int32_t movingCount,
		std::int32_t stackCount);
	bool TryMoveInventoryItemPreservingStackExtraSafe(
		RE::TESObjectREFR* src,
		RE::TESObjectREFR* dest,
		RE::TESBoundObject* object,
		std::int32_t count,
		std::optional<std::uint32_t> stackIndex,
		RE::BSTSmartPointer<RE::ExtraDataList> extra);
	bool IsReferenceLockedForLooting(RE::TESObjectREFR* ref);
	bool TryUnlockContainerForLooting(
		RE::TESObjectREFR* ref,
		RE::TESObjectREFR* playerRef,
		RE::TESObjectREFR* workshopRef,
		RE::TESForm* bobbyPin,
		RE::BGSPerk* locksmith01,
		RE::BGSPerk* locksmith02,
		RE::BGSPerk* locksmith03,
		RE::BGSPerk* locksmith04,
		bool unlockLockedContainer);
	bool TryLootWorldReference(
		RE::TESObjectREFR* ref,
		RE::TESObjectREFR* dest,
		RE::TESObjectREFR* player,
		bool playPickupSound,
		LootCapacityContext* capacity);
	bool TryLootDeferredActivationAmmoReference(
		RE::TESObjectREFR* ref,
		RE::TESObjectREFR* dest,
		RE::TESObjectREFR* player,
		bool playPickupSound,
		LootCapacityContext* capacity);
	bool TryLootActivationReference(
		RE::TESObjectREFR* ref,
		RE::TESObjectREFR* actionRef,
		RE::TESObjectREFR* player,
		bool playPickupSound,
		LootCapacityContext* capacity);
	bool IsContainerAnimationCandidate(RE::TESObjectREFR* ref);
	std::int32_t TransferInventoryItemsImpl(
		RE::TESObjectREFR* src,
		RE::TESObjectREFR* dest,
		std::uint32_t itemType,
		std::int32_t subType,
		RE::BGSKeyword* looseModKeyword,
		LootCapacityContext* capacity = nullptr,
		bool notifyMovedItems = false);
	std::int32_t TransferLootableInventoryItemsImpl(
		RE::TESObjectREFR* src,
		RE::TESObjectREFR* dest,
		std::uint32_t itemType,
		LootCapacityContext* capacity = nullptr,
		LootPassBudget* passBudget = nullptr);
	bool TryLockObject(RE::TESObjectREFR* obj);
	void UnlockObject(std::uint32_t formId);
	bool IsRecentlyLootedWorldRef(const RE::TESObjectREFR* ref);
	bool TryMarkRecentlyLootedWorldRef(RE::TESObjectREFR* ref);
	bool IsPapyrusObjectHandleAvailable(RE::TESObjectREFR* ref);
	bool IsIncludedQuestItem(const RE::TESForm* form, MatchCache* matchCache);
	InventoryItemInfo GetInventoryItemInfo(
		const RE::BGSInventoryItem& item,
		std::vector<RE::BGSMod::Attachment::Mod*>& buffer,
		std::uint32_t infoFlags);
	bool IsValidInventoryItem(
		const RE::TESForm* form,
		const InventoryItemInfo& info,
		MatchCache* matchCache);
	bool IsLootableInventoryItem(
		const RE::TESForm* form,
		const InventoryItemInfo& info,
		const PropertiesSnapshot* props);
	bool HasLootableItem(
		RE::BGSInventoryList* inventoryList,
		const PropertiesSnapshot* props,
		MatchCache* matchCache,
		bool sourceIsDead);
	bool TryIsValidFormSafe(
		RE::TESForm* form,
		const PropertiesSnapshot* props,
		MatchCache* matchCache,
		bool& outResult);
	bool TryIsLootableFormSafe(
		RE::TESForm* form,
		const PropertiesSnapshot* props,
		MatchCache* matchCache,
		bool& outResult);
	bool TryIsValidObjectSafe(
		RE::TESObjectREFR* ref,
		const PropertiesSnapshot* props,
		RE::TESForm* baseForm,
		MatchCache* matchCache,
		bool& outResult);
	bool TryIsLootableObjectSafe(
		RE::TESObjectREFR* ref,
		const PropertiesSnapshot* props,
		RE::TESForm* baseForm,
		std::vector<RE::BGSMod::Attachment::Mod*>* modBuffer,
		MatchCache* matchCache,
		bool& outResult);

	std::vector<RE::TESObjectREFR*> FindNearbyReferencesWithFormType(
		std::monostate, RE::TESObjectREFR* ref, std::uint32_t formType);
	std::vector<std::int32_t> FindNearbyReferenceIdsWithFormType(
		std::monostate, RE::TESObjectREFR* ref, std::uint32_t formType);
	std::int32_t FindNearestValidWorkshopId(std::monostate, RE::TESObjectREFR* ref);
	std::vector<MiscComponent> GetEquipmentComponents(
		std::monostate, RE::GameScript::RefrOrInventoryObj inventoryItem);
	std::uint32_t GetFormType(std::monostate, RE::TESForm* form);
	void Log(std::monostate, RE::BSFixedString message);
	void LogEvent(
		std::monostate,
		RE::BSFixedString component,
		RE::BSFixedString eventName,
		RE::BSFixedString fields,
		std::int32_t logLevel);
	void ShowSystemMessage(std::monostate, std::int32_t messageId);
	void ShowSystemMessageWithName(std::monostate, std::int32_t messageId, RE::TESForm* nameSource);
	std::int32_t GetLogLevel(std::monostate);
	void SetLogLevel(std::monostate, std::int32_t logLevel);
	std::string GetFormTypeIdentifier(std::monostate, RE::TESForm* form);
	std::string GetHexID(std::monostate, RE::TESForm* form);
	std::string GetName(std::monostate, RE::TESForm* form);
	std::int32_t DumpNearbyObjectDiagnostics(
		std::monostate, RE::TESObjectREFR* player, RE::BSFixedString context);
	void LogInventoryDiagnostics(
		std::monostate, RE::TESObjectREFR* inventoryOwner, RE::BSFixedString prefix);
	void LogWorkshopSupplyDiagnostics(
		std::monostate,
		RE::TESObjectREFR* targetWorkshop,
		RE::TESObjectREFR* lootManWorkshop,
		RE::BSFixedString prefix);
	void RememberWorkshopSupplyLink(
		std::monostate,
		RE::TESForm* targetLocationForm,
		RE::TESObjectREFR* lootManWorkshop,
		RE::BSFixedString prefix);
	void ForgetWorkshopSupplyLink(
		std::monostate, RE::TESForm* targetLocationForm, RE::BSFixedString prefix);
	std::vector<RE::TESForm*> GetInventoryItemsWithItemType(
		std::monostate, RE::TESObjectREFR* inventoryOwner, std::uint32_t itemType);
	std::vector<RE::TESForm*> GetLootableItems(
		std::monostate, RE::TESObjectREFR* inventoryOwner, std::uint32_t itemType);
	std::int32_t TransferLootableInventoryItems(
		std::monostate, RE::TESObjectREFR* src, RE::TESObjectREFR* dest, std::uint32_t itemType);
	std::int32_t TransferInventoryItems(
		std::monostate,
		RE::TESObjectREFR* src,
		RE::TESObjectREFR* dest,
		std::uint32_t itemType,
		std::int32_t subType,
		RE::BGSKeyword* looseModKeyword,
		bool suppressPlayerMessages);
	bool IsLootingSafe(std::monostate);
	void MoveInventoryItem(
		std::monostate,
		RE::TESObjectREFR* src,
		RE::TESObjectREFR* dest,
		RE::TESForm* item,
		std::int32_t count,
		bool silent);
	void MoveInventoryItems(
		std::monostate,
		RE::TESObjectREFR* src,
		RE::TESObjectREFR* dest,
		std::uint32_t itemType,
		std::int32_t subType,
		bool silent);
	std::int32_t LootNearbyReferences(
		std::monostate,
		RE::TESObjectREFR* player,
		RE::TESObjectREFR* dest,
		RE::TESObjectREFR* activator,
		RE::TESObjectREFR* workshop,
		std::uint32_t formType,
		std::uint32_t itemType,
		bool playPickupSound,
		bool playContainerAnimation,
		bool unlockLockedContainer,
		RE::TESForm* bobbyPin,
		RE::BGSPerk* locksmith01,
		RE::BGSPerk* locksmith02,
		RE::BGSPerk* locksmith03,
		RE::BGSPerk* locksmith04);
	std::vector<std::int32_t> LootNearbyEnabledReferences(
		std::monostate,
		RE::TESObjectREFR* player,
		RE::TESObjectREFR* dest,
		RE::TESObjectREFR* activator,
		RE::TESObjectREFR* workshop,
		std::uint32_t enabledFormTypeMask,
		std::uint32_t itemType,
		bool playPickupSound,
		bool playContainerAnimation,
		bool unlockLockedContainer,
		RE::TESForm* bobbyPin,
		RE::BGSPerk* locksmith01,
		RE::BGSPerk* locksmith02,
		RE::BGSPerk* locksmith03,
		RE::BGSPerk* locksmith04);
	std::vector<RE::TESForm*> GetScrappableItems(
		std::monostate, RE::TESObjectREFR* inventoryOwner, std::uint32_t itemType);
	void ScrapInventoryItems(
		std::monostate,
		RE::TESObjectREFR* inventoryOwner,
		RE::TESObjectREFR* componentReceiver,
		std::uint32_t itemType);
	std::vector<std::int32_t> ScrapInventoryItemsWithResults(
		std::monostate,
		RE::TESObjectREFR* inventoryOwner,
		RE::TESObjectREFR* componentReceiver,
		std::uint32_t itemType);
	bool IsFormTypeEquals(std::monostate, RE::TESForm* form, std::uint32_t formType);
	void PlayPickUpSound(
		std::monostate, RE::TESObjectREFR* player, RE::TESObjectREFR* obj);
	void FinalizeWorldPickup(std::monostate, RE::TESObjectREFR* ref);
	void OnUpdateLootManProperty(std::monostate, RE::BSFixedString propertyName);
	void ReleaseObject(std::monostate, std::uint32_t objId);
}

namespace RE::BSScript::detail
{
	// Keep these specializations visible to the Register() translation unit:
	// BindNativeMethod instantiates NativeFunction return packing there.
	template <>
	struct _is_structure_wrapper<papyrus_lootman::MiscComponent> :
		std::true_type
	{};

	template <>
	inline void PackVariable<TESObjectREFR*>(Variable& a_var, TESObjectREFR*&& a_val)
	{
		(void)papyrus_lootman::PackObjectReferenceSafe(a_var, a_val);
	}

	template <>
	inline void PackVariable<TESForm*>(Variable& a_var, TESForm*&& a_val)
	{
		const auto vmTypeID = a_val
			? static_cast<std::uint32_t>(a_val->GetFormType())
			: static_cast<std::uint32_t>(TESForm::FORM_ID);
		(void)papyrus_lootman::PackFormSafe(a_var, a_val, vmTypeID);
	}
}
