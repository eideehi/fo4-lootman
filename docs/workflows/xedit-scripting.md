# xEdit (FO4Edit) scripting from WSL: patterns and gotchas

General, reusable notes for authoring and running xEdit Pascal scripts that
**create or edit records** for Fallout 4, driven from a WSL/CLI environment.
These are tool-level lessons, independent of any one plugin. For this repo's
plugin-specific VMAD workflow see `xedit-vmad-cleanup.md`.

xEdit's scripting language is DWScript (Delphi-flavored). Records, subrecords,
and element names are defined in `wbDefinitionsFO4.pas`; that file is the
authority — read it instead of guessing element paths.

## Running xEdit from WSL

xEdit is a Windows GUI executable, but it can be launched from WSL the same way
the build invokes other Windows tools (it has no true headless mode — expect a
window, so launch it minimized):

```bash
WS=$(wslpath -w "$(pwd)")                       # xEdit needs Windows paths
powershell.exe -NoProfile -Command "Start-Process \
  -FilePath 'C:\\Path\\to\\xFOEdit64.exe' -WindowStyle Minimized -Wait \
  -ArgumentList '-S:${WS}\\tools\\xedit\\','-R:${WS}\\tools\\xedit\\run.log', \
                '-D:${WS}\\tools\\xedit\\stage-en','-autoload', \
                '-script:MyScript.pas','Target.esp'"
```

- Paths passed to xEdit must be Windows paths (`wslpath -w`). The
  `\\wsl.localhost\<distro>\...` UNC form works (the staged data can live in the
  WSL tree).
- `-Wait` blocks until the xEdit **process** exits. xEdit saves changed files on
  shutdown, so for a script that modifies records the window must be closed (and
  any "Save changed files?" dialog confirmed). Run the launch as a background job
  so you're notified on exit, then parse the `-R:` log.
- `-R:<file>` writes the session log (loader messages + every `AddMessage`).
  Parse it instead of watching the GUI.

### CLI flag gotchas

- **`-S:` must end with a trailing backslash.** xEdit concatenates the `-S:`
  scripts directory with the `-script:` filename **without inserting a
  separator**. `-S:...\tools\xedit` + `-script:Foo.pas` looks for
  `...\tools\xeditFoo.pas` → "Could not open script". Use `-S:...\tools\xedit\`.
- **`-nobuildrefs`** skips building the reverse-reference cache. It is fine (and
  much faster) for creation/edit/dump scripts; building refs over `Fallout4.esm`
  costs minutes. It does **not** change whether references can be *set* (see the
  self-reference limit below).
- **`-script:` runs `Process(e)` for every record in the load order** — hundreds
  of thousands for `Fallout4.esm`. A one-shot creation script must guard to run
  once (below), or it spams the log and wastes time.

## Run-once pattern

For a script that should act once on one plugin (not per record), set a guard on
the first `Process` call and locate the target file by name:

```pascal
var ran: boolean; targetFile: IInterface;

function FindFileByName(fn: string): IInterface;
var i: integer;
begin
  Result := nil;
  for i := 0 to Pred(FileCount) do
    if GetFileName(FileByIndex(i)) = fn then begin Result := FileByIndex(i); Exit; end;
end;

function Process(e: IInterface): integer;
begin
  Result := 0;
  if ran then Exit;               // fire once, whatever record xEdit passes first
  ran := True;
  targetFile := FindFileByName('Target.esp');
  if Assigned(targetFile) then BuildRecords;
end;
```

## Finding record/element definitions

Fetch the definitions and grep for the record and the sub-structs it references,
to get exact element names (signatures and display names):

```bash
curl -sSL https://raw.githubusercontent.com/TES5Edit/xedit-backup/master/wbDefinitionsFO4.pas -o /tmp/def.pas
grep -n "wbRecord(TERM" /tmp/def.pas        # then read the block
grep -n "wbScriptFragments\b\|wbScriptEntry :=" /tmp/def.pas
```

`SetElementEditValues`/`SetElementNativeValues` accept either the subrecord
signature (`'ITXT'`) or the display name (`'Item Text'`); both resolve to the
same element. Union members and array element structs are named in the def too.

## Manual FormID assignment for ESL / light plugins

Light (ESL) plugins require object IDs `<= 0xFFF`. xEdit's auto-assign (`Add`,
`wbCopyElementToFile`) draws from the header `nextObjectID`, which is usually
**above** `0xFFF` and gets rejected at save. Assign FormIDs by hand:

```pascal
// Keep the FileID (high byte); replace the WHOLE 24-bit ObjectID with objID.
procedure SetObjID(rec: IInterface; objID: cardinal);
begin
  SetLoadOrderFormID(rec, (GetLoadOrderFormID(rec) and $FF000000) or objID);
