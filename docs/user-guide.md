# LootMan User Guide

LootMan is a Fallout 4 mod that automates nearby item collection. It can loot
loose objects, corpses, and containers, then deliver matching items to LootMan,
the player inventory, or a nearby workshop depending on your settings.

## At a Glance

- Automatic looting for nearby world objects, corpses, and containers.
- Filters for object types, inventory item types, and selected advanced item
  categories.
- Optional delivery to the player inventory or workshop-linked workflows.
- MCM pages for setup, looting filters, utilities, system actions, and hotkeys.
- In-game uninstall support for clean removal and broken-install recovery.

This guide is for players. Build steps, release packaging, and contributor
workflows are documented elsewhere in this repository.

## Requirements

LootMan ships a native F4SE plugin, Papyrus scripts, MCM configuration,
localized assets, and a FOMOD installer.

Before installing, make sure your setup supports the mod's current packaged
requirements:

- Fallout 4 with the runtime option exposed by the installer: `1.11.191`.
- MCM version 2 or later.
- F4SE and a load order setup capable of loading native F4SE plugins.

This repository does not pin current third-party download links or exact
external dependency versions. Prefer the current instructions from each
dependency's own release page when setting up F4SE or MCM.

## Install and Update

### Installer Choices

The current FOMOD installer presents the following groups in order:

1. **About Compatibility** — a required acknowledgement of the 3.0.1 update
   compatibility notice. You cannot deselect it.
2. **Select plugin language.** — choose exactly one of English or Japanese for
   `LootMan.esp`.
3. **MCM translation files** — required shared MCM text for supported Fallout 4
   language codes. German and Japanese include localized text; other
   non-English languages use English fallback text.
4. **Select your game version.** — `1.11.191` is the only option and is
   required.
5. **Do you want to install the papyrus source code?** — choose `No`
   (recommended) or `Yes`. Papyrus source files are not needed for normal play.

### Updating From Older Versions

LootMan 3.0.1 supports overwrite updates from LootMan 2.x and 3.0.0.

LootMan 3.0.1 does not support overwrite updates from LootMan 1.x. If you are
upgrading from 1.x, uninstall 1.x and make a clean save before installing
3.0.1.

When a save from 2.x is loaded under 3.0.1, the v3.0.0 save migration can
change two MCM settings:

- If your 2.x save was using manual-only looting (`Looting Interval` set to
  `0`), `Enable LootMan` is turned off and `Looting Interval` is reset to
  `1.0` seconds. Open `General Settings` and turn `Enable LootMan` back on if
  you want recurring looting to resume.
- `Suppress Looting Pickup Messages` may be re-derived from your prior
  configuration. If pickup messages are now hidden or shown when you did not
  expect, adjust it in `General Settings`.

If you are removing LootMan completely or troubleshooting a broken install, use
LootMan's in-game uninstall flow before reinstalling or removing files.

## What LootMan Does

### Nearby Looting

When enabled, LootMan runs recurring looting passes around the player. It can
inspect nearby loose objects, corpses, and containers, then take items that pass
your configured filters.

The Execute Looting hotkey runs one manual looting pass each time it is pressed.
This is useful when you want direct control instead of waiting for the recurring
interval.

### LootMan Inventory and Delivery

LootMan can hold collected items, move items to the player inventory, or work
with a nearby workshop link. General settings control range, carry-weight
behavior, pickup sounds, pickup-message suppression, settlement exclusions, and
locked-container handling.

### Workshop Linking

LootMan can link or unlink with a nearby workshop. Workshop linking is used by
delivery and utility workflows that move items between the player, LootMan, and
the workshop.

If linking fails, move closer to the intended workshop and try again.

When `Automatically Link / Unlink To Workshop` is on, the link is updated as
you travel: the previously auto-linked workshop is unlinked and a new
player-owned workshop at your current location is linked. The link and unlink
notifications include the workshop's location name.

### Utility Actions

LootMan includes MCM utility actions for item movement and scrapping:

- Move Items moves all items or a selected item type between the player,
  LootMan, and nearby workshop inventories.
- Scrap Items scraps all items or selected weapon, apparel, or junk categories
  from the selected player, LootMan, or nearby workshop inventory.

After starting a utility, close the MCM menu and wait for the completion
notification. Utilities do not run while LootMan is unavailable, already
uninstalled, or busy with another utility.

