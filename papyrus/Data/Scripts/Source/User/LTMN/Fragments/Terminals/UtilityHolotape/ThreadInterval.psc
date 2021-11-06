;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
Scriptname LTMN:Fragments:Terminals:UtilityHolotape:ThreadInterval Extends Terminal Hidden Const

;BEGIN FRAGMENT Fragment_Terminal_01
Function Fragment_Terminal_01(ObjectReference akTerminalRef)
;BEGIN CODE
float maxValue = 10.0
float value = 1.0
GlobalVariable ThreadInterval = Lootman.GetProperties().ThreadInterval
ThreadInterval.SetValue(ThreadInterval.GetValue() + value)
If (ThreadInterval.GetValue() > maxValue)
    ThreadInterval.SetValue(maxValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("ThreadInterval", ThreadInterval)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_02
Function Fragment_Terminal_02(ObjectReference akTerminalRef)
;BEGIN CODE
float maxValue = 10.0
float value = 0.5
GlobalVariable ThreadInterval = Lootman.GetProperties().ThreadInterval
ThreadInterval.SetValue(ThreadInterval.GetValue() + value)
If (ThreadInterval.GetValue() > maxValue)
    ThreadInterval.SetValue(maxValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("ThreadInterval", ThreadInterval)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_03
Function Fragment_Terminal_03(ObjectReference akTerminalRef)
;BEGIN CODE
float maxValue = 10.0
float value = 0.1
GlobalVariable ThreadInterval = Lootman.GetProperties().ThreadInterval
ThreadInterval.SetValue(ThreadInterval.GetValue() + value)
If (ThreadInterval.GetValue() > maxValue)
    ThreadInterval.SetValue(maxValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("ThreadInterval", ThreadInterval)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_04
Function Fragment_Terminal_04(ObjectReference akTerminalRef)
;BEGIN CODE
float minValue = 0.1
float value = 0.1
GlobalVariable ThreadInterval = Lootman.GetProperties().ThreadInterval
ThreadInterval.SetValue(ThreadInterval.GetValue() - value)
If (ThreadInterval.GetValue() < minValue)
    ThreadInterval.SetValue(minValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("ThreadInterval", ThreadInterval)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_05
Function Fragment_Terminal_05(ObjectReference akTerminalRef)
;BEGIN CODE
float minValue = 0.1
float value = 0.5
GlobalVariable ThreadInterval = Lootman.GetProperties().ThreadInterval
ThreadInterval.SetValue(ThreadInterval.GetValue() - value)
If (ThreadInterval.GetValue() < minValue)
    ThreadInterval.SetValue(minValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("ThreadInterval", ThreadInterval)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_06
Function Fragment_Terminal_06(ObjectReference akTerminalRef)
;BEGIN CODE
float minValue = 0.1
float value = 1.0
GlobalVariable ThreadInterval = Lootman.GetProperties().ThreadInterval
ThreadInterval.SetValue(ThreadInterval.GetValue() - value)
If (ThreadInterval.GetValue() < minValue)
    ThreadInterval.SetValue(minValue)
EndIf
If (akTerminalRef)
    akTerminalRef.AddTextReplacementData("ThreadInterval", ThreadInterval)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
