Scriptname LTMN2:Looting:WorkerManagerCONT extends Quest

Quest property WorkerManagerCONT auto const mandatory

LTMN2:Properties properties
LTMN2:Looting:Worker:Impl:WorkerCONT01 worker01
LTMN2:Looting:Worker:Impl:WorkerCONT02 worker02
LTMN2:Looting:Worker:Impl:WorkerCONT03 worker03
LTMN2:Looting:Worker:Impl:WorkerCONT04 worker04
LTMN2:Looting:Worker:Impl:WorkerCONT05 worker05
LTMN2:Looting:Worker:Impl:WorkerCONT06 worker06
LTMN2:Looting:Worker:Impl:WorkerCONT07 worker07
LTMN2:Looting:Worker:Impl:WorkerCONT08 worker08

Event OnInit()
    properties = LTMN2:Properties.GetInstance()
    worker01 = WorkerManagerCONT As LTMN2:Looting:Worker:Impl:WorkerCONT01
    worker02 = WorkerManagerCONT As LTMN2:Looting:Worker:Impl:WorkerCONT02
    worker03 = WorkerManagerCONT As LTMN2:Looting:Worker:Impl:WorkerCONT03
    worker04 = WorkerManagerCONT As LTMN2:Looting:Worker:Impl:WorkerCONT04
    worker05 = WorkerManagerCONT As LTMN2:Looting:Worker:Impl:WorkerCONT05
    worker06 = WorkerManagerCONT As LTMN2:Looting:Worker:Impl:WorkerCONT06
    worker07 = WorkerManagerCONT As LTMN2:Looting:Worker:Impl:WorkerCONT07
    worker08 = WorkerManagerCONT As LTMN2:Looting:Worker:Impl:WorkerCONT08
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
    properties.ActiveWorkerThreadsCONT = 0
EndFunction

Function Looting()
    If (!properties.EnableObjectLootingOfCONT)
        Return
    EndIf

    int max = properties.MaxWorkerThreadsCONT
    int active = properties.ActiveWorkerThreadsCONT
    If (active >= max)
        Return
    EndIf

    If (properties.TurboModeCONT)
        properties.TurboModeCONT = false

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
