@echo off
rem .cplugin double-click -> validate + auto-fix one plugin (engine Tools/python only).
call "%~dp0maho_python.bat" "%~dp0fix_plugin.py" %*
echo.
pause
