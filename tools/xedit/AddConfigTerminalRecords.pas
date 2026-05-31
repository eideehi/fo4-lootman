{
  AddConfigTerminalRecords.pas
  Phase-A MCM-fallback holotape/terminal records (records + menu items + VMAD).

  Creates, in tools/xedit/stage-en/LootMan.esp:
    NOTE 0xFB6  LTMN_ConfigHolotape     (donor-copied; DNAM=Terminal, SNAM->0xFB7)
    TERM 0xFB7  LTMN_TERM_ConfigRoot          + 4 submenu items -> 0xFB8..0xFBB
    TERM 0xFB8  LTMN_TERM_ConfigGeneral       + 14 Display-Text items (ITID 1..14)
    TERM 0xFB9  LTMN_TERM_ConfigObjectFilter  + 12 items (ITID 1..12)
    TERM 0xFBA  LTMN_TERM_ConfigLogLevel      + 7 items  (ITID 1..7)
    TERM 0xFBB  LTMN_TERM_ConfigUtility       + 4 items  (ITID 1..4; FixUtilityMenu later drops item 2)
    COBJ 0xFBC  LTMN_CO_ConfigHolotape  (cloned from 0xFB5; CNAM->0xFB6)
  and binds each leaf TERM's fragment VMAD by ITID (Fragment Index == ITID,
  fragmentName = Fragment_Terminal_NN, no properties) to the compiled scripts
  ltmn2:fragments:terminals:term_config*.

  Structure is from wbDefinitionsFO4.pas (TERM Menu Item: ITXT/RNAM/ANAM Type/ITID/
  TNAM; VMAD = Script Fragments). Menu items are built from scratch; the VMAD is
  donor-copied from cDonorFragmentTerm to inherit the version/format header bytes,
  then adapted -- that is the part most likely to need a path tweak on first run
  (paste the -R: log). If cDonorFragmentTerm is left 0, the VMAD step is skipped
  and you can donor-copy the VMAD in the GUI per the runbook instead.

  UNTESTED by the author (no xEdit here). Run minimized via the project -S: pattern
  against tools/xedit/stage-en; verify FormIDs 0xFB6-0xFBC, the ESL flag, and the
  ITID<->Fragment Index 1:1 mapping; then paste the -R: log.

  Run: select the LootMan.esp file header (or any record in it), Apply Script.
}
unit AddConfigTerminalRecords;

const
  // Donor terminal-type holotape NOTE (inherits MODL/category/keyword/DNAM=3).
  cDonorHolotape = $0017B487;

  // Donor TERM that HAS a script-fragment VMAD, to copy the VMAD skeleton from.
  // Pin a vanilla Fallout4.esm terminal with fragments. Leave 0 to skip the VMAD
  // step (then donor-copy the VMAD in the GUI per the runbook).
  cDonorFragmentTerm = $0010DEE7; // vanilla Fallout4.esm TERM with a script fragment

  cDonorCobjEDID = 'LTMN_Workshop_CO_LootManContainerClean'; // 0xFB5

  cNoteObj = $0FB6;
  cRootObj = $0FB7;
  cGenObj  = $0FB8;
  cObjFObj = $0FB9;
  cLogObj  = $0FBA;
  cUtilObj = $0FBB;
  cCobjObj = $0FBC;

  cScrGen  = 'LTMN2:Fragments:Terminals:TERM_ConfigGeneral_FB8';
  cScrObjF = 'LTMN2:Fragments:Terminals:TERM_ConfigObjectFilter_FB9';
  cScrLog  = 'LTMN2:Fragments:Terminals:TERM_ConfigLogLevel_FBA';
  cScrUtil = 'LTMN2:Fragments:Terminals:TERM_ConfigUtility_FBB';

  // ANAM 'Type' enum values.
  cTypeSubmenuTerminal = 4;
  cTypeDisplayText     = 8;

var
  targetFile: IInterface;
  ran: boolean;

function FindByEDID(sig, edid: string): IInterface;
var
  grp, rec: IInterface;
  i: integer;
