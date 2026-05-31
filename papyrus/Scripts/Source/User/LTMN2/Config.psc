Scriptname LTMN2:Config native hidden
{Stateless configuration facade shared by the holotape terminal fragments.}

; Each mutator mirrors exactly what the MCM front-end does for a setting: it
; writes the backing LTMN2:Properties value (or LTMN2:MCM.LogLevel) and then runs
; LTMN2:MCM.ApplySettingSideEffects, which applies the per-id side effects and the
; native OnUpdateLootManProperty refresh. The terminal and the MCM therefore drive
; one shared state and cannot drift.
;
; The id strings are the MCM setting ids, which equal the LTMN2:Properties
; property names (see the MCM config bindings). Unknown ids are harmless: the
; dispatch no-ops and ApplySettingSideEffects ignores names native does not know.

; ---------------------------------------------------------------------------
; Boolean settings
; ---------------------------------------------------------------------------

; Absolute set for a plain or object-filter bool. Do NOT use for packed-bitmask
; ids (use ToggleBit) -- ApplySettingSideEffects XORs the packed int
; unconditionally, so an absolute set there would flip an already-correct bit.
Function SetBool(string id, bool value) global
    LTMN2:Properties properties = LTMN2:Properties.GetInstance()
    If (!WriteSettableBool(properties, id, value))
        ; Packed-bitmask ids are toggle-only: ApplySettingSideEffects XORs the
        ; packed int unconditionally, so an absolute set would desync. Reject here
        ; so the contract is enforced, not just documented; callers use ToggleBit.
        LTMN2:LootMan.LogEvent("config", "set_bool_rejected", "id=" + id + " reason=toggle_only", 3)
        Return
    EndIf
    LTMN2:MCM.GetInstance().ApplySettingSideEffects(id)
EndFunction

; Toggle a plain or object-filter bool.
Function Toggle(string id) global
    FlipBool(id)
EndFunction

; Toggle a packed-bitmask id. Flips the backing bool the MCM switcher binds to;
; ApplySettingSideEffects then XORs the matching packed int, exactly as MCM does.
; This is the only safe entry point for those ids.
Function ToggleBit(string id) global
    FlipBool(id)
EndFunction

; ---------------------------------------------------------------------------
; Numeric settings
; ---------------------------------------------------------------------------

; Add delta to a float setting, clamp to [minValue, maxValue], apply, and return
; the stored value so the caller can show it on the HUD.
float Function AdjustFloat(string id, float delta, float minValue, float maxValue) global
    LTMN2:Properties properties = LTMN2:Properties.GetInstance()
    float value = ClampFloat(ReadFloat(properties, id) + delta, minValue, maxValue)
    WriteFloat(properties, id, value)
    LTMN2:MCM.GetInstance().ApplySettingSideEffects(id)
    LTMN2:LootMan.ShowConfigFloat(GetLabelKey(id), value)
    Return value
EndFunction

; Add delta to an int setting, clamp to [minValue, maxValue], apply, and return
; the stored value so the caller can show it on the HUD.
int Function AdjustInt(string id, int delta, int minValue, int maxValue) global
    LTMN2:Properties properties = LTMN2:Properties.GetInstance()
    int value = ClampInt(ReadInt(properties, id) + delta, minValue, maxValue)
    WriteInt(properties, id, value)
    LTMN2:MCM.GetInstance().ApplySettingSideEffects(id)
    LTMN2:LootMan.ShowConfigInt(GetLabelKey(id), value)
    Return value
EndFunction

; Set a float setting to an exact preset value and apply.
Function SetFloat(string id, float value) global
    LTMN2:Properties properties = LTMN2:Properties.GetInstance()
    WriteFloat(properties, id, value)
    LTMN2:MCM.GetInstance().ApplySettingSideEffects(id)
EndFunction

; Set an int setting to an exact preset value and apply.
Function SetInt(string id, int value) global
    LTMN2:Properties properties = LTMN2:Properties.GetInstance()
    WriteInt(properties, id, value)
    LTMN2:MCM.GetInstance().ApplySettingSideEffects(id)
EndFunction

; ---------------------------------------------------------------------------
; Log level (native-only state mirrored on LTMN2:MCM)
; ---------------------------------------------------------------------------

