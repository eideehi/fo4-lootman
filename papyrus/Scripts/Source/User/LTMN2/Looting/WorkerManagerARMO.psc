Scriptname LTMN2:Looting:WorkerManagerARMO extends Quest

Quest property WorkerManagerARMO auto const mandatory

LTMN2:Properties properties
LTMN2:Looting:Worker:Impl:WorkerARMO01 worker01
LTMN2:Looting:Worker:Impl:WorkerARMO02 worker02
LTMN2:Looting:Worker:Impl:WorkerARMO03 worker03
LTMN2:Looting:Worker:Impl:WorkerARMO04 worker04
LTMN2:Looting:Worker:Impl:WorkerARMO05 worker05
LTMN2:Looting:Worker:Impl:WorkerARMO06 worker06
LTMN2:Looting:Worker:Impl:WorkerARMO07 worker07
LTMN2:Looting:Worker:Impl:WorkerARMO08 worker08

Event OnInit()
    properties = LTMN2:Properties.GetInstance()
    worker01 = WorkerManagerARMO As LTMN2:Looting:Worker:Impl:WorkerARMO01
    worker02 = WorkerManagerARMO As LTMN2:Looting:Worker:Impl:WorkerARMO02
    worker03 = WorkerManagerARMO As LTMN2:Looting:Worker:Impl:WorkerARMO03
    worker04 = WorkerManagerARMO As LTMN2:Looting:Worker:Impl:WorkerARMO04
    worker05 = WorkerManagerARMO As LTMN2:Looting:Worker:Impl:WorkerARMO05
    worker06 = WorkerManagerARMO As LTMN2:Looting:Worker:Impl:WorkerARMO06
    worker07 = WorkerManagerARMO As LTMN2:Looting:Worker:Impl:WorkerARMO07
    worker08 = WorkerManagerARMO As LTMN2:Looting:Worker:Impl:WorkerARMO08
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
    properties.ActiveWorkerThreadsARMO = 0
    properties.TurboModeARMO = false
EndFunction

Function Looting()
    If (!properties.EnableObjectLootingOfARMO)
        Return
    EndIf

    int max = properties.MaxWorkerThreadsARMO
    int active = properties.ActiveWorkerThreadsARMO
    If (active >= max)
        Return
    EndIf

    If (properties.TurboModeARMO)
        properties.TurboModeARMO = false

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
