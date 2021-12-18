Scriptname LTMN:MagicEffect:OpenInventory extends ActiveMagicEffect

Message property ErrorMessage auto const

Event OnEffectStart(Actor akTarget, Actor akCaster)
    If (LTMN:Lootman.GetProperties().IsPipboyOpen.GetValueInt() == 1)
        ErrorMessage.Show()
    Else
        LTMN:Lootman.GetFunctions().OpenInventory()
    EndIf
EndEvent