; Set the log level. Mirrors the value on LTMN2:MCM (F9B) and lets
; ApplySettingSideEffects push it to native via SetLogLevel and re-sync.
Function SetLogLevel(int value) global
    ; Valid native levels are trace(0)..off(6). Reject out-of-range input so the
    ; mirrored value never diverges from native -- a divergence would make the
    ; LogLevel resync call MCM.RefreshMenu, reaching into the MCM framework this
    ; fallback exists to work around.
    If (value < 0 || value > 6)
        LTMN2:LootMan.LogEvent("config", "set_log_level_rejected", "value=" + value + " reason=out_of_range", 3)
        Return
    EndIf
    LTMN2:MCM mcmQuest = LTMN2:MCM.GetInstance()
    mcmQuest.LogLevel = value
    mcmQuest.ApplySettingSideEffects("LogLevel")
    LTMN2:LootMan.ShowConfigText("$PAGE_GENERAL_SETTINGS_LOG_LEVEL", LogLevelValueKey(value))
EndFunction

; ---------------------------------------------------------------------------
; Internal dispatch
; ---------------------------------------------------------------------------

Function FlipBool(string id) global
    LTMN2:Properties properties = LTMN2:Properties.GetInstance()
    bool next = !ReadBool(properties, id)
    If (!WriteSettableBool(properties, id, next))
        WritePackedBool(properties, id, next)
    EndIf
    LTMN2:MCM.GetInstance().ApplySettingSideEffects(id)
    LTMN2:LootMan.ShowConfigBool(GetLabelKey(id), next)
EndFunction

float Function ClampFloat(float value, float minValue, float maxValue) global
    If (value < minValue)
        Return minValue
    ElseIf (value > maxValue)
        Return maxValue
    EndIf
    Return value
EndFunction

int Function ClampInt(int value, int minValue, int maxValue) global
    If (value < minValue)
        Return minValue
    ElseIf (value > maxValue)
        Return maxValue
    EndIf
    Return value
EndFunction

; Map a setting id to its localized label translation key. These reuse the MCM
; $PAGE_* label keys (resolved by native message_queue against LootMan_<lang>.txt),
; so the terminal and MCM show identical localized names with no MCM dependency.
; Unknown ids fall back to the raw id so a mismatch is at least visible.
string Function GetLabelKey(string id) global
    ; General settings
    If (id == "EnableLootMan")
        Return "$PAGE_GENERAL_SETTINGS_ENABLE_LOOTMAN"
    ElseIf (id == "DisplaySystemMessage")
        Return "$PAGE_GENERAL_SETTINGS_DISPLAY_SYSTEM_MESSAGE"
    ElseIf (id == "PlayPickupSound")
        Return "$PAGE_GENERAL_SETTINGS_PLAY_PICKUP_SOUND"
    ElseIf (id == "PlayContainerAnimation")
        Return "$PAGE_GENERAL_SETTINGS_PLAY_CONTAINER_ANIMATION"
    ElseIf (id == "IgnoreOverweight")
        Return "$PAGE_GENERAL_SETTINGS_IGNORE_OVERWEIGHT"
    ElseIf (id == "LootIsDeliverToPlayer")
        Return "$PAGE_GENERAL_SETTINGS_LOOT_IS_DELIVER_TO_PLAYER"
    ElseIf (id == "LootingWithoutLogs")
        ; The property was renamed DeliveredToPlayerWithoutLogs -> LootingWithoutLogs;
        ; use the key the MCM config.json binds to this id so the HUD label matches MCM.
        Return "$PAGE_GENERAL_SETTINGS_LOOTING_WITHOUT_LOGS"
    ElseIf (id == "NotLootingFromSettlement")
        Return "$PAGE_GENERAL_SETTINGS_NOT_LOOTING_FROM_SETTLEMENT"
    ElseIf (id == "AutomaticallyLinkAndUnlinkToWorkshop")
        Return "$PAGE_GENERAL_SETTINGS_AUTOMATICALLY_LINK_AND_UNLINK_TO_WORKSHOP"
    ElseIf (id == "UnlockLockedContainer")
        Return "$PAGE_GENERAL_SETTINGS_UNLOCK_LOCKED_CONTAINER"
    ElseIf (id == "LootingRange")
        Return "$PAGE_GENERAL_SETTINGS_LOOTING_RANGE"
    ElseIf (id == "CarryWeight")
        Return "$PAGE_GENERAL_SETTINGS_CARRY_WEIGHT"

    ; Object-type filters
    ElseIf (id == "EnableObjectLootingOfACTI")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_ACTI"
    ElseIf (id == "EnableObjectLootingOfALCH")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_ALCH"
    ElseIf (id == "EnableObjectLootingOfAMMO")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_AMMO"
    ElseIf (id == "EnableObjectLootingOfARMO")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_ARMO"
    ElseIf (id == "EnableObjectLootingOfBOOK")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_BOOK"
    ElseIf (id == "EnableObjectLootingOfCONT")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_CONT"
    ElseIf (id == "EnableObjectLootingOfFLOR")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_FLOR"
    ElseIf (id == "EnableObjectLootingOfINGR")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_INGR"
    ElseIf (id == "EnableObjectLootingOfKEYM")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_KEYM"
    ElseIf (id == "EnableObjectLootingOfMISC")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_MISC"
    ElseIf (id == "EnableObjectLootingOfNPC_")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_NPC_"
    ElseIf (id == "EnableObjectLootingOfWEAP")
        Return "$PAGE_LOOTING_WORKER_OBJECT_FILTER_WEAP"
    EndIf

    Return id
