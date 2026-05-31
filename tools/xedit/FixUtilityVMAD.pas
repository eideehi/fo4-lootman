{ Remove the orphaned Utility VMAD Fragment Index 4. An in-place RemoveByIndex on
  an existing VMAD does NOT persist on save (FixUtilityMenu hit this); re-authoring
  the whole VMAD via RemoveElement + wbCopyElementToRecord (the original creation
  method) DOES persist. Re-binds the Utility TERM to 3 fragments (01/02/03).
  Touches only the VMAD subrecord -- strings (incl. ja) are untouched.
  Run on ja with -cp:utf8 so the save preserves UTF-8 Japanese. }
unit FixUtilityVMAD;

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

procedure DumpFrags(term: IInterface; tag: string);
var vmad, frags, frag: IInterface; i: integer;
begin
  vmad := ElementBySignature(term, 'VMAD');
  if not Assigned(vmad) then begin AddMessage('  [' + tag + '] no VMAD'); Exit; end;
  frags := ElementByPath(vmad, 'Script Fragments\Fragments');
  AddMessage('  [' + tag + '] fragment count=' + IntToStr(ElementCount(frags)));
  for i := 0 to Pred(ElementCount(frags)) do begin
    frag := ElementByIndex(frags, i);
    AddMessage('    Index=' + GetElementEditValues(frag, 'Fragment Index') +
      ' name=' + GetElementEditValues(frag, 'fragmentName'));
  end;
end;

procedure ReauthorVMAD(term: IInterface; scriptName: string; count: integer);
var donor, dv, vmad, frags, frag, scr: IInterface; i: integer;
begin
  donor := RecordByFormID(targetFile, cDonorFragmentTerm, False);
  if not Assigned(donor) then begin AddMessage('!! donor TERM not found'); Exit; end;
  dv := ElementBySignature(donor, 'VMAD');
  if not Assigned(dv) then begin AddMessage('!! donor has no VMAD'); Exit; end;
  try
    RemoveElement(term, 'VMAD');
    vmad := wbCopyElementToRecord(dv, term, False, True);
    if not Assigned(vmad) then begin AddMessage('!! VMAD copy returned nil'); Exit; end;
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
    AddMessage('re-authored VMAD on ' + EditorID(term) + ' with ' + IntToStr(count) + ' fragments');
  except
    AddMessage('!! VMAD re-author FAILED');
  end;
end;

function Process(e: IInterface): integer;
var util: IInterface;
begin
  Result := 0;
  if done then Exit;
  done := True;
  targetFile := FindFileByName('LootMan.esp');
  if not Assigned(targetFile) then begin AddMessage('LootMan.esp not loaded'); Exit; end;
  util := FindByEDID('TERM', 'LTMN_TERM_ConfigUtility');
  if not Assigned(util) then begin AddMessage('Utility TERM not found'); Exit; end;
  AddMessage('===== BEFORE ====='); DumpFrags(util, 'before');
  ReauthorVMAD(util, cScrUtil, 3);
  AddMessage('===== AFTER ====='); DumpFrags(util, 'after');
  AddMessage('UTILITY VMAD FIX DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
