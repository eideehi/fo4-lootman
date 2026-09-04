#include "terminal_labels.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>

#include "log_settings.h"
#include "message_queue.h"
#include "properties.h"
#include "utility.h"

// Renders the current value of every value-bearing item on the LootMan config
// holotape into the item label itself, so a page shows its state without the
// player having to read the HUD confirmation line. Labels are rewritten in
// memory on `RE::BGSTerminal::MenuItem::itemText`, always composed from the
// item's captured original text, so a value suffix can never stack.
namespace
{
	// Config holotape pages. The label table writes the three value-bearing pages;
	// the root page and the utility page carry no settings and are never written.
	constexpr const char* kPrimaryPageForm = "LootMan.esp|000FB8";
	constexpr const char* kSecondaryPageForm = "LootMan.esp|000FB9";
	constexpr const char* kTertiaryPageForm = "LootMan.esp|000FBA";

	// Nothing resolves the root page; the label exists so its zero count is
	// reported on every refresh and that page's exclusion stays observable.
	constexpr const char* kRootPageLabel = "000FB7";
	constexpr const char* kPrimaryPageLabel = "000FB8";
	constexpr const char* kSecondaryPageLabel = "000FB9";
	constexpr const char* kTertiaryPageLabel = "000FBA";
	// Nothing resolves the utility page; the label exists so that page's exclusion
	// is reported alongside the pages a refresh does write.
	constexpr const char* kUtilityPageLabel = "000FBB";

	// Engine string assignment splices new text in after this prefix instead of
	// replacing the string, on a code path whose buffer arithmetic is unsafe. Any
	// item carrying it is left untouched.
	constexpr std::string_view kLocalizedStringPrefix = "<ID="sv;

	// Guards the cached page pointers, the captured original labels and the
	// per-item one-shot warning flags. Taken from the main thread, the Papyrus VM
	// thread and the task-interface thread, so critical sections stay short and the
	// menu event sink never blocks on it.
	std::mutex stateMutex;
	RE::BGSTerminal* primaryPage = nullptr;
	RE::BGSTerminal* secondaryPage = nullptr;
	RE::BGSTerminal* tertiaryPage = nullptr;

	// Read without `stateMutex` so a record can report readiness even on the path
	// that refuses to wait for that lock.
	std::atomic<bool> cacheReady{ false };

	std::atomic<bool> sinkRegistered{ false };

	// Reported once per process when the UI singleton is not available yet to
	// register the menu sink on, so the sink's silent non-registration is
	// explained instead of looking identical to a working refresh path.
	std::atomic<bool> sinkRegistrationUiMissingReported{ false };

	// Set when a refresh has been handed to the task queue and cleared again when
	// that queued work starts. A burst of triggers inside one frame therefore
	// collapses into a single rewrite instead of one rewrite per trigger.
	std::atomic<bool> refreshPending{ false };

	// Incremented on every load boundary. Work queued before the boundary carries
	// the epoch it was queued under and is dropped when it finally runs.
	std::atomic<std::uint32_t> loadEpoch{ 0 };

	std::string ReadItemText(const RE::BGSLocalizedString& text)
	{
		const auto* raw = text.c_str();
		return raw ? std::string(raw) : std::string();
	}

	// Menu items are matched by their stable record id, never by array position.
	RE::BGSTerminal::MenuItem* FindMenuItem(RE::BGSTerminal* page, std::uint16_t id)
	{
		if (!page)
		{
			return nullptr;
		}
		for (auto& item : page->menuItems)
		{
			if (item.id == id)
			{
				return &item;
			}
		}
		return nullptr;
	}

	// Resolves one config page form.
	RE::BGSTerminal* ResolvePage(const char* formReference)
	{
		auto* form = utility::LookupForm(std::string(formReference));
		return form ? form->As<RE::BGSTerminal>() : nullptr;
	}

	// Which holotape page a label row lives on. The enumerator order is also the
	// index order of the per-page counters a refresh reports.
	enum class LabelPage
	{
		kPrimary,    // LootMan.esp|000FB8
		kSecondary,  // LootMan.esp|000FB9
		kTertiary    // LootMan.esp|000FBA
	};

