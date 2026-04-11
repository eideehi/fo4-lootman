# LootMan

LootMan is a Fallout 4 mod that automates item collection with a native F4SE plugin, Papyrus scripts, localized assets, and FOMOD packaging.

## 3.0.0 Update Policy

- Supported: overwrite updates from LootMan 2.x to 3.0.0.
- Not supported: overwrite updates from LootMan 1.x to 3.0.0.
- If you are upgrading from 1.x, uninstall 1.x and make a clean save before installing 3.0.0.
- If you want to remove LootMan entirely or troubleshoot a broken install, use the in-game uninstall flow before reinstalling.

## Repository Layout

- `commonlibf4-plugin/` - native F4SE plugin source, build files, and CommonLibF4 integration
- `papyrus/` - Papyrus source scripts used by LootMan
- `translation/` - translation source assets used to maintain localized text
- `packaging/` - build, deploy, archive, and test tooling for release artifacts
- `packaging/resources/lootman/common/` - runtime assets shared by all locales
- `packaging/resources/lootman/en/` - English plugin and localized assets
- `packaging/resources/lootman/ja/` - Japanese plugin and localized assets

`commonlibf4-plugin/lib/commonlibf4/` is a Git submodule dependency used by the native plugin build.

## Build And Packaging

Release packaging is driven from `packaging/`.

```bash
cd packaging
cp .env.example .env
pnpm install
pnpm test
pnpm run build
```

All packaging commands assume the shell is already in `packaging/`. Do not use `pnpm --dir packaging ...` in release procedures.

The supported workflow is WSL-hosted: keep the source tree in WSL, run `pnpm` from WSL, and provide WSL-style paths in `packaging/.env`. Windows-only tools such as `xmake`, Papyrus Compiler, Archive2, and `7z.exe` are started through WSL interop.

For a single setup and validation checklist covering `.env`, Windows toolchain requirements, Papyrus prerequisites, and build commands, see `packaging/README.md`.

See `packaging/README.md` for packaging details and `commonlibf4-plugin/README.md` for native build details.

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