### The LootMan Trunk

LootMan ships a world object called **LootMan Trunk**. Activating it opens the
LootMan workshop inventory, the same container that workshop-linked delivery
and utility workflows use.

The trunk's in-world placement is defined by `LootMan.esp`. Look for it in
your in-game workshop build menu in settlements where you intend to use
workshop-linked workflows.

The workshop menu entry is stored as static ESP workshop menu records. MCM
uninstall clears LootMan runtime state, but it cannot remove those already-loaded
static workshop menu records from an existing save after `LootMan.esp` has been
loaded.

## How to Use LootMan

1. Install LootMan with the normal product DLL.
2. Load the game, open MCM, and go to `LootMan`.
3. Check `System`. The status should show that LootMan is installed.
4. If LootMan is not installed after exiting Vault 111, use
   `System -> Force Install`.
5. Open `General Settings` and turn on `Enable LootMan`.
6. Set your looting range, carry-weight behavior, delivery preference, and
   workshop behavior.
7. Open `Looting` and choose which object, inventory, and item categories
   LootMan may take.
8. Assign optional hotkeys from `Hotkeys`.
9. Close MCM and play normally.

If an MCM page says a feature is unavailable, LootMan is not ready to use or has
already been uninstalled.

## Configuration

LootMan configuration is organized by MCM page. Numeric ranges below are the
limits enforced by the MCM sliders.

### General Settings

| Control | Type | Range / Step | Purpose |
| --- | --- | --- | --- |
| `Enable LootMan` | switch | on / off | Master switch for recurring looting and most throttled system messages. When off, recurring looting stops. The `Execute Looting` hotkey still runs a manual pass each press because it forces the looting call; the other hotkeys (toggle enable, open inventory, toggle workshop link, dump diagnostics) also keep working. |
| `Display System Message` | switch | on / off | Suppresses LootMan's throttled HUD messages and the immediate workshop link/unlink and utility-complete messages. Install and uninstall completion messages bypass this toggle and always display. |
| `Native Log Level` | dropdown | Trace, Debug, Info, Warn, Error, Critical, Off | Controls the native DLL log level. Changes are saved to `Data/F4SE/Plugins/LootMan/config.json`. |
| `Play Pickup Sound` | switch | on / off | Plays sound effects when items are looted. |
| `Play Container Animation` | switch | on / off | Plays the open animation on containers LootMan loots from. |
| `Looting Range` | slider | 1.0 – 256, step 0.5 (meters) | Distance from the player searched on each looting pass. The native plugin clamps the effective range at 200 meters, so slider values above 200 do not enlarge the search beyond 200 meters in 3.0.1. |
| `Carry Weight` | slider | 100 – 10000, step 100 (pounds) | LootMan's maximum carry weight. |
| `Ignore Overweight` | switch | on / off | When on, LootMan continues looting past the carry-weight limit and suppresses the overweight HUD message. |
| `Loot Is Deliver To Player` | switch | on / off | When on, looted items are added to the player inventory instead of LootMan's inventory. |
| `Suppress Looting Pickup Messages` | switch | on / off | Hides Fallout 4's native pickup messages while LootMan loots items or delivers them to the player. |
| `Not Looting From Settlement` | switch | on / off | When on, LootMan does not loot objects inside settlements you own or near their workshops. |
| `Automatically Link / Unlink To Workshop` | switch | on / off | When on, LootMan links and unlinks with the workshop at your current location automatically as you travel. |
| `Unlock Locked Container` | switch | on / off | When on, LootMan consumes Bobby pins to unlock containers that you have the Locksmith perks to pick. Pins are taken from the player inventory first; if the player has none, LootMan falls back to pins stored in the LootMan workshop container. |

### Looting

The Looting page has four sections.

**Looting Interval**

| Control | Type | Range / Step | Purpose |
| --- | --- | --- | --- |
| `Looting Interval` | slider | 0.1 – 10, step 0.1 (seconds) | Interval between recurring looting passes. Recurring looting stops when `Enable LootMan` is off; hotkey looting still works. |

**Native Looting Budget**

These controls cap how much work the native looting pass may do each interval.