	// How a row turns its payload into the value shown in the label.
	enum class LabelKind
	{
		// `payload` is a properties::Key, read as a bool, a float or an int.
		kBoolProperty,
		kFloatProperty,
		kIntProperty,
		// `payload` is one bit of `enabled_looting_form_type_mask`.
		kMaskBit,
		// `payload` is the log level the row selects; only the active one is marked.
		kLogLevel
	};

	struct LabelRow
	{
		LabelPage page;
		// Stable `MenuItem::id` record id. Rows are never matched by array index.
		std::uint16_t itemId;
		LabelKind kind;
		int payload;
	};

	// Every value-bearing config holotape item, in page and item-id order. The
	// root page and the utility page hold no settings, so no row targets them.
	constexpr LabelRow kLabelRows[] = {
		{ LabelPage::kPrimary, 1, LabelKind::kBoolProperty, properties::enable_lootman },
		{ LabelPage::kPrimary, 2, LabelKind::kBoolProperty, properties::display_system_message },
		{ LabelPage::kPrimary, 3, LabelKind::kBoolProperty, properties::play_pickup_sound },
		{ LabelPage::kPrimary, 4, LabelKind::kBoolProperty, properties::play_container_animation },
		{ LabelPage::kPrimary, 5, LabelKind::kBoolProperty, properties::enable_carry_weight_limit },
		{ LabelPage::kPrimary, 6, LabelKind::kBoolProperty, properties::loot_is_deliver_to_player },
		{ LabelPage::kPrimary, 7, LabelKind::kBoolProperty, properties::display_pickup_message },
		{ LabelPage::kPrimary, 8, LabelKind::kBoolProperty, properties::enable_looting_in_settlement },
		{ LabelPage::kPrimary, 9, LabelKind::kBoolProperty, properties::automatically_link_and_unlink_to_workshop },
		{ LabelPage::kPrimary, 10, LabelKind::kBoolProperty, properties::unlock_locked_container },
		{ LabelPage::kPrimary, 11, LabelKind::kFloatProperty, properties::looting_range },
		{ LabelPage::kPrimary, 12, LabelKind::kFloatProperty, properties::looting_range },
		{ LabelPage::kPrimary, 13, LabelKind::kIntProperty, properties::carry_weight },
		{ LabelPage::kPrimary, 14, LabelKind::kIntProperty, properties::carry_weight },
		{ LabelPage::kSecondary, 1, LabelKind::kMaskBit, properties::kEnableFormTypeACTI },
		{ LabelPage::kSecondary, 2, LabelKind::kMaskBit, properties::kEnableFormTypeALCH },
		{ LabelPage::kSecondary, 3, LabelKind::kMaskBit, properties::kEnableFormTypeAMMO },
		{ LabelPage::kSecondary, 4, LabelKind::kMaskBit, properties::kEnableFormTypeARMO },
		{ LabelPage::kSecondary, 5, LabelKind::kMaskBit, properties::kEnableFormTypeBOOK },
		{ LabelPage::kSecondary, 6, LabelKind::kMaskBit, properties::kEnableFormTypeCONT },
		{ LabelPage::kSecondary, 7, LabelKind::kMaskBit, properties::kEnableFormTypeFLOR },
		{ LabelPage::kSecondary, 8, LabelKind::kMaskBit, properties::kEnableFormTypeINGR },
		{ LabelPage::kSecondary, 9, LabelKind::kMaskBit, properties::kEnableFormTypeKEYM },
		{ LabelPage::kSecondary, 10, LabelKind::kMaskBit, properties::kEnableFormTypeMISC },
		{ LabelPage::kSecondary, 11, LabelKind::kMaskBit, properties::kEnableFormTypeNPC_ },
		{ LabelPage::kSecondary, 12, LabelKind::kMaskBit, properties::kEnableFormTypeWEAP },
		{ LabelPage::kTertiary, 1, LabelKind::kLogLevel, 0 },
		{ LabelPage::kTertiary, 2, LabelKind::kLogLevel, 1 },
		{ LabelPage::kTertiary, 3, LabelKind::kLogLevel, 2 },
		{ LabelPage::kTertiary, 4, LabelKind::kLogLevel, 3 },
		{ LabelPage::kTertiary, 5, LabelKind::kLogLevel, 4 },
		{ LabelPage::kTertiary, 6, LabelKind::kLogLevel, 5 },
		{ LabelPage::kTertiary, 7, LabelKind::kLogLevel, 6 },
	};

