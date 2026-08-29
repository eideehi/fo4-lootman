{ Read-only audit of the twelve LootMan worker-manager quest VMAD entries.
  This script intentionally makes no edits. It only accepts the workspace
  stage-en/stage-ja layout with Fallout4.esm and LootMan.esp loaded. Launch
  xEdit with -D: pointing directly at one of those stage directories. }
unit DumpWorkerManagerVMAD;

var
  report: TStringList;

procedure DumpElementTree(e: IInterface; const indent: string);
var
  i: Integer;
begin
  if not Assigned(e) then Exit;
  report.Add(indent + Name(e) + '|EditValue=' + GetEditValue(e));
  for i := 0 to Pred(ElementCount(e)) do
    DumpElementTree(ElementByIndex(e, i), indent + '  ');
end;

function StageName: string;
var
  p: string;
begin
  p := ExcludeTrailingBackslash(DataPath);
  Result := ExtractFileName(p);
end;

function IsSupportedStage: Boolean;
begin
  Result := SameText(StageName, 'stage-en') or SameText(StageName, 'stage-ja');
end;

function FindFileByName(const fileName: string): IInterface;
var
  i: Integer;
begin
  Result := nil;
  for i := 0 to Pred(FileCount) do
    if SameText(GetFileName(FileByIndex(i)), fileName) then begin
      Result := FileByIndex(i);
      Exit;
    end;
end;

function HasExpectedFiles: Boolean;
var
  i: Integer;
  fileName: string;
  hasFallout4, hasLootMan: Boolean;
begin
  hasFallout4 := False;
  hasLootMan := False;
  Result := False;
  report.Add('FileCount=' + IntToStr(FileCount));
  if (FileCount <> 2) and (FileCount <> 3) then Exit;
  for i := 0 to Pred(FileCount) do begin
    fileName := GetFileName(FileByIndex(i));
    report.Add('LoadedFile=' + fileName);
    if SameText(fileName, 'Fallout4.esm') then hasFallout4 := True
    else if SameText(fileName, 'LootMan.esp') then hasLootMan := True
    else if SameText(fileName, 'Fallout4.exe') then begin
      { xEdit 4.1 exposes the current runtime as a generated read-only module. }
    end
    else Exit;
  end;
  Result := hasFallout4 and hasLootMan;
end;

procedure DumpQuest(lootmanFile: IInterface; localFormID: Cardinal);
var
  quest, vmad, scripts, scriptEntry, properties, propertyEntry: IInterface;
  i, j: Integer;
begin
  quest := RecordByFormID(lootmanFile, localFormID, False);
  if not Assigned(quest) then begin
    report.Add('QuestMissing=' + IntToHex(localFormID, 8));
    Exit;
  end;
  report.Add('Quest=' + IntToHex(FixedFormID(quest), 8) + '|EDID=' + EditorID(quest));
  vmad := ElementBySignature(quest, 'VMAD');
  if not Assigned(vmad) then begin
    report.Add('  VMAD=<absent>');
    Exit;
  end;
  report.Add('  VMADTree:');
  DumpElementTree(vmad, '    ');
  scripts := ElementByPath(vmad, 'Scripts');
  report.Add('  ScriptCount=' + IntToStr(ElementCount(scripts)));
  for i := 0 to Pred(ElementCount(scripts)) do begin
    scriptEntry := ElementByIndex(scripts, i);
    report.Add('  Script=' + GetElementEditValues(scriptEntry, 'scriptName') +
      '|Flags=' + GetElementEditValues(scriptEntry, 'Flags'));
    properties := ElementByPath(scriptEntry, 'Properties');
    report.Add('    PropertyCount=' + IntToStr(ElementCount(properties)));
    for j := 0 to Pred(ElementCount(properties)) do begin
      propertyEntry := ElementByIndex(properties, j);
      report.Add('    Property=' + GetElementEditValues(propertyEntry, 'propertyName') +
        '|Type=' + GetElementEditValues(propertyEntry, 'Type') +
        '|Value=' + GetElementEditValues(propertyEntry, 'Value'));
    end;
  end;
end;

function Initialize: Integer;
var
  lootmanFile: IInterface;
  localFormID: Cardinal;
  reportPath: string;
begin
  Result := 0;
  report := TStringList.Create;
  reportPath := ScriptsPath + 'worker-manager-vmad-' + StageName + '-' +
    FormatDateTime('yyyymmdd-hhnnss', Now) + '.txt';
  report.Add('DataPath=' + DataPath);
  report.Add('Stage=' + StageName);
  if not IsSupportedStage then begin
    report.Add('ABORT=unsupported-stage');
    AddMessage('Worker-manager VMAD dump aborted: use -D: with stage-en or stage-ja.');
    report.SaveToFile(reportPath);
    Exit;
  end;
  if not HasExpectedFiles then begin
    report.Add('ABORT=unexpected-loaded-files');
    AddMessage('Worker-manager VMAD dump aborted: unexpected loaded files.');
    report.SaveToFile(reportPath);
    Exit;
  end;
  lootmanFile := FindFileByName('LootMan.esp');
  if not Assigned(lootmanFile) then begin
    report.Add('ABORT=lootman-not-loaded');
    AddMessage('Worker-manager VMAD dump aborted: LootMan.esp is not loaded.');
    report.SaveToFile(reportPath);
    Exit;
  end;
  for localFormID := $01000F9C to $01000FA7 do
    DumpQuest(lootmanFile, localFormID);
  report.SaveToFile(reportPath);
  AddMessage('Wrote ' + reportPath);
end;

function Finalize: Integer;
begin
  Result := 0;
  report.Free;
end;

end.
