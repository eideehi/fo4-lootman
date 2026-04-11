# Project Rules

## .reference/ - read-only legacy code

The `.reference/` directory contains the previous version (v1) of this project's source code.
It exists solely as a reference for understanding old behavior and design decisions.

- NEVER modify, delete, rename, or create files inside `.reference/`
- NEVER use `.reference/` files as templates or copy them into the active codebase
- You MAY read files in `.reference/` to understand legacy behavior when relevant to current work

## Submodules -- absolutely read-only

Git submodule contents are immutable in this project.

- NEVER modify, delete, rename, or create files inside any submodule directory
- NEVER run restore/checkout/reset/format/edit commands inside a submodule
- Specifically, `commonlibf4-plugin/lib/commonlibf4/` must never be changed
- If a submodule appears dirty, STOP and report it to the user without changing anything

## CommonLibF4 -- do not use `structure_wrapper`

Do not use `RE::BSScript::structure_wrapper<"Foo", "Bar">`.

There is a known bug where it triggers a `BSFixedString` assertion in debug
builds.

Cause:

- `structure_wrapper::name` is a `std::string_view` derived from `TStaticString`.
- The internal `char c[N]` buffer of `TStaticString` does not include a trailing
  NUL.
- When that `name` is passed to VM APIs such as `CreateStruct` or
  `GetScriptStructType`, it is implicitly converted to `const BSFixedString&`.
- That conversion can hit the assertion in `BSFixedString.h` that checks
  `view.data()[view.length()] == value_type{}`.

Workaround:

- When a Papyrus struct is needed, call the VM API directly.
- Pass the type name as a `const char*` literal or construct a
  `RE::BSFixedString` from a string literal.

```cpp
// Bad: can trip the debug assertion.
using MyStruct = RE::BSScript::structure_wrapper<"ScriptName", "StructName">;

// Good: string literals use the safe BSFixedString(const_pointer) path.
RE::BSFixedString typeName("ScriptName#StructName");
RE::BSTSmartPointer<RE::BSScript::Struct> st;
vm->CreateStruct(typeName, st);
// Set fields with st->Find("fieldName"sv)->Pack(...).
```

## Native DLL builds must go through packaging

When building or verifying the CommonLibF4 native plugin from WSL, do not run
`xmake` directly from `commonlibf4-plugin/` as the primary workflow. Use the
packaging CLI from `packaging/` so the repository's staged WSL-to-Windows build
path is exercised consistently.

- Run build commands from `packaging/` only
- Prefer `pnpm run build -- --no-papyrus` for native DLL verification
- Use `pnpm run build` only when Papyrus/archive outputs are also required
- If the staged Windows build tree needs to be reset, use
  `pnpm run clean:wsl-build`
- Assume `packaging/.env`, `pnpm install`, and Windows tool reachability are
  prerequisites for DLL builds

## xEdit VMAD Cleanup Workflow

When editing staged plugin VMAD data, do not improvise. Use the staged xEdit
workflow below. In this repository, the current release target is
`LootMan.esp`, and the validated quest IDs and safe-removal lists remain
project-specific.

### Preconditions

- Treat the xEdit executable path as a variable and verify it before use. Only
  after verification may you rely on a concrete path such as
  `C:\Programs\xEdit\xFOEdit64.exe`.
- xEdit does not have a true nogui/headless mode in this workflow. Expect a
  GUI window and launch it minimized.
- Do not edit the live game `Data\<TargetPlugin>` in place for release work.
- Do not write scripts into the xEdit install directory unless the user
  explicitly asks for that. Use workspace-local scripts with `-S:`.

### Required workspace layout

Use `tools/xedit/` for all automation assets.

- `tools/xedit/stage-<locale>/` is a temporary FO4 Data directory for a staged
  locale build.
- Keep xEdit scripts, logs, dumps, and reports under `tools/xedit/`.
- In this repository, the current staged locales are `stage-en/` and
  `stage-ja/`.

### Stage setup

For each locale, create a minimal staged Data directory containing only:

- `Fallout4.esm`
- the target plugin file for that stage

Example:

```powershell
$workspace = (Resolve-Path '.').Path
$locale = '<locale>'
$targetPlugin = '<TargetPlugin>'
$stageName = 'stage-' + $locale
$stageDir = Join-Path $workspace ('tools\xedit\' + $stageName)
$gameDataDir = 'G:\steam\steamapps\common\Fallout 4\Data'
$sourcePlugin = Join-Path $workspace ('packaging\resources\<packaging-subdir>\' + $locale + '\' + $targetPlugin)
$stagedPlugin = Join-Path $stageDir (Split-Path $sourcePlugin -Leaf)

New-Item -ItemType Directory -Force $stageDir | Out-Null
Copy-Item (Join-Path $gameDataDir 'Fallout4.esm') (Join-Path $stageDir 'Fallout4.esm') -Force
Copy-Item $sourcePlugin $stagedPlugin -Force
```