	constexpr std::size_t kLabelRowCount = std::size(kLabelRows);
	constexpr std::size_t kLabelPageCount = 3;

	struct CapturedLabel
	{
		// The item text exactly as the plugin shipped it. Captured once per process
		// and never re-read from the item, so a composed label can never be composed
		// on top of an already composed one.
		std::string original;
		bool captured = false;
		// At most one skip warning per item for the whole process.
		bool prefixWarned = false;
		// At most one missing-item warning per item for the whole process.
		bool missingWarned = false;
		// At most one unavailable-value warning per item for the whole process.
		bool valueUnavailableWarned = false;
	};

	// Parallel to kLabelRows; guarded by `stateMutex`.
	CapturedLabel capturedLabels[kLabelRowCount];

	// Indexed by LabelPage; guarded by `stateMutex`. At most one unresolved-page
	// warning per page for the whole process.
	bool pageMissingWarned[kLabelPageCount]{};

	// Reported once per save load when a refresh is asked for before Papyrus has
	// published a resolved set of property values, so the silence is explained
	// exactly once per session instead of only on the first one.
	std::atomic<bool> cacheNotReadyReported{ false };

	// Reported once per process when there is no task interface to serialize the
	// write on, so the skipped refreshes are explained without one record each.
	std::atomic<bool> taskInterfaceMissingReported{ false };

	// Whether label composition may read the property cache. Both halves are
	// required: Papyrus has to have published a full update, and that update has to
	// have actually resolved its values. Without the second half a failed
	// resolution renders every getter's default as if it were the player's setting.
	bool CacheReady()
	{
		return cacheReady.load(std::memory_order_acquire) && properties::IsResolved();
	}

	// Callers must hold `stateMutex`.
	RE::BGSTerminal* PageForLocked(LabelPage page)
	{
		switch (page)
		{
		case LabelPage::kPrimary:
			return primaryPage;
		case LabelPage::kSecondary:
			return secondaryPage;
		default:
			return tertiaryPage;
		}
	}

	const char* PageLabel(LabelPage page)
	{
		switch (page)
		{
		case LabelPage::kPrimary:
			return kPrimaryPageLabel;
		case LabelPage::kSecondary:
			return kSecondaryPageLabel;
		default:
			return kTertiaryPageLabel;
		}
	}

	// Callers must hold `stateMutex`. Records the shipped text of every row that
	// has not been captured yet. Runs as soon as the page pointers are published,
	// which is before any write can reach those items, so what is captured is
	// always the plugin's own text.
	void CaptureOriginalLabelsLocked()
	{
		for (std::size_t index = 0; index < kLabelRowCount; ++index)
		{
			auto& captured = capturedLabels[index];
			if (captured.captured)
			{
				continue;
			}

			const auto& row = kLabelRows[index];
			auto* item = FindMenuItem(PageForLocked(row.page), row.itemId);
			if (!item)
			{
				continue;
			}

			auto current = ReadItemText(item->itemText);
			if (current.starts_with(kLocalizedStringPrefix))
			{
				// Left uncaptured and unreported here; the refresh reports the skip, so
				// the warning is emitted once regardless of which path noticed first.
				continue;
			}

			captured.original = std::move(current);
			captured.captured = true;
		}
	}

	std::string ResolveStateText(bool value)
	{
		return value
			? message_queue::ResolveText("$LTMN_CFG_STATE_ON", "On")
			: message_queue::ResolveText("$LTMN_CFG_STATE_OFF", "Off");
	}

	std::string FormatValueLabel(const std::string& name, const std::string& value)
	{
		return message_queue::FormatLocalizedText(
			"$LTMN_CFG_ITEM_LABEL",
			"{name} [{value}]",
			{ { "{name}", name }, { "{value}", value } });
	}

