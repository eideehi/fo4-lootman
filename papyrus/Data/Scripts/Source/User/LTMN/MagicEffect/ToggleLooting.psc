Scriptname LTMN:MagicEffect:ToggleLooting extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Lootman.GetFunctions().ToggleLooting()
EndEvent
