# Packaging Workflow

Packaging commands run from the repository root and invoke implementation under
`packaging/scripts/`.

## Setup

```bash
cp packaging/.env.example packaging/.env
pnpm install
pnpm run test:packaging
```

Configure `packaging/.env` with WSL paths:

- `FO4_GAME_DIR`: Fallout 4 install directory.
- `SEVENZIP_PATH`: path to `7z.exe`.
- `PROJECT_ROOT`: optional repository root override. The default `../` is resolved from `packaging/`.
- `DLL_BUILD_DIR`: optional native DLL output override.
- `WSL_STAGE_DIR`: optional Windows-local staging root for xmake.

Do not use Windows drive syntax like `Z:/...` in `.env`.
Relative paths in `packaging/.env` are resolved from `packaging/`, not from the
current shell directory.

## Commands

```bash
pnpm run package:clean
pnpm run package:clean:wsl-build
pnpm run package:clean:all
pnpm run package:build
pnpm run package:build -- --no-papyrus
pnpm run package:deploy
pnpm run package:deploy -- --build --with-papyrus
pnpm run package:undeploy
```

The raw CLI is:

```bash
tsx packaging/scripts/cli.ts clean [--all] [--wsl-build]
tsx packaging/scripts/cli.ts build [--mode=product] [--with-papyrus|--no-papyrus]
tsx packaging/scripts/cli.ts deploy [--mode=product] [--lang=en|ja] [--with-papyrus] [--full-sync] [--build]
tsx packaging/scripts/cli.ts undeploy
```

## Native DLL Verification

From WSL, verify the native DLL through the packaging CLI:

```bash
pnpm run package:build -- --no-papyrus
```

Do not use direct `xmake` commands from `commonlibf4-plugin/` as the primary WSL
verification path.

## Prerequisites

- Root `pnpm install` has completed.
- `commonlibf4-plugin/lib/commonlibf4/` exists.
- WSL interop can launch Windows `.exe` tools.
- `xmake.exe` 3.0.0+ is reachable from WSL.
- A C++23-capable Windows toolchain is installed.
- Fallout 4 and Creation Kit are installed.
- Papyrus Compiler and Archive2 exist under the Fallout 4 install.
- F4SE and MCM Papyrus sources exist under the Fallout 4 source tree.
