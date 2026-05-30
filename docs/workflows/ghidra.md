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

## Importing and analyzing a new game build (GUI)

The probe reads an already-analyzed program, so import and analyze a new
`Fallout4.exe` build through the WSL Ghidra GUI before probing.

Two constraints decide success:

- Use the same Ghidra version as the headless probe. Ghidra is backward- but not
  forward-compatible: its release notes state that programs created or modified
  in a release are not usable by an earlier Ghidra version. A program analyzed in
  a newer Ghidra (for example Windows 12.1) therefore cannot be opened by the
  12.0.4 headless analyzer. This machine runs WSL Ghidra 12.0.4 (the `ghidra`
  launcher); do not analyze in a Windows Ghidra install of a different version.
- Open the project as the WSL user that created it. From another account Ghidra
  refuses the local project with `Project is owned by ...`, so use the WSL GUI,
  not a Windows one.

Steps:

1. Copy the exe into WSL. The import file chooser cannot reach `/mnt/<drive>`
   paths (9P) reliably, and the space in `Fallout 4` fails there. Copy to ext4
   and verify the copy's bytes:

   ```bash
   cp "/mnt/g/steam/steamapps/common/Fallout 4/Fallout4.exe" \
      ~/ghidra-projects/Fallout4-<version>.exe
   sha256sum ~/ghidra-projects/Fallout4-<version>.exe
   ```

2. Launch the WSL Ghidra GUI; WSLg supplies the display:

   ```bash
   ghidra
   ```

3. `File → Open Project` → the `.gpr` under `$FO4_GHIDRA_PROJECT_DIR`. Accept the
   cleanup prompt for any stale `*.lock.stale-*` file.
4. `File → Import File` → the copied exe. Name the program
   `Fallout4-<version>.exe` so earlier builds stay as separate programs in the
   same project. Import copies the bytes into the project database; the copied
   exe can be deleted afterward.
5. Open the program, run `Auto Analyze` with defaults, then save with Ctrl+S.
   The probe reads the saved database; an unsaved analysis is invisible to it.
6. Point the probe at the new program: set `programName` in
   `tools/ghidra/headless.local.json`, then follow `Probe Command`.

Auto-analysis can throw `StringIndexOutOfBoundsException` in
`AutoAnalysisManager.getTaskTimesString` while printing the analysis-time
summary; a negative task time formats to a four-character string and overruns
`"000".substring(...)`. It is cosmetic and fires after analysis finishes:
dismiss the dialog and save. The program is fully analyzed, and a re-run can hit
it again with the same harmless result.

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
