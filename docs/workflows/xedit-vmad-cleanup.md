# xEdit VMAD Cleanup Workflow

When editing staged plugin VMAD data, do not improvise. In this repository, the
current release target is `LootMan.esp`, and the validated quest IDs and
safe-removal lists are project-specific.

## Preconditions

- Treat the xEdit executable path as a variable and verify it before use.
- xEdit does not have a true nogui/headless mode in this workflow. Expect a GUI
  window and launch it minimized.
- Do not edit the live game `Data\LootMan.esp` in place for release work.
- Do not write scripts into the xEdit install directory unless the user
  explicitly asks for that. Use workspace-local scripts with `-S:`.

## Required Workspace Layout

Use `tools/xedit/` for all automation assets.

- `tools/xedit/stage-<locale>/` is a temporary FO4 Data directory for a staged locale build.
- Keep xEdit scripts, logs, dumps, and reports under `tools/xedit/`.
- Current staged locales are `stage-en/` and `stage-ja/`.

## Stage Setup

For each locale, create a minimal staged Data directory containing only:

- `Fallout4.esm`
- `LootMan.esp`

```powershell
$workspace = (Resolve-Path '.').Path
$locale = '<locale>'
$targetPlugin = 'LootMan.esp'
$stageName = 'stage-' + $locale
$stageDir = Join-Path $workspace ('tools\xedit\' + $stageName)
$gameDataDir = 'G:\steam\steamapps\common\Fallout 4\Data'
$sourcePlugin = Join-Path $workspace ('packaging\resources\lootman\' + $locale + '\' + $targetPlugin)
$stagedPlugin = Join-Path $stageDir (Split-Path $sourcePlugin -Leaf)

New-Item -ItemType Directory -Force $stageDir | Out-Null
Copy-Item (Join-Path $gameDataDir 'Fallout4.esm') (Join-Path $stageDir 'Fallout4.esm') -Force
Copy-Item $sourcePlugin $stagedPlugin -Force
```

Use `<locale>` = `en` or `ja`.

## Required xEdit Command Pattern

Always point xEdit at the workspace scripts path and the staged Data path.

```powershell
$xEditExe = 'C:\Programs\xEdit\xFOEdit64.exe'
$workspace = (Resolve-Path '.').Path
$locale = '<locale>'
$stageName = 'stage-' + $locale
$stageDir = Join-Path $workspace ('tools\xedit\' + $stageName)
$scriptsDir = Join-Path $workspace 'tools\xedit'
$logPath = Join-Path $workspace ('tools\xedit\' + $stageName + '-run.log')
$scriptName = 'DumpWorkerManagerVMAD.pas'
$targetPlugin = 'LootMan.esp'

Start-Process -FilePath $xEditExe -WindowStyle Minimized -ArgumentList @(
  '-S:' + $scriptsDir,
  '-R:' + $logPath,
  '-D:' + $stageDir,
  '-autoload',
  '-nobuildrefs',
  '-script:' + $scriptName,
  $targetPlugin
) -Wait
```

Switch meanings:

- `-S:` sets the xEdit scripts path to the workspace.
- `-R:` writes the xEdit session log to a known workspace file.
- `-D:` points xEdit at the staged Data directory.
- `-autoload` loads the requested files immediately.
- `-nobuildrefs` avoids unnecessary reference building.
- `-script:` runs the named script from the `-S:` directory.

## Scripts

`tools/xedit/` does not hold a fixed, permanent script inventory. Most
cleanup passes are done with single-use list/dump/remove `.pas` scripts
written for that specific pass, then deleted once the resulting plugin
change is validated and committed (for example, the mutation script for the
worker-manager cleanup below was dropped after its one-time job completed;
see commit `2fb51cd`). The `ListLoadedFiles.pas`, `ListLootManQuests.pas`,
`DumpLootManProperties.pas`, and `RemoveUnusedProperties.pas` scripts
formerly referenced here followed the same pattern and no longer exist in
the repo. If you need that behavior again, write a new script of the same
shape rather than expecting a durable copy to be present.

Any new or historical script name should not depend on a hardcoded plugin
path.

`tools/xedit/DumpWorkerManagerVMAD.pas` is a reusable, read-only exception to
that pattern. It dumps the full VMAD element tree, attached script list, and
property list for the twelve `LTMN_WorkerManager{ACTI,ALCH,AMMO,ARMO,BOOK,
CONT,FLOR,INGR,KEYM,MISC,NPC_,WEAP}` quest records (form range
`01000F9C`-`01000FA7`) from a `stage-en` or `stage-ja` directory, after
validating that only `Fallout4.esm` and `LootMan.esp` (plus the xEdit-exposed
`Fallout4.exe` runtime module, when present) are loaded. It writes a
timestamped report file under `tools/xedit/` and makes no plugin edits. See
"Worker-Manager Self-Reference Removal" below.

## Verified Quest IDs

- `LTMN_System` -> `[QUST:FE000F99]`
- `LTMN_Properties` -> `[QUST:FE000F9A]`
- `LTMN_MCM` -> `[QUST:FE000F9B]`

