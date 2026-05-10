# Ghidra Workflow

Ghidra automation is owned by `tools/ghidra/`.

## Files

- `tools/ghidra/headless.example.json` - checked-in config template.
- `tools/ghidra/headless.local.json` - ignored local config override.
- `tools/ghidra/scripts/` - Ghidra Java scripts and TypeScript probe wrapper.
- `tools/ghidra/projects/` - ignored local spillover for Ghidra project
  database files.
- `tools/ghidra/reports/` - generated analysis reports.
- `tools/ghidra/reports/local/` - ignored development reports for reuse during
  local investigation.

## Project Directory

Store the Fallout 4 Ghidra project database outside this repository and point
the checked-in config at it with an environment variable:

```bash
export FO4_GHIDRA_PROJECT_DIR=/path/to/fo4-ghidra-project
```

`tools/ghidra/headless.example.json` uses `${FO4_GHIDRA_PROJECT_DIR}` for
`projectLocation`. If the variable is not set, the probe wrapper fails before
starting Ghidra. Use `tools/ghidra/headless.local.json` only for machine-local
overrides that should stay ignored.

## Probe Command

Run from the repository root:

```bash
pnpm run ghidra:probe
```

Optional overrides are passed through to the TypeScript wrapper:

```bash
pnpm run ghidra:probe -- --config=tools/ghidra/headless.local.json --report=tools/ghidra/reports/local/custom-probe.txt
```

## Rules

- Keep local machine paths in `tools/ghidra/headless.local.json`; do not commit
  machine-specific paths.
- Do not commit Ghidra project databases. `tools/ghidra/projects/` is ignored
  spillover, not the primary storage location.
- Prefer read-only, no-analysis headless probes for verification.
- Write development-only reports under `tools/ghidra/reports/local/`.
- Do not commit generated reports unless they are required durable evidence for
  native-hook or Ghidra work.
- Keep required durable evidence reports outside `local/` so Git can show them
  as intentional candidates.
- Link native-hook proof metadata to report paths instead of embedding
  unexplained addresses.
