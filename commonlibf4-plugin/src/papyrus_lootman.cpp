#include "papyrus_lootman_internal.h"

using namespace std::literals;

namespace papyrus_lootman
{
	void InstallInventoryRebuildHooks()
	{
		InstallEncounterZoneResetSuppressionHooks();
		InstallWorkbenchSharedContainerHooks();
		InstallWorkshopMaterialProbeHooks();
	}

	bool Register(RE::BSScript::IVirtualMachine* vm)
	{
		REX::DEBUG("[ Started binding papyrus functions for LootMan ]");

		vm->BindNativeMethod("LTMN2:LootMan"sv, "FindNearestValidWorkshopId"sv,
			&FindNearestValidWorkshopId, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "LogEvent"sv,
			&LogEvent, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetLogLevel"sv,
			&GetLogLevel, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "SetLogLevel"sv,
			&SetLogLevel, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "GetHexID"sv,
			&GetHexID, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "RememberWorkshopSupplyLink"sv,
			&RememberWorkshopSupplyLink, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "ForgetWorkshopSupplyLink"sv,
			&ForgetWorkshopSupplyLink, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "TransferLootableInventoryItems"sv,
			&TransferLootableInventoryItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "TransferInventoryItems"sv,
			&TransferInventoryItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "IsLootingSafe"sv,
			&IsLootingSafe, true, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "MoveInventoryItem"sv,
			&MoveInventoryItem, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "MoveInventoryItems"sv,
			&MoveInventoryItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "LootNearbyReferences"sv,
			&LootNearbyReferences, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "LootNearbyEnabledReferences"sv,
			&LootNearbyEnabledReferences, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "ScrapInventoryItems"sv,
			&ScrapInventoryItems, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "ScrapInventoryItemsWithResults"sv,
			&ScrapInventoryItemsWithResults, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "PlayPickUpSound"sv,
			&PlayPickUpSound, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "FinalizeWorldPickup"sv,
			&FinalizeWorldPickup, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "OnUpdateLootManProperty"sv,
			&OnUpdateLootManProperty, false, false);

		REX::DEBUG("  Papyrus functions binding is complete");
		return true;
	}

	void OnPreLoadGame()
	{
		ResetTransientState();
	}
}