| Control | Type | Range / Step | Purpose |
| --- | --- | --- | --- |
| `Use Time Budget` | switch | on / off | When on, the count limits below are ignored and each pass stops after the time budget. |
| `Time Budget` | slider | 0.5 – 30, step 0.5 (milliseconds) | Maximum native processing time per pass. Used only while `Use Time Budget` is on. |
| `Max Objects Per Pass` | slider | 1 – 256, step 1 | Maximum number of nearby objects the native pass may process per interval. |
| `Max Containers Per Pass` | slider | 0 – 64, step 1 | Maximum number of containers processed per pass. Set to 0 to skip containers. |
| `Max Corpses Per Pass` | slider | 0 – 64, step 1 | Maximum number of dead actors processed per pass. Set to 0 to skip corpses. |
| `Max Activators / Flora Per Pass` | slider | 0 – 128, step 1 | Maximum number of activators and flora processed per pass. Set to 0 to skip both. |

**Object Filter**

Enables or disables looting for each type of object placed in the world.

- `Allow Looting Of Activator Object`
- `Allow Looting Of Chemistry / Food Object`
- `Allow Looting Of Ammo Object`
- `Allow Looting Of Apparel Object`
- `Allow Looting Of Book Object`
- `Allow Looting Of Container Object`
- `Allow Looting Of Flora Object`
- `Allow Looting Of Ingredient Object`
- `Allow Looting Of Key Object`
- `Allow Looting Of Junk / Mod Object`
- `Allow Looting Of Corpse Object`
- `Allow Looting Of Weapon Object`

**Inventory Filter**

Enables or disables looting per item type when LootMan reads inventories of
corpses and containers. The inventory filter does not include Activator,
Container, Flora, or Corpse, because those categories only apply to objects in
the world, not items inside an inventory.

- `Allow Looting Of Chemistry / Food Item`
- `Allow Looting Of Ammo Item`
- `Allow Looting Of Apparel Item`
- `Allow Looting Of Book Item`
- `Allow Looting Of Ingredient Item`
- `Allow Looting Of Key Item`
- `Allow Looting Of Junk / Mod Item`
- `Allow Looting Of Weapon Item`

**Advanced Filter**

Item-level toggles applied on top of the inventory and object filters.

- Equipment:
  - `Legendary Only` — when on, only legendary armor and weapons are looted.
  - `Always Looting Explosives` — when on, explosives are looted even while
    `Legendary Only` is on.
- Chemistry / Food: `Alcohol`, `Chemistry`, `Food`, `Nuka-Cola`, `Stimpak`,
  `Syringer Ammo`, `Water`, `Other`.
- Book: `Perk Magazine`, `Other`.
- Junk / Mod: `Bobblehead`, `Other`.
- Weapon: `Grenade`, `Mine`, `Other`.

### Utility

The Utility page exposes two actions. Each action displays a busy notice while
running, and shows a completion message when it finishes.

**Move Items**

| Control | Choices |
| --- | --- |
| `Source Inventory` | Player, LootMan, Workshop |
| `Destination Inventory` | Player, LootMan, Workshop |
| `Item Type To Move` | All, Weapon, Apparel, Chemistry / Food, Junk, Mod, Ammo, Book, Key |
| `Execute Move Items` | Button. Close MCM after starting. |

**Scrap Items**

| Control | Choices |
| --- | --- |
| `Target Inventory` | Player, LootMan, Workshop |
| `Item Type To Scrap` | All, Weapon, Apparel, Junk |
| `Execute Scrap Items` | Button. Close MCM after starting. |

Scrap intentionally omits Chemistry / Food, Mods, Ammo, Book, and Key.

### System

The System page shows installation status and exposes install and uninstall
actions:

- Status reads `[ LootMan Is Not Installed ]`, `[ LootMan Is Installed ]`, or
  `[ LootMan Is Uninstalled ]`.
- The `Force Install` section exposes an `Execute Force Install` button.
  Use it if LootMan is not installed after exiting Vault 111.
- The `Uninstall` section exposes an `Execute Uninstall` switch. Turning it on
  reveals a warning and a `[ Yes ]` button that starts LootMan's in-game
  uninstall process, clears LootMan runtime state, and disables runtime LootMan
  behavior.

Use the in-game uninstall process before removing the mod or before
reinstalling to troubleshoot a broken setup.

### Hotkeys

All hotkeys ship unbound and accept modifier keys. Assign them from the Hotkeys
page.

