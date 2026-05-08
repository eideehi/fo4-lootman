# Project Map

This is the LLM-first routing map for LootMan. Use it before editing so work
lands in the directory that owns the behavior.

## Ownership

| Path | Ownership | Notes |
| --- | --- | --- |
| `package.json` | Root Node command surface | Commands run from the repository root. Versions are exact-pinned from the lockfile. |
| `packaging/` | Release build/deploy/archive/clean implementation and FOMOD resources | No local package command surface. Root scripts call `packaging/scripts/cli.ts`. |
| `tools/native-hooks/` | Native hook address manifest, generator, verifier, resolver, and review bundles | Root commands use the `native-hooks:*` prefix. |
| `tools/ghidra/` | Ghidra headless config, scripts, projects, and reports | Root command uses `ghidra:probe`; Java scripts remain under `tools/ghidra/scripts/`. |
| `tools/xedit/` | Workspace-local xEdit automation assets | Use staged Data directories only. |
| `commonlibf4-plugin/` | Native F4SE plugin source | Verify WSL DLL builds through `pnpm run package:build -- --no-papyrus`. |
| `commonlibf4-plugin/lib/commonlibf4/` | Git submodule dependency | Read-only. Never edit or format. |
| `papyrus/` | Papyrus source scripts | Packaged through the packaging CLI. |
| `translation/` | Translation source assets | Maintains localized text. |
| `docs/` | Durable project knowledge | Use for workflows, constraints, and investigation records. |
| `plans/` | Implementation plans and research inputs | Treat current plan files as scoped execution contracts. |

## Command Routing

Run these from the repository root:

```bash
pnpm install
pnpm run test
pnpm run test:packaging
pnpm run test:tools
pnpm run package:build
pnpm run package:build -- --no-papyrus
pnpm run native-hooks:generate
pnpm run native-hooks:verify
pnpm run native-hooks:resolve
pnpm run ghidra:probe
```

Do not route native-hook or Ghidra work through `packaging/`. `packaging/`
owns release artifacts; `tools/` owns maintenance and analysis tooling.

## Detailed Workflows

- Packaging: `docs/workflows/packaging.md`
- Native hooks: `docs/workflows/native-hooks.md`
- Ghidra: `docs/workflows/ghidra.md`
- xEdit VMAD cleanup: `docs/workflows/xedit-vmad-cleanup.md`
- CommonLibF4 hazards: `docs/constraints/commonlibf4-hazards.md`
