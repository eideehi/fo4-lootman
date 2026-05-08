# Native Hook Maintenance

Native hook address maintenance is owned by `tools/native-hooks/`.

## Files

- `tools/native-hooks/papyrus_lootman_hooks.addresses.json` - source manifest.
- `tools/native-hooks/scripts/` - generator, verifier, resolver, and review bundle code.
- `tools/native-hooks/reports/<runtime>/` - generated review bundles and candidate data.
- `commonlibf4-plugin/src/papyrus_lootman_hook_addresses.generated.h` - generated C++ include.

## Commands

Run from the repository root:

```bash
pnpm run native-hooks:generate
pnpm run native-hooks:bundle
pnpm run native-hooks:verify
pnpm run native-hooks:resolve
pnpm run test:tools
```

`pnpm run native-hooks:resolve` is non-writing unless resolver arguments request
write behavior. Use non-writing resolver output first when validating proof.

## Rules

- Do not edit the generated C++ header by hand. Update the manifest or generator
  and run `pnpm run native-hooks:generate`.
- Keep Ghidra evidence paths workspace-relative.
- Keep raw internal layout offsets as `layout_offset`; do not relabel them as
  executable RVAs.
- If a call-site family has ambiguous or missing proof, leave it unproven and
  generate a review bundle instead of guessing.
- Verify native C++ integration through `pnpm run package:build -- --no-papyrus`
  when generated constants change.