| Hotkey | Behavior |
| --- | --- |
| `Toggle Enable LootMan` | Toggles `Enable LootMan` on and off. |
| `Open LootMan's Inventory` | Opens LootMan's inventory. |
| `Toggle Link To Workshop` | Switches the link status between LootMan and a nearby workshop. |
| `Toggle Execute Looting` | Runs one looting pass per key press. |
| `Dump Nearby Object Diagnostics` | Writes detailed nearby object diagnostics to the LootMan log. Use this only when troubleshooting. |

Hotkey actions do nothing when LootMan is not installed, not initialized, or
already uninstalled.

## Behavior Reference

This section documents player-visible behaviors that are not captured by the
MCM controls alone.

### Install Gate

After a fresh game, the LootMan install process waits until either the player
is holding the Pip-Boy or the Institute Radio quest is running. The system
re-checks roughly every ten seconds until one of those conditions is met, then
runs the install and posts the installation HUD message. This is why a brand
new save can show `LootMan Is Not Installed` for a short time after exiting
Vault 111. If the wait is unusually long, use `System -> Force Install`.

### Locked Containers and Locksmith Perks

When `Unlock Locked Container` is on, LootMan only opens a locked container if
the player meets the corresponding Locksmith perk requirement and Bobby pins
are available. Pin availability is checked first against the player inventory
and then against the LootMan workshop container; the first source that holds
enough pins is debited:

| Lock level | Required perk | Bobby pins consumed |
| --- | --- | --- |
| Unlocked | none | 0 |
| Easy | none | 1 |
| Average | Locksmith 1 | 2 |
| Hard | Locksmith 2 | 3 |
| Very Hard | Locksmith 3 | 4 |

If the player has Locksmith 4 (Master), no Bobby pins are consumed regardless
of lock level. Containers that fail the perk or pin check are silently skipped;
LootMan does not post a HUD message for them.

### Automatic Workshop Link / Unlink

When `Automatically Link / Unlink To Workshop` is on, LootMan reacts to player
location changes:

- The previously auto-linked workshop is unlinked when you leave it.
- If the new location has a workshop you own, LootMan links to it.
- Link and unlink HUD messages include the workshop's location name (for
  example, `[LootMan] Workshop linked: Sanctuary.`).

If LootMan does not auto-link, use `Toggle Link To Workshop` near the intended
workshop.

### Shipment Interception

If a shipment form is added to LootMan's NPC reference, LootMan removes it from
that reference and adds it to the LootMan workshop container. This prevents
shipments from being absorbed by the workshop's workbench, which would
otherwise lose the shipment's payload.

### Settlement Exclusion

When `Not Looting From Settlement` is on, LootMan pauses looting when the
player enters a settlement location, a workshop-settlement location, or comes
near an owned workshop. A throttled reminder HUD message can appear when
entering such a location. Looting resumes when you leave the settlement.

### Carry-Weight Behavior

LootMan enforces carry weight differently depending on the delivery mode:

- **Workshop delivery** (`Loot Is Deliver To Player` off, the default mode).
  LootMan tracks the LootMan workshop container's inventory weight against the
  `Carry Weight` slider. When the weight exceeds the limit and `Ignore Overweight`
  is off, LootMan stops looting and a throttled overweight HUD message can
  appear. When the weight drops back below the limit, looting resumes.
- **Delivery to player** (`Loot Is Deliver To Player` on). Looted items go
  straight to the player, so the LootMan workshop container stays empty and
  the `Carry Weight` slider does not gate looting. Instead, the native plugin
  uses the player character's current carry capacity (the vanilla `CarryWeight`
  actor value, including any perks, equipment, or mods that change it). When
  the player is at or over their personal carry capacity, delivery-to-player
  looting stops silently — the throttled `[LootMan] Maximum carry-weight has
  been exceeded.` message described above only tracks the workshop container,
  not the player, so it will not appear in this mode.

`Ignore Overweight` bypasses both checks: when on, both the workshop-mode
stop and the delivery-to-player capacity stop are disabled and LootMan keeps
looting regardless of either limit.

### Uninstall Returns Items

Running `System -> Uninstall` attempts to move every item held by LootMan and
by the LootMan workshop container back into the player's inventory before
stopping LootMan's quest. The Papyrus return path does not verify the transfer
result, so check the player inventory (and consider re-opening the LootMan
trunk while the quest is still running) before removing the mod files.

