;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
Scriptname LTMN:Fragments:Terminals:UtilityHolotape:LootingRange Extends Terminal Hidden Const

;BEGIN FRAGMENT Fragment_Terminal_01
Function Fragment_Terminal_01(ObjectReference akTerminalRef)
;BEGIN CODE
int maxValue = 32000
int value = 500
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
LootingRange.SetValueInt(LootingRange.GetValueInt() + value)
If (LootingRange.GetValueInt() > maxValue)
  LootingRange.SetValueInt(maxValue)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_02
Function Fragment_Terminal_02(ObjectReference akTerminalRef)
;BEGIN CODE
int maxValue = 32000
int value = 100
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
LootingRange.SetValueInt(LootingRange.GetValueInt() + value)
If (LootingRange.GetValueInt() > maxValue)
  LootingRange.SetValueInt(maxValue)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_03
Function Fragment_Terminal_03(ObjectReference akTerminalRef)
;BEGIN CODE
int maxValue = 32000
int value = 50
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
LootingRange.SetValueInt(LootingRange.GetValueInt() + value)
If (LootingRange.GetValueInt() > maxValue)
  LootingRange.SetValueInt(maxValue)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_04
Function Fragment_Terminal_04(ObjectReference akTerminalRef)
;BEGIN CODE
int minValue = 100
int value = 50
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
LootingRange.SetValueInt(LootingRange.GetValueInt() - value)
If (LootingRange.GetValueInt() < minValue)
  LootingRange.SetValueInt(minValue)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_05
Function Fragment_Terminal_05(ObjectReference akTerminalRef)
;BEGIN CODE
int minValue = 100
int value = 100
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
LootingRange.SetValueInt(LootingRange.GetValueInt() - value)
If (LootingRange.GetValueInt() < minValue)
  LootingRange.SetValueInt(minValue)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_06
Function Fragment_Terminal_06(ObjectReference akTerminalRef)
;BEGIN CODE
int minValue = 100
int value = 500
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
LootingRange.SetValueInt(LootingRange.GetValueInt() - value)
If (LootingRange.GetValueInt() < minValue)
  LootingRange.SetValueInt(minValue)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_07
Function Fragment_Terminal_07(ObjectReference akTerminalRef)
;BEGIN CODE
int maxValue = 32000
int value = 5000
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
LootingRange.SetValueInt(LootingRange.GetValueInt() + value)
If (LootingRange.GetValueInt() > maxValue)
  LootingRange.SetValueInt(maxValue)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_08
Function Fragment_Terminal_08(ObjectReference akTerminalRef)
;BEGIN CODE
int maxValue = 32000
int value = 1000
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
LootingRange.SetValueInt(LootingRange.GetValueInt() + value)
If (LootingRange.GetValueInt() > maxValue)
  LootingRange.SetValueInt(maxValue)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_09
Function Fragment_Terminal_09(ObjectReference akTerminalRef)
;BEGIN CODE
int minValue = 100
int value = 1000
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
LootingRange.SetValueInt(LootingRange.GetValueInt() - value)
If (LootingRange.GetValueInt() < minValue)
  LootingRange.SetValueInt(minValue)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_10
Function Fragment_Terminal_10(ObjectReference akTerminalRef)
;BEGIN CODE
int minValue = 100
int value = 5000
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
LootingRange.SetValueInt(LootingRange.GetValueInt() - value)
If (LootingRange.GetValueInt() < minValue)
  LootingRange.SetValueInt(minValue)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_12
Function Fragment_Terminal_12(ObjectReference akTerminalRef)
;BEGIN CODE
int value = 100
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
If (LootingRange.GetValueInt() != value)
  LootingRange.SetValueInt(value)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_13
Function Fragment_Terminal_13(ObjectReference akTerminalRef)
;BEGIN CODE
int value = 400
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
If (LootingRange.GetValueInt() != value)
  LootingRange.SetValueInt(value)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_14
Function Fragment_Terminal_14(ObjectReference akTerminalRef)
;BEGIN CODE
int value = 32000
GlobalVariable LootingRange = LTMN:Lootman.GetProperties().LootingRange
If (LootingRange.GetValueInt() != value)
  LootingRange.SetValueInt(value)
EndIf
If (akTerminalRef)
  akTerminalRef.AddTextReplacementData("LootingRange", LootingRange)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
