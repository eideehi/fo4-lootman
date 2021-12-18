Scriptname LTMN:Thread:Looting extends Quest hidden

CustomEvent CallLooting

Actor property player auto hidden
LTMN:Quest:Properties property properties auto hidden

bool threadRunning = false

string processId = "0000000000";; Debug

Event LTMN:Thread:Looting.CallLooting(LTMN:Thread:Looting sender, Var[] args)
    If (sender == self && IsLootingEnabled())
        threadRunning = true
        Looting()
        threadRunning = false
    EndIf
EndEvent

Function Initialize()
    player = Game.GetPlayer()
    properties = LTMN:Lootman.GetProperties()
    RegisterForCustomEvent(self, "CallLooting")
EndFunction

Function Finalize()
    player = None
    properties = None
    UnregisterForCustomEvent(self, "CallLooting")
EndFunction

Function Run()
    SendCustomEvent("CallLooting")
EndFunction

bool Function Busy()
    Return threadRunning
EndFunction

Function Looting()
    processId = LTMN:Debug.GetRandomProcessID();; Debug
    string prefix = ("| Looting @ " + GetThreadID() + " | " + processId + " | ");; Debug
    LTMN:Debug.Log(prefix + "*** Start looting ***");; Debug
    int objectIndex = 1;; Debug
    ObjectReference[] refs = LTMN:Lootman.FindAllLootingTarget(player, properties.LootingRange.GetValueInt(), GetFindObjectFormType())
    LTMN:Debug.Log(prefix + "  Total number of objects found: " + refs.Length);; Debug
    float time = Utility.GetCurrentRealTime()
    int i = refs.Length
    While i
        i -= 1

        ; Since it is not desirable to keep processing an old list of objects forever, we force the thread to be released when the processing time exceeds 2 seconds.
        If ((Utility.GetCurrentRealTime() - time) > properties.ThreadAllowedWorkingTime.GetValue())
            LTMN:Debug.Log(prefix + "*** Force the thread to be released because the processing time of the thread has exceeded the threshold ***");; Debug
            Return
        EndIf

        ; If the looting disabled in the middle of the loop, release the thread immediately.
        If (!IsLootingEnabled())
            LTMN:Debug.Log(prefix + "*** Force the thread to be released because the looting disabled ***");; Debug
            Return
        EndIf

        ObjectReference ref = refs[i]
        If (LTMN:Lootman.IsValidRef(ref))
            If (!ref.HasKeyword(properties.LootingMarker))
                ref.AddKeyword(properties.LootingMarker)

                ; Verify whether the processed object should be skipped for a certain period of time, and if necessary, set the timestamp to be used for the skipping process.
                If (IsToBeSkipped(ref))
                    ref.SetValue(properties.LastLootingTimestamp, Utility.GetCurrentRealTime())
                EndIf

                LTMN:Debug.Log(prefix + "  [Object_" + objectIndex + "]");; Debug
                TraceObject(ref);; Debug
                If (IsLootingTarget(ref))
                    LootObject(ref)
                Else;; Debug
                    LTMN:Debug.Log(prefix + "    ** Is not a target of looting **");; Debug
                EndIf

                ref.ResetKeyword(properties.LootingMarker)
            Else;; Debug
                LTMN:Debug.Log(prefix + "  ** Object_" + objectIndex + " was skipped because it is being processed by another thread **");; Debug
            EndIf
        Else;; Debug
            LTMN:Debug.Log(prefix + "  ** Object_" + objectIndex + " is not a valid object **");; Debug
        EndIf

        objectIndex += 1;; Debug
    EndWhile
    LTMN:Debug.Log(prefix + "*** Looting complete ***");; Debug
EndFunction

int Function GetFindObjectFormType()
    Return 0
EndFunction

bool Function IsLootingTarget(ObjectReference ref)
EndFunction

Function LootObject(ObjectReference ref)
EndFunction

bool Function IsToBeSkipped(ObjectReference ref)
    Return true
EndFunction

Actor Function GetLootingActor()
    If (properties.LootInPlayerDirectly.GetValueInt() == 1)
        Return player
    EndIf
    Return properties.LootmanActor
EndFunction

; Verify that automatic looting is enabled
bool Function IsLootingEnabled()
    If (properties.IsInitializing.GetValueInt() == 1 || properties.IsUninstalled.GetValueInt() == 1)
        Return false
    EndIf
    If (properties.IsEnabled.GetValueInt() != 1)
        Return false
    EndIf
    If (properties.IgnoreOverweight.GetValueInt() != 1 && properties.IsOverweight.GetValueInt() == 1)
        Return false
    EndIf
    If (properties.LootingDisabledInSettlement.GetValueInt() == 1 && properties.IsInSettlement.GetValueInt() == 1)
        Return false
    EndIf
    If (!LTMN:Lootman.GetSystem().IsWorldActive())
        Return false
    EndIf
    Return true
EndFunction

