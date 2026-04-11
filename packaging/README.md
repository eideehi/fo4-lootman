# Packaging CLI

This directory contains the build, deploy, archive, and test tooling used to
produce LootMan release artifacts.

The supported day-to-day setup is:

- source tree on WSL
- `pnpm` and the packaging CLI running on WSL
- Windows-only tools invoked through WSL interop when needed

All commands below assume your shell is already in `packaging/`.

```bash
cd packaging
```

## Typical workflow

```bash
cp .env.example .env
pnpm install
pnpm test
pnpm run build
```

## WSL-hosted Windows build checklist

Use this checklist before running `pnpm run build` from WSL.

Repository setup:

- Keep the repository on the WSL filesystem.
- Initialize submodules from the repository root with `git submodule update --init --recursive`.
- Confirm `commonlibf4-plugin/lib/commonlibf4` exists.
- Run `pnpm install` in `packaging/`.
- Run packaging commands from `packaging/` only.

Environment:

- Create `packaging/.env` from `.env.example`.
- Set `PROJECT_ROOT` to the repository root as a WSL path.
- Set `STEAM_GAME_DIR` to the Steam `steamapps/common` directory as a WSL path.
- Set `SEVENZIP_PATH` to `7z.exe` as a WSL path.
- Leave `DLL_BUILD_DIR` at its default unless the native DLL output directory has been customized.
- Optionally set `WSL_STAGE_DIR` if you want the DLL build staging directory somewhere other than `/mnt/c/tmp/lootman-wsl-build`.
- Do not use Windows drive syntax like `Z:/...` in `.env`.

Windows tools reachable from WSL:

- Use Windows x64.
- Enable WSL interop so WSL can launch Windows `.exe` files.
- Install `xmake.exe` 3.0.0+ and make sure it is reachable from WSL.
- Install a C++23-capable Windows toolchain: `MSVC` via Visual Studio Build Tools or `Clang-CL`.
- Make sure `7z.exe` exists at `SEVENZIP_PATH`.
- If compiler detection fails in `xmake`, install or repair the Windows C++ toolchain, open a new shell, and try again.
- The DLL build (xmake) requires file staging to a local Windows path because xmake cannot operate on UNC paths. The staging directory is `WSL_STAGE_DIR/build`.
- Other Windows tools (PapyrusCompiler, Archive2, 7-Zip) work directly with UNC paths (`\\wsl$\...`) via PowerShell interop and do not require staging.

Fallout 4 and Papyrus prerequisites:

- Install Fallout 4 and Creation Kit.
- Confirm `PapyrusCompiler.exe` exists at `<STEAM_GAME_DIR>/Fallout 4/Papyrus Compiler/PapyrusCompiler.exe`.
- Confirm `Archive2.exe` exists at `<STEAM_GAME_DIR>/Fallout 4/Tools/Archive2/Archive2.exe`.
- Confirm `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source` exists.
- Install F4SE Papyrus sources under `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source/F4SE/**/*.psc`.
- Install MCM Papyrus sources under `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source/User/**/*.psc`.
- Confirm `Institute_Papyrus_Flags.flg` exists at `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source/Base/Institute_Papyrus_Flags.flg`.
- Confirm both `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source/Base` and `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source/User` exist.

One-time validation commands:

```bash
git submodule update --init --recursive
cd packaging
cp .env.example .env
pnpm install
pnpm test
xmake --version
test -f "$STEAM_GAME_DIR/Fallout 4/Papyrus Compiler/PapyrusCompiler.exe"
test -f "$STEAM_GAME_DIR/Fallout 4/Tools/Archive2/Archive2.exe"
test -f "$STEAM_GAME_DIR/Fallout 4/Data/Scripts/Source/Base/Institute_Papyrus_Flags.flg"
test -d "$STEAM_GAME_DIR/Fallout 4/Data/Scripts/Source/F4SE"
test -d "$STEAM_GAME_DIR/Fallout 4/Data/Scripts/Source/User"
pnpm run build
```

## Available scripts

Run these from `packaging/`.

- `pnpm run clean` - remove current version build temp output
- `pnpm run clean:wsl-build` - remove the persistent DLL build stage under `WSL_STAGE_DIR/build` when you need to recover from a broken staged build tree
- `pnpm run clean:all` - remove all build output including cache and all of `WSL_STAGE_DIR`
- `pnpm run build` - build production artifacts and create a distributable FOMOD archive
- `pnpm run deploy` - deploy production artifacts into the game Data directory
- `pnpm run deploy:dev` - build production artifacts with Papyrus and deploy them into the game Data directory
- `pnpm run undeploy` - remove LootMan files from the game Data directory
- `pnpm test` - run the packaging unit test suite
- `pnpm run test:watch` - run packaging tests in watch mode

