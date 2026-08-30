#include "constructible_object.h"
#include "form_cache.h"
#include "injection_data.h"
#include "log_settings.h"
#include "message_queue.h"
#include "papyrus_lootman.h"
#include "properties.h"
#include "runtime_probe.h"
#include "terminal_labels.h"
#include "vendor_chest.h"

#include <atomic>

namespace
{
	std::atomic<std::uint32_t> lifecycleProbeRecords{ 0 };
	constexpr std::uint32_t kLifecycleProbeRecordLimit = 128;

	const char* GetMessageName(std::uint32_t type)
	{
		switch (type)
		{
		case F4SE::MessagingInterface::kGameLoaded:
			return "game_loaded";
		case F4SE::MessagingInterface::kPreLoadGame:
			return "pre_load_game";
		case F4SE::MessagingInterface::kPostLoadGame:
			return "post_load_game";
		default:
			return "other";
		}
	}

	void TraceLifecycleMessage(std::uint32_t type, const char* phase)
	{
		if (type != F4SE::MessagingInterface::kGameLoaded &&
			type != F4SE::MessagingInterface::kPreLoadGame &&
			type != F4SE::MessagingInterface::kPostLoadGame)
		{
			return;
		}
		if (!runtime_probe::IsEnabled())
		{
			return;
		}

		if (!runtime_probe::TryReserve(lifecycleProbeRecords, kLifecycleProbeRecordLimit))
		{
			return;
		}

		const auto sequence = runtime_probe::NextSequence();
		REX::TRACE(
			"source=native component=runtime_probe event=f4se_message probe_schema=1 ordering=reservation_only seq={} thread_id={} message_type={} message_name={} phase={}",
			sequence,
			REX::W32::GetCurrentThreadId(),
			type,
			GetMessageName(type),
			phase);
	}
}

void OnMessage(F4SE::MessagingInterface::Message* a_msg)
{
	TraceLifecycleMessage(a_msg->type, "enter");
	if (a_msg->type == F4SE::MessagingInterface::kGameLoaded)
	{
		// These systems depend on resolved game/plugin forms, so initialize them only after load.
		form_cache::Initialize();
		TraceLifecycleMessage(a_msg->type, "before_properties_initialize");
		properties::Initialize();
		TraceLifecycleMessage(a_msg->type, "after_properties_initialize");
		// Resolves the config holotape pages, captures their shipped item text and
		// registers the menu sink that refreshes the value shown in each label.
		terminal_labels::Initialize();
		injection_data::LoadInjectionData();
		vendor_chest::Initialize();
		constructible_object::Initialize();
		TraceLifecycleMessage(a_msg->type, "game_loaded_initializers_complete");
	}
	else if (a_msg->type == F4SE::MessagingInterface::kPreLoadGame)
	{
		// Clear transient handles from the previous runtime before another save is loaded.
		papyrus_lootman::OnPreLoadGame();
		// Drops the config holotape label state that belongs to the outgoing session:
		// queued refreshes, the cached page pointers and the property-cache readiness
		// flag. The captured original item text is per-process and is kept.
		terminal_labels::OnPreLoadGame();
		// Drop any queued HUD messages so a backlog from the previous session
		// cannot surface in the newly loaded game.
		message_queue::Reset();
	}
	else if (a_msg->type == F4SE::MessagingInterface::kPostLoadGame)
	{
		// Re-resolves the config holotape pages that OnPreLoadGame() cleared. Needed
		// because kGameLoaded fires once per process, not once per save load, so
		// nothing else would resolve those pages again.
		terminal_labels::OnPostLoadGame();
	}
	TraceLifecycleMessage(a_msg->type, "exit");
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

	log_settings::Initialize();
	REX::INFO("source=native component=plugin event=loading_started");
	papyrus_lootman::InstallInventoryRebuildHooks();

	if (!injection_data::Initialize())
	{
		REX::ERROR("source=native component=plugin event=load_failed reason=injection_data_initialization_failed");
		return false;
	}

	message_queue::Initialize();

	auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage))
	{
		REX::ERROR("source=native component=plugin event=load_failed reason=messaging_listener_registration_failed");
		return false;
	}

	auto papyrus = F4SE::GetPapyrusInterface();
	if (!papyrus || !papyrus->Register(RegisterPapyrus))
	{
		REX::ERROR("source=native component=plugin event=load_failed reason=papyrus_registration_failed");
		return false;
	}

	REX::INFO("source=native component=plugin event=loaded");
	return true;
}
