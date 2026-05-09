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
		REX::DEBUG("source=native component=papyrus event=binding_started script=LTMN2:LootMan");

		vm->BindNativeMethod("LTMN2:LootMan"sv, "FindNearestValidWorkshopId"sv,
			&FindNearestValidWorkshopId, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "LogEvent"sv,
			&LogEvent, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "ShowSystemMessage"sv,
			&ShowSystemMessage, false, false);
		vm->BindNativeMethod("LTMN2:LootMan"sv, "ShowSystemMessageWithName"sv,
			&ShowSystemMessageWithName, false, false);
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

		REX::DEBUG("source=native component=papyrus event=binding_completed script=LTMN2:LootMan");
		return true;
	}

	void OnPreLoadGame()
	{
		ResetTransientState();
	}
}
