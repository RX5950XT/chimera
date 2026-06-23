@echo off
REM One-click Chimera launcher (double-click this file).
REM Picks the custom gfxstream 60fps runtime when available, else stock gRPC.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-chimera.ps1" %*