EndFunction

; Map a native log level (trace 0 .. off 6) to its localized value label key.
string Function LogLevelValueKey(int value) global
    If (value == 0)
        Return "$PAGE_GENERAL_SETTINGS_LOG_LEVEL_TRACE"
    ElseIf (value == 1)
        Return "$PAGE_GENERAL_SETTINGS_LOG_LEVEL_DEBUG"
    ElseIf (value == 2)
        Return "$PAGE_GENERAL_SETTINGS_LOG_LEVEL_INFO"
    ElseIf (value == 3)
        Return "$PAGE_GENERAL_SETTINGS_LOG_LEVEL_WARN"
    ElseIf (value == 4)
        Return "$PAGE_GENERAL_SETTINGS_LOG_LEVEL_ERROR"
    ElseIf (value == 5)
        Return "$PAGE_GENERAL_SETTINGS_LOG_LEVEL_CRITICAL"
    EndIf
    Return "$PAGE_GENERAL_SETTINGS_LOG_LEVEL_OFF"
EndFunction

bool Function ReadBool(LTMN2:Properties properties, string id) global
    ; Plain config bools
    If (id == "EnableLootMan")
        Return properties.EnableLootMan
    ElseIf (id == "DisplaySystemMessage")
        Return properties.DisplaySystemMessage
    ElseIf (id == "PlayPickupSound")
        Return properties.PlayPickupSound
    ElseIf (id == "PlayContainerAnimation")
        Return properties.PlayContainerAnimation
    ElseIf (id == "UseLootingTimeBudget")
        Return properties.UseLootingTimeBudget
    ElseIf (id == "IgnoreOverweight")
        Return properties.IgnoreOverweight
    ElseIf (id == "LootIsDeliverToPlayer")
        Return properties.LootIsDeliverToPlayer
    ElseIf (id == "LootingWithoutLogs")
        Return properties.LootingWithoutLogs
    ElseIf (id == "NotLootingFromSettlement")
        Return properties.NotLootingFromSettlement
    ElseIf (id == "AutomaticallyLinkAndUnlinkToWorkshop")
        Return properties.AutomaticallyLinkAndUnlinkToWorkshop
    ElseIf (id == "UnlockLockedContainer")
        Return properties.UnlockLockedContainer
    ElseIf (id == "LootingLegendaryOnly")
        Return properties.LootingLegendaryOnly
    ElseIf (id == "AlwaysLootingExplosives")
        Return properties.AlwaysLootingExplosives

    ; Object-type filters (12 bools native rebuilds into a mask)
    ElseIf (id == "EnableObjectLootingOfACTI")
        Return properties.EnableObjectLootingOfACTI
    ElseIf (id == "EnableObjectLootingOfALCH")
        Return properties.EnableObjectLootingOfALCH
    ElseIf (id == "EnableObjectLootingOfAMMO")
        Return properties.EnableObjectLootingOfAMMO
    ElseIf (id == "EnableObjectLootingOfARMO")
        Return properties.EnableObjectLootingOfARMO
    ElseIf (id == "EnableObjectLootingOfBOOK")
        Return properties.EnableObjectLootingOfBOOK
    ElseIf (id == "EnableObjectLootingOfCONT")
        Return properties.EnableObjectLootingOfCONT
    ElseIf (id == "EnableObjectLootingOfFLOR")
        Return properties.EnableObjectLootingOfFLOR
    ElseIf (id == "EnableObjectLootingOfINGR")
        Return properties.EnableObjectLootingOfINGR
    ElseIf (id == "EnableObjectLootingOfKEYM")
        Return properties.EnableObjectLootingOfKEYM
    ElseIf (id == "EnableObjectLootingOfMISC")
        Return properties.EnableObjectLootingOfMISC
    ElseIf (id == "EnableObjectLootingOfNPC_")
        Return properties.EnableObjectLootingOfNPC_
    ElseIf (id == "EnableObjectLootingOfWEAP")
        Return properties.EnableObjectLootingOfWEAP

    ; Packed-bitmask backing bools: inventory filter
    ElseIf (id == "EnableInventoryLootingOfALCH")
        Return properties.EnableInventoryLootingOfALCH
    ElseIf (id == "EnableInventoryLootingOfAMMO")
        Return properties.EnableInventoryLootingOfAMMO
    ElseIf (id == "EnableInventoryLootingOfARMO")
        Return properties.EnableInventoryLootingOfARMO
    ElseIf (id == "EnableInventoryLootingOfBOOK")
        Return properties.EnableInventoryLootingOfBOOK
    ElseIf (id == "EnableInventoryLootingOfINGR")
        Return properties.EnableInventoryLootingOfINGR
    ElseIf (id == "EnableInventoryLootingOfKEYM")
        Return properties.EnableInventoryLootingOfKEYM
    ElseIf (id == "EnableInventoryLootingOfMISC")
        Return properties.EnableInventoryLootingOfMISC
    ElseIf (id == "EnableInventoryLootingOfWEAP")
        Return properties.EnableInventoryLootingOfWEAP

    ; Packed-bitmask backing bools: ALCH subtype filter
    ElseIf (id == "EnableALCHItemAlcohol")
        Return properties.EnableALCHItemAlcohol
    ElseIf (id == "EnableALCHItemChemistry")
        Return properties.EnableALCHItemChemistry
    ElseIf (id == "EnableALCHItemFood")
        Return properties.EnableALCHItemFood
    ElseIf (id == "EnableALCHItemNukaCola")
        Return properties.EnableALCHItemNukaCola
    ElseIf (id == "EnableALCHItemStimpak")
        Return properties.EnableALCHItemStimpak
    ElseIf (id == "EnableALCHItemSyringerAmmo")
        Return properties.EnableALCHItemSyringerAmmo
    ElseIf (id == "EnableALCHItemWater")
        Return properties.EnableALCHItemWater
    ElseIf (id == "EnableALCHItemOther")
        Return properties.EnableALCHItemOther

    ; Packed-bitmask backing bools: BOOK subtype filter
    ElseIf (id == "EnableBOOKItemPerkMagazine")
        Return properties.EnableBOOKItemPerkMagazine
    ElseIf (id == "EnableBOOKItemOther")
        Return properties.EnableBOOKItemOther

    ; Packed-bitmask backing bools: MISC subtype filter
    ElseIf (id == "EnableMISCItemBobblehead")
        Return properties.EnableMISCItemBobblehead
    ElseIf (id == "EnableMISCItemOther")
        Return properties.EnableMISCItemOther

    ; Packed-bitmask backing bools: WEAP subtype filter
    ElseIf (id == "EnableWEAPItemGrenade")
        Return properties.EnableWEAPItemGrenade
    ElseIf (id == "EnableWEAPItemMine")
        Return properties.EnableWEAPItemMine
    ElseIf (id == "EnableWEAPItemOther")
        Return properties.EnableWEAPItemOther
    EndIf

    Return false
