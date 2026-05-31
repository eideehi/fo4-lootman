{ Round-trip encoding test: can an xEdit -script write Japanese into a non-localized
  FO4 plugin string field so the SAVED bytes are correct UTF-8?
  - Reads an existing Japanese FULL (read path; note: also bounded by the -R: log
    encoding, so the saved-bytes check below is the authoritative result).
  - Writes a distinctive test string with an ASCII "XYZ" anchor to an existing
    record's FULL, then re-reads it in memory.
  After the run, grep the saved stage-ja/LootMan.esp for the exact UTF-8 bytes. }
unit TestJaEncoding;

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

function Process(e: IInterface): integer;
var
  lm, rec: IInterface;
begin
  Result := 0;
  if done then Exit;
  done := True;
  lm := FindFileByName('LootMan.esp');
  if not Assigned(lm) then begin AddMessage('LootMan.esp not loaded'); Exit; end;

  rec := FindByEDID(lm, 'DOOR', 'LTMN_WorkshopLootManContainerDirty');
  if not Assigned(rec) then begin AddMessage('!! test DOOR LTMN_WorkshopLootManContainerDirty not found'); Exit; end;

  AddMessage('READ existing FULL=[' + GetElementEditValues(rec, 'FULL') + ']');
  SetElementEditValues(rec, 'FULL', 'XYZ翻訳テスト日本語XYZ');
  AddMessage('WROTE readback FULL=[' + GetElementEditValues(rec, 'FULL') + ']');
  AddMessage('TEST DONE -- now check the saved esp bytes for the UTF-8 of the test string');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