### System Message Throttling

Each LootMan HUD message has its own throttle. By default the same message
cannot repeat within 30 seconds; after 600 seconds of silence the throttle
counter resets. If a HUD message you expected does not appear, it is most
likely throttled.

A few messages bypass the throttle and always display when triggered:

- Workshop link and unlink messages.
- Utility processing complete.

Turning off `Display System Message` suppresses every throttled message and the
immediate workshop and utility messages above. Installation and uninstallation
completion messages are routed directly through the native plugin and bypass
the `Display System Message` toggle, so they always display regardless of the
setting.

### Native Plugin Config (`Data/F4SE/Plugins/LootMan/config.json`)

The native DLL reads and writes a JSON config relative to the Fallout 4
executable at `Data/F4SE/Plugins/LootMan/config.json`. The file is created with
default values the first time LootMan starts, and the MCM `Native Log Level`
dropdown writes to it.

Current schema (only `log.level` is read):

```json
{
    "log": {
        "level": "info"
    }
}
```

Accepted `log.level` values:

- String: `trace`, `debug`, `info`, `warn` (also `warning`), `error` (also
  `err`), `critical`, `off`.
- Integer: `0` through `6`, mapping in the same order as the string list.

The default is `info`. If the file is missing, malformed, or contains an
unknown value, the native plugin logs a warning and starts the next session at
`info`; the previous level is not preserved across a bad hand-edit. Use the
MCM `Native Log Level` dropdown or fix the file value and restart Fallout 4 to
restore the level you want.

### Injection Data (`Data/LootMan/*.json`)

Injection data is separate from the native plugin config above. It is advanced
configuration for players and modlist maintainers who need to tune special
looting rules, exclusions, advanced item classification, or native pickup item
notification filters.

LootMan ships the default data as
`Data/LootMan/injection-data-default.json`. The native plugin loads regular
`.json` files from `Data/LootMan` on startup, in sorted file order. Make edits
while Fallout 4 is closed, then restart before testing. Prefer a separate
custom file, such as `Data/LootMan/zzz-custom-injection-data.json`, instead of
editing the packaged default file directly; back up local edits before
updating.

Injection data refines special cases. It does not globally override disabled
MCM object or inventory categories, native preconditions, settlement exclusion,
locked-container checks, carry-weight checks, or advanced subtype toggles. For
example, adding a form to a subtype list helps LootMan classify it, but the
matching MCM subtype still needs to be enabled before LootMan takes it.

Use identifiers in this format:

```text
PluginFilename|localHexFormID
```

The filename can be an `.esm`, `.esp`, or another loaded plugin filename. The
form ID is hexadecimal and belongs to that source plugin's local record, not
the current load-order-prefixed runtime ID. For example,
`Fallout4.esm|03000C` refers to the `03000C` record from `Fallout4.esm`.

Custom files should use arrays of strings:

```json
{
    "include": {
        "activator": [
            "Fallout4.esm|03000C"
        ]
    },
    "exclude": {
        "form": [
            "Fallout4.esm|1A62D4"
        ]
    },
    "notify": {
        "item": [
            "Fallout4.esm|03000C"
        ],
        "category": [
            "ALCH",
            "AMMO",
            "MISC"
        ],
        "legendary-equipment": true
    }
}
```

List paths add array values across sorted files. Do not use scalar strings in
custom files unless you intentionally want to replace that path. The
recommended player format is arrays.

`/notify/category` replaces the previous category mask whenever a later sorted
file contains it. Use the full final category list in that file. Supported
tokens are `ALCH`, `AMMO`, `ARMO`, `BOOK`, `INGR`, `KEYM`, `MISC`, and `WEAP`.
`/notify/legendary-equipment` replaces the previous boolean whenever a later
sorted file contains it. Use the final `true` or `false` value in that file.

Supported paths:

