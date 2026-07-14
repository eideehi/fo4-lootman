{ Read-only: dump every ORIGINAL ARMO record across the loaded FO4 masters so we
  can classify vanilla + DLC clothing (armor rating 0, body/under/accessory
  slots, no armor-class keywords) for the LootMan legendary-only clothing
  exception default block. Emits one tab-delimited CLOTHROW line per record to
  the -R: session log. Does NOT modify or save any record. }
unit DumpArmoClothing;

var
  done: boolean;

function KwList(rec: IInterface): string;
var
  kwda, k, kw: IInterface;
  i: integer;
  s, ed: string;
begin
  Result := '';
  kwda := ElementBySignature(rec, 'KWDA');
  if not Assigned(kwda) then Exit;
  s := '';
  for i := 0 to Pred(ElementCount(kwda)) do begin
    k := ElementByIndex(kwda, i);
    kw := LinksTo(k);
    if Assigned(kw) then ed := EditorID(kw) else ed := IntToHex(GetNativeValue(k), 8);
    if s <> '' then s := s + ';';
    s := s + ed;
  end;
  Result := s;
end;

function OneLine(rec: IInterface; modname: string): string;
var
  objid: cardinal;
  slots: cardinal;
  ar: integer;
  play, full, flagsStr: string;
begin
  objid := GetLoadOrderFormID(rec) and $00FFFFFF;
  ar := StrToIntDef(GetElementEditValues(rec, 'FNAM\Armor Rating'), 0);
  slots := GetElementNativeValues(rec, 'BOD2\First Person Flags');
  flagsStr := GetElementEditValues(rec, 'Record Header\Record Flags');
  if Pos('Non-Playable', flagsStr) > 0 then play := 'N' else play := 'Y';
  full := GetElementEditValues(rec, 'FULL');
  Result := 'CLOTHROW' + #9 +
            'mod=' + modname + #9 +
            'id=' + IntToHex(objid, 6) + #9 +
            'ar=' + IntToStr(ar) + #9 +
            'slots=' + IntToHex(slots, 8) + #9 +
            'play=' + play + #9 +
            'kw=' + KwList(rec) + #9 +
            'edid=' + EditorID(rec) + #9 +
            'full=' + full;
end;

function Process(e: IInterface): integer;
var
  i, j, cnt, emitted: integer;
  f, grp, rec, mrec: IInterface;
  fn: string;
begin
  Result := 0;
  if done then Exit;
  done := True;
  emitted := 0;
  for i := 0 to Pred(FileCount) do begin
    f := FileByIndex(i);
    fn := GetFileName(f);
    grp := GroupBySignature(f, 'ARMO');
    if not Assigned(grp) then Continue;
    cnt := ElementCount(grp);
    AddMessage('FILEINFO' + #9 + 'file=' + fn + #9 + 'armo=' + IntToStr(cnt));
    for j := 0 to Pred(cnt) do begin
      rec := ElementByIndex(grp, j);
      mrec := MasterOrSelf(rec);
      { originals only: emit each armor once, from the plugin that authored it }
      if GetFileName(GetFile(mrec)) <> fn then Continue;
      AddMessage(OneLine(rec, fn));
      Inc(emitted);
    end;
  end;
  AddMessage('EMITTED' + #9 + IntToStr(emitted));
  AddMessage('DUMP DONE');
end;

function Finalize: integer;
begin
  Result := 0;
end;

end.
