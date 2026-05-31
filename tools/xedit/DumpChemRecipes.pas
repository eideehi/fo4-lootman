{ Read-only: enumerate Fallout4.esm COBJ recipes that build at the Chemistry Station
  (BNAM = WorkbenchChemlab 00102158) and report their FNAM categories, so we can pick
  the right category keyword (e.g. Utility) for the config holotape. }
unit DumpChemRecipes;

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
  esm, grp, c, fnam: IInterface;
  i, j, cnt: integer;
  cats, line: string;
begin
  Result := 0;
  if done then Exit;
  done := True;
  esm := FindFileByName('Fallout4.esm');
  if not Assigned(esm) then begin AddMessage('Fallout4.esm not loaded'); Exit; end;

  grp := GroupBySignature(esm, 'COBJ');
  cnt := 0;
  AddMessage('===== Chemistry Station COBJ (BNAM=WorkbenchChemlab 00102158) =====');
  for i := 0 to Pred(ElementCount(grp)) do begin
    c := ElementByIndex(grp, i);
    if GetElementNativeValues(c, 'BNAM') = $00102158 then begin
      cats := '';
      fnam := ElementByName(c, 'FNAM');
      if Assigned(fnam) then
        for j := 0 to Pred(ElementCount(fnam)) do
          cats := cats + GetEditValue(ElementByIndex(fnam, j)) + ' | ';
      line := 'CHEM ' + EditorID(c) + '  CNAM=[' + GetElementEditValues(c, 'CNAM') + ']  FNAM=[' + cats + ']';
      AddMessage(line);
      cnt := cnt + 1;
      if cnt = 1 then begin
        AddMessage('--- full sample structure of first chem COBJ ---');
        DumpElem(c, 4, '    ');
        AddMessage('--- end sample ---');
      end;
      if cnt >= 50 then begin AddMessage('(capped at 50)'); Break; end;
    end;
  end;
  AddMessage('chem COBJ printed: ' + IntToStr(cnt));
  AddMessage('DUMP DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
