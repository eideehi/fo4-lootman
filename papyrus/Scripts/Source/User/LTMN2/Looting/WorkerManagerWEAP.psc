Scriptname LTMN2:Looting:WorkerManagerWEAP extends Quest

Quest property WorkerManagerWEAP auto const mandatory

LTMN2:Properties properties
LTMN2:Looting:Worker:Impl:WorkerWEAP01 worker01
LTMN2:Looting:Worker:Impl:WorkerWEAP02 worker02
LTMN2:Looting:Worker:Impl:WorkerWEAP03 worker03
LTMN2:Looting:Worker:Impl:WorkerWEAP04 worker04
LTMN2:Looting:Worker:Impl:WorkerWEAP05 worker05
LTMN2:Looting:Worker:Impl:WorkerWEAP06 worker06
LTMN2:Looting:Worker:Impl:WorkerWEAP07 worker07
LTMN2:Looting:Worker:Impl:WorkerWEAP08 worker08

Event OnInit()
    properties = LTMN2:Properties.GetInstance()
    worker01 = WorkerManagerWEAP As LTMN2:Looting:Worker:Impl:WorkerWEAP01
    worker02 = WorkerManagerWEAP As LTMN2:Looting:Worker:Impl:WorkerWEAP02
    worker03 = WorkerManagerWEAP As LTMN2:Looting:Worker:Impl:WorkerWEAP03
    worker04 = WorkerManagerWEAP As LTMN2:Looting:Worker:Impl:WorkerWEAP04
    worker05 = WorkerManagerWEAP As LTMN2:Looting:Worker:Impl:WorkerWEAP05
    worker06 = WorkerManagerWEAP As LTMN2:Looting:Worker:Impl:WorkerWEAP06
    worker07 = WorkerManagerWEAP As LTMN2:Looting:Worker:Impl:WorkerWEAP07
    worker08 = WorkerManagerWEAP As LTMN2:Looting:Worker:Impl:WorkerWEAP08
EndEvent

Function Looting()
    If (!properties.EnableObjectLootingOfWEAP)
        Return
    EndIf

    int max = properties.MaxWorkerThreadsWEAP
    int active = properties.ActiveWorkerThreadsWEAP
    If (active >= max)
        Return
    EndIf

    If (properties.TurboModeWEAP)
        properties.TurboModeWEAP = false

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