	// The one cache read a row makes. Taken once per row and then used for both the
	// availability decision and the rendered text: reading the cache twice would
	// take the properties module's lock twice, and an update landing between the two
	// acquisitions would let a row pass the availability check and still render from
	// a value that is no longer there.
	properties::Value RowValue(const LabelRow& row)
	{
		// Only the three property kinds carry a properties::Key in `payload`, so the
		// conversion happens per branch. A mask row's payload runs up to 2048, which
		// is outside the range that unfixed enum can represent.
		switch (row.kind)
		{
		case LabelKind::kBoolProperty:
		case LabelKind::kFloatProperty:
		case LabelKind::kIntProperty:
			return properties::Get(static_cast<properties::Key>(row.payload));
		case LabelKind::kMaskBit:
			// Every object-filter row reads the same packed mask.
			return properties::Get(properties::enabled_looting_form_type_mask);
		default:
			// The log level rows read the log settings, never the property cache.
			return properties::Value();
		}
	}

	// Whether `value`, the row's single cache snapshot, holds a value of the kind
	// this row renders. Decided per row rather than from the global readiness flag,
	// because that flag is a single witness for the whole cache: one property can
	// fail to resolve while the rest is published, and a single-property update can
	// publish a null value long after readiness was marked. Composing such a row
	// would put a fallback on the page as if it were the player's own setting.
	bool RowValueAvailable(const LabelRow& row, const properties::Value& value)
	{
		switch (row.kind)
		{
		case LabelKind::kBoolProperty:
			return value.type == properties::boolean;
		case LabelKind::kFloatProperty:
			return value.type == properties::decimal;
		case LabelKind::kIntProperty:
		case LabelKind::kMaskBit:
			return value.type == properties::integer;
		case LabelKind::kLogLevel:
			// The log level rows read the log settings, never the property cache.
			return true;
		default:
			return true;
		}
	}

	// The text a row should currently show, always composed from `original`, from
	// the same snapshot the availability decision was made on. `value` carries the
	// kind this row renders, and `logLevel` is the one level reading the whole
	// refresh shares.
	std::string ComposeRowText(
		const LabelRow& row,
		const std::string& original,
		const properties::Value& value,
		std::int32_t logLevel)
	{
		switch (row.kind)
		{
		case LabelKind::kBoolProperty:
			return FormatValueLabel(original, ResolveStateText(value.data.b));
		case LabelKind::kFloatProperty:
			return FormatValueLabel(original, utility::FormatConfigFloat(value.data.f));
		case LabelKind::kIntProperty:
			return FormatValueLabel(original, std::to_string(value.data.i));
		case LabelKind::kMaskBit:
			return FormatValueLabel(original, ResolveStateText((value.data.i & row.payload) != 0));
		case LabelKind::kLogLevel:
			// Exactly one item on the log level page carries the marker; every other
			// item is put back to its shipped text.
			return logLevel == row.payload
				? message_queue::FormatLocalizedText(
					  "$LTMN_CFG_ITEM_SELECTED", "{name} [*]", { { "{name}", original } })
				: original;
		default:
			return original;
		}
	}

	// Callers must hold `stateMutex`. Emitted at most once per item so a repeating
	// refresh cannot flood the log.
	void WarnLocalizedPrefixSkipLocked(CapturedLabel& captured, const LabelRow& row)
	{
		if (captured.prefixWarned)
		{
			return;
		}
		captured.prefixWarned = true;
		REX::WARN(
			"source=native component=terminal_labels event=label_skipped form={} item={} reason=in_place_splice_unsafe",
			PageLabel(row.page),
			row.itemId);
	}

	// Callers must hold `stateMutex`. Emitted at most once per page so a page that
	// never resolves cannot flood the log across repeated refreshes.
	void WarnPageMissingLocked(LabelPage page)
	{
		auto& warned = pageMissingWarned[static_cast<std::size_t>(page)];
		if (warned)
		{
			return;
		}
		warned = true;
		REX::WARN(
			"source=native component=terminal_labels event=label_skipped form={} reason=page_unresolved",
			PageLabel(page));
	}