EndFunction

; Write a plain or object-filter bool (the absolute-settable classes). Returns true
; if id matched one of these, false otherwise (e.g. a packed-bitmask id, which
; SetBool must not write). With WritePackedBool this also serves FlipBool.
bool Function WriteSettableBool(LTMN2:Properties properties, string id, bool value) global
    ; Plain config bools
    If (id == "EnableLootMan")
        properties.EnableLootMan = value
    ElseIf (id == "DisplaySystemMessage")
        properties.DisplaySystemMessage = value
    ElseIf (id == "PlayPickupSound")
        properties.PlayPickupSound = value
    ElseIf (id == "PlayContainerAnimation")
        properties.PlayContainerAnimation = value
    ElseIf (id == "UseLootingTimeBudget")
        properties.UseLootingTimeBudget = value
    ElseIf (id == "IgnoreOverweight")
        properties.IgnoreOverweight = value
    ElseIf (id == "LootIsDeliverToPlayer")
        properties.LootIsDeliverToPlayer = value
    ElseIf (id == "LootingWithoutLogs")
        properties.LootingWithoutLogs = value
    ElseIf (id == "NotLootingFromSettlement")
        properties.NotLootingFromSettlement = value
    ElseIf (id == "AutomaticallyLinkAndUnlinkToWorkshop")
        properties.AutomaticallyLinkAndUnlinkToWorkshop = value
    ElseIf (id == "UnlockLockedContainer")
        properties.UnlockLockedContainer = value
    ElseIf (id == "LootingLegendaryOnly")
        properties.LootingLegendaryOnly = value
    ElseIf (id == "AlwaysLootingExplosives")
        properties.AlwaysLootingExplosives = value

    ; Object-type filters
    ElseIf (id == "EnableObjectLootingOfACTI")
        properties.EnableObjectLootingOfACTI = value
    ElseIf (id == "EnableObjectLootingOfALCH")
        properties.EnableObjectLootingOfALCH = value
    ElseIf (id == "EnableObjectLootingOfAMMO")
        properties.EnableObjectLootingOfAMMO = value
    ElseIf (id == "EnableObjectLootingOfARMO")
        properties.EnableObjectLootingOfARMO = value
    ElseIf (id == "EnableObjectLootingOfBOOK")
        properties.EnableObjectLootingOfBOOK = value
    ElseIf (id == "EnableObjectLootingOfCONT")
        properties.EnableObjectLootingOfCONT = value
    ElseIf (id == "EnableObjectLootingOfFLOR")
        properties.EnableObjectLootingOfFLOR = value
    ElseIf (id == "EnableObjectLootingOfINGR")
        properties.EnableObjectLootingOfINGR = value
    ElseIf (id == "EnableObjectLootingOfKEYM")
        properties.EnableObjectLootingOfKEYM = value
    ElseIf (id == "EnableObjectLootingOfMISC")
        properties.EnableObjectLootingOfMISC = value
    ElseIf (id == "EnableObjectLootingOfNPC_")
        properties.EnableObjectLootingOfNPC_ = value
    ElseIf (id == "EnableObjectLootingOfWEAP")
        properties.EnableObjectLootingOfWEAP = value
    Else
        Return false
    EndIf
    Return true
