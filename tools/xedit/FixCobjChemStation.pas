{ Re-point the config-holotape COBJ from the settlement workshop (Furniture > Containers)
  to the Chemistry Station (Utility category) and make it free to craft.
  BNAM / FNAM reference Fallout4.esm keywords (masters), so they are scriptable. }
unit FixCobjChemStation;

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
  lm, esm, cobj, chemKwd, recipeUtil, grp, rec, fnam, kw, fvpa: IInterface;
  i, guard: integer;
  ed: string;
begin
  Result := 0;
  if done then Exit;
  done := True;
  lm := FindFileByName('LootMan.esp');
  esm := FindFileByName('Fallout4.esm');
  if not Assigned(lm) or not Assigned(esm) then begin AddMessage('files not loaded'); Exit; end;

  cobj := FindByEDID(lm, 'COBJ', 'LTMN_CO_ConfigHolotape');
  if not Assigned(cobj) then begin AddMessage('COBJ LTMN_CO_ConfigHolotape not found'); Exit; end;

  chemKwd := FindByEDID(esm, 'KYWD', 'WorkbenchChemlab');

  // Enumerate Recipe* keywords (chem-station categories) and capture the Utility one.
  recipeUtil := nil;
  grp := GroupBySignature(esm, 'KYWD');
  AddMessage('===== Recipe* category keywords =====');
  if Assigned(grp) then
    for i := 0 to Pred(ElementCount(grp)) do begin
      rec := ElementByIndex(grp, i);
      ed := EditorID(rec);
      if Pos('Recipe', ed) = 1 then begin
        AddMessage('  ' + IntToHex(GetLoadOrderFormID(rec), 8) + '  ' + ed);
        if (Pos('Utilit', ed) > 0) and (not Assigned(recipeUtil)) then recipeUtil := rec;
      end;
    end;

  AddMessage('===== applying fixes =====');

  // 1) BNAM -> Chemistry Station
  if Assigned(chemKwd) then begin
    SetElementNativeValues(cobj, 'BNAM', GetLoadOrderFormID(chemKwd));
    AddMessage('BNAM -> ' + EditorID(chemKwd) + ' [' + IntToHex(GetLoadOrderFormID(chemKwd), 8) + ']');
  end else
    AddMessage('!! WorkbenchChemlab not found; BNAM unchanged');

  // 2) FNAM category -> Utility
  if Assigned(recipeUtil) then begin
    fnam := ElementBySignature(cobj, 'FNAM');
    if Assigned(fnam) then begin
      kw := ElementByName(fnam, 'Keyword');
      if not Assigned(kw) and (ElementCount(fnam) > 0) then kw := ElementByIndex(fnam, 0);
      if Assigned(kw) then begin
        SetNativeValue(kw, GetLoadOrderFormID(recipeUtil));
        AddMessage('FNAM category -> ' + EditorID(recipeUtil) + ' [' + IntToHex(GetLoadOrderFormID(recipeUtil), 8) + ']');
      end else
        AddMessage('!! FNAM has no Keyword child; unchanged');
    end else
      AddMessage('!! FNAM element missing; unchanged');
  end else
    AddMessage('!! RecipeUtility-style keyword not found; FNAM left as-is');

  // 3) Make it free to craft. Empty the components array, but CAP the loop so a
  // misbehaving RemoveByIndex (it did not decrement ElementCount here) can never
  // spin forever. If emptying fails, drop the whole FVPA subrecord (vanilla free
  // recipes have no FVPA at all).
  fvpa := ElementBySignature(cobj, 'FVPA');
  if Assigned(fvpa) then begin
    guard := 0;
    while (ElementCount(fvpa) > 0) and (guard < 30) do begin
      RemoveByIndex(fvpa, 0, False);
      guard := guard + 1;
    end;
    AddMessage('FVPA empty attempt: count=' + IntToStr(ElementCount(fvpa)) + ' iterations=' + IntToStr(guard));
    if ElementCount(fvpa) > 0 then begin
      for i := 0 to Pred(ElementCount(cobj)) do
        if Signature(ElementByIndex(cobj, i)) = 'FVPA' then begin
          RemoveByIndex(cobj, i, False);
          AddMessage('fallback: removed whole FVPA subrecord');
          Break;
        end;
    end;
  end else
    AddMessage('FVPA already absent (free)');

  AddMessage('===== result (in-memory, will save on close) =====');
  DumpElem(cobj, 4, '  ');
  AddMessage('FIX DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
