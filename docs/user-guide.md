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

- Fallout 4 with the runtime option exposed by the installer: `1.10.163`.
- MCM version 2 or later.
- F4SE and a load order setup capable of loading native F4SE plugins.

This repository does not pin current third-party download links or exact
external dependency versions. Prefer the current instructions from each
dependency's own release page when setting up F4SE or MCM.

## Install and Update

### Installer Choices

The current FOMOD installer exposes these options:

- Language: English or Japanese.
- Game version: `1.10.163`.
- DLL type: product or debug.
- Papyrus source code: optional.

Choose the product DLL for normal play. The debug DLL writes much more log
output and is intended for troubleshooting. Papyrus source files are not needed
for normal play.

### Updating From Older Versions

LootMan 3.0.0 supports overwrite updates from LootMan 2.x.

LootMan 3.0.0 does not support overwrite updates from LootMan 1.x. If you are
upgrading from 1.x, uninstall 1.x and make a clean save before installing
3.0.0.

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

### Utility Actions

LootMan includes MCM utility actions for item movement and scrapping:

- Move Items moves all items or a selected item type between the player,
  LootMan, and nearby workshop inventories.
- Scrap Items scraps all items or selected weapon, apparel, or junk categories
  from the selected player, LootMan, or nearby workshop inventory.

After starting a utility, close the MCM menu and wait for the completion
notification. Utilities do not run while LootMan is unavailable, already
uninstalled, or busy with another utility.

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

LootMan configuration is organized by MCM page.

### General Settings

Use General Settings for the main behavior of the mod:

- Enable or disable recurring LootMan looting.
- Show or hide LootMan system messages.
- Set the native DLL log level. Changes are saved to
  `Data/F4SE/Plugins/LootMan/config.json`.
- Play pickup sounds or container animations while looting.
- Set looting range and carry-weight behavior.
- Deliver loot to the player inventory.
- Suppress native pickup messages during LootMan looting and delivery.
- Exclude settlement objects from looting.
- Automatically link or unlink with a nearby workshop.
- Unlock locked containers by consuming a bobby pin.

### Looting

Use the Looting page to control how often LootMan works and what it is allowed
to take:

- Looting Interval sets the interval for recurring looting. Turn off
  `Enable LootMan` to stop recurring looting.
- Native Looting Budget controls how much work the native looting pass may do
  each interval.
- Object Filter enables looting for world object types such as activators,
  chemistry or food, ammo, apparel, books, containers, flora, ingredients,
  keys, junk or mods, corpses, and weapons.
- Inventory Filter controls which item types are taken from corpses and
  containers.
- Advanced Filter adds item-specific filters, including legendary equipment,
  explosives, chemistry or food subtypes, perk magazines, bobbleheads, and
  weapon subtypes.

Set a category off when you never want LootMan to take that type of object or
item.

### Utility

Use Utility for one-off maintenance actions:

- Move Items for transferring inventory contents between supported locations.
- Scrap Items for scrapping selected categories from supported locations.

Close MCM after starting a utility and wait for the completion message.

### System

Use System for install status and maintenance:

- Force Install starts LootMan's installation process. Use it if LootMan is not
  installed after exiting Vault 111.
- Uninstall starts LootMan's in-game uninstall process and disables all LootMan
  features.

Use the in-game uninstall process before removing the mod or before reinstalling
to troubleshoot a broken setup.

### Hotkeys

Configure hotkeys in MCM:

- Toggle Enable LootMan toggles LootMan on or off.
- Open LootMan's Inventory opens LootMan's inventory.
- Toggle Link To Workshop switches the link status between LootMan and a nearby
  workshop.
- Execute Looting runs one looting pass for each key press.
- Dump Nearby Object Diagnostics writes detailed nearby object diagnostics to
  the LootMan log. Use this only when troubleshooting.

Hotkey actions do nothing when LootMan is not installed, not initialized, or
already uninstalled.

## Troubleshooting

Check MCM first:

- If LootMan is not installed after exiting Vault 111, use
  `System -> Force Install`.
- If LootMan says a feature is unavailable, confirm LootMan is installed and
  not already uninstalled.
- If no items are looted, check `Enable LootMan`, range, carry weight, overweight
  behavior, settlement exclusion, and object or inventory filters.
- If settlement objects are not looted, check the settlement exclusion setting.
- If locked containers are ignored, check locked-container handling and make
  sure the player has a bobby pin.
- If workshop linking fails, move near the intended workshop and try
  `Toggle Link To Workshop` again.
- If a utility does not run, wait for any existing utility to finish, then try
  again after LootMan is installed and initialized.
- If you need detailed object information for a bug report, use
  `Dump Nearby Object Diagnostics` and inspect the LootMan log.

LootMan can display system messages for installation, uninstallation, enabled
or disabled state, overweight state, settlement looting exclusion, workshop link
state, missing bobby pins, and utility completion.

## Uninstalling

Use `MCM -> System -> Uninstall` before removing the mod. Wait for the
uninstallation completion message.

For LootMan 1.x to 3.0.0 upgrades, uninstall 1.x and make a clean save before
installing 3.0.0. For LootMan 2.x to 3.0.0, overwrite updates are supported.

## Source, Credits, and Licensing

The repository contains LootMan's native plugin, Papyrus scripts, translations,
and packaging assets.

Repository content is MIT by default. The `commonlibf4-plugin/` area is licensed
separately under GPL-3.0 because the native plugin build is based on the
CommonLibF4 template. See the repository README and license files for the exact
terms.

The repository contributor list includes Al12rs.
