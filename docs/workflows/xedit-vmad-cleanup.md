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
$logPath = Join-Path $workspace ('tools\xedit\' + $stageName + '-remove.log')
$scriptName = 'RemoveUnusedProperties.pas'
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

These scripts are expected under `tools/xedit/`.

- `ListLoadedFiles.pas`
- `ListLootManQuests.pas`
- `DumpLootManProperties.pas`
- `RemoveUnusedProperties.pas`

The historical filenames should not depend on a hardcoded plugin path.

## Verified Quest IDs

- `LTMN_System` -> `[QUST:FE000F99]`
- `LTMN_Properties` -> `[QUST:FE000F9A]`
- `LTMN_MCM` -> `[QUST:FE000F9B]`

Do not rename the plugin or compact/reassign these FormIDs.

## Safe VMAD Removals For 3.0.0

Only these `LTMN2:Properties` VMAD properties are validated for removal:

- `TemporaryContainerRef`
- `BobbyPin`
- `Locksmith01`
- `Locksmith02`
- `Locksmith03`
- `Locksmith04`
- `ObjectTypeLooseMod`

Do not remove these yet:

- `MaxWorkerThreads*`
- `ActiveWorkerThreads*`
- `TurboMode*`
- `WorkerManagerACTI` through `WorkerManagerWEAP`
- `DeliveredToPlayerWithoutLogs`

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

1. Re-run `DumpLootManProperties.pas` against the same staged directory.
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
