Scriptname LTMN2:Looting:WorkerManagerACTI extends Quest

Quest property WorkerManagerACTI auto const mandatory

LTMN2:Properties properties
LTMN2:Looting:Worker:Impl:WorkerACTI01 worker01
LTMN2:Looting:Worker:Impl:WorkerACTI02 worker02
LTMN2:Looting:Worker:Impl:WorkerACTI03 worker03
LTMN2:Looting:Worker:Impl:WorkerACTI04 worker04
LTMN2:Looting:Worker:Impl:WorkerACTI05 worker05
LTMN2:Looting:Worker:Impl:WorkerACTI06 worker06
LTMN2:Looting:Worker:Impl:WorkerACTI07 worker07
LTMN2:Looting:Worker:Impl:WorkerACTI08 worker08

Event OnInit()
    properties = LTMN2:Properties.GetInstance()
    worker01 = WorkerManagerACTI As LTMN2:Looting:Worker:Impl:WorkerACTI01
    worker02 = WorkerManagerACTI As LTMN2:Looting:Worker:Impl:WorkerACTI02
    worker03 = WorkerManagerACTI As LTMN2:Looting:Worker:Impl:WorkerACTI03
    worker04 = WorkerManagerACTI As LTMN2:Looting:Worker:Impl:WorkerACTI04
    worker05 = WorkerManagerACTI As LTMN2:Looting:Worker:Impl:WorkerACTI05
    worker06 = WorkerManagerACTI As LTMN2:Looting:Worker:Impl:WorkerACTI06
    worker07 = WorkerManagerACTI As LTMN2:Looting:Worker:Impl:WorkerACTI07
    worker08 = WorkerManagerACTI As LTMN2:Looting:Worker:Impl:WorkerACTI08
EndEvent

Function Initialize()
    worker01.Initialize()
    worker02.Initialize()
    worker03.Initialize()
    worker04.Initialize()
    worker05.Initialize()
    worker06.Initialize()
    worker07.Initialize()
    worker08.Initialize()
    properties.ActiveWorkerThreadsACTI = 0
    properties.TurboModeACTI = false
EndFunction

Function Looting()
    If (!properties.EnableObjectLootingOfACTI)
        Return
    EndIf

    int max = properties.MaxWorkerThreadsACTI
    int active = properties.ActiveWorkerThreadsACTI
    If (active >= max)
        Return
    EndIf

    If (properties.TurboModeACTI)
        properties.TurboModeACTI = false

        int call = max - active
        If (call > 0 && !worker01.Busy())
            worker01.Run()
            call -= 1
        EndIf
        If (call > 0 && !worker02.Busy())
            worker02.Run()
            call -= 1
        EndIf
        If (call > 0 && !worker03.Busy())
            worker03.Run()
            call -= 1
        EndIf
        If (call > 0 && !worker04.Busy())
            worker04.Run()
            call -= 1
        EndIf
        If (call > 0 && !worker05.Busy())
            worker05.Run()
            call -= 1
        EndIf
        If (call > 0 && !worker06.Busy())
            worker06.Run()
            call -= 1
        EndIf
        If (call > 0 && !worker07.Busy())
            worker07.Run()
            call -= 1
        EndIf
        If (call > 0 && !worker08.Busy())
            worker08.Run()
            call -= 1
        EndIf
    Else
        If (!worker01.Busy())
            worker01.Run()
        ElseIf (!worker02.Busy())
            worker02.Run()
        ElseIf (!worker03.Busy())
            worker03.Run()
        ElseIf (!worker04.Busy())
            worker04.Run()
        ElseIf (!worker05.Busy())
            worker05.Run()
        ElseIf (!worker06.Busy())
            worker06.Run()
        ElseIf (!worker07.Busy())
            worker07.Run()
        ElseIf (!worker08.Busy())
            worker08.Run()
        EndIf
    EndIf
EndFunction
