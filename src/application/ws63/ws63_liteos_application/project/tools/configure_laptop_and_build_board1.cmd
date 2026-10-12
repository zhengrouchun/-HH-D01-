@echo off
rem Backend normally runs on the teammate laptop; do not enforce local IP ownership.
call "%~dp0configure_backend_and_build_board1.cmd" %*
exit /b %ERRORLEVEL%