end;
```

The mask must be **`$FF000000`**, not `$FFFFF000`. `$FFFFF000` clears only the
low 12 bits and leaves higher bits of the auto-assigned object ID (e.g. bit
`0x1000` from `nextObjectID 0x174F`), producing an object ID `> 0xFFF` that
breaks ESL validity. This bug is invisible in light mode (auto-assign there is
already `<= 0xFFF`) and only appears when auto-assign returns a large object ID.

## You cannot set a same-file (self) reference from a script

Setting a FormID reference to a record **in the same plugin** fails:

```pascal
SetElementNativeValues(rec, 'TNAM', GetLoadOrderFormID(targetInSameFile));
// -> Exception: Load order FileID [N] can not be mapped to file FileID for file "X.esp"
```

xEdit's `LoadOrderFormIDtoFileFormID` cannot map the plugin's own FileID (a file
is not a master of itself, and the self-index is not applied for script-set
refs). In this session it failed across **light** (FE-space FileID) and
**regular ESP** (after stripping the light flag), and across four setter methods:
native load-order FormID, native self-index FormID, edit-value hex, and
`SetEditValue` with the `Name [SIG:FormID]` string.

**Set self-references in the GUI** (drag the target onto the field, or
right-click → Edit). Cross-file references — to a record in a loaded master such
as `Fallout4.esm` — work fine via the same `SetElementNativeValues` call. So a
practical division is: script everything except same-file refs, log a precise
list of the refs to set by hand, and finish those in the GUI.

(This is the observed behavior in CLI-staged load orders; revisit on future
xEdit versions, but plan around self-ref setting being GUI-only.)

## Union subrecords decided by another field

Some subrecords are `wbUnion(SIG, name, decider, [members...])` whose active
member is chosen by another field. FO4 `NOTE.SNAM` is a union decided by
`NOTE.DNAM` (the type enum): `DNAM=3` (Terminal) selects a `TERM` FormID member;
other types select a sound/scene/unused member.

- Set the **deciding field first**, then the union member.
- Editing a donor record's union member of a *different* type throws
  "`<SIG> - Data can not be edited`" — the existing data doesn't match the member
  the decider now selects. Creating the record from scratch (clean union) avoids
  the donor mismatch. (Setting the member to a same-file FormID still hits the
  self-reference limit above — so a self-ref union member is GUI-only too.)

## Copying elements between records

- `wbCopyElementToFile(elem, file, asNew, deepCopy)` — copy a whole **record**
  into a file (creates a new record). Good for cloning a donor record.
- `wbCopyElementToRecord(elem, record, asNew, deepCopy)` — copy a **subrecord**
  (e.g. `VMAD`) into an existing record. Use this to inherit a donor's subrecord
  structure, including binary header/magic bytes you don't want to hand-author.
- `ElementAssign(container, HighInteger, nil, False)` — append a new **empty**
  array element. Passing a source element here did **not** reliably copy a
  cross-file subrecord into a record (returned `nil`); use
  `wbCopyElementToRecord` for that.

Donor-copy is the robust way to author intricate subrecords (e.g. terminal/perk
`VMAD` script fragments): copy the donor subrecord, then overwrite the
string/integer fields you control (script name, fragment indices, function
names) and clear inherited properties. Fragment bindings keyed by string/index
(not FormID) have no self-reference problem.

## Array and struct manipulation idioms

```pascal
item := ElementAssign(arr, HighInteger, nil, False);   // append a new element
while ElementCount(arr) > 0 do RemoveByIndex(arr, 0, False); // empty, keep array
SetElementEditValues(item, 'ITXT', 'Label');           // string field
SetElementNativeValues(item, 'ANAM', 8);               // enum/int field
```

Prefer emptying an array (`RemoveByIndex` in a loop) over `RemoveElement` on the
whole array: removing the array element can drop its count and malform the
parent struct on save.

## Editing a light plugin as a regular ESP (flag strip)

The record-header flags are a `uint32` at **file offset 8** (little-endian); the
light/ESL bit is `0x200`. For a header whose flags are exactly `0x200`, byte
offset 9 holds it. You can strip it before loading, edit as a regular ESP, then
re-add it; object IDs `<= 0xFFF` keep it ESL-valid afterward:

```bash
xxd -s 8 -l 4 -p file.esp                                   # expect 00020000 (ESL)
printf '\x00' | dd of=file.esp bs=1 seek=9 count=1 conv=notrunc   # strip
# ... run xEdit ...
printf '\x02' | dd of=file.esp bs=1 seek=9 count=1 conv=notrunc   # restore
```

Caveats learned here: regular-ESP mode did **not** lift the self-reference
limit (it failed too), and it changes auto-assigned object IDs into the high
range (which is what exposed the `SetObjID` mask bug). So flag-stripping is a
real technique, but it was not the fix for self-references.

## try/except keeps long scripts alive

DWScript supports `try ... except ... end`. Wrap risky steps so a long
creation script completes (and produces partial records + a clear manual TODO)
instead of aborting on the first failure:

```pascal
try
  SetElementNativeValues(note, 'SNAM', GetLoadOrderFormID(root));
except
  AddMessage('  !! SNAM set failed -> set in GUI');
end;
```

## Verifying results without the GUI

The saved `.esp` is binary but contains record EDIDs, lstring text, and
fragment script/function names as readable strings:

```bash
grep -aoE "MY_EDID_PREFIX[A-Za-z0-9_]+" out.esp | sort -u      # records created
grep -aqF "Menu item label" out.esp && echo present            # menu-item text
grep -aoiE "myns:fragments:[a-z0-9_:]+|Fragment_Terminal_[0-9]{2}" out.esp | sort | uniq -c
xxd -s 8 -l 4 -p out.esp                                        # confirm ESL flag bit
```

Authoritative checks (record errors, in-game behavior) still need the GUI
"Check for Errors" pass and a game launch; the CLI greps are fast confidence
checks that the script wrote what you intended.
