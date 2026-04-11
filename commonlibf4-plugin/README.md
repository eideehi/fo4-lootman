# LootMan CommonLibF4 Plugin

This directory contains the F4SE plugin for LootMan, built with XMake and CommonLibF4.

## Requirements

- Windows x64
- [XMake](https://xmake.io) `3.0.0+`
- A C++23-capable compiler (`MSVC` or `Clang-CL`)
- Git with submodule support

## Repository Setup

From the repository root:

```bat
git submodule update --init --recursive
```

Then move to this directory:

```bat
cd commonlibf4-plugin
```

`lib/commonlibf4` must be present because this plugin depends on it.

## Build

Run these commands in `commonlibf4-plugin`:

```bat
xmake f -m releasedbg -a x64
xmake build
```

No environment variable is required for compilation.

If you are using the repository from WSL, the recommended path is to run the top-level packaging flow from `packaging/`. That flow keeps orchestration in WSL and invokes `xmake.exe` through WSL interop when the DLL build is needed.

## Build Output

The default output directory is:

```text
build/windows/x64/releasedbg/
```

The plugin binary is generated as:

```text
build/windows/x64/releasedbg/lootman.dll
```

## License

This directory is licensed separately under GPL-3.0 because the native plugin
build is based on the CommonLibF4 template. See `LICENSE` for the directory
license notice and `../EXCEPTIONS` for the additional permissions used for
modding and library linkage.

For historical context, the plugin README for LootMan 2.x.x and earlier
described the plugin as MIT-licensed. The GPL-3.0 licensing model applies to
the current 3.x.x CommonLibF4-template-based plugin layout.

## Optional Environment Variables

These do not affect compilation, but they affect install destination used by CommonLibF4 plugin rules:

- `XSE_FO4_MODS_PATH`: Mod manager mods directory
- `XSE_FO4_GAME_PATH`: Fallout 4 install directory (installs under `<GamePath>/Data`)

## Troubleshooting

- If package resolution fails:

  ```bat
  xmake repo --update
  xmake require --upgrade
  ```

- If compiler detection fails, install/configure Visual Studio Build Tools (C++ workload) or Clang-CL, then open a new shell and run `xmake f` again.

## Credits

### Version 3.x.x

Current plugin builds and runtime code depend on or are based on:

- [CommonLibF4 Template](https://github.com/libxse/commonlibf4-template) - template foundation for the current plugin layout, build rules, and GPL licensing model
- [CommonLibF4](https://github.com/libxse/commonlibf4) - runtime and plugin framework used by the current implementation
- [F4SE](https://f4se.silverlock.org/) - script extender interface used by the plugin at runtime
- [XMake](https://xmake.io) - build system used for 3.x.x plugin builds
- [nlohmann/json](https://github.com/nlohmann/json) - JSON parsing library used by the current implementation

### Version 2.x.x

Historical credits preserved from the 2.x.x plugin README:

- [F4SE](https://f4se.silverlock.org/) - dependency
- [RapidJSON](https://github.com/Tencent/rapidjson) - dependency
- [Xbyak](https://github.com/herumi/xbyak) - dependency
- [CommonLibF4](https://github.com/Ryan-rsm-McKenzie/CommonLibF4) - reference code
- [CommonLibSSE](https://github.com/Ryan-rsm-McKenzie/CommonLibSSE) - reference code
- [MCM](https://github.com/reg2k/f4mcm) - reference code
