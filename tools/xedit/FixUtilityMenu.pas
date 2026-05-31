{ Concern 3: remove the "Open LootMan storage" item (ITID 2) from the Utility TERM
  (a container UI cannot present over an open terminal), renumber the remaining
  items to 1..3, resync ISIZ, and drop the now-orphaned VMAD Fragment Index 4.
  Matches the renumbered TERM_ConfigUtility_FBB.psc (01=Loot,02=Install,03=Uninstall).
  All loops are bounded; only single RemoveByIndex calls -- cannot hang. }
unit FixUtilityMenu;

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

procedure DumpItems(items: IInterface; tag: string);
var
  i: integer;
  item: IInterface;
begin
  AddMessage('  [' + tag + '] item count=' + IntToStr(ElementCount(items)));
  for i := 0 to Pred(ElementCount(items)) do begin
    item := ElementByIndex(items, i);
    AddMessage('    item[' + IntToStr(i) + '] ITID=' + GetElementEditValues(item, 'ITID') +
      ' ANAM=[' + GetElementEditValues(item, 'ANAM') + '] ITXT=[' + GetElementEditValues(item, 'ITXT') + ']');
  end;
end;

procedure DumpFrags(frags: IInterface; tag: string);
var
  i: integer;
  frag: IInterface;
begin
  if not Assigned(frags) then begin AddMessage('  [' + tag + '] no fragments'); Exit; end;
  AddMessage('  [' + tag + '] fragment count=' + IntToStr(ElementCount(frags)));
  for i := 0 to Pred(ElementCount(frags)) do begin
    frag := ElementByIndex(frags, i);
    AddMessage('    frag[' + IntToStr(i) + '] Index=' + GetElementEditValues(frag, 'Fragment Index') +
      ' name=' + GetElementEditValues(frag, 'fragmentName'));
  end;
end;

function Process(e: IInterface): integer;
var
  lm, util, items, item, vmad, frags: IInterface;
  i, removeIdx, fragIdx: integer;
begin
  Result := 0;
  if done then Exit;
  done := True;
  lm := FindFileByName('LootMan.esp');
  if not Assigned(lm) then begin AddMessage('LootMan.esp not loaded'); Exit; end;

  util := FindByEDID(lm, 'TERM', 'LTMN_TERM_ConfigUtility');
  if not Assigned(util) then begin AddMessage('Utility TERM not found'); Exit; end;
  items := ElementByName(util, 'Menu Items');
  if not Assigned(items) then begin AddMessage('Utility has no Menu Items'); Exit; end;

  AddMessage('===== BEFORE =====');
  DumpItems(items, 'before');

  // Locate the "Open LootMan storage" item: ITID 2 AND label contains "torage".
  removeIdx := -1;
  for i := 0 to Pred(ElementCount(items)) do begin
    item := ElementByIndex(items, i);
    if (GetElementNativeValues(item, 'ITID') = 2) and (Pos('torage', GetElementEditValues(item, 'ITXT')) > 0) then begin
      removeIdx := i;
      Break;
    end;
  end;
  if removeIdx < 0 then begin
    AddMessage('!! ITID=2 storage item not found (already removed?); leaving menu unchanged.');
  end else begin
    RemoveByIndex(items, removeIdx, False);
    AddMessage('removed storage item at array index ' + IntToStr(removeIdx));
    // Renumber the remaining items to a clean 1..N so ITID == Fragment Index.
    for i := 0 to Pred(ElementCount(items)) do
      SetElementNativeValues(ElementByIndex(items, i), 'ITID', i + 1);
    SetElementNativeValues(util, 'ISIZ', ElementCount(items));
    AddMessage('renumbered items 1..' + IntToStr(ElementCount(items)) + '; ISIZ synced');

    // Drop the orphaned VMAD fragment (Fragment Index 4) so it no longer points at a
    // function the renumbered .pex dropped. Indices 1..3 already map 01/02/03.
    vmad := ElementBySignature(util, 'VMAD');
    if Assigned(vmad) then begin
      frags := ElementByPath(vmad, 'Script Fragments\Fragments');
      if Assigned(frags) then begin
        fragIdx := -1;
        for i := 0 to Pred(ElementCount(frags)) do
          if GetElementNativeValues(ElementByIndex(frags, i), 'Fragment Index') = 4 then begin
            fragIdx := i;
            Break;
          end;
        if fragIdx >= 0 then begin
          RemoveByIndex(frags, fragIdx, False);
          AddMessage('removed VMAD Fragment Index 4 (array pos ' + IntToStr(fragIdx) + ')');
        end else
          AddMessage('!! VMAD Fragment Index 4 not found');
      end;
    end;
  end;

  AddMessage('===== AFTER =====');
  DumpItems(items, 'after');
  vmad := ElementBySignature(util, 'VMAD');
  if Assigned(vmad) then
    DumpFrags(ElementByPath(vmad, 'Script Fragments\Fragments'), 'after');
  AddMessage('UTILITY FIX DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
