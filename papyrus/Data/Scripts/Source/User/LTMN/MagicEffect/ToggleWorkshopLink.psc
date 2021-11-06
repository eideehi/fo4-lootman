Scriptname LTMN:MagicEffect:ToggleWorkshopLink extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Lootman.GetFunctions().ToggleWorkshopLink()
EndEvent