	// Callers must hold `stateMutex`. Emitted at most once per item, for the item id
	// the table expects, so a record the plugin no longer ships is named instead of
	// being silently skipped.
	void WarnItemMissingLocked(CapturedLabel& captured, const LabelRow& row)
	{
		if (captured.missingWarned)
		{
			return;
		}
		captured.missingWarned = true;
		REX::WARN(
			"source=native component=terminal_labels event=label_skipped form={} item={} reason=item_missing",
			PageLabel(row.page),
			row.itemId);
	}

	// Callers must hold `stateMutex`. Emitted at most once per item, so a row whose
	// value never resolves names itself once instead of on every refresh.
	void WarnValueUnavailableLocked(CapturedLabel& captured, const LabelRow& row)
	{
		if (captured.valueUnavailableWarned)
		{
			return;
		}
		captured.valueUnavailableWarned = true;
		REX::WARN(
			"source=native component=terminal_labels event=label_skipped form={} item={} reason=value_unavailable",
			PageLabel(row.page),
			row.itemId);
	}

	// Callers must hold `stateMutex`. Puts one row back to the text the plugin
	// shipped. Obeys exactly the rules a composing write obeys: it resolves the page
	// and the item first, leaves an item carrying the in-place splice prefix
	// untouched, and assigns from an owning live buffer. A row that was never
	// captured has nothing to put back and still carries its shipped text, so it is
	// left alone.
	void RestoreOriginalLocked(const LabelRow& row, CapturedLabel& captured)
	{
		if (!captured.captured)
		{
			return;
		}

		auto* page = PageForLocked(row.page);
		if (!page)
		{
			WarnPageMissingLocked(row.page);
			return;
		}

		// Items are found by their record id; the array is never resized, reordered,
		// inserted into or erased from.
		auto* item = FindMenuItem(page, row.itemId);
		if (!item)
		{
			WarnItemMissingLocked(captured, row);
			return;
		}

		if (ReadItemText(item->itemText).starts_with(kLocalizedStringPrefix))
		{
			WarnLocalizedPrefixSkipLocked(captured, row);
			return;
		}

		// `restored` owns a NUL-terminated buffer that stays alive across the
		// assignment, which is what the engine's string interning requires.
		const std::string restored = captured.original;
		item->itemText = std::string_view{ restored };
	}

	void LogPageRefreshCount(const char* formLabel, std::uint32_t writtenCount)
	{
		REX::DEBUG(
			"source=native component=terminal_labels event=labels_refreshed form={} written={}",
			formLabel,
			writtenCount);
	}

