Scriptname LTMN2:Looting:WorkerManagerFLOR extends Quest

Quest property WorkerManagerFLOR auto const mandatory

LTMN2:Properties properties
LTMN2:Looting:Worker:Impl:WorkerFLOR01 worker01
LTMN2:Looting:Worker:Impl:WorkerFLOR02 worker02
LTMN2:Looting:Worker:Impl:WorkerFLOR03 worker03
LTMN2:Looting:Worker:Impl:WorkerFLOR04 worker04
LTMN2:Looting:Worker:Impl:WorkerFLOR05 worker05
LTMN2:Looting:Worker:Impl:WorkerFLOR06 worker06
LTMN2:Looting:Worker:Impl:WorkerFLOR07 worker07
LTMN2:Looting:Worker:Impl:WorkerFLOR08 worker08

Event OnInit()
    properties = LTMN2:Properties.GetInstance()
    worker01 = WorkerManagerFLOR As LTMN2:Looting:Worker:Impl:WorkerFLOR01
    worker02 = WorkerManagerFLOR As LTMN2:Looting:Worker:Impl:WorkerFLOR02
    worker03 = WorkerManagerFLOR As LTMN2:Looting:Worker:Impl:WorkerFLOR03
    worker04 = WorkerManagerFLOR As LTMN2:Looting:Worker:Impl:WorkerFLOR04
    worker05 = WorkerManagerFLOR As LTMN2:Looting:Worker:Impl:WorkerFLOR05
    worker06 = WorkerManagerFLOR As LTMN2:Looting:Worker:Impl:WorkerFLOR06
    worker07 = WorkerManagerFLOR As LTMN2:Looting:Worker:Impl:WorkerFLOR07
    worker08 = WorkerManagerFLOR As LTMN2:Looting:Worker:Impl:WorkerFLOR08
EndEvent

Function Looting()
    If (!properties.EnableObjectLootingOfFLOR)
        Return
    EndIf

    int max = properties.MaxWorkerThreadsFLOR
    int active = properties.ActiveWorkerThreadsFLOR
    If (active >= max)
        Return
    EndIf

    If (properties.TurboModeFLOR)
        properties.TurboModeFLOR = false

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