EndFunction

; Write a packed-bitmask backing bool (the bool the MCM switcher binds to). These
; are toggle-only: ApplySettingSideEffects XORs the matching packed int, so they
; are reached only through FlipBool, never the absolute SetBool path.
Function WritePackedBool(LTMN2:Properties properties, string id, bool value) global
    ; Packed-bitmask backing bools: inventory filter
    If (id == "EnableInventoryLootingOfALCH")
        properties.EnableInventoryLootingOfALCH = value
    ElseIf (id == "EnableInventoryLootingOfAMMO")
        properties.EnableInventoryLootingOfAMMO = value
    ElseIf (id == "EnableInventoryLootingOfARMO")
        properties.EnableInventoryLootingOfARMO = value
    ElseIf (id == "EnableInventoryLootingOfBOOK")
        properties.EnableInventoryLootingOfBOOK = value
    ElseIf (id == "EnableInventoryLootingOfINGR")
        properties.EnableInventoryLootingOfINGR = value
    ElseIf (id == "EnableInventoryLootingOfKEYM")
        properties.EnableInventoryLootingOfKEYM = value
    ElseIf (id == "EnableInventoryLootingOfMISC")
        properties.EnableInventoryLootingOfMISC = value
    ElseIf (id == "EnableInventoryLootingOfWEAP")
        properties.EnableInventoryLootingOfWEAP = value

    ; Packed-bitmask backing bools: ALCH subtype filter
    ElseIf (id == "EnableALCHItemAlcohol")
        properties.EnableALCHItemAlcohol = value
    ElseIf (id == "EnableALCHItemChemistry")
        properties.EnableALCHItemChemistry = value
    ElseIf (id == "EnableALCHItemFood")
        properties.EnableALCHItemFood = value
    ElseIf (id == "EnableALCHItemNukaCola")
        properties.EnableALCHItemNukaCola = value
    ElseIf (id == "EnableALCHItemStimpak")
        properties.EnableALCHItemStimpak = value
    ElseIf (id == "EnableALCHItemSyringerAmmo")
        properties.EnableALCHItemSyringerAmmo = value
    ElseIf (id == "EnableALCHItemWater")
        properties.EnableALCHItemWater = value
    ElseIf (id == "EnableALCHItemOther")
        properties.EnableALCHItemOther = value

    ; Packed-bitmask backing bools: BOOK subtype filter
    ElseIf (id == "EnableBOOKItemPerkMagazine")
        properties.EnableBOOKItemPerkMagazine = value
    ElseIf (id == "EnableBOOKItemOther")
        properties.EnableBOOKItemOther = value

    ; Packed-bitmask backing bools: MISC subtype filter
    ElseIf (id == "EnableMISCItemBobblehead")
        properties.EnableMISCItemBobblehead = value
    ElseIf (id == "EnableMISCItemOther")
        properties.EnableMISCItemOther = value

    ; Packed-bitmask backing bools: WEAP subtype filter
    ElseIf (id == "EnableWEAPItemGrenade")
        properties.EnableWEAPItemGrenade = value
    ElseIf (id == "EnableWEAPItemMine")
        properties.EnableWEAPItemMine = value
    ElseIf (id == "EnableWEAPItemOther")
        properties.EnableWEAPItemOther = value
    EndIf
