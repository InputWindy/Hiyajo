@echo off
setlocal EnableExtensions

rem .cplugin double-click -> validate + auto-fix one plugin (engine Tools/python only).
rem %* is the path Explorer passes for the double-clicked file (absolute).
rem Console tool: the fix log must stay visible, so it pauses at the end.

call "%~dp0maho_python.bat" "%~dp0fix_plugin.py" %*
set "ERR=%ERRORLEVEL%"
echo.
pause
exit /b %ERR%
