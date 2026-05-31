{ Concern 2 fix: change every leaf config-menu item from "Display Text" (ANAM 8,
  which shows a blank result page) to "Force Redraw" so selecting an item runs its
  fragment and then redraws the SAME menu (no navigation, no blank screen).
  The Force Redraw ANAM integer is discovered from vanilla Fallout4.esm terminals.
  ANAM is a plain enum int (no FormID), so this is fully scriptable. }
unit FixLeafItemsForceRedraw;

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

// Scan vanilla terminals for a menu item whose ANAM enum name contains "edraw"
// (Force Redraw). Logs distinct ANAM type names seen for sanity. Returns -1 if none.
function FindForceRedrawValue(esm: IInterface): integer;
var
  grp, term, items, item: IInterface;
  i, j: integer;
  v, seen: string;
begin
  Result := -1;
  seen := '|';
  grp := GroupBySignature(esm, 'TERM');
  if not Assigned(grp) then Exit;
  for i := 0 to Pred(ElementCount(grp)) do begin
    term := ElementByIndex(grp, i);
    items := ElementByName(term, 'Menu Items');
    if Assigned(items) then
      for j := 0 to Pred(ElementCount(items)) do begin
        item := ElementByIndex(items, j);
        v := GetElementEditValues(item, 'ANAM');
        if (Length(v) > 0) and (Pos('|' + v + '|', seen) = 0) then begin
          seen := seen + v + '|';
          AddMessage('  ANAM type: "' + v + '" = ' + IntToStr(GetElementNativeValues(item, 'ANAM')));
        end;
        if (Result < 0) and (Pos('edraw', v) > 0) then begin
          Result := GetElementNativeValues(item, 'ANAM');
          AddMessage('  -> Force Redraw value = ' + IntToStr(Result) + ' (from ' + EditorID(term) + ')');
        end;
      end;
    if (Result >= 0) and (i > 50) then Exit; // found + enough type samples
  end;
end;

procedure SetLeafType(lm: IInterface; edid: string; frValue: integer);
var
  term, items, item: IInterface;
  j: integer;
begin
  term := FindByEDID(lm, 'TERM', edid);
  if not Assigned(term) then begin AddMessage(edid + ': not found'); Exit; end;
  items := ElementByName(term, 'Menu Items');
  if not Assigned(items) then begin AddMessage(edid + ': no items'); Exit; end;
  for j := 0 to Pred(ElementCount(items)) do begin
    item := ElementByIndex(items, j);
    SetElementNativeValues(item, 'ANAM', frValue);
  end;
  AddMessage(edid + ': set ' + IntToStr(ElementCount(items)) + ' items ANAM -> ' + IntToStr(frValue));
end;

function Process(e: IInterface): integer;
var
  lm, esm: IInterface;
  fr: integer;
begin
  Result := 0;
  if done then Exit;
  done := True;
  lm := FindFileByName('LootMan.esp');
  esm := FindFileByName('Fallout4.esm');
  if not Assigned(lm) or not Assigned(esm) then begin AddMessage('files not loaded'); Exit; end;

  AddMessage('===== discovering Force Redraw ANAM value =====');
  fr := FindForceRedrawValue(esm);
  if fr < 0 then begin
    AddMessage('!! Force Redraw type not found in scanned terminals; see ANAM types above and tell me the value.');
    Exit;
  end;

  AddMessage('===== applying to leaf TERMs (root submenu items left as Submenu-Terminal) =====');
  SetLeafType(lm, 'LTMN_TERM_ConfigGeneral', fr);
  SetLeafType(lm, 'LTMN_TERM_ConfigObjectFilter', fr);
  SetLeafType(lm, 'LTMN_TERM_ConfigLogLevel', fr);
  SetLeafType(lm, 'LTMN_TERM_ConfigUtility', fr);
  AddMessage('FORCE REDRAW SET DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
