Scriptname LTMN:MagicEffect:OpenInventory extends ActiveMagicEffect

Message property ErrorMessage auto const

Event OnEffectStart(Actor akTarget, Actor akCaster)
    If (Lootman.GetProperties().IsPipboyOpen.GetValueInt() == 1)
        ErrorMessage.Show()
    Else
        Lootman.GetFunctions().OpenInventory()
    EndIf
EndEvent
