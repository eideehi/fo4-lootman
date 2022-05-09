Scriptname LTMN2:Looting:WorkerManagerNPC_ extends Quest

Quest property WorkerManagerNPC_ auto const mandatory

LTMN2:Properties properties
LTMN2:Looting:Worker:Impl:WorkerNPC_01 worker01
LTMN2:Looting:Worker:Impl:WorkerNPC_02 worker02
LTMN2:Looting:Worker:Impl:WorkerNPC_03 worker03
LTMN2:Looting:Worker:Impl:WorkerNPC_04 worker04
LTMN2:Looting:Worker:Impl:WorkerNPC_05 worker05
LTMN2:Looting:Worker:Impl:WorkerNPC_06 worker06
LTMN2:Looting:Worker:Impl:WorkerNPC_07 worker07
LTMN2:Looting:Worker:Impl:WorkerNPC_08 worker08

Event OnInit()
    properties = LTMN2:Properties.GetInstance()
    worker01 = WorkerManagerNPC_ As LTMN2:Looting:Worker:Impl:WorkerNPC_01
    worker02 = WorkerManagerNPC_ As LTMN2:Looting:Worker:Impl:WorkerNPC_02
    worker03 = WorkerManagerNPC_ As LTMN2:Looting:Worker:Impl:WorkerNPC_03
    worker04 = WorkerManagerNPC_ As LTMN2:Looting:Worker:Impl:WorkerNPC_04
    worker05 = WorkerManagerNPC_ As LTMN2:Looting:Worker:Impl:WorkerNPC_05
    worker06 = WorkerManagerNPC_ As LTMN2:Looting:Worker:Impl:WorkerNPC_06
    worker07 = WorkerManagerNPC_ As LTMN2:Looting:Worker:Impl:WorkerNPC_07
    worker08 = WorkerManagerNPC_ As LTMN2:Looting:Worker:Impl:WorkerNPC_08
EndEvent

Function Looting()
    If (!properties.EnableObjectLootingOfNPC_)
        Return
    EndIf

    int max = properties.MaxWorkerThreadsNPC_
    int active = properties.ActiveWorkerThreadsNPC_
    If (active >= max)
        Return
    EndIf

    If (properties.TurboModeNPC_)
        properties.TurboModeNPC_ = false

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
