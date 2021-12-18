;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
Scriptname LTMN:Fragments:Terminals:UtilityHolotape:AdvancedFilterExtra Extends Terminal Hidden Const

;BEGIN FRAGMENT Fragment_Terminal_01
Function Fragment_Terminal_01(ObjectReference akTerminalRef)
;BEGIN CODE
LTMN:Lootman.GetProperties().AdvancedFilterLegendaryOnly.SetValueInt(0)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_Terminal_02
Function Fragment_Terminal_02(ObjectReference akTerminalRef)
;BEGIN CODE
LTMN:Lootman.GetProperties().AdvancedFilterLegendaryOnly.SetValueInt(1)
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
