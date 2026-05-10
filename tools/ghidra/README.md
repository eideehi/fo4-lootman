# Ghidra Tools

Ghidra automation assets live here.

Root command:

```bash
pnpm run ghidra:probe
```

Machine-local config belongs in ignored `tools/ghidra/headless.local.json`.
Set `FO4_GHIDRA_PROJECT_DIR` to the external Fallout 4 Ghidra project
directory before running the checked-in example config.

Checked-in examples and reusable evidence reports stay under this directory.
Use ignored `tools/ghidra/reports/local/` for development-only reports. Do not
commit generated reports unless they are required durable evidence.

See `docs/workflows/ghidra.md`.
