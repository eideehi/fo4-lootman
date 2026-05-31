# LootMan

LootMan is a Fallout 4 mod that automates item collection with a native F4SE
plugin, Papyrus scripts, localized assets, and FOMOD packaging.

## Start Here

This repository is organized by ownership boundary. Run Node tooling from the
repository root; keep implementation files in the domain directory that owns the
work.

| Area | Owns | Start with |
| --- | --- | --- |
| `commonlibf4-plugin/` | Native F4SE plugin code and CommonLibF4 integration | `commonlibf4-plugin/README.md`, `docs/constraints/commonlibf4-hazards.md` |
| `papyrus/` | Papyrus source scripts | `papyrus/README.md` |
| `translation/` | Translation source assets | `translation/README.md` |
| `packaging/` | Release build, deploy, archive, clean, FOMOD resources, and packaging workflow | `packaging/README.md`, `docs/workflows/packaging.md` |
| `tools/native-hooks/` | Native hook address manifest, generator, verifier, resolver, and review bundles | `docs/workflows/native-hooks.md` |
| `tools/ghidra/` | Ghidra headless config, scripts, projects, and reports | `docs/workflows/ghidra.md` |
| `tools/xedit/` | Workspace-local xEdit automation assets when present | `docs/workflows/xedit-vmad-cleanup.md` |
| `plans/` | Implementation plans and research inputs | Current plan file |
| `docs/` | Durable project knowledge for humans and LLM agents | `docs/project-map.md` |

`commonlibf4-plugin/lib/commonlibf4/` is a Git submodule dependency and must be
treated as read-only.

## Root Commands

Install dependencies once from the repository root:

```bash
pnpm install
```

Common commands:

```bash
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

The short `pnpm run build` alias maps to `pnpm run package:build`.

## 3.1.0 Update Policy

- Supported: overwrite updates from LootMan 2.x and 3.0.x releases to 3.1.0.
- Not supported: overwrite updates from LootMan 1.x to 3.1.0.
- If you are upgrading from 1.x, uninstall 1.x and make a clean save before installing 3.1.0.
- If you want to remove LootMan entirely or troubleshoot a broken install, use the in-game uninstall flow before reinstalling.

## Build And Packaging

Release packaging is implemented in `packaging/`, but commands run from the
repository root.

```bash
cp packaging/.env.example packaging/.env
pnpm install
pnpm run test:packaging
pnpm run package:build
```

The supported workflow is WSL-hosted: keep the source tree in WSL, run `pnpm`
from WSL, and provide WSL-style paths in `packaging/.env`. Windows-only tools
such as `xmake`, Papyrus Compiler, Archive2, and `7z.exe` are started through
WSL interop.

For setup details, see `packaging/README.md`.

## Documentation

- `docs/user-guide.md` - player-facing install, update, configuration, troubleshooting, and uninstall guide.
- `docs/project-map.md` - LLM-first repository map and routing rules.
- `docs/workflows/packaging.md` - release packaging workflow.
- `docs/workflows/native-hooks.md` - native hook address maintenance.
- `docs/workflows/ghidra.md` - Ghidra headless workflow.
- `docs/workflows/xedit-vmad-cleanup.md` - staged VMAD cleanup workflow.
- `docs/constraints/commonlibf4-hazards.md` - CommonLibF4 crash-prone APIs and required workarounds.

## Licensing

- Repository content is MIT by default. See `LICENSE`.
- `commonlibf4-plugin/` is licensed separately under GPL-3.0 because the native plugin build is based on the CommonLibF4 template. See `commonlibf4-plugin/LICENSE` and `EXCEPTIONS`.
- `papyrus/`, `translation/`, and `packaging/` keep their own MIT `LICENSE` files.

## Release Notes Checklist

Before shipping a release candidate:

- confirm installer/update messaging matches the supported migration policy
- verify both localized ESP files package the intended VMAD state
- run packaging tests and a fresh distributable build
- smoke test fresh install and 2.x overwrite-update flows in game

## Contributors

The following people have contributed to LootMan:

- [Al12rs](https://github.com/Al12rs)
- [UserNo84](https://github.com/UserNo84)