	// Rewrites every row of the label table from its captured original text.
	// Idempotent: the same property values and log level always produce the same
	// text, because nothing is ever composed on top of a previous composition.
	void RunRefreshAll(std::uint32_t queuedEpoch)
	{
		if (!CacheReady())
		{
			// Before Papyrus publishes resolved property values there is nothing to
			// render, and rendering the getters' defaults would put settings on the page
			// that the player never chose. Reported once per session, because this path
			// can be reached on every menu open.
			if (!cacheNotReadyReported.exchange(true, std::memory_order_acq_rel))
			{
				REX::DEBUG("source=native component=terminal_labels event=labels_refreshed outcome=cache_not_ready");
			}

			// Leaving the pages as they are would keep whatever an earlier successful
			// refresh composed, so a value the cache can no longer vouch for would stay
			// on screen for as long as readiness never returns. Every row goes back to
			// its shipped text instead, under the same lock and the same write rules the
			// composing path uses.
			std::lock_guard<std::mutex> lock(stateMutex);
			// The same reason the composing path compares here: a load boundary can
			// clear the page pointers and a post-load resolve can publish new ones,
			// and this refresh belongs to the session it was queued under.
			if (queuedEpoch != loadEpoch.load(std::memory_order_acquire))
			{
				return;
			}

			for (std::size_t index = 0; index < kLabelRowCount; ++index)
			{
				RestoreOriginalLocked(kLabelRows[index], capturedLabels[index]);
			}
			return;
		}

		// Read once for the whole refresh. Sampling it per row lets a level change
		// land between two rows and mark both of them, or neither; one reading always
		// marks exactly the row that holds the level it named.
		const auto logLevel = log_settings::GetLogLevel();

		std::uint32_t written[kLabelPageCount]{};
		{
			std::lock_guard<std::mutex> lock(stateMutex);
			// Compared here rather than before the lock: in the gap between an unlocked
			// check and the assignment, a load boundary can clear the page pointers and a
			// post-load resolve can publish new ones, and this refresh would then write
			// the incoming pages from the outgoing session's property values.
			if (queuedEpoch != loadEpoch.load(std::memory_order_acquire))
			{
				return;
			}

			for (std::size_t index = 0; index < kLabelRowCount; ++index)
			{
				const auto& row = kLabelRows[index];
				auto& captured = capturedLabels[index];

				auto* page = PageForLocked(row.page);
				if (!page)
				{
					WarnPageMissingLocked(row.page);
					continue;
				}

				// Items are found by their record id; the array is never resized,
				// reordered, inserted into or erased from.
				auto* item = FindMenuItem(page, row.itemId);
				if (!item)
				{
					WarnItemMissingLocked(captured, row);
					continue;
				}

				auto current = ReadItemText(item->itemText);
				if (current.starts_with(kLocalizedStringPrefix))
				{
					WarnLocalizedPrefixSkipLocked(captured, row);
					continue;
				}

				if (!captured.captured)
				{
					captured.original = std::move(current);
					captured.captured = true;
				}

				// One read of the property cache serves both the decision below and the
				// text composed from it, so no update can slip in between the two and make
				// the row render something the check never saw.
				const auto value = RowValue(row);

				// A row whose value did not resolve is put back to its shipped text and is
				// counted apart from the written rows, so neither a fallback nor a value
				// read before the failure can stand on the page as the player's setting.
				if (!RowValueAvailable(row, value))
				{
					// `restored` owns a NUL-terminated buffer that stays alive across the
					// assignment, which is what the engine's string interning requires.
					const std::string restored = captured.original;
					item->itemText = std::string_view{ restored };
					WarnValueUnavailableLocked(captured, row);
					continue;
				}

				// `composed` owns a NUL-terminated buffer that stays alive across the
				// assignment, which is what the engine's string interning requires.
				const std::string composed = ComposeRowText(row, captured.original, value, logLevel);
				item->itemText = std::string_view{ composed };
				++written[static_cast<std::size_t>(row.page)];
			}
		}

		// The root page and the utility page hold no settings. Their zero counts are
		// reported too, so their exclusion is observable instead of merely absent.
		LogPageRefreshCount(kRootPageLabel, 0);
		LogPageRefreshCount(kPrimaryPageLabel, written[static_cast<std::size_t>(LabelPage::kPrimary)]);
		LogPageRefreshCount(kSecondaryPageLabel, written[static_cast<std::size_t>(LabelPage::kSecondary)]);
		LogPageRefreshCount(kTertiaryPageLabel, written[static_cast<std::size_t>(LabelPage::kTertiary)]);
		LogPageRefreshCount(kUtilityPageLabel, 0);
	}

	// Every refresh is handed to the F4SE task interface, so writes triggered from
	// the menu event sink and from the Papyrus property hook run serialized on one
	// queue instead of on their caller's thread. It also keeps the event sink from
	// ever waiting on `stateMutex` while the event source holds its own lock.
	// `epoch` is the load epoch the caller read its values under; the queued work
	// carries it and drops itself if a save load has moved the module on since.
	void QueueRefreshAll(std::uint32_t epoch)
	{
		auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface)
		{
			// Running the write here instead would put it on the Papyrus VM thread, or
			// inside the menu event sink while the event source holds its own lock. The
			// refresh is skipped and the next trigger retries. Reported once per process.
			if (!taskInterfaceMissingReported.exchange(true, std::memory_order_acq_rel))
			{
				REX::DEBUG("source=native component=terminal_labels event=labels_refreshed outcome=task_interface_missing");
			}
			return;
		}

