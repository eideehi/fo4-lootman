# Ghidra Workflow

Ghidra automation is owned by `tools/ghidra/`.

## Files

- `tools/ghidra/headless.example.json` - checked-in config template.
- `tools/ghidra/headless.local.json` - ignored local config override.
- `tools/ghidra/scripts/` - Ghidra Java scripts and TypeScript probe wrapper.
- `tools/ghidra/projects/` - ignored local Ghidra project database files.
- `tools/ghidra/reports/` - generated analysis reports.

## Probe Command

Run from the repository root:

```bash
pnpm run ghidra:probe
```

Optional overrides are passed through to the TypeScript wrapper:

```bash
pnpm run ghidra:probe -- --config=tools/ghidra/headless.local.json --report=tools/ghidra/reports/custom-probe.txt
```

## Rules

- Keep local machine paths in `tools/ghidra/headless.local.json`; do not commit
  machine-specific paths.
- Do not commit `tools/ghidra/projects/`; the project database is local and can
  be large.
- Prefer read-only, no-analysis headless probes for verification.
- Write generated reports under `tools/ghidra/reports/`.
- Link native-hook proof metadata to report paths instead of embedding
  unexplained addresses.
