Scriptname LTMN2:Attachment:WorkshopLootManTrunk extends ObjectReference

Event OnOpen(ObjectReference akActionRef)
    Actor akActorRef = akActionRef as Actor
    If (akActorRef == Game.GetPlayer())
        RegisterForMenuOpenCloseEvent("ContainerMenu")
        LTMN2:Properties.GetInstance().LootManWorkshopRef.Activate(Game.GetPlayer(), true)
    EndIf
EndEvent

Event OnMenuOpenCloseEvent(string asMenuName, bool abOpening)
    If (asMenuName == "ContainerMenu")
        If (!abOpening)
            UnregisterForMenuOpenCloseEvent("ContainerMenu")
            self.Activate(Game.GetPlayer())
        EndIf
    EndIf
EndEvent