begin
  Result := nil;
  grp := GroupBySignature(targetFile, sig);
  if not Assigned(grp) then Exit;
  for i := 0 to Pred(ElementCount(grp)) do begin
    rec := ElementByIndex(grp, i);
    if EditorID(rec) = edid then begin Result := rec; Exit; end;
  end;
end;

// Keep the FileID (high byte), replace the whole ObjectID with objID (<= 0xFFF).
// Must clear all 24 object-id bits ($FF000000): a regular-ESP auto-assign uses
// nextObjectID (0x174F), whose bit 0x1000 would survive a $FFFFF000 mask.
procedure SetObjID(rec: IInterface; objID: cardinal);
begin
  SetLoadOrderFormID(rec, (GetLoadOrderFormID(rec) and $FF000000) or objID);
end;

function EnsureGroup(sig: string): IInterface;
begin
  Result := GroupBySignature(targetFile, sig);
  if not Assigned(Result) then Result := Add(targetFile, sig, True);
end;

function NewShell(sig, edid, full: string; objID: cardinal): IInterface;
var
  rec: IInterface;
begin
  rec := FindByEDID(sig, edid);
  if not Assigned(rec) then begin
    rec := Add(EnsureGroup(sig), sig, True);
    SetObjID(rec, objID);
    SetElementEditValues(rec, 'EDID', edid);
    AddMessage('created ' + sig + ' ' + edid + ' -> ' + IntToHex(GetLoadOrderFormID(rec), 8));
  end else
    AddMessage('exists: ' + edid);
  SetElementEditValues(rec, 'FULL', full);
  Result := rec;
end;

function Pad2(i: integer): string;
begin
  if i < 10 then Result := '0' + IntToStr(i) else Result := IntToStr(i);
end;

// Append a 'Menu Item' to a TERM's 'Menu Items' array and return it.
function AddMenuItem(term: IInterface; itid, anam: integer; itxt: string): IInterface;
var
  items, item: IInterface;
begin
  items := ElementByName(term, 'Menu Items');
  if not Assigned(items) then begin
    items := Add(term, 'Menu Items', True);
    // Add() seeds the array with one default (empty) element; drop it so item
    // indices start clean and ISIZ matches the real item count.
    while ElementCount(items) > 0 do RemoveByIndex(items, 0, False);
  end;
  item := ElementAssign(items, HighInteger, nil, False);
  SetElementEditValues(item, 'ITXT', itxt);
  SetElementNativeValues(item, 'ANAM', anam); // Type
  SetElementNativeValues(item, 'ITID', itid); // Item ID
  Result := item;
end;

procedure AddSubmenu(term: IInterface; itid: integer; itxt: string; submenu: IInterface);
begin
  // TNAM (the submenu FormID ref) is a same-file self-reference; xEdit cannot set
  // those from a script in this staged setup, so it is set by hand in the GUI.
  AddMenuItem(term, itid, cTypeSubmenuTerminal, itxt);
end;

// Build leaf Display-Text items ITID 1..N from a '|'-delimited label list.
procedure AddLeafItems(term: IInterface; labelsPipe: string);
var
  s, lbl: string;
  i, p: integer;
begin
  s := labelsPipe;
  i := 0;
  while Length(s) > 0 do begin
    p := Pos('|', s);
    if p > 0 then begin lbl := Copy(s, 1, p - 1); Delete(s, 1, p); end
    else begin lbl := s; s := ''; end;
    i := i + 1;
    AddMenuItem(term, i, cTypeDisplayText, lbl);
  end;
  SetElementNativeValues(term, 'ISIZ', i); // menu item count (also auto-maintained)
end;

// Donor-copy the VMAD skeleton onto term, then bind N fragments by ITID.
procedure BindFragments(term: IInterface; scriptName: string; count: integer);
var
  donor, dv, vmad, frags, frag, scr: IInterface;
  i: integer;