		if (refreshPending.exchange(true, std::memory_order_acq_rel))
		{
			// A refresh is already queued and has not started running yet. It reads the
			// property values and the log level when it runs, so it renders this
			// trigger's state as well and a second task would only repeat the work.
			return;
		}

		try
		{
			taskInterface->AddTask([epoch]() {
				// Cleared as this work begins, not when it ends: a trigger that arrives
				// while the rewrite is running has to queue its own refresh, because this
				// one may already have read the values it changed. It is also cleared ahead
				// of the epoch check below, so work a load boundary supersedes still leaves
				// the flag clear and the next trigger queues its own refresh normally.
				refreshPending.store(false, std::memory_order_release);
				try
				{
					// The epoch is decisive only inside the write's critical section, but
					// checking it here too drops work queued before a save load without
					// taking the lock for it.
					if (epoch != loadEpoch.load(std::memory_order_acquire))
					{
						return;
					}
					RunRefreshAll(epoch);
				}
				catch (...)
				{
				}
			});
		}
		catch (...)
		{
			// Handing the work over allocates a delegate and can therefore fail. The
			// refresh was never queued, so the flag has to go back: left set, it would
			// suppress every later trigger for the rest of the process in favour of a
			// refresh that does not exist.
			refreshPending.store(false, std::memory_order_release);
			throw;
		}
	}

	class MenuEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			// The event source calls sinks under its own lock, so the body stays
			// short, never waits on a lock of its own and hands anything expensive to
			// the task queue. Nothing may escape from here into engine dispatch.
			try
			{
				const auto* rawName = a_event.menuName.c_str();
				const std::string menuName = rawName ? rawName : "";

				// The config holotape presents through the terminal menu. The design
				// assumes the opening event arrives before the page list is built.
				// That ordering was observed against an inline write; whether the
				// task queue drains before the list is built has not been measured,
				// so a refresh queued here is not proven to land on the text the page
				// is about to read.
				if (a_event.opening && std::string_view{ menuName } == RE::TerminalMenu::MENU_NAME)
				{
					// The menu opens in the session the module is on right now, so this
					// trigger carries the current epoch.
					QueueRefreshAll(loadEpoch.load(std::memory_order_acquire));
				}
			}
			catch (...)
			{
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// Shared by Initialize() and OnPostLoadGame(): resolves the config pages,
	// publishes the cached page pointers and captures the shipped label text of
	// every table row before anything can write to those items. Kept in one place
	// so a save load re-resolves through the exact same path the initial process
	// load used.
	void ResolveAndPublishConfigPages()
	{
		auto* primary = ResolvePage(kPrimaryPageForm);
		auto* secondary = ResolvePage(kSecondaryPageForm);
		auto* tertiary = ResolvePage(kTertiaryPageForm);

		{
			std::lock_guard<std::mutex> lock(stateMutex);
			primaryPage = primary;
			secondaryPage = secondary;
			tertiaryPage = tertiary;
			CaptureOriginalLabelsLocked();
		}
	}

	void RegisterMenuSinkOnce()
	{
		if (sinkRegistered.load(std::memory_order_acquire))
		{
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui)
		{
			// Without the UI singleton the sink cannot register, so labels stop
			// refreshing on menu open and only update when a setting changes. Reported
			// once per process instead of once per refresh, because nothing here is a
			// refresh attempt.
			if (!sinkRegistrationUiMissingReported.exchange(true, std::memory_order_acq_rel))
			{
				REX::DEBUG("source=native component=terminal_labels event=labels_refreshed outcome=ui_missing");
			}
			return;
		}

		// Outlives registration for the rest of the process; never unregistered.
		static MenuEventSink sink;
		ui->RegisterSink<RE::MenuOpenCloseEvent>(&sink);
		sinkRegistered.store(true, std::memory_order_release);
	}
}

