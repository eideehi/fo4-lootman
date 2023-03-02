Scriptname LTMN2:Looting:WorkerManagerAMMO extends Quest

Quest property WorkerManagerAMMO auto const mandatory

LTMN2:Properties properties
LTMN2:Looting:Worker:Impl:WorkerAMMO01 worker01
LTMN2:Looting:Worker:Impl:WorkerAMMO02 worker02
LTMN2:Looting:Worker:Impl:WorkerAMMO03 worker03
LTMN2:Looting:Worker:Impl:WorkerAMMO04 worker04
LTMN2:Looting:Worker:Impl:WorkerAMMO05 worker05
LTMN2:Looting:Worker:Impl:WorkerAMMO06 worker06
LTMN2:Looting:Worker:Impl:WorkerAMMO07 worker07
LTMN2:Looting:Worker:Impl:WorkerAMMO08 worker08

Event OnInit()
    properties = LTMN2:Properties.GetInstance()
    worker01 = WorkerManagerAMMO As LTMN2:Looting:Worker:Impl:WorkerAMMO01
    worker02 = WorkerManagerAMMO As LTMN2:Looting:Worker:Impl:WorkerAMMO02
    worker03 = WorkerManagerAMMO As LTMN2:Looting:Worker:Impl:WorkerAMMO03
    worker04 = WorkerManagerAMMO As LTMN2:Looting:Worker:Impl:WorkerAMMO04
    worker05 = WorkerManagerAMMO As LTMN2:Looting:Worker:Impl:WorkerAMMO05
    worker06 = WorkerManagerAMMO As LTMN2:Looting:Worker:Impl:WorkerAMMO06
    worker07 = WorkerManagerAMMO As LTMN2:Looting:Worker:Impl:WorkerAMMO07
    worker08 = WorkerManagerAMMO As LTMN2:Looting:Worker:Impl:WorkerAMMO08
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
    properties.ActiveWorkerThreadsAMMO = 0
EndFunction

Function Looting()
    If (!properties.EnableObjectLootingOfAMMO)
        Return
    EndIf

    int max = properties.MaxWorkerThreadsAMMO
    int active = properties.ActiveWorkerThreadsAMMO
    If (active >= max)
        Return
    EndIf

    If (properties.TurboModeAMMO)
        properties.TurboModeAMMO = false

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