EndFunction

float Function ReadFloat(LTMN2:Properties properties, string id) global
    If (id == "LootingRange")
        Return properties.LootingRange
    ElseIf (id == "WorkerInvokeInterval")
        Return properties.WorkerInvokeInterval
    ElseIf (id == "LootingTimeBudgetMs")
        Return properties.LootingTimeBudgetMs
    EndIf
    Return 0.0
EndFunction

Function WriteFloat(LTMN2:Properties properties, string id, float value) global
    If (id == "LootingRange")
        properties.LootingRange = value
    ElseIf (id == "WorkerInvokeInterval")
        properties.WorkerInvokeInterval = value
    ElseIf (id == "LootingTimeBudgetMs")
        properties.LootingTimeBudgetMs = value
    EndIf
EndFunction

int Function ReadInt(LTMN2:Properties properties, string id) global
    If (id == "CarryWeight")
        Return properties.CarryWeight
    ElseIf (id == "MaxLootableObjectsPerPass")
        Return properties.MaxLootableObjectsPerPass
    ElseIf (id == "MaxContainersPerPass")
        Return properties.MaxContainersPerPass
    ElseIf (id == "MaxActorsPerPass")
        Return properties.MaxActorsPerPass
    ElseIf (id == "MaxActivationRefsPerPass")
        Return properties.MaxActivationRefsPerPass
    EndIf
    Return 0
EndFunction

Function WriteInt(LTMN2:Properties properties, string id, int value) global
    If (id == "CarryWeight")
        properties.CarryWeight = value
    ElseIf (id == "MaxLootableObjectsPerPass")
        properties.MaxLootableObjectsPerPass = value
    ElseIf (id == "MaxContainersPerPass")
        properties.MaxContainersPerPass = value
    ElseIf (id == "MaxActorsPerPass")
        properties.MaxActorsPerPass = value
    ElseIf (id == "MaxActivationRefsPerPass")
        properties.MaxActivationRefsPerPass = value
    EndIf
EndFunction
