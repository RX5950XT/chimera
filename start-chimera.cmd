@echo off
REM One-click Chimera launcher (double-click this file).
REM Default to the STOCK gRPC display path: the picture stays live and clicking
REM works. The -Fast custom-gfxstream shared-texture path is currently disabled by
REM default because its producer freezes a few seconds after boot (the guest keeps
REM running and receiving input, but the host picture stops updating on a stale
REM ColorBuffer -> "picture but nothing responds"). Pass -Fast manually to opt back
REM into the experimental smooth path once that freeze is fixed.
REM
REM Priority: default (below_normal) instead of -InteractiveFirst (normal). Normal
REM priority is the most audio-contentious setting (starves the host audio thread ->
REM crackle/static in music), and on the stock path it buys almost no FPS. Add
REM -AudioFirst for maximum audio protection (idle/EcoQoS), or -InteractiveFirst back
REM for the smoothest UI at the cost of host-audio glitches.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-chimera.ps1" %*
