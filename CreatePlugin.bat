@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem CreatePlugin — create a plugin in the engine's Extension/ (the catalog).

set "PYW=%~dp0Tools\python\pythonw.exe"
if not exist "%PYW%" set "PYW=%~dp0Tools\python\Scripts\pythonw.exe"
if not exist "%PYW%" set "PYW=%~dp0Tools\python\python.exe"
if not exist "%PYW%" set "PYW=%~dp0Tools\python\Scripts\python.exe"

if not exist "%PYW%" (
	echo [ERROR] Engine-local Python missing. Run Setup.bat first.
	pause
	exit /b 1
)

start "" "%PYW%" "%~dp0Tools\create_plugin_ui.py" "%~dp0Extension"
exit /b 0