namespace terminal_labels
{
	void Initialize()
	{
		// Called directly by the F4SE message dispatcher, which has no exception
		// boundary of its own. Nothing thrown inside this module may escape here,
		// the same protection the menu event sink and the queued task body already
		// give their own bodies.
		try
		{
			// Resolves the config pages and captures the shipped label text before any
			// write can reach those items. Safe to run again on a later game load: the
			// page pointers are simply re-resolved.
			ResolveAndPublishConfigPages();

			RegisterMenuSinkOnce();
		}
		catch (...)
		{
		}
	}

	void OnPreLoadGame()
	{
		// Called directly by the F4SE message dispatcher; see Initialize() for why
		// nothing thrown below may leave this function.
		try
		{
			// The captured original labels and the per-item warning flags are
			// deliberately kept: originals are captured once per process, so a
			// recomposed label can never stack on top of an already composed one.
			std::lock_guard<std::mutex> lock(stateMutex);
			// Anything queued under the previous epoch is dropped when it runs. The
			// increment shares this critical section with the clears below, so a refresh
			// that already holds the lock cannot compare against an epoch that goes stale
			// while it is still assigning: it sees the whole boundary or none of it.
			loadEpoch.fetch_add(1, std::memory_order_acq_rel);
			cacheReady.store(false, std::memory_order_release);
			// Reset with the readiness flag it reports on, so a refresh skipped in the
			// incoming session explains itself instead of being silently dropped because
			// an earlier session had already used up the one report.
			cacheNotReadyReported.store(false, std::memory_order_release);
			// A refresh queued for the outgoing session no longer stands in for one the
			// incoming session asks for. Cleared under the same lock as the epoch, so the
			// flag and the boundary it belongs to are never observed apart; a queued task
			// the boundary supersedes also clears the flag before it rejects itself, so no
			// trigger is left waiting on a refresh that will never run.
			refreshPending.store(false, std::memory_order_release);
			primaryPage = nullptr;
			secondaryPage = nullptr;
			tertiaryPage = nullptr;
		}
		catch (...)
		{
		}
	}

	void OnPostLoadGame()
	{
		// Called directly by the F4SE message dispatcher; see Initialize() for why
		// nothing thrown below may leave this function.
		try
		{
			// kGameLoaded fires once per process, not once per save load, so nothing
			// else re-resolves the page pointers OnPreLoadGame() just cleared.
			ResolveAndPublishConfigPages();

			// Retries sink registration in case Initialize() ran before the UI
			// singleton existed. RegisterMenuSinkOnce() stays one-shot on success and
			// keeps its own bounded report, so a repeated failure here does not add a
			// new record on every save load.
			RegisterMenuSinkOnce();
		}
		catch (...)
		{
		}
	}

	std::uint32_t CurrentLoadEpoch()
	{
		return loadEpoch.load(std::memory_order_acquire);
	}

	void MarkPropertyCacheReady(std::uint32_t epoch)
	{
		// Called directly by the Papyrus VM; see Initialize() for why nothing thrown
		// below may leave this function.
		try
		{
			if (epoch != loadEpoch.load(std::memory_order_acquire))
			{
				// The update this readiness stands for began before a save load, so the
				// values it published describe the outgoing session. Marking the cache ready
				// would declare them current for the incoming one.
				return;
			}
			// A full property refresh means the native cache now holds published values,
			// so label composition can rely on them.
			cacheReady.store(true, std::memory_order_release);
		}
		catch (...)
		{
		}
	}

	void RefreshAll(std::uint32_t epoch)
	{
		// Called directly by the Papyrus VM via papyrus_lootman::OnUpdateLootManProperty,
		// which has no exception boundary of its own; see Initialize() for the same
		// protection. QueueRefreshAll() already resets refreshPending before rethrowing
		// on a failed enqueue, so that reset still happens here; this boundary only
		// stops the rethrown exception from reaching the caller.
		try
		{
			if (epoch != loadEpoch.load(std::memory_order_acquire))
			{
				// The values this refresh would render were read for a session the module
				// has already left behind. Rechecked when the queued work runs and again
				// under the write's own lock, because the boundary can also land later.
				return;
			}
			QueueRefreshAll(epoch);
		}
		catch (...)
		{
		}
	}
}
