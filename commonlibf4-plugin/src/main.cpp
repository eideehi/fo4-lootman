#include "constructible_object.h"
#include "form_cache.h"
#include "injection_data.h"
#include "message_queue.h"
#include "papyrus_lootman.h"
#include "properties.h"
#include "vendor_chest.h"

void OnMessage(F4SE::MessagingInterface::Message* a_msg)
{
	if (a_msg->type == F4SE::MessagingInterface::kGameLoaded)
	{
		// These systems depend on resolved game/plugin forms, so initialize them only after load.
		form_cache::Initialize();
		properties::Initialize();
		injection_data::LoadInjectionData();
		vendor_chest::Initialize();
		constructible_object::Initialize();
	}
	else if (a_msg->type == F4SE::MessagingInterface::kPreLoadGame)
	{
		// Clear transient handles from the previous runtime before another save is loaded.
		papyrus_lootman::OnPreLoadGame();
	}
}

bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
{
	return papyrus_lootman::Register(a_vm);
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::InitInfo initInfo;
	initInfo.trampoline = true;
	initInfo.trampolineSize = 1024;
	F4SE::Init(a_f4se, initInfo);

	REX::INFO("LootMan plugin loading...");
	papyrus_lootman::InstallInventoryRebuildHooks();

	if (!injection_data::Initialize())
	{
		REX::ERROR("Failed to initialize injection data");
		return false;
	}

	message_queue::Initialize();

	auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage))
	{
		REX::ERROR("Failed to register messaging listener");
		return false;
	}

	auto papyrus = F4SE::GetPapyrusInterface();
	if (!papyrus || !papyrus->Register(RegisterPapyrus))
	{
		REX::ERROR("Failed to register papyrus functions");
		return false;
	}

	REX::INFO("LootMan plugin loaded successfully");
	return true;
}
