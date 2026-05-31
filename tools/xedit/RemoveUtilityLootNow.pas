{ Remove the Utility terminal's "Loot now" item (ITID 1): ExecuteLooting had no
  effect from a terminal fragment. Leaves Install (ITID 1) and Uninstall (ITID 2).
  Removes the menu item (Menu Items RemoveByIndex persists), renumbers, resyncs
  ISIZ, and re-authors the VMAD to 2 fragments (in-place VMAD edits do NOT persist;
  the donor-copy re-author does). Item identified by position (ITID 1 / index 0),
  so this works for both en and ja. Run ja with -cp:utf8 to preserve UTF-8. }
unit RemoveUtilityLootNow;

const
  cDonorFragmentTerm = $0010DEE7;
  cScrUtil = 'LTMN2:Fragments:Terminals:TERM_ConfigUtility_FBB';

var
  done: boolean;
  targetFile: IInterface;

function FindFileByName(fn: string): IInterface;
var i: integer;
begin
  Result := nil;
  for i := 0 to Pred(FileCount) do
    if GetFileName(FileByIndex(i)) = fn then begin Result := FileByIndex(i); Exit; end;
end;

function FindByEDID(sig, edid: string): IInterface;
var grp, rec: IInterface; i: integer;
begin
  Result := nil;
  grp := GroupBySignature(targetFile, sig);
  if not Assigned(grp) then Exit;
  for i := 0 to Pred(ElementCount(grp)) do begin
    rec := ElementByIndex(grp, i);
    if EditorID(rec) = edid then begin Result := rec; Exit; end;
  end;
end;

function Pad2(i: integer): string;
begin
  if i < 10 then Result := '0' + IntToStr(i) else Result := IntToStr(i);
end;

procedure DumpItemsFrags(term: IInterface; tag: string);
var items, item, vmad, frags, frag: IInterface; i: integer;
begin
  items := ElementByName(term, 'Menu Items');
  AddMessage('  [' + tag + '] ISIZ=' + GetElementEditValues(term, 'ISIZ') + ' items=' + IntToStr(ElementCount(items)));
  for i := 0 to Pred(ElementCount(items)) do begin
    item := ElementByIndex(items, i);
    AddMessage('    ITID=' + GetElementEditValues(item, 'ITID') + ' ITXT=[' + GetElementEditValues(item, 'ITXT') + ']');
  end;
  vmad := ElementBySignature(term, 'VMAD');
  if Assigned(vmad) then begin
    frags := ElementByPath(vmad, 'Script Fragments\Fragments');
    AddMessage('  [' + tag + '] vmadFrags=' + IntToStr(ElementCount(frags)));
  end;
end;

procedure ReauthorVMAD(term: IInterface; scriptName: string; count: integer);
var donor, dv, vmad, frags, frag, scr: IInterface; i: integer;
begin
  donor := RecordByFormID(targetFile, cDonorFragmentTerm, False);
  dv := ElementBySignature(donor, 'VMAD');
  if not Assigned(dv) then begin AddMessage('!! donor VMAD missing'); Exit; end;
  try
    RemoveElement(term, 'VMAD');
    vmad := wbCopyElementToRecord(dv, term, False, True);
    scr := ElementByPath(vmad, 'Scripts');
    if Assigned(scr) then while ElementCount(scr) > 0 do RemoveByIndex(scr, 0, False);
    scr := ElementByPath(vmad, 'Script Fragments\Script');
    if Assigned(scr) then begin
      SetElementEditValues(scr, 'scriptName', scriptName);
      frag := ElementByPath(scr, 'Properties');
      if Assigned(frag) then while ElementCount(frag) > 0 do RemoveByIndex(frag, 0, False);
    end;
    frags := ElementByPath(vmad, 'Script Fragments\Fragments');
    if Assigned(frags) then while ElementCount(frags) > 0 do RemoveByIndex(frags, 0, False);
    for i := 1 to count do begin
      frag := ElementAssign(frags, HighInteger, nil, False);
      SetElementNativeValues(frag, 'Fragment Index', i);
      SetElementEditValues(frag, 'scriptName', scriptName);
      SetElementEditValues(frag, 'fragmentName', 'Fragment_Terminal_' + Pad2(i));
    end;
    AddMessage('re-authored VMAD with ' + IntToStr(count) + ' fragments');
  except
    AddMessage('!! VMAD re-author FAILED');
  end;
end;

function Process(e: IInterface): integer;
var util, items: IInterface; i: integer;
begin
  Result := 0;
  if done then Exit;
  done := True;
  targetFile := FindFileByName('LootMan.esp');
  if not Assigned(targetFile) then begin AddMessage('LootMan.esp not loaded'); Exit; end;
  util := FindByEDID('TERM', 'LTMN_TERM_ConfigUtility');
  if not Assigned(util) then begin AddMessage('Utility TERM not found'); Exit; end;
  items := ElementByName(util, 'Menu Items');

  AddMessage('===== BEFORE ====='); DumpItemsFrags(util, 'before');

  if ElementCount(items) <> 3 then begin
    AddMessage('!! expected 3 items, got ' + IntToStr(ElementCount(items)) + '; aborting (no change).');
    Exit;
  end;
  if GetElementNativeValues(ElementByIndex(items, 0), 'ITID') <> 1 then begin
    AddMessage('!! item[0] ITID != 1; aborting.');
    Exit;
  end;
  AddMessage('removing item[0] (Loot now) ITXT=[' + GetElementEditValues(ElementByIndex(items, 0), 'ITXT') + ']');
  RemoveByIndex(items, 0, False);
  for i := 0 to Pred(ElementCount(items)) do
    SetElementNativeValues(ElementByIndex(items, i), 'ITID', i + 1);
  SetElementNativeValues(util, 'ISIZ', ElementCount(items));
  ReauthorVMAD(util, cScrUtil, 2);

  AddMessage('===== AFTER ====='); DumpItemsFrags(util, 'after');
  AddMessage('REMOVE LOOT NOW DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
