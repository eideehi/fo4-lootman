{ Read-only: dump the config terminal records' reference fields for verification.
  Reads NOTE SNAM via the Terminal child / LinksTo so a set ref is not missed. }
unit DumpConfigRefs;

var
  done: boolean;

function FindFileByName(fn: string): IInterface;
var
  i: integer;
begin
  Result := nil;
  for i := 0 to Pred(FileCount) do
    if GetFileName(FileByIndex(i)) = fn then begin Result := FileByIndex(i); Exit; end;
end;

function FindByEDID(f: IInterface; sig, edid: string): IInterface;
var
  grp, rec: IInterface;
  i: integer;
begin
  Result := nil;
  grp := GroupBySignature(f, sig);
  if not Assigned(grp) then Exit;
  for i := 0 to Pred(ElementCount(grp)) do begin
    rec := ElementByIndex(grp, i);
    if EditorID(rec) = edid then begin Result := rec; Exit; end;
  end;
end;

procedure DumpTerm(f: IInterface; edid: string; full: boolean);
var
  term, items, item: IInterface;
  i: integer;
begin
  term := FindByEDID(f, 'TERM', edid);
  if not Assigned(term) then begin AddMessage(edid + ': not found'); Exit; end;
  items := ElementByName(term, 'Menu Items');
  AddMessage(edid + ': ISIZ=' + GetElementEditValues(term, 'ISIZ') +
    ' itemCount=' + IntToStr(ElementCount(items)));
  if full and Assigned(items) then
    for i := 0 to Pred(ElementCount(items)) do begin
      item := ElementByIndex(items, i);
      AddMessage('  [' + IntToStr(i) + '] ITID=' + GetElementEditValues(item, 'ITID') +
        ' ITXT=[' + GetElementEditValues(item, 'ITXT') + '] ANAM=[' + GetElementEditValues(item, 'ANAM') +
        '] TNAM=[' + GetElementEditValues(item, 'TNAM') + ']');
    end;
end;

function Process(e: IInterface): integer;
var
  f, note, snam, child, lr, cobj: IInterface;
begin
  Result := 0;
  if done then Exit;
  done := True;
  f := FindFileByName('LootMan.esp');
  if not Assigned(f) then begin AddMessage('LootMan.esp not loaded'); Exit; end;

  note := FindByEDID(f, 'NOTE', 'LTMN_ConfigHolotape');
  if Assigned(note) then begin
    AddMessage('NOTE LTMN_ConfigHolotape DNAM=[' + GetElementEditValues(note, 'DNAM') + ']');
    snam := ElementBySignature(note, 'SNAM');
    if not Assigned(snam) then
      AddMessage('  SNAM: <absent>')
    else begin
      AddMessage('  SNAM editvalue=[' + GetEditValue(snam) + '] childCount=' + IntToStr(ElementCount(snam)));
      AddMessage('  SNAM\Terminal=[' + GetElementEditValues(note, 'SNAM\Terminal') + ']');
      lr := LinksTo(snam);
      if not Assigned(lr) and (ElementCount(snam) > 0) then begin
        child := ElementByIndex(snam, 0);
        AddMessage('  SNAM child editvalue=[' + GetEditValue(child) + ']');
        lr := LinksTo(child);
      end;
      if Assigned(lr) then AddMessage('  SNAM -> ' + Name(lr))
      else AddMessage('  SNAM -> <null/unset>');
    end;
  end;

  DumpTerm(f, 'LTMN_TERM_ConfigRoot', True);
  DumpTerm(f, 'LTMN_TERM_ConfigGeneral', False);
  DumpTerm(f, 'LTMN_TERM_ConfigObjectFilter', False);
  DumpTerm(f, 'LTMN_TERM_ConfigLogLevel', False);
  DumpTerm(f, 'LTMN_TERM_ConfigUtility', False);

  cobj := FindByEDID(f, 'COBJ', 'LTMN_CO_ConfigHolotape');
  if Assigned(cobj) then
    AddMessage('COBJ LTMN_CO_ConfigHolotape CNAM=[' + GetElementEditValues(cobj, 'CNAM') + ']');

  AddMessage('DUMP DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