Do not rename the plugin or compact/reassign these FormIDs.

## Safe VMAD Removals

This list is cumulative across releases: entries are added as further
properties are validated for removal, and it is not tied to one version.

Only these `LTMN2:Properties` VMAD properties are validated for removal:

- `TemporaryContainerRef`
- `BobbyPin`
- `Locksmith01`
- `Locksmith02`
- `Locksmith03`
- `Locksmith04`
- `ObjectTypeLooseMod`

None of the above have been removed yet: `papyrus/Scripts/Source/User/LTMN2/Properties.psc`
still declares all seven as `auto const mandatory`, and all seven still
appear in the compiled `LTMN_Properties` VMAD in both
`packaging/resources/lootman/en/LootMan.esp` and
`packaging/resources/lootman/ja/LootMan.esp`. This list only records what is
safe to remove once that cleanup pass is actually run.

Do not remove yet:

- `WorkerManagerACTI` through `WorkerManagerWEAP` - these are `LTMN2:System`
  properties declared on `LTMN_System` (not `LTMN2:Properties`), still
  `auto const mandatory` in `System.psc`, still present in both compiled
  plugins, and still actively read (`System.psc` calls `.Stop()` on each).
  They are in active use, not merely unvalidated for removal.

Already removed in a previous pass (kept here as a record; there is nothing
left to remove for these):

- `MaxWorkerThreads*`
- `ActiveWorkerThreads*`
- `TurboMode*`

None of these three appear anywhere in `papyrus/Scripts/Source/` or in
either compiled `LootMan.esp` anymore.

`DeliveredToPlayerWithoutLogs` is a special case: the Papyrus property
declaration is intentionally kept in `Properties.psc` ("Legacy property kept
so existing saves can migrate their setting") and is read and written by
`Patch.psc` for save migration, but it no longer appears in either compiled
plugin's VMAD. Do not delete the Papyrus declaration or the `Patch.psc`
migration code; there is no VMAD entry left to remove.

## Worker-Manager Self-Reference Removal

Distinct from the `LTMN2:Properties` safe-removal list above. Validated by
commit `2fb51cd` (`git show 2fb51cd` for the full description) and already
applied to both localized plugins.

This class removes an obsolete quest-level self-reference `Object` property
(for example, the property on `LTMN_WorkerManagerACTI`'s manager script that
pointed back at `LTMN_WorkerManagerACTI` itself) from each of the twelve
inert legacy `LTMN_WorkerManager{ACTI,ALCH,AMMO,ARMO,BOOK,CONT,FLOR,INGR,
KEYM,MISC,NPC_,WEAP}` quest records (form range `01000F9C`-`01000FA7`).

Validation required before mutation:

- (a) The target manager script declares no Papyrus properties at all (see
  e.g. `papyrus/Scripts/Source/User/LTMN2/Looting/WorkerManagerACTI.psc`), so
  the compiled VMAD property is a true orphan.
- (b) The record's VMAD header version, object format, and attached-script
  count match an expected shape. Abort before mutation if any record
  differs.
- (c) The removal is done as validate -> rebuild-from-clean-donor ->
  re-validate, not in-place property deletion, because this xEdit build does
  not reliably persist in-place VMAD property removal.
- (d) Donor records are rotated so every rebuilt record is rebuilt from an
  already-validated-clean donor.

`tools/xedit/DumpWorkerManagerVMAD.pas` is the read-only inspection tool for
this class; use it to re-verify record state before and after a mutation
pass, and see "Scripts" above for what it dumps.

## Save Behavior

Do not explicitly overwrite `DataPath + <TargetPluginFileName>` from inside the
xEdit script with `FileWriteToStream`. xEdit saves the modified staged plugin on
shutdown, and forcing a same-file write from the script causes a file-lock
error.

Expected successful log lines include:

- `Saving: LootMan.esp.save...`
- `Queued renaming of save ... to ...\LootMan.esp on shutdown.`
- `Done saving.`

## Verification

After each scripted cleanup:

1. Re-run a read-only dump script against the same staged directory to
   confirm the change. For the worker-manager self-reference removal class,
   use `tools/xedit/DumpWorkerManagerVMAD.pas`. For other VMAD property
   removals, write a dump script following the same read-only pattern (the
   original `DumpLootManProperties.pas` no longer exists in the repo; see
   "Scripts" above).
2. Confirm the dump no longer contains the safe-removal names.
3. Confirm these remaining properties are still present:
   - `ActivatorRef`
   - `LootManLocation`
   - `LootManRef`
   - `LootManWorkshopRef`
   - `Pipboy`
   - `RadioInstitute`
   - `ShipmentItemList`
   - `WorkshopCaravan`
   - `WorkshopParent`
4. Confirm the staged target plugin hash changed relative to the original source file.
5. Only then copy the staged plugin back to:
   - `packaging\resources\lootman\en\LootMan.esp`
   - `packaging\resources\lootman\ja\LootMan.esp`
