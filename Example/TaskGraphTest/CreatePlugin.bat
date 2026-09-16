@echo off
setlocal
cd /d "%~dp0"

rem CreatePlugin.bat — open the new-plugin UI (creates at the project root).

set "PYW=../../Tools/python/pythonw.exe"
if not exist "%PYW%" set "PYW=../../Tools/python/Scripts/pythonw.exe"
if not exist "%PYW%" set "PYW=../../Tools/python/python.exe"
if not exist "%PYW%" set "PYW=../../Tools/python/Scripts/python.exe"

if not exist "%PYW%" (
	echo [ERROR] Engine-local Python missing. Run Setup.bat in the engine root:
	echo         ../..
	pause
	exit /b 1
)

start "" "%PYW%" "../../Tools/create_plugin_ui.py" "%CD%\Source"
exit /b 0