begin
  if cDonorFragmentTerm = 0 then begin
    AddMessage('  VMAD skipped for ' + EditorID(term) + ' (cDonorFragmentTerm=0; donor-copy in GUI).');
    Exit;
  end;
  donor := RecordByFormID(targetFile, cDonorFragmentTerm, False);
  if not Assigned(donor) then begin
    AddMessage('  !! donor fragment TERM not found; VMAD skipped for ' + EditorID(term));
    Exit;
  end;
  dv := ElementBySignature(donor, 'VMAD');
  if not Assigned(dv) then begin
    AddMessage('  !! donor has no VMAD; VMAD skipped for ' + EditorID(term));
    Exit;
  end;
  try
    RemoveElement(term, 'VMAD');
    vmad := wbCopyElementToRecord(dv, term, False, True); // copy donor VMAD into term
    if not Assigned(vmad) then begin
      AddMessage('  !! VMAD copy returned nil for ' + EditorID(term));
      Exit;
    end;
    // Empty the donor's record-level scripts (keep the array, count 0).
    scr := ElementByPath(vmad, 'Scripts');
    if Assigned(scr) then
      while ElementCount(scr) > 0 do RemoveByIndex(scr, 0, False);
    // Point the fragment script entry at ours; drop inherited properties.
    scr := ElementByPath(vmad, 'Script Fragments\Script');
    if Assigned(scr) then begin
      SetElementEditValues(scr, 'scriptName', scriptName);
      frag := ElementByPath(scr, 'Properties');
      if Assigned(frag) then
        while ElementCount(frag) > 0 do RemoveByIndex(frag, 0, False);
    end;
    // Rebuild the Fragments array: one per ITID (Fragment Index == ITID).
    frags := ElementByPath(vmad, 'Script Fragments\Fragments');
    if Assigned(frags) then
      while ElementCount(frags) > 0 do RemoveByIndex(frags, 0, False);
    for i := 1 to count do begin
      frag := ElementAssign(frags, HighInteger, nil, False);
      SetElementNativeValues(frag, 'Fragment Index', i);
      SetElementEditValues(frag, 'scriptName', scriptName);
      SetElementEditValues(frag, 'fragmentName', 'Fragment_Terminal_' + Pad2(i));
    end;
    AddMessage('  bound ' + IntToStr(count) + ' fragments on ' + EditorID(term) + ' (' + scriptName + ')');
  except
    AddMessage('  !! VMAD bind FAILED for ' + EditorID(term) + ' -- do this TERM via GUI donor-copy.');
  end;
end;

procedure BuildRecords;
var
  donor, note, root, gen, objf, lvl, util, cobjDonor, cobj: IInterface;
