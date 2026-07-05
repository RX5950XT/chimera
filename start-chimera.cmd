@echo off
REM One-click Chimera launcher (double-click this file).
REM
REM Default = -Fast: the custom-gfxstream shared-texture display path (GPU-direct
REM D3D11), which is smooth and clickable. Session 108 re-verified it healthy over
REM 150s of real gRPC input: producer, host consumer, and on-screen pixels all
REM advance together (~1:1), zero freeze. The Session 107 "-Fast display freezes"
REM diagnosis was WRONG -- it mistook idle-static content (the guest correctly not
REM redrawing an unchanged screen) for a freeze, and fell back to the stock gRPC
REM path, which is genuinely laggy (~10-19fps). Pass -Stock to force that slow path.
REM
REM Priority: default (below_normal), NOT -InteractiveFirst (normal). Normal is the
REM most audio-contentious setting (starves the host audio thread -> crackle in
REM music); below_normal keeps -Fast smooth while protecting host audio. If music
REM still crackles, add -AudioFirst (idle/EcoQoS, max audio protection). For the
REM absolute smoothest UI at the cost of host-audio glitches, add -InteractiveFirst.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-chimera.ps1" -Fast %*