; Verify that it is a valid object reference
bool Function IsValidObject(ObjectReference ref)
    If (ref.HasKeywordInFormList(properties.ExcludeKeywordList))
        Return false
    EndIf

    If (ref.IsQuestItem() || ref.HasKeyword(properties.BlockWorkshopInteraction) || !ref.GetDisplayName())
        Return false
    EndIf

    int i = properties.ExcludeLocationRefList.GetSize()
    While i
        i -= 1
        Form item = properties.ExcludeLocationRefList.GetAt(i)
        LocationRefType locRefType = (item As LocationRefType)
        If (locRefType && ref.HasLocRefType(locRefType))
            Return false
        EndIf
    EndWhile

    Form base = ref.GetBaseObject()
    If (properties.ExcludeFormList.HasForm(base) || base.HasKeywordInFormList(properties.ExcludeKeywordList))
        Return false
    EndIf

    Location loc = ref.GetCurrentLocation()
    If (properties.LootingDisabledInSettlement.GetValueInt() == 1)
        If (loc.HasKeyword(Game.GetCommonProperties().LocTypeSettlement) || loc.HasKeyword(Game.GetCommonProperties().LocTypeWorkshopSettlement))
            Return false
        EndIf
    EndIf

    Return true
EndFunction

; Verify that the ownership of the object is safe to loot
bool Function IsLootableOwnership(ObjectReference ref)
    If (ref.IsOwnedBy(player))
        Return true
    EndIf

    Cell parentCell = ref.GetParentCell()
    If (parentCell)
        ActorBase cellOwner = parentCell.GetActorOwner()
        If (cellOwner && cellOwner.IsUnique())
            Actor cellOwnerActor = cellOwner.GetUniqueActor()
            If (cellOwnerActor && cellOwnerActor.GetRelationshipRank(player) < 1)
                Return false
            EndIf
        EndIf

        Faction cellFactionOwner = parentCell.GetFactionOwner()
        If (cellFactionOwner && cellFactionOwner.GetFactionReaction(player) < 2)
            Return false
        EndIf
    EndIf

    Actor owner = ref.GetActorRefOwner()
    If (!owner)
        ActorBase ownerBase = ref.GetActorOwner()
        If (ownerBase && ownerBase.IsUnique())
            owner = ownerBase.GetUniqueActor()
        EndIf
    EndIf

    If (owner && owner.GetRelationshipRank(player) < 1)
        Return false
    EndIf

    Faction factionOwner = ref.GetFactionOwner()
    If (factionOwner && factionOwner.GetFactionReaction(player) < 3)
        Return false
    EndIf

    Return true
EndFunction

; Verify that the item is of lootable rarity
bool Function IsLootableRarity(Form item)
    If (item.HasKeyword(properties.FeaturedItem) && !properties.AllowedFeaturedItemList.HasForm(item))
        Return false
    EndIf

    If (properties.UniqueItemList.HasForm(item) && !properties.AllowedUniqueItemList.HasForm(item))
        Return false
    EndIf

    Return true
EndFunction

; Verify that the item is a lootable ALCH category item
bool Function IsLootableAlchemyItem(Form item)
    If (properties.AdvancedFilterOtherAlchemy.GetValueInt() == 1)
        If (properties.AdvancedFilterAlcohol.GetValueInt() != 1 && item.HasKeyword(properties.ObjectTypeAlcohol))
            Return false
        EndIf

        If (properties.AdvancedFilterChem.GetValueInt() != 1 && item.HasKeyword(properties.ObjectTypeChem))
            Return false
        EndIf

        If (properties.AdvancedFilterFood.GetValueInt() != 1 && item.HasKeyword(properties.ObjectTypeFood))
            Return false
        EndIf

        If (properties.AdvancedFilterNukaCola.GetValueInt() != 1 && item.HasKeyword(properties.ObjectTypeNukaCola))
            Return false
        EndIf

        If (properties.AdvancedFilterStimpak.GetValueInt() != 1 && item.HasKeyword(properties.ObjectTypeStimpak))
            Return false
        EndIf

        If (properties.AdvancedFilterSyringerAmmo.GetValueInt() != 1 && item.HasKeyword(properties.ObjectTypeSyringerAmmo))
            Return false
        EndIf

        If (properties.AdvancedFilterWater.GetValueInt() != 1 && item.HasKeyword(properties.ObjectTypeWater))
            Return false
        EndIf

        Return true
    Else
        If (item.HasKeyword(properties.ObjectTypeAlcohol) && properties.AdvancedFilterAlcohol.GetValueInt() == 1)
            Return true
        EndIf

        If (item.HasKeyword(properties.ObjectTypeChem) && properties.AdvancedFilterChem.GetValueInt() == 1)
            Return true
        EndIf

        If (item.HasKeyword(properties.ObjectTypeFood) && properties.AdvancedFilterFood.GetValueInt() == 1)
            Return true
        EndIf

        If (item.HasKeyword(properties.ObjectTypeNukaCola) && properties.AdvancedFilterNukaCola.GetValueInt() == 1)
            Return true
        EndIf

        If (item.HasKeyword(properties.ObjectTypeStimpak) && properties.AdvancedFilterStimpak.GetValueInt() == 1)
            Return true
        EndIf

        If (item.HasKeyword(properties.ObjectTypeSyringerAmmo) && properties.AdvancedFilterSyringerAmmo.GetValueInt() == 1)
            Return true
        EndIf

        If (item.HasKeyword(properties.ObjectTypeWater) && properties.AdvancedFilterWater.GetValueInt() == 1)
            Return true
        EndIf

        Return false
    EndIf
