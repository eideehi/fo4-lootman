;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
Scriptname LTMN:Fragments:Terminals:UtilityHolotape:CarryWeight Extends Terminal Hidden Const

;BEGIN FRAGMENT Fragment_Terminal_01
Function Fragment_Terminal_01(ObjectReference akTerminalRef)
;BEGIN CODE
int maxValue = 1000000
int value = 100000
GlobalVariable CarryWeight = Lootman.GetProperties().CarryWeight
CarryWeight.SetValueInt(CarryWeight.GetValueInt() + value)
If (CarryWeight.GetValueInt() > maxValue)
    CarryWeight.SetValueInt(maxValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("CarryWeight", CarryWeight)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_02
Function Fragment_Terminal_02(ObjectReference akTerminalRef)
;BEGIN CODE
int maxValue = 1000000
int value = 10000
GlobalVariable CarryWeight = Lootman.GetProperties().CarryWeight
CarryWeight.SetValueInt(CarryWeight.GetValueInt() + value)
If (CarryWeight.GetValueInt() > maxValue)
    CarryWeight.SetValueInt(maxValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("CarryWeight", CarryWeight)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_03
Function Fragment_Terminal_03(ObjectReference akTerminalRef)
;BEGIN CODE
int maxValue = 1000000
int value = 1000
GlobalVariable CarryWeight = Lootman.GetProperties().CarryWeight
CarryWeight.SetValueInt(CarryWeight.GetValueInt() + value)
If (CarryWeight.GetValueInt() > maxValue)
    CarryWeight.SetValueInt(maxValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("CarryWeight", CarryWeight)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_04
Function Fragment_Terminal_04(ObjectReference akTerminalRef)
;BEGIN CODE
int maxValue = 1000000
int value = 500
GlobalVariable CarryWeight = Lootman.GetProperties().CarryWeight
CarryWeight.SetValueInt(CarryWeight.GetValueInt() + value)
If (CarryWeight.GetValueInt() > maxValue)
    CarryWeight.SetValueInt(maxValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("CarryWeight", CarryWeight)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_05
Function Fragment_Terminal_05(ObjectReference akTerminalRef)
;BEGIN CODE
int minValue = 500
int value = 500
GlobalVariable CarryWeight = Lootman.GetProperties().CarryWeight
CarryWeight.SetValueInt(CarryWeight.GetValueInt() - value)
If (CarryWeight.GetValueInt() < minValue)
    CarryWeight.SetValueInt(minValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("CarryWeight", CarryWeight)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_06
Function Fragment_Terminal_06(ObjectReference akTerminalRef)
;BEGIN CODE
int minValue = 500
int value = 1000
GlobalVariable CarryWeight = Lootman.GetProperties().CarryWeight
CarryWeight.SetValueInt(CarryWeight.GetValueInt() - value)
If (CarryWeight.GetValueInt() < minValue)
    CarryWeight.SetValueInt(minValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("CarryWeight", CarryWeight)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_07
Function Fragment_Terminal_07(ObjectReference akTerminalRef)
;BEGIN CODE
int minValue = 500
int value = 10000
GlobalVariable CarryWeight = Lootman.GetProperties().CarryWeight
CarryWeight.SetValueInt(CarryWeight.GetValueInt() - value)
If (CarryWeight.GetValueInt() < minValue)
    CarryWeight.SetValueInt(minValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("CarryWeight", CarryWeight)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_08
Function Fragment_Terminal_08(ObjectReference akTerminalRef)
;BEGIN CODE
int minValue = 500
int value = 100000
GlobalVariable CarryWeight = Lootman.GetProperties().CarryWeight
CarryWeight.SetValueInt(CarryWeight.GetValueInt() - value)
If (CarryWeight.GetValueInt() < minValue)
    CarryWeight.SetValueInt(minValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("CarryWeight", CarryWeight)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
