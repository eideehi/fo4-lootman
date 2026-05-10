# Project Rules

## LLM-First Routing

- Start with `README.md` and `docs/project-map.md` to identify the owning area
  before editing.
- Run Node tooling from the repository root through root `package.json`.
- Keep implementation in the domain that owns it:
  - `packaging/` for release build, deploy, archive, clean, and FOMOD resources.
  - `tools/native-hooks/` for native-hook address maintenance.
  - `tools/ghidra/` for Ghidra automation and reports.
  - `commonlibf4-plugin/` for native plugin source.
  - `papyrus/` and `translation/` for their respective source assets.
- Do not create a root `scripts/` directory that swallows `packaging/` or
  `tools/`; those directories are domain boundaries.

## Immutable Areas

Git submodule contents are immutable in this project.

- NEVER modify, delete, rename, or create files inside any submodule directory.
- NEVER run restore, checkout, reset, format, or edit commands inside a submodule.
- Specifically, `commonlibf4-plugin/lib/commonlibf4/` must never be changed.
- If a submodule appears dirty, STOP and report it to the user without changing it.

## CommonLibF4 Hazards

Read `docs/constraints/commonlibf4-hazards.md` before touching native code that
uses CommonLibF4 VM or form APIs.

Hard rules:

- Do not use `RE::BSScript::structure_wrapper<"Foo", "Bar">`; call VM struct
  APIs directly with a string-literal-backed `RE::BSFixedString`.
- Do not call `BGSMod::Attachment::Mod::GetData()`; read the property-mod block
  directly through `GetBuffer<T>(kPMOD)`.
- Do not let the default `PackVariable<object T>` path marshal returned
  `TESForm`-derived pointers or vectors. Use the local safe TESForm pointer
  packing specialization pattern described in the hazards document.

## Native DLL Builds

When building or verifying the CommonLibF4 native plugin from WSL, do not run
`xmake` directly from `commonlibf4-plugin/` as the primary workflow. Use the
root package script that invokes the packaging CLI so the staged WSL-to-Windows
path is exercised consistently.

- Prefer `pnpm run package:build -- --no-papyrus` for native DLL verification.
- Use `pnpm run package:build` only when Papyrus/archive outputs are also required.
- If the staged Windows build tree needs to be reset, use
  `pnpm run package:clean:wsl-build`.
- Assume `packaging/.env`, root `pnpm install`, and Windows tool reachability
  are prerequisites for DLL builds.

## Ghidra Local Artifacts

- Keep Ghidra project databases outside this repository. Configure the
  Fallout 4 project directory through `FO4_GHIDRA_PROJECT_DIR`.
- Do not commit `tools/ghidra/projects/`; it is ignored spillover for local
  project databases only.
- Put reusable development-only reports under `tools/ghidra/reports/local/`.
- Do not commit generated `tools/ghidra/reports/` output unless it is required
  durable evidence for native-hook or Ghidra work.

## xEdit VMAD Cleanup

When editing staged plugin VMAD data, do not improvise. Use
`docs/workflows/xedit-vmad-cleanup.md`.

Hard rules:

- Do not edit the live game `Data\LootMan.esp` in place for release work.
- Do not write scripts into the xEdit install directory unless the user
  explicitly asks for that. Use workspace-local scripts with `-S:`.
- Use staged Data directories under `tools/xedit/stage-en/` and
  `tools/xedit/stage-ja/`.
- Current release target: `LootMan.esp`.
- Fixed quest records must not be renamed, compacted, or reassigned:
  - `LTMN_System` -> `[QUST:FE000F99]`
  - `LTMN_Properties` -> `[QUST:FE000F9A]`
  - `LTMN_MCM` -> `[QUST:FE000F9B]`
- Only the safe-removal list in the xEdit workflow document may be removed from
  VMAD for the current cleanup.

## Root Commands

- Install tooling dependencies: `pnpm install`
- Full tests: `pnpm run test`
- Packaging tests: `pnpm run test:packaging`
- Native-hook/Ghidra tests: `pnpm run test:tools`
- Release build: `pnpm run package:build`
- DLL-only verification: `pnpm run package:build -- --no-papyrus`
- Native-hook generation: `pnpm run native-hooks:generate`
- Native-hook verification: `pnpm run native-hooks:verify`
- Non-writing native-hook resolver: `pnpm run native-hooks:resolve`
- Ghidra probe: `pnpm run ghidra:probe`
