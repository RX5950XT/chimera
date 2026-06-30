@echo off
REM One-click Chimera launcher (double-click this file).
REM Default to the fastest usable path: custom gfxstream + GuestVulkan + interactive priority.
REM Pass -Stock manually to force the slower stock gRPC fallback.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-chimera.ps1" -Fast -InteractiveFirst %*
