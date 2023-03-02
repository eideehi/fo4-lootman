Scriptname LTMN2:Looting:WorkerManagerALCH extends Quest

Quest property WorkerManagerALCH auto const mandatory

LTMN2:Properties properties
LTMN2:Looting:Worker:Impl:WorkerALCH01 worker01
LTMN2:Looting:Worker:Impl:WorkerALCH02 worker02
LTMN2:Looting:Worker:Impl:WorkerALCH03 worker03
LTMN2:Looting:Worker:Impl:WorkerALCH04 worker04
LTMN2:Looting:Worker:Impl:WorkerALCH05 worker05
LTMN2:Looting:Worker:Impl:WorkerALCH06 worker06
LTMN2:Looting:Worker:Impl:WorkerALCH07 worker07
LTMN2:Looting:Worker:Impl:WorkerALCH08 worker08

Event OnInit()
    properties = LTMN2:Properties.GetInstance()
    worker01 = WorkerManagerALCH As LTMN2:Looting:Worker:Impl:WorkerALCH01
    worker02 = WorkerManagerALCH As LTMN2:Looting:Worker:Impl:WorkerALCH02
    worker03 = WorkerManagerALCH As LTMN2:Looting:Worker:Impl:WorkerALCH03
    worker04 = WorkerManagerALCH As LTMN2:Looting:Worker:Impl:WorkerALCH04
    worker05 = WorkerManagerALCH As LTMN2:Looting:Worker:Impl:WorkerALCH05
    worker06 = WorkerManagerALCH As LTMN2:Looting:Worker:Impl:WorkerALCH06
    worker07 = WorkerManagerALCH As LTMN2:Looting:Worker:Impl:WorkerALCH07
    worker08 = WorkerManagerALCH As LTMN2:Looting:Worker:Impl:WorkerALCH08
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
    properties.ActiveWorkerThreadsALCH = 0
EndFunction

Function Looting()
    If (!properties.EnableObjectLootingOfALCH)
        Return
    EndIf

    int max = properties.MaxWorkerThreadsALCH
    int active = properties.ActiveWorkerThreadsALCH
    If (active >= max)
        Return
    EndIf

    If (properties.TurboModeALCH)
        properties.TurboModeALCH = false

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