## Dependency guard

Every package script runs a preflight guard before `tsx` or `vitest` starts.

The guard checks:
- the current directory is `packaging/`
- the required local tool package exists in `node_modules/`
- the required executable shim exists in `node_modules/.bin/`

If preflight fails, the script stops and tells you to repair dependencies manually with `cd packaging` and `pnpm install`. It does not try to install or repair anything automatically.

## Raw CLI usage

Run these from `packaging/` only.

```bash
tsx scripts/cli.ts clean [--all] [--wsl-build]
tsx scripts/cli.ts build [--mode=product] [--with-papyrus|--no-papyrus]
tsx scripts/cli.ts deploy [--mode=product] [--lang=en|ja] [--with-papyrus] [--full-sync] [--build]
tsx scripts/cli.ts undeploy
```

## Environment setup

Create a `.env` file in `packaging/` before running build/deploy scripts.

```bash
cp .env.example .env
```

Set these values in `.env`:

- `STEAM_GAME_DIR` (required): WSL path to your Steam common directory, e.g. `/mnt/z/SteamLibrary/steamapps/common`
- `SEVENZIP_PATH` (required): WSL path to `7z.exe`, e.g. `/mnt/z/programs/7-Zip/7z.exe`
- `PROJECT_ROOT` (recommended): repository root path, written as a WSL path if set explicitly
- `DLL_BUILD_DIR` (optional): defaults to `commonlibf4-plugin/build/windows/x64/{mode}`
- `WSL_STAGE_DIR` (optional): local Windows-accessible staging root for the DLL build (xmake); defaults to `/mnt/c/tmp/lootman-wsl-build`

Use WSL paths in `.env`, not Windows drive syntax like `Z:/...`. The packaging scripts validate paths with WSL-side Node.js and convert paths for Windows executables only at execution time.

## Papyrus compile prerequisites

Papyrus compilation requires Fallout 4 and Creation Kit to be installed.

- Papyrus compiler must exist at `<STEAM_GAME_DIR>/Fallout 4/Papyrus Compiler/PapyrusCompiler.exe`
- Archive2 must exist at `<STEAM_GAME_DIR>/Fallout 4/Tools/Archive2/Archive2.exe`
- Papyrus source root must exist at `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source`
- F4SE Papyrus sources must be installed under `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source/F4SE/**/*.psc`
- MCM Papyrus sources must be installed under `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source/User/**/*.psc`
- Import directory: `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source/Base`
- Import directory: `<STEAM_GAME_DIR>/Fallout 4/Data/Scripts/Source/User`
- Project Papyrus sources are collected from `<PROJECT_ROOT>/papyrus/Scripts/Source/User/**/*.psc`

## Additional prerequisites

- Run `pnpm install` in `packaging/` to install Node.js dependencies
- `<PROJECT_ROOT>/commonlibf4-plugin` must exist and be buildable
- `xmake.exe` must be reachable from WSL for DLL builds
- `SEVENZIP_PATH` must point to a working 7-Zip executable
- WSL interop must be enabled so WSL can start Windows `.exe` files

## WSL Notes

The packaging system relies on WSL interop to invoke Windows `.exe` tools from the WSL-hosted source tree. Most Windows tools are launched via PowerShell, which can hold a UNC working directory (`\\wsl$\...`).

- `build`, `deploy --build`, and the Papyrus/archive steps run from WSL and call Windows `.exe` tools directly via PowerShell interop.
- Most Windows tools (PapyrusCompiler, Archive2, 7-Zip) operate directly on UNC paths and do not require file staging.
- The DLL build (xmake) is the sole exception: xmake cannot operate on UNC paths, so DLL sources are staged to a local Windows path under `WSL_STAGE_DIR/build`. DLL staging reuse is keyed off the current DLL source contents plus the checked-out `commonlibf4` commit, so source edits invalidate the staged DLL tree automatically.
- `sync-deploy` and `undeploy` use WSL-side file operations against `/mnt/.../Fallout 4/Data`.
- `pnpm run clean` removes the current version packaging output.
- `pnpm run clean:wsl-build` removes only `WSL_STAGE_DIR/build` and is mainly intended for manual recovery if the staged DLL build tree becomes unusable.
- `pnpm run clean:all` removes all build output including cache and `WSL_STAGE_DIR`.

Migration note: if you have a leftover `WSL_STAGE_DIR/package` directory from a previous version, it is no longer used. Remove it manually or run `pnpm run clean:all` to clean up.

## License

The scripts and supporting files in `packaging/` are released under the MIT
License. See `LICENSE`.
