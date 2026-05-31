{ Read-only: verify the translated ja LootMan.esp kept its structure (refs, ANAM
  Force Redraw, VMAD fragments, COBJ chem-station) while showing the Japanese ITXT.
  Run with -cp:utf8 so Japanese reads correctly. }
unit DumpJaVerify;

var
  done: boolean;

function FindFileByName(fn: string): IInterface;
var i: integer;
begin
  Result := nil;
  for i := 0 to Pred(FileCount) do
    if GetFileName(FileByIndex(i)) = fn then begin Result := FileByIndex(i); Exit; end;
end;

function FindByEDID(f: IInterface; sig, edid: string): IInterface;
var grp, rec: IInterface; i: integer;
begin
  Result := nil;
  grp := GroupBySignature(f, sig);
  if not Assigned(grp) then Exit;
  for i := 0 to Pred(ElementCount(grp)) do begin
    rec := ElementByIndex(grp, i);
    if EditorID(rec) = edid then begin Result := rec; Exit; end;
  end;
end;

function FragCount(term: IInterface): integer;
var vmad, frags: IInterface;
begin
  Result := 0;
  vmad := ElementBySignature(term, 'VMAD');
  if not Assigned(vmad) then Exit;
  frags := ElementByPath(vmad, 'Script Fragments\Fragments');
  if Assigned(frags) then Result := ElementCount(frags);
end;

procedure DumpTerm(lm: IInterface; edid: string; allItems: boolean);
var term, items, item: IInterface; i: integer;
begin
  term := FindByEDID(lm, 'TERM', edid);
  if not Assigned(term) then begin AddMessage(edid + ': NOT FOUND'); Exit; end;
  items := ElementByName(term, 'Menu Items');
  AddMessage(edid + ': FULL=[' + GetElementEditValues(term, 'FULL') + '] ISIZ=' +
    GetElementEditValues(term, 'ISIZ') + ' items=' + IntToStr(ElementCount(items)) +
    ' vmadFrags=' + IntToStr(FragCount(term)));
  for i := 0 to Pred(ElementCount(items)) do begin
    if (not allItems) and (i > 0) and (i < Pred(ElementCount(items))) then Continue;
    item := ElementByIndex(items, i);
    AddMessage('  [' + IntToStr(i) + '] ITID=' + GetElementEditValues(item, 'ITID') +
      ' ANAM=[' + GetElementEditValues(item, 'ANAM') + '] ITXT=[' + GetElementEditValues(item, 'ITXT') +
      '] TNAM=[' + GetElementEditValues(item, 'TNAM') + ']');
  end;
end;

function Process(e: IInterface): integer;
var lm, note, snam, cobj: IInterface;
    fvpaState: string;
begin
  Result := 0;
  if done then Exit;
  done := True;
  lm := FindFileByName('LootMan.esp');
  if not Assigned(lm) then begin AddMessage('LootMan.esp not loaded'); Exit; end;

  AddMessage('===== NOTE =====');
  note := FindByEDID(lm, 'NOTE', 'LTMN_ConfigHolotape');
  if Assigned(note) then begin
    snam := ElementBySignature(note, 'SNAM');
    AddMessage('NOTE: FULL=[' + GetElementEditValues(note, 'FULL') + '] DNAM=[' +
      GetElementEditValues(note, 'DNAM') + '] SNAM\Terminal=[' + GetElementEditValues(note, 'SNAM\Terminal') + ']');
  end else AddMessage('NOTE not found');

  AddMessage('===== TERMs (FULL ja, ISIZ, item count, VMAD frag count, items) =====');
  DumpTerm(lm, 'LTMN_TERM_ConfigRoot', True);
  DumpTerm(lm, 'LTMN_TERM_ConfigGeneral', False);
  DumpTerm(lm, 'LTMN_TERM_ConfigObjectFilter', False);
  DumpTerm(lm, 'LTMN_TERM_ConfigLogLevel', False);
  DumpTerm(lm, 'LTMN_TERM_ConfigUtility', True);

  AddMessage('===== COBJ =====');
  cobj := FindByEDID(lm, 'COBJ', 'LTMN_CO_ConfigHolotape');
  if Assigned(cobj) then begin
    if Assigned(ElementBySignature(cobj, 'FVPA')) then fvpaState := 'YES (has components)'
    else fvpaState := 'no (free craft)';
    AddMessage('COBJ: BNAM=[' + GetElementEditValues(cobj, 'BNAM') + '] FNAM=[' +
      GetElementEditValues(cobj, 'FNAM\Keyword') + '] CNAM=[' + GetElementEditValues(cobj, 'CNAM') +
      '] FVPA=' + fvpaState);
  end else AddMessage('COBJ not found');

  AddMessage('JA VERIFY DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