In this repository, use `<locale>` = `en` or `ja`,
`<TargetPlugin>` = `LootMan.esp`, and `<packaging-subdir>` = `lootman`.

### Required xEdit command-line pattern

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
$targetPlugin = '<TargetPlugin>'

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

Meaning of the switches used here:

- `-S:` sets the xEdit scripts path to the workspace so scripts do not need to
  be copied into `C:\Programs\xEdit\Edit Scripts`.
- `-R:` writes the xEdit session log to a known file in the workspace.
- `-D:` points xEdit at the staged Data directory instead of the live game Data
  directory.
- `-autoload` loads the requested files immediately.
- `-nobuildrefs` avoids unnecessary reference building during scripted VMAD
  edits.
- `-script:` runs the named xEdit script from the `-S:` directory.

### Scripts to use

These scripts are expected to live in `tools/xedit/`.

- `ListLoadedFiles.pas`: verifies that `-D:` worked and only the expected files
  are loaded.
- `ListLootManQuests.pas`: lists quest records in the single staged target
  plugin. The filename is historical, but the script should not depend on a
  hardcoded plugin path.
- `DumpLootManProperties.pas`: dumps the `LTMN_Properties` VMAD tree for
  verification. The filename is historical, but the script should operate on
  the single staged target plugin.
- `RemoveUnusedProperties.pas`: removes only the validated unused properties
  from `LTMN2:Properties`.

### Verified quest IDs

For the current `LootMan.esp` release workflow, the fixed quest records used by
scripts and native code are:

- `LTMN_System` -> `[QUST:FE000F99]`
- `LTMN_Properties` -> `[QUST:FE000F9A]`
- `LTMN_MCM` -> `[QUST:FE000F9B]`

Do not rename the plugin or compact/reassign these FormIDs.

### Safe VMAD removals for 3.0.0

Only these `LTMN2:Properties` VMAD properties are currently validated for
removal:

- `TemporaryContainerRef`
- `BobbyPin`
- `Locksmith01`
- `Locksmith02`
- `Locksmith03`
- `Locksmith04`
- `ObjectTypeLooseMod`

Do not remove these from VMAD yet, even if they look stale, because code and/or
MCM still reference them:

- `MaxWorkerThreads*`
- `ActiveWorkerThreads*`
- `TurboMode*`
- `WorkerManagerACTI` through `WorkerManagerWEAP`

Do not remove `DeliveredToPlayerWithoutLogs` during the 3.0.0 release cleanup
because migration code still reads it.

### Save behavior

Do not explicitly overwrite `DataPath + <TargetPluginFileName>` from inside the
xEdit script with `FileWriteToStream`. xEdit already saves the modified staged
plugin on shutdown, and forcing a same-file write from the script causes a
file-lock error.

Expected log behavior after a successful cleanup includes lines like:

- `Saving: <TargetPlugin>.save...`
- `Queued renaming of save ... to ...\<TargetPlugin> on shutdown.`
- `Done saving.`

For the current release target, `<TargetPlugin>` is `LootMan.esp`.

### Verification procedure

After each scripted cleanup:

1. Re-run `DumpLootManProperties.pas` against the same staged directory.
2. Confirm the dump no longer contains any of these names:
   - `TemporaryContainerRef`
   - `BobbyPin`
   - `Locksmith01`
   - `Locksmith02`
   - `Locksmith03`
   - `Locksmith04`
   - `ObjectTypeLooseMod`
3. Confirm the remaining properties are still present:
   - `ActivatorRef`
   - `LootManLocation`
   - `LootManRef`
   - `LootManWorkshopRef`
   - `Pipboy`
   - `RadioInstitute`
   - `ShipmentItemList`
   - `WorkshopCaravan`
   - `WorkshopParent`
4. Confirm the staged target plugin hash changed relative to the original source
   file.
5. Only then copy the staged plugin back to the corresponding packaging
   location for each locale in this repository:
   - `packaging\resources\lootman\en\LootMan.esp`
   - `packaging\resources\lootman\ja\LootMan.esp`

### Known-good notes

This workflow has already been validated in this repository with:

- workspace-local scripts loaded via `-S:`
- staged Data directories loaded via `-D:`
- minimized xEdit launches via `Start-Process -WindowStyle Minimized`
- VMAD verification dumps written under `tools/xedit/` with stage-scoped names
  such as `xedit-vmad-dump-stage-<locale>.txt`

If xEdit shows a GUI script compatibility error, first verify:

- the script is loaded through `-S:` from the workspace
- the filename passed to `-script:` matches the script file exactly
- the script unit name matches the filename without the `.pas` extension
