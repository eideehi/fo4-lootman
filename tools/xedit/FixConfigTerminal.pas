{ In-place fix: remove the empty leading 'Menu Item' (ITID blank) that Add() seeded
  in each config TERM, and resync ISIZ. Touches no references, so it is scriptable. }
unit FixConfigTerminal;

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

procedure TrimEmpty(f: IInterface; edid: string);
var
  term, items, item0: IInterface;
begin
  term := FindByEDID(f, 'TERM', edid);
  if not Assigned(term) then begin AddMessage(edid + ': not found'); Exit; end;
  items := ElementByName(term, 'Menu Items');
  if not Assigned(items) or (ElementCount(items) = 0) then begin AddMessage(edid + ': no items'); Exit; end;
  item0 := ElementByIndex(items, 0);
  if GetElementEditValues(item0, 'ITID') = '' then begin
    RemoveByIndex(items, 0, False);
    SetElementNativeValues(term, 'ISIZ', ElementCount(items));
    AddMessage(edid + ': trimmed empty leading item; now ' + IntToStr(ElementCount(items)) + ' items, ISIZ synced');
  end else
    AddMessage(edid + ': item 0 ITID=' + GetElementEditValues(item0, 'ITID') + ' (not empty) -- left as-is');
end;

function Process(e: IInterface): integer;
var
  f: IInterface;
begin
  Result := 0;
  if done then Exit;
  done := True;
  f := FindFileByName('LootMan.esp');
  if not Assigned(f) then begin AddMessage('LootMan.esp not loaded'); Exit; end;
  TrimEmpty(f, 'LTMN_TERM_ConfigRoot');
  TrimEmpty(f, 'LTMN_TERM_ConfigGeneral');
  TrimEmpty(f, 'LTMN_TERM_ConfigObjectFilter');
  TrimEmpty(f, 'LTMN_TERM_ConfigLogLevel');
  TrimEmpty(f, 'LTMN_TERM_ConfigUtility');
  AddMessage('TRIM DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