| JSON pointer | Values | Behavior |
| --- | --- | --- |
| `/include/activation-block` | Forms or keywords | Allows listed activation-blocked or activation-ignored objects through that special protection. |
| `/include/activator` | Forms or keywords | Allows listed activator objects to be considered for looting. |
| `/include/featured-item` | Forms or keywords | Allows listed featured items through the featured-item protection. |
| `/include/quest-item` | Forms or keywords | Allows listed quest items or quest-flagged world objects through the quest-item protection. |
| `/include/unique-item` | Forms or keywords | Allows listed unique items through the unique-item protection. |
| `/exclude/form` | Forms | Prevents listed forms from being looted. |
| `/exclude/keyword` | Keywords | Prevents forms or objects with listed keywords from being looted. |
| `/notify/item` | Forms or keywords | Shows native pickup item notifications when a looted item matches one of these entries. |
| `/notify/category` | Category tokens | Replaces the native pickup item notification category mask. |
| `/notify/legendary-equipment` | Boolean | Shows native pickup item notifications for legendary armor and weapons when `true`. |
| `/alch-type/alcohol` | Forms or keywords | Classifies matching chemistry / food items as Alcohol for the Advanced Filter. |
| `/alch-type/chemistry` | Forms or keywords | Classifies matching chemistry / food items as Chemistry. |
| `/alch-type/food` | Forms or keywords | Classifies matching chemistry / food items as Food. |
| `/alch-type/nuka-cola` | Forms or keywords | Classifies matching chemistry / food items as Nuka-Cola. |
| `/alch-type/stimpak` | Forms or keywords | Classifies matching chemistry / food items as Stimpak. |
| `/alch-type/syringe-ammo` | Forms or keywords | Classifies matching chemistry / food items as Syringer Ammo. |
| `/alch-type/water` | Forms or keywords | Classifies matching chemistry / food items as Water. |
| `/book-type/perk-magazine` | Forms or keywords | Classifies matching books as Perk Magazine. Prefer this spelling in new custom files. |
| `/book-type/park-magazine` | Forms or keywords | Compatibility spelling accepted by LootMan and used by the shipped default file; it maps to Perk Magazine. |
| `/misc-type/bobblehead` | Forms or keywords | Classifies matching junk / mod items as Bobblehead. |
| `/weap-type/grenade` | Forms or keywords | Classifies matching weapons as Grenade. |
| `/weap-type/mine` | Forms or keywords | Classifies matching weapons as Mine. |

The `notify` paths control native pickup item notifications only. They do not
change the system HUD message table or the `Display System Message` behavior
described later in this guide.

Malformed JSON can prevent LootMan's native plugin from loading. Invalid item
types inside a valid JSON file are logged and skipped or degraded where
possible. Unresolved form identifiers are logged and skipped after the game
loads. If `Data/LootMan` or `injection-data-default.json` is removed, restore
the packaged injection data directory or reinstall LootMan.

## Troubleshooting

Check MCM first:

- If LootMan is not installed after exiting Vault 111, use
  `System -> Force Install`. The install gate above explains why this can
  happen on new saves.
- After updating from LootMan 2.x, check that `Enable LootMan` is on. The
  v3.0.0 save migration can disable it when your 2.x save was using
  manual-only looting.
- If LootMan says a feature is unavailable, confirm LootMan is installed and
  not already uninstalled.
- If no items are looted, check `Enable LootMan`, `Looting Range`,
  `Carry Weight`, `Ignore Overweight`, `Not Looting From Settlement`, and the
  Object, Inventory, and Advanced filters on the Looting page.
- If looting stops while `Loot Is Deliver To Player` is on and
  `Ignore Overweight` is off, check the player character's own carry capacity
  — delivery-to-player uses the vanilla `CarryWeight` actor value (perks,
  equipment, mods included) and stops silently when the player is overweight,
  regardless of the `Carry Weight` slider value. Turn on `Ignore Overweight`
  to bypass both the workshop-mode and player-mode carry-weight stops.
- If settlement objects are not looted, check `Not Looting From Settlement`.
- If locked containers are skipped, confirm you have the matching Locksmith
  perk for the lock level, that pins are available in the player inventory
  or in the LootMan workshop container, and that `Unlock Locked Container` is
  on. LootMan skips containers silently when these checks fail.
- If workshop linking fails, move near the intended workshop and try
  `Toggle Link To Workshop` again.
- If a utility does not run, wait for any existing utility to finish, then try
  again after LootMan is installed and initialized.
- If an expected HUD message does not appear, it may be throttled (see the
  throttle rules above) or `Display System Message` may be off.
- If LootMan stops loading after you edit injection data, check the LootMan log
  for JSON parse errors, then fix or remove the malformed custom file and
  restart Fallout 4. If `Data/LootMan` or the packaged default JSON file is
  missing, restore it from the installer or reinstall LootMan.
