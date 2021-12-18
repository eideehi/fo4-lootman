Scriptname LTMN:MagicEffect:ToggleWorkshopLink extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    LTMN:Lootman.GetFunctions().ToggleWorkshopLink()
EndEvent
