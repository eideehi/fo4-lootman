# Packaging

This directory owns the release build, deploy, archive, clean, FOMOD resources,
and packaging implementation for LootMan.

Run Node commands from the repository root. The package command surface is
centralized in root `package.json`; this directory no longer owns a local
`package.json` or local `node_modules`.

## Typical Workflow

From the repository root:

```bash
cp packaging/.env.example packaging/.env
pnpm install
pnpm run test:packaging
pnpm run package:build
```

Native DLL-only verification from WSL:

```bash
pnpm run package:build -- --no-papyrus
```

## Available Root Commands

- `pnpm run package:clean` - remove current version build temp output
- `pnpm run package:clean:wsl-build` - remove the persistent DLL build stage under `WSL_STAGE_DIR/build`
- `pnpm run package:clean:all` - remove all build output including cache and all of `WSL_STAGE_DIR`
- `pnpm run package:build` - build production artifacts and create a distributable FOMOD archive
- `pnpm run package:deploy` - deploy production artifacts into the game Data directory
- `pnpm run package:deploy:dev` - build production artifacts with Papyrus and deploy them into the game Data directory
- `pnpm run package:undeploy` - remove LootMan files from the game Data directory
- `pnpm run test:packaging` - run packaging unit tests

The raw CLI remains under `packaging/scripts/cli.ts` and is invoked by the root
scripts.

```bash
tsx packaging/scripts/cli.ts clean [--all] [--wsl-build]
tsx packaging/scripts/cli.ts build [--mode=product] [--with-papyrus|--no-papyrus]
tsx packaging/scripts/cli.ts deploy [--mode=product] [--lang=en|ja] [--with-papyrus] [--full-sync] [--build]
tsx packaging/scripts/cli.ts undeploy
```

## WSL-Hosted Windows Build Checklist

Repository setup:

- Keep the repository on the WSL filesystem.
- Initialize submodules from the repository root with `git submodule update --init --recursive`.
- Confirm `commonlibf4-plugin/lib/commonlibf4` exists.
- Run `pnpm install` from the repository root.

Environment:

- Create `packaging/.env` from `packaging/.env.example`.
- Set `PROJECT_ROOT` to the repository root as a WSL path, keep the default `../`, or omit it to use the repository root.
- Set `FO4_GAME_DIR` to the Fallout 4 install directory as a WSL path.
- Set `SEVENZIP_PATH` to `7z.exe` as a WSL path.
- Leave `DLL_BUILD_DIR` at its default unless the native DLL output directory has been customized.
- Optionally set `WSL_STAGE_DIR` if you want the DLL build staging directory somewhere other than `/mnt/c/tmp/lootman-wsl-build`.
- Do not use Windows drive syntax like `Z:/...` in `.env`.
- Relative paths in `packaging/.env` are resolved from `packaging/`, so the default `PROJECT_ROOT=../` points at the repository root even though commands run from the repository root.

Windows tools reachable from WSL:

- Use Windows x64.
- Enable WSL interop so WSL can launch Windows `.exe` files.
- Install `xmake.exe` 3.0.0+ and make sure it is reachable from WSL.
- Install a C++23-capable Windows toolchain: `MSVC` via Visual Studio Build Tools or `Clang-CL`.
- Make sure `7z.exe` exists at `SEVENZIP_PATH`.
- If compiler detection fails in `xmake`, install or repair the Windows C++ toolchain, open a new shell, and try again.
- The DLL build uses a local Windows staging path because xmake cannot operate on UNC paths. The staging directory is `WSL_STAGE_DIR/build`.

Fallout 4 and Papyrus prerequisites:

- Install Fallout 4 and Creation Kit.
- Confirm `PapyrusCompiler.exe` exists at `<FO4_GAME_DIR>/Papyrus Compiler/PapyrusCompiler.exe`.
- Confirm `Archive2.exe` exists at `<FO4_GAME_DIR>/Tools/Archive2/Archive2.exe`.
- Confirm `<FO4_GAME_DIR>/Data/Scripts/Source` exists.
- Install F4SE Papyrus sources under `<FO4_GAME_DIR>/Data/Scripts/Source/F4SE/**/*.psc`.
- Install MCM Papyrus sources under `<FO4_GAME_DIR>/Data/Scripts/Source/User/**/*.psc`.
- Confirm `Institute_Papyrus_Flags.flg` exists at `<FO4_GAME_DIR>/Data/Scripts/Source/Base/Institute_Papyrus_Flags.flg`.
- Confirm both `<FO4_GAME_DIR>/Data/Scripts/Source/Base` and `<FO4_GAME_DIR>/Data/Scripts/Source/User` exist.

One-time validation commands from the repository root:

```bash
git submodule update --init --recursive
cp packaging/.env.example packaging/.env
pnpm install
pnpm run test:packaging
xmake --version
fo4GameDir="/mnt/z/SteamLibrary/steamapps/common/Fallout 4"
test -f "$fo4GameDir/Papyrus Compiler/PapyrusCompiler.exe"
test -f "$fo4GameDir/Tools/Archive2/Archive2.exe"
test -f "$fo4GameDir/Data/Scripts/Source/Base/Institute_Papyrus_Flags.flg"
test -d "$fo4GameDir/Data/Scripts/Source/F4SE"
test -d "$fo4GameDir/Data/Scripts/Source/User"
pnpm run package:build
```

The shell checks above use a local `fo4GameDir` variable. The packaging CLI
loads `packaging/.env` itself; copying `.env.example` does not export variables
into the current shell.

## Dependency Guard

Root package scripts run `tools/scripts/preflight.js` before `tsx` or `vitest`
starts.

The guard checks:

- the current directory is the repository root
- the required local tool package exists in root `node_modules/`
- the required executable shim exists in root `node_modules/.bin/`

If preflight fails, repair dependencies manually from the repository root:

```bash
pnpm install
```

The scripts do not repair dependencies automatically.

## WSL Notes

The packaging system relies on WSL interop to invoke Windows `.exe` tools from
the WSL-hosted source tree. Most Windows tools are launched via PowerShell,
which can hold a UNC working directory (`\\wsl$\...`).

- `package:build`, `package:deploy -- --build`, and the Papyrus/archive steps run from WSL and call Windows `.exe` tools directly via PowerShell interop.
- Most Windows tools (PapyrusCompiler, Archive2, 7-Zip) operate directly on UNC paths and do not require file staging.
- The DLL build (xmake) is the sole exception: xmake cannot operate on UNC paths, so DLL sources are staged to a local Windows path under `WSL_STAGE_DIR/build`.
- `sync-deploy` and `undeploy` use WSL-side file operations against `/mnt/.../Fallout 4/Data`.
- `pnpm run package:clean:wsl-build` removes only `WSL_STAGE_DIR/build` and is mainly intended for manual recovery if the staged DLL build tree becomes unusable.

Migration note: if you have a leftover `WSL_STAGE_DIR/package` directory from a
previous version, it is no longer used. Remove it manually or run
`pnpm run package:clean:all` to clean up.

## License

The scripts and supporting files in `packaging/` are released under the MIT
License. See `LICENSE`.