begin
  // --- NOTE 0xFB6: created from scratch (clean DNAM/SNAM; no donor union baggage).
  // MODL (holotape mesh) intentionally omitted -- add a vanilla holotape model in a
  // polish pass; the NOTE is still playable from the Pip-Boy without one.
  note := FindByEDID('NOTE', 'LTMN_ConfigHolotape');
  if not Assigned(note) then begin
    note := Add(EnsureGroup('NOTE'), 'NOTE', True);
    SetObjID(note, cNoteObj);
    SetElementEditValues(note, 'EDID', 'LTMN_ConfigHolotape');
    AddMessage('created NOTE -> ' + IntToHex(GetLoadOrderFormID(note), 8));
  end else
    AddMessage('exists: LTMN_ConfigHolotape');
  SetElementEditValues(note, 'FULL', 'LootMan Configuration');
  SetElementNativeValues(note, 'DNAM', 3);

  // --- TERM shells ---
  root := NewShell('TERM', 'LTMN_TERM_ConfigRoot', 'LootMan Configuration', cRootObj);
  gen  := NewShell('TERM', 'LTMN_TERM_ConfigGeneral', 'General Settings', cGenObj);
  objf := NewShell('TERM', 'LTMN_TERM_ConfigObjectFilter', 'Object Looting Filters', cObjFObj);
  lvl  := NewShell('TERM', 'LTMN_TERM_ConfigLogLevel', 'Log Level', cLogObj);
  util := NewShell('TERM', 'LTMN_TERM_ConfigUtility', 'Utilities & System', cUtilObj);

  // NOTE DNAM=3 (Terminal). SNAM -> root TERM is a same-file self-ref, set by hand
  // in the GUI (xEdit cannot set same-file refs from a script in this setup).
  if Assigned(note) then
    SetElementNativeValues(note, 'DNAM', 3);

  // --- Menu items (only if not already populated) ---
  if not Assigned(ElementByName(root, 'Menu Items')) then begin
    AddSubmenu(root, 1, 'General Settings', gen);
    AddSubmenu(root, 2, 'Object Looting Filters', objf);
    AddSubmenu(root, 3, 'Log Level', lvl);
    AddSubmenu(root, 4, 'Utilities & System', util);
    SetElementNativeValues(root, 'ISIZ', 4);
    AddMessage('root submenu items added');
  end;
  if not Assigned(ElementByName(gen, 'Menu Items')) then
    AddLeafItems(gen, 'Toggle LootMan|Toggle system messages|Toggle pickup sound|Toggle container animation|Toggle ignore overweight|Toggle deliver to player|Toggle silent looting|Toggle no looting in settlement|Toggle auto workshop link|Toggle unlock containers|Looting range +|Looting range -|Carry weight +|Carry weight -');
  if not Assigned(ElementByName(objf, 'Menu Items')) then
    AddLeafItems(objf, 'Toggle activators (ACTI)|Toggle aid (ALCH)|Toggle ammo (AMMO)|Toggle armor (ARMO)|Toggle books (BOOK)|Toggle containers (CONT)|Toggle flora (FLOR)|Toggle ingredients (INGR)|Toggle keys (KEYM)|Toggle misc (MISC)|Toggle corpses (NPC_)|Toggle weapons (WEAP)');
  if not Assigned(ElementByName(lvl, 'Menu Items')) then
    AddLeafItems(lvl, 'Trace|Debug|Info|Warn|Error|Critical|Off');
  if not Assigned(ElementByName(util, 'Menu Items')) then
    AddLeafItems(util, 'Loot now|Open LootMan storage|Install LootMan|Uninstall LootMan');

  // --- VMAD fragment binding (donor-copy + adapt) ---
  BindFragments(gen, cScrGen, 14);
  BindFragments(objf, cScrObjF, 12);
  BindFragments(lvl, cScrLog, 7);
  BindFragments(util, cScrUtil, 4);

  // --- COBJ 0xFBC ---
  cobj := FindByEDID('COBJ', 'LTMN_CO_ConfigHolotape');
  if not Assigned(cobj) then begin
    cobjDonor := FindByEDID('COBJ', cDonorCobjEDID);
    if not Assigned(cobjDonor) then
      AddMessage('!! donor COBJ not found; skipping COBJ.')
    else begin
      cobj := wbCopyElementToFile(cobjDonor, targetFile, True, True);
      SetObjID(cobj, cCobjObj);
      SetElementEditValues(cobj, 'EDID', 'LTMN_CO_ConfigHolotape');
      // CNAM (created object -> NOTE) is a same-file self-ref; retarget by hand in
      // GUI (the clone's CNAM still points at the donor's container until then).
      AddMessage('created COBJ -> ' + IntToHex(GetLoadOrderFormID(cobj), 8));
    end;
  end else AddMessage('exists: LTMN_CO_ConfigHolotape');

  AddMessage('Done. Records + menu items created. Set these 6 same-file references by hand in xEdit:');
  AddMessage('  1) NOTE LTMN_ConfigHolotape : SNAM -> TERM LTMN_TERM_ConfigRoot');
  AddMessage('  2-5) TERM LTMN_TERM_ConfigRoot : the 4 submenu items'' TNAM -> Config General/ObjectFilter/LogLevel/Utility');
  AddMessage('  6) COBJ LTMN_CO_ConfigHolotape : CNAM -> NOTE LTMN_ConfigHolotape');
end;

function FindFileByName(fn: string): IInterface;
var
  i: integer;
begin
  Result := nil;
  for i := 0 to Pred(FileCount) do
    if GetFileName(FileByIndex(i)) = fn then begin
      Result := FileByIndex(i);
      Exit;
    end;
end;

function Process(e: IInterface): integer;
begin
  Result := 0;
  if ran then Exit;          // run exactly once, regardless of which record xEdit passes
  ran := True;
  targetFile := FindFileByName('LootMan.esp');
  if not Assigned(targetFile) then begin
    AddMessage('LootMan.esp not loaded.');
    Exit;
  end;
  BuildRecords;
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
