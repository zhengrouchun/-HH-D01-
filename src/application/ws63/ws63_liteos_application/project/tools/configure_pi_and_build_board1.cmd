@echo off
rem Compatibility entry: deployment now targets the laptop.
call "%~dp0configure_laptop_and_build_board1.cmd" %*
exit /b %ERRORLEVEL%
