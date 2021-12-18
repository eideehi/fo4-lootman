Scriptname LTMN:MagicEffect:ToggleLooting extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    LTMN:Lootman.GetFunctions().ToggleLooting()
EndEvent
