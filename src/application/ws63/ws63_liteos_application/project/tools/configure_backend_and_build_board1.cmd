@echo off
powershell.exe -NoLogo -NoProfile -NoExit -ExecutionPolicy Bypass -File "%~dp0configure_backend_and_build_board1.ps1" %*