- If a custom injection-data entry does not work, check the plugin filename and
  source-local hexadecimal form ID. Unresolved identifiers are logged and
  skipped.
- If you need detailed object information for a bug report, use
  `Dump Nearby Object Diagnostics` and inspect the LootMan log. Injection-data
  reasons include `excluded_by_injection_data` and `requires_include_*` values
  such as `requires_include_activator` or `requires_include_quest_item`.

To raise or lower the native log verbosity without opening MCM, edit
`Data/F4SE/Plugins/LootMan/config.json` while the game is closed and restart
Fallout 4.

## Uninstalling

Use `MCM -> System -> Uninstall` before removing the mod. Wait for the
uninstallation completion message. MCM uninstall clears LootMan runtime state
and attempts to return LootMan-held and LootMan-workshop-held items to the
player; verify the player inventory before deleting the mod files.

MCM uninstall cannot remove static ESP workshop menu records that are already
loaded into an existing save. After uninstalling, make a cleanup save before
removing files. If you hard-remove `LootMan.esp` or its assets from a save that
still references those static records, another workshop-menu mod may still warn
about the missing LootMan Trunk entry.

For LootMan 1.x to 3.0.1 upgrades, uninstall 1.x and make a clean save before
installing 3.0.1. For LootMan 2.x or 3.0.0 to 3.0.1, overwrite updates are
supported.

## Source, Credits, and Licensing

The repository contains LootMan's native plugin, Papyrus scripts, translations,
and packaging assets.

Repository content is MIT by default. The `commonlibf4-plugin/` area is licensed
separately under GPL-3.0 because the native plugin build is based on the
CommonLibF4 template. See the repository README and license files for the exact
terms.

The repository contributor list includes Al12rs.

## Appendix: System Messages

LootMan's HUD messages and their triggers. The throttled column indicates
whether the 30-second / 600-second throttle described above applies, and the
`Display System Message` column indicates whether the `Display System Message`
toggle suppresses the row.

| English text | Trigger | Throttled | `Display System Message` |
| --- | --- | --- | --- |
| `[LootMan] Installation is complete.` | LootMan finishes its install process. | No | Bypassed (always displays) |
| `[LootMan] Uninstallation is complete.` | LootMan finishes its uninstall process. | No | Bypassed (always displays) |
| `[LootMan] LootMan is enabled.` | The `Toggle Enable LootMan` hotkey turned LootMan on. The MCM `Enable LootMan` switcher does not post this message. | No | Suppressed when off |
| `[LootMan] LootMan is disabled.` | The `Toggle Enable LootMan` hotkey turned LootMan off. The MCM `Enable LootMan` switcher does not post this message. | No | Suppressed when off |
| `[LootMan] Maximum carry-weight has been exceeded.` | Workshop container weight passes `Carry Weight` while `Ignore Overweight` is off. | Yes | Suppressed when off |
| `[LootMan] Looting from settlements has been disabled.` | Player enters a settlement or owned workshop while `Not Looting From Settlement` is on. | Yes | Suppressed when off |
| `[LootMan] Could not find the workshop.` | `Toggle Link To Workshop` could not find a workshop nearby. | No | Suppressed when off |
| `[LootMan] Linked to the workshop.` | LootMan linked to a workshop whose location has no name. | No | Suppressed when off |
| `[LootMan] Workshop linked: {workshopName}.` | LootMan linked to a workshop whose location has a name. The name is substituted into `{workshopName}`. | No | Suppressed when off |
| `[LootMan] Unlinked to the workshop.` | LootMan unlinked from a workshop whose location has no name. | No | Suppressed when off |
| `[LootMan] Workshop unlinked: {workshopName}.` | LootMan unlinked from a workshop whose location has a name. The name is substituted into `{workshopName}`. | No | Suppressed when off |
| `[LootMan] Utility processing is complete.` | A Move Items or Scrap Items utility finishes. | No | Suppressed when off |

Turning off `Display System Message` suppresses every row except the two
install / uninstall completion messages, which are routed directly through the
native plugin and always display.

One additional localized string ships with LootMan but is not displayed by
the current 3.0.1 code paths:

- `[LootMan] Locked container ignored because there is no Bobby pin.`

Failed unlock attempts are skipped silently in this version; if you see this
string at runtime, it has been triggered by a future change or by another mod.
