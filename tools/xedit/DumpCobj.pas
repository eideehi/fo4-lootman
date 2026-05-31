{ Read-only: inspect the config-holotape COBJ (what it inherited from the workshop
  container donor) and enumerate Fallout4.esm crafting-bench keywords so we can
  re-point BNAM to the Chemistry Station. }
unit DumpCobj;

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

procedure DumpElem(e: IInterface; depth: integer; indent: string);
var
  i: integer;
begin
  AddMessage(indent + '<' + Signature(e) + '> ' + Name(e) + ' = [' + GetEditValue(e) + ']');
  if depth > 0 then
    for i := 0 to Pred(ElementCount(e)) do
      DumpElem(ElementByIndex(e, i), depth - 1, indent + '  ');
end;

function Process(e: IInterface): integer;
var
  lm, esm, cobj, donor, grp, rec: IInterface;
  i: integer;
  ed: string;
begin
  Result := 0;
  if done then Exit;
  done := True;
  lm := FindFileByName('LootMan.esp');
  esm := FindFileByName('Fallout4.esm');

  AddMessage('===== OUR COBJ: LTMN_CO_ConfigHolotape =====');
  cobj := FindByEDID(lm, 'COBJ', 'LTMN_CO_ConfigHolotape');
  if Assigned(cobj) then DumpElem(cobj, 4, '  ') else AddMessage('  not found');

  AddMessage('===== DONOR COBJ: LTMN_Workshop_CO_LootManContainerClean (BNAM only) =====');
  donor := FindByEDID(lm, 'COBJ', 'LTMN_Workshop_CO_LootManContainerClean');
  if Assigned(donor) then
    AddMessage('  BNAM=[' + GetElementEditValues(donor, 'BNAM') + ']')
  else
    AddMessage('  not found');

  AddMessage('===== Fallout4.esm KYWD with Workbench/Chem in EDID =====');
  if Assigned(esm) then begin
    grp := GroupBySignature(esm, 'KYWD');
    if Assigned(grp) then
      for i := 0 to Pred(ElementCount(grp)) do begin
        rec := ElementByIndex(grp, i);
        ed := EditorID(rec);
        if (Pos('Workbench', ed) > 0) or (Pos('Chem', ed) > 0) then
          AddMessage('  KYWD ' + IntToHex(GetLoadOrderFormID(rec), 8) + '  ' + ed);
      end;
  end;

  AddMessage('DUMP DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