EndFunction

; Verify that the item is a lootable WEAP category item
bool Function IsLootableWeaponItem(Form item)
    If (properties.AdvancedFilterOtherWeapon.GetValueInt() == 1)
        If (properties.AdvancedFilterGrenade.GetValueInt() != 1 && item.HasKeywordInFormList(properties.WeaponTypeGrenadeKeywordList))
            Return false
        EndIf

        If (properties.AdvancedFilterMine.GetValueInt() != 1 && item.HasKeywordInFormList(properties.WeaponTypeMineKeywordList))
            Return false
        EndIf

        Return true
    Else
        If (item.HasKeywordInFormList(properties.WeaponTypeGrenadeKeywordList) && properties.AdvancedFilterGrenade.GetValueInt() == 1)
            Return true
        EndIf

        If (item.HasKeywordInFormList(properties.WeaponTypeMineKeywordList) && properties.AdvancedFilterMine.GetValueInt() == 1)
            Return true
        EndIf

        Return false
    EndIf
EndFunction

; Verify that items in inventory are lootable
bool Function IsLootableItem(ObjectReference ref, Form item)
    int formType = LTMN:Lootman.GetFormType(item)

    If (formType == properties.FORM_TYPE_ALCH)
        If (!IsLootableAlchemyItem(item))
            Return false
        EndIf
    ElseIf (formType == properties.FORM_TYPE_ARMO)
        If (properties.AdvancedFilterLegendaryOnly.GetValueInt() == 1 && !LTMN:Lootman.HasLegendaryItem(ref, item))
            Return false
        EndIf
    ElseIf (formType == properties.FORM_TYPE_BOOK)
        If (item.HasKeyword(properties.PerkMagazine))
            Return properties.AdvancedFilterPerkMagazine.GetValueInt() == 1
        EndIf

        If (properties.AdvancedFilterOtherBook.GetValueInt() != 1)
            Return false
        EndIf
    ElseIf (formType == properties.FORM_TYPE_WEAP)
        If (!IsLootableWeaponItem(item))
            Return false
        EndIf
        If (properties.AdvancedFilterLegendaryOnly.GetValueInt() == 1)
            If (!item.HasKeywordInFormList(properties.WeaponTypeGrenadeKeywordList) && !item.HasKeywordInFormList(properties.WeaponTypeMineKeywordList))
                If (!LTMN:Lootman.HasLegendaryItem(ref, item))
                    Return false
                EndIf
            EndIf
        EndIf
    EndIf

    Return IsLootableRarity(item)
EndFunction

; Return an array for inventory item filtering
int[] Function GetItemFilters()
    int[] filters = new int[8]

    If (properties.CategoryFilterALCH.GetValueInt() == 1)
        filters[0] = properties.FORM_TYPE_ALCH
    EndIf
    If (properties.CategoryFilterAMMO.GetValueInt() == 1)
        filters[1] = properties.FORM_TYPE_AMMO
    EndIf
    If (properties.CategoryFilterARMO.GetValueInt() == 1)
        filters[2] = properties.FORM_TYPE_ARMO
    EndIf
    If (properties.CategoryFilterBOOK.GetValueInt() == 1)
        filters[3] = properties.FORM_TYPE_BOOK
    EndIf
    If (properties.CategoryFilterINGR.GetValueInt() == 1)
        filters[4] = properties.FORM_TYPE_INGR
    EndIf
    If (properties.CategoryFilterKEYM.GetValueInt() == 1)
        filters[5] = properties.FORM_TYPE_KEYM
    EndIf
    If (properties.CategoryFilterMISC.GetValueInt() == 1)
        filters[6] = properties.FORM_TYPE_MISC
    EndIf
    If (properties.CategoryFilterWEAP.GetValueInt() == 1)
        filters[7] = properties.FORM_TYPE_WEAP
    EndIf

    Return filters
EndFunction

; Return the process ID to be output to the log (Debug only)
string Function GetProcessID();; Debug
    Return processId;; Debug
EndFunction;; Debug

; Return the thread identifier to be output to the log. Override it in the worker's script (Debug only)
string Function GetThreadID();; Debug
    Return "UNKNOWN";; Debug
EndFunction;; Debug

; Output the trace log of an object (Debug only)
Function TraceObject(ObjectReference ref);; Debug
EndFunction;; Debug
